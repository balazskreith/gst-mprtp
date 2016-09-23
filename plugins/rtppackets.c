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
#include "rtppackets.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>
#include "mprtpspath.h"
#include "rtpfecbuffer.h"
#include "lib_swplugins.h"

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define _now(this) gst_clock_get_time (this->sysclock)

//#define THIS_READLOCK(this)
//#define THIS_READUNLOCK(this)
//#define THIS_WRITELOCK(this)
//#define THIS_WRITEUNLOCK(this)
//#define _cmp_uint16(x,y) ((x==y)?0:((gint16) (x - y)) < 0 ? -1 : 1)

//static gint
//_cmp_uint16 (guint16 x, guint16 y)
//{
//  if(x == y) return 0;
//  if(x < y && y - x < 32768) return -1;
//  if(x > y && x - y > 32768) return -1;
//  if(x < y && y - x > 32768) return 1;
//  if(x > y && x - y < 32768) return 1;
//  return 0;
//}


typedef struct{
  void (*callback)(gpointer udata, RTPPacket* packet);
  gpointer udata;
}StalledNotifier;

GST_DEBUG_CATEGORY_STATIC (rtppackets_debug_category);
#define GST_CAT_DEFAULT rtppackets_debug_category

G_DEFINE_TYPE (RTPPackets, rtppackets, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void rtppackets_finalize (GObject * object);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
rtppackets_class_init (RTPPacketsClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = rtppackets_finalize;

  GST_DEBUG_CATEGORY_INIT (rtppackets_debug_category, "rtppackets", 0,
      "MpRTP Manual Sending Controller");

}

void
rtppackets_finalize (GObject * object)
{
  RTPPackets *this;
  this = RTPPACKETS(object);
  g_object_unref(this->sysclock);
  g_object_unref(this->packets);
}

void
rtppackets_init (RTPPackets * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sysclock = gst_system_clock_obtain();
  this->packets = g_malloc0(sizeof(RTPPacket) * 65536);
}


void rtppackets_reset(RTPPackets* this)
{
  memset((gpointer)this->packets, 0, sizeof(RTPPackets) * 65536);
}


RTPPackets* make_rtppackets(void)
{
  RTPPackets* this;
  this = g_object_new (RTPPACKETS_TYPE, NULL);
  return this;
}

void rtppackets_add_stalled_notifier(RTPPackets* this, void (*callback)(gpointer udata, RTPPacket* packet), gpointer udata)
{
  StalledNotifier *stalled_notifier = g_malloc0(sizeof(StalledNotifier));
  stalled_notifier->callback = callback;
  stalled_notifier->udata = udata;
  this->stalled_notifiers = g_slist_prepend(this->stalled_notifiers, stalled_notifier);
}

void rtppackets_add_subflow(RTPPackets* this, guint8 subflow_id)
{
  if(this->subflows[subflow_id]){
    return;
  }
  this->subflows[subflow_id] = g_malloc0(sizeof(RTPPacket*) * 65536);
}

void _stalled_notify_caller(gpointer item, gpointer udata)
{
  RTPPacket* packet = udata;
  StalledNotifier *notifier = item;
  notifier->callback(notifier->udata, packet);
}


RTPPacket* rtppackets_get_packet(RTPPackets* this, GstBuffer *buffer)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  RTPPacket* result;
  guint16 abs_seq;
  gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp);
  abs_seq = gst_rtp_buffer_get_seq(&rtp);
  result            = this->packets + abs_seq;

  if(0 < result->ref){
    if(!result->sent){//we have never sent this.
      goto done;
    }
    //we sent the packet, but it remained and a new one wants to replace it.
    if(this->stalled_notifiers){
      g_slist_foreach(this->stalled_notifiers, _stalled_notify_caller, result);
    }
  }

  memset(result, 0, sizeof(RTPPacket));
  result->added        = _now(this);
  result->buffer       = gst_buffer_ref(buffer);
  result->timestamp    = gst_rtp_buffer_get_timestamp(&rtp);
  result->abs_seq      = abs_seq;
  result->ssrc         = gst_rtp_buffer_get_ssrc(&rtp);
  result->payload_size = gst_rtp_buffer_get_payload_len(&rtp);
  result->subflow_id   = 0;
  result->header_size  = gst_rtp_buffer_get_header_len(&rtp);
  result->subflow_seq  = 0;
  result->sent         = 0;
  result->ref          = 1;
  result->acknowledged = FALSE;
  result->lost    = FALSE;
  result->payload      = gst_rtp_buffer_get_payload(&rtp);
  gst_rtp_buffer_unmap(&rtp);
done:
  return result;
}

void rtppackets_reset_packet(RTPPacket* packet)
{

}

void rtppackets_map_to_subflow(RTPPackets* this, RTPPacket *packet, guint8 subflow_id, guint16 subflow_seq)
{
  this->subflows[subflow_seq][subflow_seq] = packet;
}


void rtppackets_packet_sent(RTPPackets* this, RTPPacket *packet)
{
  packet->buffer = NULL;
  packet->sent   = _now(this);

  if(0 < packet->ref) {
    --packet->ref;
  }

}


RTPPacket* rtppackets_get_by_abs_seq(RTPPackets* this, guint16 abs_seq)
{
  return this->packets + abs_seq;
}


RTPPacket* rtppackets_get_by_subflow_seq(RTPPackets* this, guint8 subflow_id, guint16 sub_seq)
{
  RTPPacket** items = this->subflows[subflow_id];
  return items[sub_seq];
}




#undef DEBUG_PRINT_TOOLS
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
