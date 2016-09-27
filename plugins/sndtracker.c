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
#include "sndtracker.h"

GST_DEBUG_CATEGORY_STATIC (sndtracker_debug_category);
#define GST_CAT_DEFAULT sndtracker_debug_category

#define _now(this) gst_clock_get_time (this->sysclock)
#define _priv(this) ((Private*)(this->priv))
//#define _get_subflow(this, subflow_id) ((Subflow*)(_priv(this)->subflows + subflow_id))

G_DEFINE_TYPE (SndTracker, sndtracker, G_TYPE_OBJECT);

typedef struct{
  void     (*callback)(gpointer udata, SndTrackerStat* stat);
  gpointer   udata;
}SubflowNotifier;

typedef struct _Subflow{
  SndTrackerStat stat;
  Observer*      on_stat_changed;
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
_packets_rem_pipe(SndTracker* this, RTPPacket* packet);

static void
_fec_rem_pipe(SndTracker* this, FECEncoderResponse* response);

static Private*
_priv_ctor(void);

static void
_priv_dtor(
    Private *priv);

static Subflow* _get_subflow(
    SndTracker *this,
    guint8 subflow_id);

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
  g_object_unref(this->fec_sw);
  _priv_dtor(this->priv);
}


void
sndtracker_init (SndTracker * this)
{
  this->sysclock = gst_system_clock_obtain();
  this->packets_sw = make_slidingwindow(1000, GST_SECOND);
  this->fec_sw = make_slidingwindow(500, GST_SECOND);
  this->priv = _priv_ctor();

  slidingwindow_on_rem_item_cb(this->packets_sw, (NotifierFunc) _packets_rem_pipe, this);
  slidingwindow_on_rem_item_cb(this->fec_sw, (NotifierFunc) _fec_rem_pipe, this);
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


void sndtracker_add_stat_notifier(SndTracker * this, NotifierFunc callback, gpointer udata)
{
  if(!this->on_stat_changed){
    this->on_stat_changed = make_observer();
  }
  observer_add_listener(this->on_stat_changed, callback, udata);
}

void sndtracker_add_stat_subflow_notifier(SndTracker * this,
                                    guint8 subflow_id,
                                    NotifierFunc callback, gpointer udata)
{
  Subflow *subflow;
  if(!subflow->on_stat_changed){
    subflow->on_stat_changed = make_observer();
  }
  observer_add_listener(subflow->on_stat_changed, callback, udata);
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

  if(packet->subflow_id != 0){
    Subflow* subflow = _get_subflow(this, packet->subflow_id);
    subflow->stat.sent_bytes_in_1s += packet->payload_size;
    ++subflow->stat.sent_packets_in_1s;

    subflow->stat.total_sent_bytes += packet->payload_size;
    ++subflow->stat.total_sent_packets;
  }

  slidingwindow_add_data(this->packets_sw, packet);
}

void sndtracker_add_fec_response(SndTracker * this, FECEncoderResponse *fec_response)
{
  this->stat.sent_fec_bytes_in_1s += fec_response->payload_size;
  ++this->stat.sent_fec_packets_in_1s;

  if(fec_response->subflow_id != 0){
    Subflow* subflow = _get_subflow(this, fec_response->subflow_id);
    subflow->stat.sent_fec_bytes_in_1s += fec_response->payload_size;
    ++subflow->stat.sent_fec_packets_in_1s;
  }
  fecencoder_ref_response(fec_response);
  slidingwindow_add_data(this->fec_sw, fec_response);
}


void _packets_rem_pipe(SndTracker* this, RTPPacket* packet)
{

  this->stat.sent_bytes_in_1s -= packet->payload_size;
  --this->stat.sent_packets_in_1s;

  observer_notify(this->on_stat_changed, &this->stat);

  if(packet->subflow_id != 0){
    Subflow* subflow = _get_subflow(this, packet->subflow_id);
    subflow->stat.sent_bytes_in_1s -= packet->payload_size;
    --subflow->stat.sent_packets_in_1s;

    observer_notify(subflow->on_stat_changed, &this->stat);
  }
}


void _fec_rem_pipe(SndTracker* this, FECEncoderResponse* response)
{
  this->stat.sent_fec_bytes_in_1s -= response->payload_size;
  --this->stat.sent_fec_packets_in_1s;

  if(response->subflow_id != 0){
    Subflow* subflow = _get_subflow(this, response->subflow_id);
    subflow->stat.sent_fec_bytes_in_1s -= response->payload_size;
    --subflow->stat.sent_fec_packets_in_1s;
  }

  fecencoder_unref_response(response);
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
  }
}

Subflow* _get_subflow(SndTracker *this, guint8 subflow_id)
{
  Subflow *result;
  result = _priv(this)->subflows + subflow_id;
  return result;
}

#undef _get_subflow;
