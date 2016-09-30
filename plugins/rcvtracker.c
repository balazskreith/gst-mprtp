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
#include <math.h>
#include <gst/gst.h>
#include <stdlib.h>
#include <string.h>
#include "rcvctrler.h"
#include "rtppackets.h"


GST_DEBUG_CATEGORY_STATIC (rcvtracker_debug_category);
#define GST_CAT_DEFAULT rcvtracker_debug_category

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define _now(this) gst_clock_get_time (this->sysclock)
#define _priv(this) ((Private*)(this->priv))
//#define _get_subflow(this, subflow_id) ((Subflow*)(_priv(this)->subflows + subflow_id))

G_DEFINE_TYPE (RcvTracker, rcvtracker, G_TYPE_OBJECT);

typedef struct _Subflow{
  gboolean              initialized;
  RcvTrackerSubflowStat stat;

  Observer*             on_stat_changed;
  Observer*             on_received_packet;
  Observer*             on_lost_packet;

  GstClockTime          last_mprtp_delay;
  gboolean              seq_initialized;

  SlidingWindow*        skews;

  gdouble               path_skew;
  SlidingWindow*        path_skews;
}Subflow;

typedef struct _Priv{
  Subflow subflows[256];
}Private;

typedef struct{
  GstClockTime timestamp;
}GstClockTimeCover;

static void _init_subflow(RcvTracker *this, guint8 subflow_id);
static Subflow* _get_subflow(SndTracker *this, guint8 subflow_id);

static Private* _priv_ctor(void);
static void _priv_dtor(Private *priv);
//----------------------------------------------------------------------
//-------- Private functions belongs to the object ----------
//----------------------------------------------------------------------

static void
rcvtracker_finalize (
    GObject * object);


//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

