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
#include "sndratedistor.h"
#include <math.h>
#include <gst/gst.h>
#include <stdlib.h>
#include <string.h>
#include "streamsplitter.h"
#include "sndctrler.h"


GST_DEBUG_CATEGORY_STATIC (sndtracker_debug_category);
#define GST_CAT_DEFAULT sndtracker_debug_category

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define _now(this) gst_clock_get_time (this->sysclock)
#define _priv(this) ((Private*)(this->priv))
#define _get_subflow(this, subflow_id) ((Subflow*)(_priv(this)->subflows + subflow_id))


G_DEFINE_TYPE (SndTracker, sndtracker, G_TYPE_OBJECT);

typedef struct{
  void     (*callback)(gpointer udata, SndTrackerStat* stat);
  gpointer   udata;
}SubflowNotifier;

typedef struct _Subflow{
  SndTrackerStat stat;
  GSList*        notifiers;
}Subflow;

typedef struct _Priv{
  Subflow subflows[256];
}Private;

//----------------------------------------------------------------------
//-------- Private functions belongs to the object ----------
//----------------------------------------------------------------------

static void
sndtracker_finalize (
    GObject * object);

static void
_packets_rem_pipe(gpointer udata, gpointer item);

static void
_notify(gpointer item, gpointer udata);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
sndtracker_class_init (SndTrackerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = sndtracker_finalize;

  GST_DEBUG_CATEGORY_INIT (sndtracker_debug_category, "sndtracker", 0,
      "MpRTP Sending Rate Distributor");
}

void
sndtracker_finalize (GObject * object)
{
  SndTracker * this;
  this = SNDTRACKER(object);
  g_object_unref(this->sysclock);
  g_object_unref(this->packets_sw);
  g_free(this->priv);
}


void
sndtracker_init (SndTracker * this)
{
  this->sysclock = gst_system_clock_obtain();
  this->packets_sw = make_slidingwindow(2000, GST_SECOND);
  this->priv = g_malloc0(sizeof(Private));

  slidingwindow_add_pipes(this->packets_sw, _packets_rem_pipe, this, NULL, NULL);
}

void sndtracker_refresh(SndTracker * this)
{
  slidingwindow_refresh(this->packets_sw);
}

void sndtracker_add_packet_notifier(SndTracker * this,
                                        void (*add_callback)(gpointer udata, gpointer item),
                                        gpointer add_udata,
                                        void (*rem_callback)(gpointer udata, gpointer item),
                                        gpointer rem_udata)
{
  slidingwindow_add_pipes(this->packets_sw, rem_callback, rem_udata, add_callback, add_udata);
}


void sndtracker_add_stat_notifier(SndTracker * this,
                                    void (*callback)(gpointer udata, SndTrackerStat* stat),
                                    gpointer udata)
{
  SndTrackerStatNotifier* notifier = g_slice_new0(SndTrackerStatNotifier);
  notifier->callback = callback;
  notifier->udata = udata;
  this->notifiers = g_slist_prepend(this->notifiers, notifier);
}

void sndtracker_add_stat_subflow_notifier(SndTracker * this,
                                    guint8 subflow_id,
                                    void (*callback)(gpointer udata, SndTrackerStat* stat),
                                    gpointer udata)
{
  SndTrackerStatNotifier* notifier = g_slice_new0(SndTrackerStatNotifier);
  notifier->callback = callback;
  notifier->udata = udata;
  _get_subflow(this, subflow_id)->notifiers = g_slist_prepend(_get_subflow(this, subflow_id)->notifiers, notifier);
}

SndTrackerStat* sndtracker_get_accumulated_stat(SndTracker * this)
{
  return &this->stat;
}

SndTrackerStat* sndtracker_get_subflow_stat(SndTracker * this, guint8 subflow_id)
{
  return _priv(this)->subflows + subflow_id;
}

void sndtracker_add_packet(SndTracker * this, RTPPacket* packet)
{
  this->stat.sent_bytes_in_1s += packet->payload_size;
  ++this->stat.sent_packets_in_1s;

  this->stat.total_sent_bytes += packet->payload_size;
  ++this->stat.total_sent_packets;

  _refresh_notifiers(this->notifiers, &this->stat);

  if(packet->subflow_id != 0){
    Subflow* subflow = _get_subflow(this, packet->subflow_id);
    subflow->stat.sent_bytes_in_1s += packet->payload;
    ++subflow->stat.sent_packets_in_1s;

    subflow->stat.total_sent_bytes += packet->payload_size;
    ++subflow->stat.total_sent_packets;

    _refresh_notifiers(subflow->notifiers, &subflow->stat);
  }

}

void _packets_rem_pipe(gpointer udata, gpointer item)
{
  SndTracker* this = udata;
  RTPPacket* packet = item;

  this->stat.sent_bytes_in_1s -= packet->payload_size;
  --this->stat.sent_packets_in_1s;

  _refresh_notifiers(this->notifiers, &this->stat);

  if(packet->subflow_id != 0){
    Subflow* subflow = _get_subflow(this, packet->subflow_id);
    subflow->stat.sent_bytes_in_1s -= packet->payload;
    --subflow->stat.sent_packets_in_1s;

    _refresh_notifiers(subflow->notifiers, &subflow->stat);
  }
}


void _refresh_notifiers(GSList* notifiers, SndTrackerStat * stat)
{
  if(notifiers){
    g_slist_foreach(notifiers, _notify, stat);
  }
}

void _notify(gpointer item, gpointer udata)
{
  SndTrackerStatNotifier *notifier;
  SndTrackerStat* stat = udata;
  notifier->callback(notifier->udata, stat)
}
