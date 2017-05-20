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
#include "streampackets.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>
#include "rtpfecbuffer.h"
#include "lib_swplugins.h"

#define _now(this) gst_clock_get_time (this->sysclock)

GST_DEBUG_CATEGORY_STATIC (streampackets_debug_category);
#define GST_CAT_DEFAULT streampackets_debug_category

G_DEFINE_TYPE (StreamPackets, streampackets, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void streampackets_finalize (GObject * object);
static void _setup_streampacket(StreamPacket* packet, GstRTPBuffer *rtp);
static void _extract_mprtp_info(StreamPackets* this, StreamPacket* packet, GstRTPBuffer *rtp);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

DEFINE_RECYCLE_TYPE(static, streampacket, StreamPacket);

static void _streampacket_shaper(StreamPacket* result, gpointer udata)
{
  memset(result, 0, sizeof(StreamPacket));
}

void
streampackets_class_init (StreamPacketsClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = streampackets_finalize;

  GST_DEBUG_CATEGORY_INIT (streampackets_debug_category, "streampackets", 0,
      "MpRTP Manual Sending Controller");

}

void
streampackets_finalize (GObject * object)
{
  StreamPackets *this;

  this = STREAMPACKETS(object);

  g_object_unref(this->recycle);
  g_object_unref(this->sysclock);

}

StreamPackets* make_streampackets(void)
{
  StreamPackets* this;
  this = g_object_new (STREAMPACKETS_TYPE, NULL);
  this->recycle = make_recycle_streampacket(256, (RecycleItemShaper)_streampacket_shaper);
  return this;
}


void
streampackets_init (StreamPackets * this)
{
  this->sysclock                 = gst_system_clock_obtain();
  this->mprtp_ext_header_id      = MPRTP_DEFAULT_EXTENSION_HEADER_ID;
  this->abs_time_ext_header_id   = ABS_TIME_DEFAULT_EXTENSION_HEADER_ID;
  this->pivot_address_subflow_id = 0;
  this->pivot_address            = NULL;
}


void streampackets_reset(StreamPackets* this)
{

}


StreamPacket* streampackets_get_packet(StreamPackets* this, GstBuffer* buffer)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  StreamPacket* packet;


  packet = recycle_retrieve_and_shape(this->recycle, NULL);

  gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp);

  _setup_streampacket(packet, &rtp);
  if(gst_buffer_is_mprtp(buffer, this->mprtp_ext_header_id)){
    _extract_mprtp_info(this, packet, &rtp);
  }

  gst_rtp_buffer_unmap(&rtp);

  packet->ref      = 1;
  packet->received = _now(this);
  packet->destiny  = this->recycle;
  _check_buffer_meta_data(this, packet);
  return packet;
}



void streampackets_set_abs_time_ext_header_id(StreamPackets* this, guint8 abs_time_ext_header_id)
{
  this->abs_time_ext_header_id = abs_time_ext_header_id;
}

guint8 streampackets_get_abs_time_ext_header_id(StreamPackets* this)
{
  return this->abs_time_ext_header_id;
}

void streampackets_set_mprtp_ext_header_id(StreamPackets* this, guint8 mprtp_ext_header_id)
{
  this->mprtp_ext_header_id = mprtp_ext_header_id;
}

guint8 streampackets_get_mprtp_ext_header_id(StreamPackets* this)
{
  return this->mprtp_ext_header_id;
}

void streampacket_unref(StreamPacket *packet)
{
  if(0 < --packet->ref){
    return;
  }

  recycle_add(packet->destiny, packet);
}

StreamPacket* streampacket_ref(StreamPacket *packet)
{
  ++packet->ref;
  return packet;
}

void streampacket_print(StreamPacket *packet, printfnc print)
{
  print("------- Packet %hu --------\n"
        "Timestamp:    %-10u\n"
        "Marker:       %-10d\n"
        "Payload type: %-10d\n"
        "Payload size: %-10d\n"
        "SSRC:         %-10u\n"
        "ref:          %-10d\n"
        ,
        packet->abs_seq,
        packet->timestamp,
        packet->marker,
        packet->payload_type,
        packet->payload_size,
        packet->ssrc,
        packet->ref
        );
}

void _setup_streampacket(StreamPacket* packet, GstRTPBuffer *rtp)
{
  packet->abs_seq      = gst_rtp_buffer_get_seq(rtp);
  packet->timestamp    = gst_rtp_buffer_get_timestamp(rtp);
  packet->ssrc         = gst_rtp_buffer_get_ssrc(rtp);
  packet->payload_size = gst_rtp_buffer_get_payload_len(rtp);
  packet->payload_type = gst_rtp_buffer_get_payload_type(rtp);
  packet->header_size  = gst_rtp_buffer_get_header_len(rtp);
  packet->marker       = gst_rtp_buffer_get_marker(rtp);
}


void _extract_mprtp_info(StreamPackets* this, StreamPacket* packet, GstRTPBuffer *rtp)
{

  guint8 mprtp_ext_header_id = streampackets_get_mprtp_ext_header_id(this);

  packet->abs_rcv_ntp_time  = NTP_NOW;
  packet->abs_snd_ntp_chunk = gst_rtp_buffer_get_abs_time_extension_new(rtp, this->abs_time_ext_header_id);

  packet->abs_snd_ntp_time = gst_rtp_buffer_get_abs_time_extension(rtp, this->abs_time_ext_header_id);

  packet->delay = get_epoch_time_from_ntp_in_ns(NTP_NOW - packet->abs_snd_ntp_time);

  gst_rtp_buffer_get_mprtp_extension(rtp, mprtp_ext_header_id, &packet->subflow_id, &packet->subflow_seq);

}