static void _subflow_skews_median_pipe(gpointer udata, swpercentilecandidates_t *candidates)
{
  Subflow *subflow = udata;
  PercentileResult(GstClockTimeCover, timestamp, candidates, subflow->stat.skew_median, subflow->stat.skew_min, subflow->stat.skew_max, 0);

  if(subflow->path_skew == 0.){
    subflow->path_skew = subflow->stat.skew_median;
  }else{
    subflow->path_skew = subflow->path_skew * .99 + (gdouble)(subflow->stat.skew_median * .01);
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
  g_object_unref(this->path_skews);
  g_object_unref(this->on_discarded_packet);
  g_object_unref(this->on_received_packet);
  g_object_unref(this->on_stat_changed);
  _priv_dtor(this->priv);
}


void
rcvtracker_init (RcvTracker * this)
{
  this->sysclock = gst_system_clock_obtain();
  this->priv                = _priv_ctor();
  this->path_skews          = make_slidingwindow_double(256, 0);
  this->on_discarded_packet = make_observer();
  this->on_received_packet   = make_observer();
  this->on_stat_changed     = make_observer();

  slidingwindow_add_plugin(this->path_skews, make_swminmax(bintree3cmp_double, _skews_minmax_pipe, this));

}


static void _rcvtracker_refresh_helper(Subflow* subflow, RcvTracker *this)
{
  slidingwindow_refresh(subflow->path_skews);
}

void rcvtracker_refresh(RcvTracker * this)
{
  g_slist_foreach(this->joined_subflows, (GFunc) _rcvtracker_refresh_helper, this);
}

void rcvtracker_add_discarded_seq(RcvTracker* this, DiscardedPacket* discarded_packet)
{
  Subflow *subflow;

  ++this->stat.discarded_packets;
  if(discarded_packet->repaired){
    ++this->stat.recovered_packets;
  }

  observer_notify(this->on_discarded_packet, discarded_packet);
}

void rcvtracker_add_on_received_packet_callback(RcvTracker * this, NotifierFunc callback, gpointer udata)
{
  observer_add_listener(this->on_received_packet, callback, udata);
}


void rcvtracker_add_on_stat_changed_cb(RcvTracker * this, NotifierFunc callback, gpointer udata)
{
  observer_add_listener(this->on_stat_changed, callback, udata);
}

void rcvtracker_subflow_add_on_stat_changed_cb(RcvTracker * this,
                                    guint8 subflow_id,
                                    NotifierFunc callback,
                                    gpointer udata)
{
  Subflow *subflow = _get_subflow(this, subflow_id);
  observer_add_listener(this->on_stat_changed, callback, udata);
}


void rcvtracker_add_on_discarded_packet_cb(RcvTracker * this,
                                    guint8 subflow_id,
                                    NotifierFunc callback,
                                    gpointer udata)
{
  Subflow *subflow = _get_subflow(this, subflow_id);
  observer_add_listener(this->on_discarded_packet, callback, udata);
}

void rcvtracker_subflow_add_on_lost_packet_cb(RcvTracker * this,
                                    guint8 subflow_id,
                                    NotifierFunc callback,
                                    gpointer udata)
{
  Subflow *subflow = _get_subflow(this, subflow_id);
  observer_add_listener(subflow->on_lost_packet, callback, udata);
}

void rcvtracker_subflow_add_on_received_packet_cb(RcvTracker * this,
                                        guint8 subflow_id,
                                        NotifierFunc callback,
                                        gpointer udata)
{
  Subflow *subflow = _get_subflow(this, subflow_id);
  observer_add_listener(subflow->on_received_packet, callback, udata);
}

void rcvtracker_subflow_rem_on_received_packet_cb(RcvTracker * this,
                                        guint8 subflow_id,
                                        NotifierFunc callback)
{
  Subflow *subflow = _get_subflow(this, subflow_id);
  observer_rem_listener(subflow->on_received_packet, callback);
}



RcvTrackerSubflowStat* rcvtracker_get_subflow_stat(RcvTracker * this, guint8 subflow_id)
{
  return &_get_subflow(this, subflow_id)->stat;
}

static void _subflow_add_packet(RcvTracker * this, Subflow *subflow, RTPPacket* packet)
{
  gint cmp;
  gint64 skew;

  observer_notify(subflow->on_received_packet, packet);

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

  cmp = _cmp_seq(packet->subflow_seq, subflow->stat.highest_seq + 1);
  if(cmp < 0){
    goto done;
  }

  if(!cmp){
    subflow->stat.highest_seq = packet->subflow_seq;
    slidingwindow_add_data(subflow->skews, &skew);
  }else{
    guint16 act_seq;
    for(act_seq = subflow->stat.highest_seq; act_seq != packet->subflow_seq; ++act_seq){
      LostPacket lost_packet;
      lost_packet.subflow_seq = act_seq;
      observer_notify(subflow->on_lost_packet, &lost_packet);
    }
    subflow->stat.highest_seq = packet->subflow_seq;
  }

done:
  observer_notify(subflow->on_stat_changed, &subflow->stat);
}

void rcvtracker_add_packet(RcvTracker * this, RTPPacket* packet)
{
  Subflow* subflow;

  observer_notify(this->on_received_packet, packet);

  if(packet->subflow_id == 0){
    goto done;
  }

  subflow = _get_subflow(this, packet->subflow_id);
  _subflow_add_packet(this, subflow, packet);

done:
  observer_notify(this->on_stat_changed, &this->stat);

}


Subflow* _get_subflow(SndTracker *this, guint8 subflow_id)
{
  Subflow *result;
  result = _priv(this)->subflows + subflow_id;
  if(!result->initialized){
    _init_subflow(this, subflow_id);
  }
  return result;
}




void _init_subflow(RcvTracker *this, guint8 subflow_id)
{
  Subflow* subflow = _get_subflow(this, subflow_id);

  subflow->skews = make_slidingwindow_int64(100, 2 * GST_SECOND);
  slidingwindow_add_plugin(subflow->skews,
                           make_swpercentile(50, bintree3cmp_int64, _subflow_skews_median_pipe, this));

  subflow->path_skews = this->path_skews;
  this->joined_subflows = g_slist_prepend(this->joined_subflows, subflow);

}


static Private* _priv_ctor(void)
{
  Private* result = g_malloc0(sizeof(Private));
  return result;
}

static void _priv_dtor(Private *priv)
{
  Subflow *subflow;
  gint i;
  for(i = 0; i < 256; ++i){
    subflow = priv->subflows + i;
    if(subflow->on_stat_changed){
      g_object_unref(subflow->on_stat_changed);
    }
    if(subflow->on_lost_packet){
      g_object_unref(subflow->on_lost_packet);
    }
    if(subflow->on_received_packet){
      g_object_unref(subflow->on_received_packet);
    }
    g_object_unref(subflow->skews);
  }
}