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
#include "sndpackets.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>
#include "rtpfecbuffer.h"
#include "lib_swplugins.h"

#define _now(this) gst_clock_get_time (this->sysclock)

GST_DEBUG_CATEGORY_STATIC (sndpackets_debug_category);
#define GST_CAT_DEFAULT sndpackets_debug_category

G_DEFINE_TYPE (SndPackets, sndpackets, G_TYPE_OBJECT);

/* Evaluates to a mask with n bits set */
#define BITS_MASK(n) ((1<<(n))-1)

/* Returns len bits, with the LSB at position bit */
#define BITS_GET(val, bit, len) (((val)>>(bit))&BITS_MASK(len))

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void sndpackets_finalize (GObject * object);
static void _setup_sndpacket(SndPacket* result, GstBuffer* buffer);
static void _setup_abs_time_extension(SndPacket* packet);
static gboolean _vp8_keyframe_filter(GstBuffer* rtp);

DEFINE_RECYCLE_TYPE(static, sndpacket, SndPacket);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
sndpackets_class_init (SndPacketsClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = sndpackets_finalize;

  GST_DEBUG_CATEGORY_INIT (sndpackets_debug_category, "sndpackets", 0,
      "MpRTP Manual Sending Controller");

}

void
sndpackets_finalize (GObject * object)
{
  SndPackets *this;
  this = SNDPACKETS(object);

  g_object_unref(this->sysclock);

}

SndPackets* make_sndpackets(void)
{
  SndPackets* this;
  this = g_object_new (SNDPACKETS_TYPE, NULL);
  return this;
}


void
sndpackets_init (SndPackets * this)
{
  this->sysclock                 = gst_system_clock_obtain();
  this->mprtp_ext_header_id      = MPRTP_DEFAULT_EXTENSION_HEADER_ID;
  this->abs_time_ext_header_id   = ABS_TIME_DEFAULT_EXTENSION_HEADER_ID;

  this->recycle                  = make_recycle_sndpacket(32, (RecycleItemShaper) _setup_sndpacket);

  this->keyframe_filtercb        = NULL;
}


void sndpackets_reset(SndPackets* this)
{

}

SndPacket* sndpackets_make_packet(SndPackets* this, GstBuffer* buffer)
{
  SndPacket* result = recycle_retrieve_and_shape(this->recycle, buffer);

  result->made    = _now(this);
  result->buffer  = buffer;
  result->destiny = this->recycle;

  result->mprtp_ext_header_id    = this->mprtp_ext_header_id;
  result->abs_time_ext_header_id = this->abs_time_ext_header_id;

  result->keyframe               = this->keyframe_filtercb ? this->keyframe_filtercb(buffer) : FALSE;

  return result;
}

void sndpackets_set_keyframe_filter_mode(SndPackets* this, guint filtering_mode)
{
  switch(filtering_mode){
  case 1:
    this->keyframe_filtercb = _vp8_keyframe_filter;
    break;
  default:
    this->keyframe_filtercb = NULL;
    break;
  }
}

void sndpackets_set_abs_time_ext_header_id(SndPackets* this, guint8 abs_time_ext_header_id)
{
  this->abs_time_ext_header_id = abs_time_ext_header_id;
}

guint8 sndpackets_get_abs_time_ext_header_id(SndPackets* this)
{
  return this->abs_time_ext_header_id;
}

void sndpackets_set_mprtp_ext_header_id(SndPackets* this, guint8 mprtp_ext_header_id)
{
  this->mprtp_ext_header_id = mprtp_ext_header_id;
}

guint8 sndpackets_get_mprtp_ext_header_id(SndPackets* this)
{
  return this->mprtp_ext_header_id;
}


GstBuffer* sndpacket_retrieve(SndPacket* packet)
{
  GstBuffer *result;
  DISABLE_LINE _setup_abs_time_extension(packet);
  _setup_abs_time_extension(packet);
  result = packet->buffer;
//  g_print("Packet %hu buffer is null by retrieve\n", packet->abs_seq);
  packet->buffer = NULL;
  sndpacket_unref(packet);
  return result;
}


void sndpacket_unref(SndPacket *packet)
{
  if(0 < --packet->ref){
    return;
  }
//  g_print("Packet %hu buffer is null by unref\n", packet->abs_seq);
  packet->buffer = NULL;
  recycle_add( packet->destiny, packet);
}

SndPacket* sndpacket_ref(SndPacket *packet)
{
  ++packet->ref;
  return packet;
}



void sndpacket_setup_mprtp(SndPacket *packet, guint8 subflow_id, guint16 subflow_seq)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  if(!packet->buffer){
    g_print("Packet %hu has no buffer\n", packet->abs_seq);
  }
  packet->buffer = gst_buffer_make_writable(packet->buffer);

  gst_rtp_buffer_map(packet->buffer, GST_MAP_READWRITE, &rtp);
  gst_rtp_buffer_set_mprtp_extension(&rtp, packet->mprtp_ext_header_id, subflow_id, subflow_seq);
  gst_rtp_buffer_unmap(&rtp);

  packet->subflow_id  = subflow_id;
  packet->subflow_seq = subflow_seq;

}

void _setup_sndpacket(SndPacket* result, GstBuffer* buffer)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  memset(result, 0, sizeof(SndPacket));

  gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp);
  result->abs_seq      = gst_rtp_buffer_get_seq(&rtp);
  result->timestamp    = gst_rtp_buffer_get_timestamp(&rtp);
  result->ssrc         = gst_rtp_buffer_get_ssrc(&rtp);
  result->payload_size = gst_rtp_buffer_get_payload_len(&rtp);
  result->payload_type = gst_rtp_buffer_get_payload_type(&rtp);
  result->header_size  = gst_rtp_buffer_get_header_len(&rtp);
  gst_rtp_buffer_unmap(&rtp);

  result->ref     = 1;
}

void _setup_abs_time_extension(SndPacket* packet)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  packet->buffer = gst_buffer_make_writable(packet->buffer);

  gst_rtp_buffer_map(packet->buffer, GST_MAP_READWRITE, &rtp);
  gst_rtp_buffer_set_abs_time_extension(&rtp, packet->abs_time_ext_header_id);
  gst_rtp_buffer_unmap(&rtp);
}



gboolean
_vp8_keyframe_filter(GstBuffer* buffer)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  gboolean is_keyframe;
  guint8 *p;
  unsigned long raw;
  gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp);
  p = gst_rtp_buffer_get_payload(&rtp);
  /* The frame header is defined as a three byte little endian
  * value
  */
  raw = p[0] | (p[1] << 8) | (p[2] << 16);
  is_keyframe     = !BITS_GET(raw, 0, 1);
  gst_rtp_buffer_unmap(&rtp);
  return is_keyframe;
}

