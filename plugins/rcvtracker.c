/* GStreamer Scheduling tree
 * Copyright (C) 2015 Balázs Kreith (contact: balazs.kreith@gmail.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include "rcvratedistor.h"
#include <math.h>
#include <gst/gst.h>
#include <stdlib.h>
#include <string.h>
#include "streamsplitter.h"
#include "rcvctrler.h"


GST_DEBUG_CATEGORY_STATIC (rcvtracker_debug_category);
#define GST_CAT_DEFAULT rcvtracker_debug_category

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define _now(this) gst_clock_get_time (this->sysclock)
#define _priv(this) ((Private*)(this->priv))
#define _get_subflow(this, subflow_id) ((Subflow*)(_priv(this)->subflows + subflow_id))


G_DEFINE_TYPE (RcvTracker, rcvtracker, G_TYPE_OBJECT);

typedef struct{
  void     (*callback)(gpointer udata, RcvTrackerSubflowStat* stat);
  gpointer   udata;
}SubflowNotifier;

typedef struct _Subflow{
  gboolean              initialized;
  RcvTrackerSubflowStat stat;
  GSList*               stat_notifiers;
  GSList*               packet_notifiers;
  GstClockTime          last_mprtp_delay;
  gboolean              seq_initialized;

  SlidingWindow*        skews;

  GstClockTime          skew_median, skew_min, skew_max;
  gdouble               path_skew;
  SlidingWindow*        path_skews;
}Subflow;

typedef struct _Priv{
  Subflow subflows[256];
}Private;

typedef struct{
  GstClockTime timestamp;
}GstClockTimeCover;

//----------------------------------------------------------------------
//-------- Private functions belongs to the object ----------
//----------------------------------------------------------------------

static void
rcvtracker_finalize (
    GObject * object);

static void
_refresh_packet_notifiers(GSList* notifiers, RTPPacket *packet);

static void
_packet_notify(gpointer item, gpointer udata);

static void
_refresh_stat_notifiers(GSList* notifiers, RcvTrackerSubflowStat * stat);

static void
_stat_notify(gpointer item, gpointer udata);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

static void _subflow_skews_median_pipe(gpointer udata, swpercentilecandidates_t *candidates)
{
  Subflow *subflow = udata;
  PercentileResult(GstClockTimeCover, timestamp, candidates, subflow->skew_median, subflow->skew_min, subflow->skew_max, 0);

  if(subflow->path_skew == 0.){
    subflow->path_skew = subflow->skew_median;
  }else{
    subflow->path_skew = subflow->path_skew * .99 + (gdouble)(subflow->skew_median * .01);
  }

  slidingwindow_add_data(subflow->path_skews, &subflow->path_skew);
}

static void _skews_minmax_pipe(gpointer udata, swminmaxstat_t* stat)
{
  RcvTracker* this = udata;
  if(_now(this) - 500 * GST_USECOND < this->skew_minmax_updated){
    return;
  }

  this->skew_minmax_updated = _now(this);

  this->stat.min_skew = (gdouble)stat->min;
  this->stat.max_skew = (gdouble)stat->max;
}


void
rcvtracker_class_init (RcvTrackerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = rcvtracker_finalize;

  GST_DEBUG_CATEGORY_INIT (rcvtracker_debug_category, "rcvtracker", 0,
      "MpRTP Sending Rate Distributor");
}

void
rcvtracker_finalize (GObject * object)
{
  RcvTracker * this;
  this = RCVTRACKER(object);
  g_object_unref(this->sysclock);
  g_free(this->priv);
}


void
rcvtracker_init (RcvTracker * this)
{
  this->sysclock = gst_system_clock_obtain();
  this->priv = g_malloc0(sizeof(Private));
  this->path_skews = make_slidingwindow_double(256, 0);

  slidingwindow_add_plugin(this->path_skews, make_swminmax(bintree3cmp_double, _skews_minmax_pipe, this));

}

void rcvtracker_deinit_subflow(RcvTracker *this, guint8 subflow_id)
{
  Subflow* subflow = _get_subflow(this, subflow_id);
  if(subflow->initialized){
    GST_WARNING_OBJECT(this, "Subflow already deinitialized");
    return;
  }

  g_object_unref(subflow->skews);
  this->joined_subflows = g_slist_prepend(this->joined_subflows, subflow);
}


void rcvtracker_init_subflow(RcvTracker *this, guint8 subflow_id)
{
  Subflow* subflow = _get_subflow(this, subflow_id);
  if(!subflow->initialized){
    GST_WARNING_OBJECT(this, "Subflow already initialized");
    return;
  }

  subflow->skews = make_slidingwindow_uint64(100, 2 * GST_SECOND);
  slidingwindow_add_plugin(subflow->skews,
                           make_swpercentile(50, bintree3cmp_uint64, _subflow_skews_median_pipe, this));

  subflow->path_skews = this->path_skews;
  this->joined_subflows = g_slist_remove(this->joined_subflows, subflow);
}

static void _rcvtracker_refresh_helper(Subflow* subflow, RcvTracker *this)
{

}

void rcvtracker_refresh(RcvTracker * this)
{
  g_slist_foreach(this->joined_subflows, (GFunc) _rcvtracker_refresh_helper, this);
}

void rcvtracker_add_packet_notifier(RcvTracker * this,
                                        void (*callback)(gpointer udata, gpointer item),
                                        gpointer udata)
{
  RcvTrackerPacketNotifier* notifier = g_slice_new0(RcvTrackerPacketNotifier);
  notifier->callback = callback;
  notifier->udata = udata;
  this->packet_notifiers = g_slist_prepend(this->packet_notifiers, notifier);
}


void rcvtracker_add_stat_notifier(RcvTracker * this,
                                    void (*callback)(gpointer udata, RcvTrackerSubflowStat* stat),
                                    gpointer udata)
{
  RcvTrackerStatNotifier* notifier = g_slice_new0(RcvTrackerStatNotifier);
  notifier->callback = callback;
  notifier->udata = udata;
  this->stat_notifiers = g_slist_prepend(this->stat_notifiers, notifier);
}

void rcvtracker_add_stat_subflow_notifier(RcvTracker * this,
                                    guint8 subflow_id,
                                    void (*callback)(gpointer udata, RcvTrackerSubflowStat* stat),
                                    gpointer udata)
{
  RcvTrackerStatNotifier* notifier = g_slice_new0(RcvTrackerStatNotifier);
  notifier->callback = callback;
  notifier->udata = udata;
  _get_subflow(this, subflow_id)->stat_notifiers = g_slist_prepend(_get_subflow(this, subflow_id)->stat_notifiers, notifier);
}


void rcvtracker_add_packet_on_subflow_notifier(RcvTracker * this,
                                    guint8 subflow_id,
                                    void (*callback)(gpointer udata, RcvTrackerSubflowStat* stat),
                                    gpointer udata)
{
  RcvTrackerPacketNotifier* notifier = g_slice_new0(RcvTrackerPacketNotifier);
  notifier->callback = callback;
  notifier->udata = udata;
  _get_subflow(this, subflow_id)->packet_notifiers = g_slist_prepend(_get_subflow(this, subflow_id)->packet_notifiers, notifier);
}

RcvTrackerSubflowStat* rcvtracker_get_subflow_stat(RcvTracker * this, guint8 subflow_id)
{
  return _priv(this)->subflows + subflow_id;
}

void rcvtracker_add_packet(RcvTracker * this, RTPPacket* packet)
{
  Subflow* subflow;
  gint64 skew;
  gint cmp;

  _refresh_packet_notifiers(this->packet_notifiers, packet);
  if(packet->subflow_id == 0){
    return;
  }
  subflow = _get_subflow(this, packet->subflow_id);
  if(!subflow->initialized){
    GST_WARNING_OBJECT(this, "Uninitialized subflow in rcvtracker got a packet");
    return;
  }

  _refresh_packet_notifiers(subflow->packet_notifiers, packet);

  if (subflow->seq_initialized == FALSE) {
    subflow->stat.highest_seq = packet->subflow_seq;
    subflow->stat.total_received_packets = 1;
    subflow->stat.total_received_bytes = packet->payload_size;
    subflow->last_mprtp_delay = packet->received_info.delay;
    subflow->seq_initialized = TRUE;
    goto done;
  }

  //normal jitter calculation for regular rtcp reports
  skew = (((gint64)subflow->last_mprtp_delay - (gint64)packet->received_info.delay));
  subflow->stat.jitter += ((skew < 0?-1*skew:skew) - subflow->stat.jitter) / 16;
  subflow->last_mprtp_delay = packet->received_info.delay;
  ++subflow->stat.total_received_packets;
  subflow->stat.total_received_bytes += packet->payload_size;

  cmp = _cmp_seq(subflow->stat.highest_seq + 1, packet->subflow_seq);
  if(cmp == 0){
    subflow->stat.highest_seq = packet->subflow_seq;
    slidingwindow_add_data(subflow->skews, &skew);
  }else if(cmp < 0){
    subflow->stat.highest_seq = packet->subflow_seq;
  }

  _refresh_stat_notifiers(subflow->stat_notifiers, &subflow->stat);
done:
  _refresh_stat_notifiers(this->stat_notifiers, &this->stat);

}


//Only process rtp packets
void
mprtpr_path_process_rtp_packet (MpRTPRPath * this, GstMpRTPBuffer *mprtp)
{
  gint64 skew;

  THIS_WRITELOCK (this);
  if(!mprtp->delay){
    GST_WARNING_OBJECT(this, "A packet delay should not be 0, the mprtpr path process doesn't work with it");
    goto done;
  }

  if (this->seq_initialized == FALSE) {
    this->highest_seq = mprtp->subflow_seq;
    this->total_packets_received = 1;
    this->total_payload_received = mprtp->payload_bytes;
    this->last_rtp_timestamp = mprtp->timestamp;
    this->last_mprtp_delay = mprtp->delay;
    _add_delay(this, mprtp->delay);
    this->seq_initialized = TRUE;
    goto done;
  }

  //normal jitter calculation for regular rtcp reports
  skew = (((gint64)this->last_mprtp_delay - (gint64)mprtp->delay));
  this->jitter += ((skew < 0?-1*skew:skew) - this->jitter) / 16;
  this->last_mprtp_delay = mprtp->delay;
  ++this->total_packets_received;
  this->total_payload_received += mprtp->payload_bytes;

  //collect and evaluate skew in another way
  if(_cmp_seq32(this->last_rtp_timestamp, mprtp->timestamp) < 0){
    this->last_rtp_timestamp = mprtp->timestamp;
  }
  _add_delay(this, mprtp->delay);

  if(this->packetstracker){
    this->packetstracker(this->packetstracker_data, mprtp);
  }

  //consider cycle num increase with allowance of a little gap
  if(65472 < this->highest_seq && mprtp->subflow_seq < 128){
    ++this->cycle_num;
  }

  //set the new packet seq as the highest seq
  this->highest_seq = mprtp->subflow_seq;

done:
  THIS_WRITEUNLOCK(this);
}



void _refresh_packet_notifiers(GSList* notifiers, RTPPacket * packet)
{
  if(notifiers){
    g_slist_foreach(notifiers, _stat_notify, packet);
  }
}

void _packet_notify(gpointer item, gpointer udata)
{
  RcvTrackerPacketNotifier *notifier;
  RTPPacket* packet = udata;
  notifier->callback(notifier->udata, packet);
}

void _refresh_stat_notifiers(GSList* notifiers, RcvTrackerSubflowStat * stat)
{
  if(notifiers){
    g_slist_foreach(notifiers, _stat_notify, stat);
  }
}

void _stat_notify(gpointer item, gpointer udata)
{
  RcvTrackerStatNotifier *notifier;
  RcvTrackerSubflowStat* stat = udata;
  notifier->callback(notifier->udata, stat);
}
