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
#include "rcvpackets.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>
#include "rtpfecbuffer.h"
#include "lib_swplugins.h"

#define _now(this) gst_clock_get_time (this->sysclock)

GST_DEBUG_CATEGORY_STATIC (rcvpackets_debug_category);
#define GST_CAT_DEFAULT rcvpackets_debug_category

G_DEFINE_TYPE (RcvPackets, rcvpackets, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void rcvpackets_finalize (GObject * object);
static void _check_buffer_meta_data(RcvPackets * this, RcvPacket *packet);
static void _setup_rcvpacket(RcvPacket* packet, GstRTPBuffer *rtp);
static void _extract_mprtp_info(RcvPackets* this, RcvPacket* packet, GstRTPBuffer *rtp);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

DEFINE_RECYCLE_TYPE(static, rcvpacket, RcvPacket);

static void _rcvpacket_shaper(RcvPacket* result, gpointer udata)
{
  memset(result, 0, sizeof(RcvPacket));
}

void
rcvpackets_class_init (RcvPacketsClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = rcvpackets_finalize;

  GST_DEBUG_CATEGORY_INIT (rcvpackets_debug_category, "rcvpackets", 0,
      "MpRTP Manual Sending Controller");

}

void
rcvpackets_finalize (GObject * object)
{
  RcvPackets *this;

  this = RCVPACKETS(object);

  g_object_unref(this->recycle);
  g_object_unref(this->sysclock);

}

RcvPackets* make_rcvpackets(void)
{
  RcvPackets* this;
  this = g_object_new (RCVPACKETS_TYPE, NULL);
  this->recycle = make_recycle_rcvpacket(256, (RecycleItemShaper)_rcvpacket_shaper);
  return this;
}


void
rcvpackets_init (RcvPackets * this)
{
  this->sysclock                 = gst_system_clock_obtain();
  this->mprtp_ext_header_id      = MPRTP_DEFAULT_EXTENSION_HEADER_ID;
  this->abs_time_ext_header_id   = ABS_TIME_DEFAULT_EXTENSION_HEADER_ID;
  this->pivot_address_subflow_id = 0;
  this->pivot_address            = NULL;
}


void rcvpackets_reset(RcvPackets* this)
{

}


RcvPacket* rcvpackets_get_packet(RcvPackets* this, GstBuffer* buffer)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  RcvPacket* packet;

  packet = recycle_retrieve_and_shape(this->recycle, NULL);

  gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp);
  _setup_rcvpacket(packet, &rtp);
  _extract_mprtp_info(this, packet, &rtp);
  gst_rtp_buffer_unmap(&rtp);

  packet->buffer   = buffer;
  packet->ref      = 1;
  packet->received = _now(this);
  packet->destiny  = this->recycle;

  _check_buffer_meta_data(this, packet);

  return packet;
}


void rcvpackets_set_abs_time_ext_header_id(RcvPackets* this, guint8 abs_time_ext_header_id)
{
  this->abs_time_ext_header_id = abs_time_ext_header_id;
}

guint8 rcvpackets_get_abs_time_ext_header_id(RcvPackets* this)
{
  return this->abs_time_ext_header_id;
}

void rcvpackets_set_mprtp_ext_header_id(RcvPackets* this, guint8 mprtp_ext_header_id)
{
  this->mprtp_ext_header_id = mprtp_ext_header_id;
}

guint8 rcvpackets_get_mprtp_ext_header_id(RcvPackets* this)
{
  return this->mprtp_ext_header_id;
}

GstBuffer* rcvpacket_retrieve_buffer_and_unref(RcvPacket* packet)
{
  GstBuffer *result = packet->buffer;
  packet->buffer = NULL;
  rcvpacket_unref(packet);
  return result;
}


void rcvpacket_unref(RcvPacket *packet)
{
  if(0 < --packet->ref){
    return;
  }

  recycle_add(packet->destiny, packet);
}

RcvPacket* rcvpacket_ref(RcvPacket *packet)
{
  ++packet->ref;
  return packet;
}



void _check_buffer_meta_data(RcvPackets * this, RcvPacket *packet)
{
  GstNetAddressMeta *meta;
  //to avoid the check_collision problem in rtpsession.
  meta = gst_buffer_get_net_address_meta (packet->buffer);

  if(!meta){
    return;
  }

  if (!this->pivot_address) {
    this->pivot_address_subflow_id = packet->subflow_id;
    this->pivot_address = G_SOCKET_ADDRESS (g_object_ref (meta->addr));
    return;
  }

  if (packet->subflow_seq == this->pivot_address_subflow_id) {
    return;
  }

  if(gst_buffer_is_writable(packet->buffer))
    gst_buffer_add_net_address_meta (packet->buffer, this->pivot_address);
  else{
    packet->buffer = gst_buffer_make_writable(packet->buffer);
    gst_buffer_add_net_address_meta (packet->buffer, this->pivot_address);
  }
}

void _setup_rcvpacket(RcvPacket* packet, GstRTPBuffer *rtp)
{
  packet->abs_seq      = gst_rtp_buffer_get_seq(rtp);
  packet->timestamp    = gst_rtp_buffer_get_timestamp(rtp);
  packet->ssrc         = gst_rtp_buffer_get_ssrc(rtp);
  packet->payload_size = gst_rtp_buffer_get_payload_len(rtp);
  packet->payload_type = gst_rtp_buffer_get_payload_type(rtp);
  packet->header_size  = gst_rtp_buffer_get_header_len(rtp);
}


void _extract_mprtp_info(RcvPackets* this, RcvPacket* packet, GstRTPBuffer *rtp)
{

  guint8 mprtp_ext_header_id = rcvpackets_get_mprtp_ext_header_id(this);

  packet->abs_snd_ntp_time = gst_rtp_buffer_get_abs_time_extension(rtp, this->abs_time_ext_header_id);

  packet->delay = get_epoch_time_from_ntp_in_ns(NTP_NOW - packet->abs_snd_ntp_time);

  gst_rtp_buffer_get_mprtp_extension(rtp, mprtp_ext_header_id, &packet->subflow_id, &packet->subflow_seq);

}
