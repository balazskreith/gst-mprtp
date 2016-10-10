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

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void sndpackets_finalize (GObject * object);
static void _setup_abs_time_extension(SndPacket* packet);;

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
  g_free(this->packets);

}

SndPackets* make_sndpackets(void)
{
  SndPackets* this;
  this = g_object_new (SNDPACKETS_TYPE, NULL);
  this->packets = g_malloc0(sizeof(SndPacket) * 65536);
  return this;
}


void
sndpackets_init (SndPackets * this)
{
  this->sysclock                 = gst_system_clock_obtain();
  this->mprtp_ext_header_id      = MPRTP_DEFAULT_EXTENSION_HEADER_ID;
  this->abs_time_ext_header_id   = ABS_TIME_DEFAULT_EXTENSION_HEADER_ID;
}


void sndpackets_reset(SndPackets* this)
{
  memset((gpointer)this->packets, 0, sizeof(SndPackets) * 65536);
}

gboolean sndpacket_is_already_in_use(SndPackets* this, GstBuffer* buffer)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  SndPacket* packet;
  gboolean result;
  gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp);
  packet = this->packets +  gst_rtp_buffer_get_seq(&rtp);
  result = 0 < packet->ref && 0 < packet->sent;
  gst_rtp_buffer_unmap(&rtp);
  return result;
}

SndPacket* sndpackets_make_packet(SndPackets* this, GstBuffer* buffer)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  SndPacket* result;
  gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp);
  result = this->packets + gst_rtp_buffer_get_seq(&rtp);
  result->abs_seq      = gst_rtp_buffer_get_seq(&rtp);
  result->timestamp    = gst_rtp_buffer_get_timestamp(&rtp);
  result->ssrc         = gst_rtp_buffer_get_ssrc(&rtp);
  result->payload_size = gst_rtp_buffer_get_payload_len(&rtp);
  result->payload_type = gst_rtp_buffer_get_payload_type(&rtp);
  result->header_size  = gst_rtp_buffer_get_header_len(&rtp);
  gst_rtp_buffer_unmap(&rtp);

  result->buffer  = gst_buffer_make_writable(buffer);
  result->ref     = 1;
  result->base_db = this;
  result->made    = _now(this);

  return result;
}

SndPacket* sndpackets_get_by_abs_seq(SndPackets* this, guint16 abs_seq)
{
  return this->packets + abs_seq;
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

GstBuffer* sndpacket_retrieve_and_send(SndPacket* packet)
{
  GstBuffer *result = packet->buffer;
  _setup_abs_time_extension(packet);
  packet->sent = _now(packet->base_db);
  packet->buffer = NULL;
  sndpacket_unref(packet);
  return result;
}


GstBuffer* sndpacket_retrieve(SndPacket* packet)
{
  GstBuffer *result = packet->buffer;
  packet->buffer = NULL;
  sndpacket_unref(packet);
  return result;
}


void sndpacket_unref(SndPacket *packet)
{
  if(0 < --packet->ref){
    return;
  }
  memset(packet, 0, sizeof(SndPacket));
}

void sndpacket_ref(SndPacket *packet)
{
  ++packet->ref;
}

void sndpacket_setup_mprtp(SndPacket *packet, SndSubflow* subflow)
{
  guint8  mprtp_ext_header_id = sndpackets_get_mprtp_ext_header_id(packet->base_db);
  guint16 subflow_seq = sndsubflow_get_next_subflow_seq(subflow);
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  packet->buffer = gst_buffer_make_writable(packet->buffer);

  gst_rtp_buffer_map(packet->buffer, GST_MAP_READWRITE, &rtp);
  gst_rtp_buffer_set_mprtp_extension(&rtp, mprtp_ext_header_id, subflow->id, subflow_seq);
  gst_rtp_buffer_unmap(&rtp);

  packet->subflow_id = subflow->id;
  packet->subflow_seq = subflow_seq;

}

void _setup_abs_time_extension(SndPacket* packet)
{
  guint8 abs_time_header_ext = sndpackets_get_abs_time_ext_header_id(packet->base_db);
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  packet->buffer = gst_buffer_make_writable(packet->buffer);

  gst_rtp_buffer_map(packet->buffer, GST_MAP_READWRITE, &rtp);
  gst_rtp_buffer_set_abs_time_extension(&rtp, abs_time_header_ext);
  gst_rtp_buffer_unmap(&rtp);
}

