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
#include "monitorpackets.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>
#include "bintree.h"

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define _content(this) this->contents[this->contents_actual]

//#define THIS_READLOCK(this)
//#define THIS_READUNLOCK(this)
//#define THIS_WRITELOCK(this)
//#define THIS_WRITEUNLOCK(this)

GST_DEBUG_CATEGORY_STATIC (monitorpackets_debug_category);
#define GST_CAT_DEFAULT monitorpackets_debug_category

G_DEFINE_TYPE (MonitorPackets, monitorpackets, G_TYPE_OBJECT);



//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void monitorpackets_finalize (GObject * object);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
monitorpackets_class_init (MonitorPacketsClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = monitorpackets_finalize;

  GST_DEBUG_CATEGORY_INIT (monitorpackets_debug_category, "monitorpackets", 0,
      "MpRTP Manual Sending Controller");

}

void
monitorpackets_finalize (GObject * object)
{
  MonitorPackets *this;
  this = MONITORPACKETS(object);
  g_object_unref(this->sysclock);
}


void
monitorpackets_init (MonitorPackets * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sysclock          = gst_system_clock_obtain();
  this->queue             = g_queue_new();
  this->protected_packets_num = 0;
  this->max_protected_packets_num = 128;
  monitorpackets_reset(this);
}


void monitorpackets_reset(MonitorPackets *this)
{
  THIS_WRITELOCK(this);
  while(g_queue_is_empty(this->queue) == FALSE){
      gst_buffer_unref(g_queue_pop_tail(this->queue));
  }
  THIS_WRITEUNLOCK(this);
}

void monitorpackets_set_fec_payload_type(MonitorPackets *this, guint8 payload_type)
{
  THIS_WRITELOCK(this);
  this->fec_payload_type = payload_type;
  THIS_WRITEUNLOCK(this);
}

MonitorPackets *make_monitorpackets(void)
{
  MonitorPackets *result;
  result = g_object_new (MONITORPACKETS_TYPE, NULL);
  return result;
}

void monitorpackets_add_outgoing_rtp_packet(MonitorPackets *this,
                         GstBuffer *buf)
{
  GstMapInfo info = GST_MAP_INFO_INIT;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint16 packet_length;
  guint8 tmp[10];
  guint8 *src;
  gint i,c;
  THIS_WRITELOCK(this);
  if(++this->protected_packets_num == this->max_protected_packets_num){
    memset(this->produced_fec_packet, 0, MONITORPACKETS_MAX_LENGTH);
    this->produced_fec_packet_length = 0;
    this->protected_packets_num = 1;
    this->produced_sn_base = -1;
  }

  gst_buffer_map(buf, &info, GST_MAP_READ);
  memcpy(&tmp[0], info.data, 8);
  packet_length = g_htons(g_ntohs(info.size-12));
  memcpy(&tmp[8], &packet_length, 2);
  gst_buffer_unmap(buf, &info);

  for(i=0; i<10; ++i){
      this->produced_fec_packet[i] ^= tmp[i];
  }
  gst_rtp_buffer_map(buf, GST_MAP_READ, &rtp);
  c = gst_rtp_buffer_get_packet_len(&rtp);
  src = gst_rtp_buffer_get_payload(&rtp);
  for(i=0; i<c; ++i){
    this->produced_fec_packet[i+10] ^= src[i];
  }
  if(this->produced_sn_base == -1){
      this->produced_sn_base = gst_rtp_buffer_get_seq(&rtp);
  }
  gst_rtp_buffer_unmap(&rtp);
  this->produced_fec_packet_length = MAX(this->produced_fec_packet_length, c + 10);
  THIS_WRITEUNLOCK(this);
}

void monitorpackets_add_incoming_rtp_packet(MonitorPackets *this, GstBuffer *buf)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  THIS_WRITELOCK(this);
  //implement counter here.

  THIS_WRITEUNLOCK(this);
}

GstBuffer *monitorpackets_process_FEC_packet(MonitorPackets *this, GstBuffer *buf)
{
  GstBuffer *result = NULL;
  THIS_WRITELOCK(this);

  //add fec packet to a list;
  gst_buffer_unref(buf);

  THIS_WRITEUNLOCK(this);
  return result;
}

GstBuffer * monitorpackets_provide_FEC_packet(MonitorPackets *this,
                                              guint8 mprtp_ext_header_id,
                                              guint8 subflow_id)
{
  GstBuffer*                   result = NULL;
  GstRTPBuffer                 rtp = GST_RTP_BUFFER_INIT;
  GstRTPFECHeader             *fecheader;
  guint8                      *fecpayload;
  MPRTPSubflowHeaderExtension *data;
  guint16                      length;
  guint8*                      databed;
  gint i;

  THIS_WRITELOCK(this);
  length = this->produced_fec_packet_length - 10 + sizeof(GstRTPFECHeader);
  result = gst_rtp_buffer_new_allocate (this->produced_fec_packet_length, 0, 0);
//
  gst_rtp_buffer_map(result, GST_MAP_READWRITE, &rtp);
  gst_rtp_buffer_set_payload_type(&rtp, this->fec_payload_type);
  data.id = subflow_id;
  if (++(this->monitor_seq) == 0) {
    ++(this->monitor_cycle);
  }
  data.seq = this->monitor_seq;
  gst_rtp_buffer_set_seq(&rtp, this->monitor_seq);
  gst_rtp_buffer_add_extension_onebyte_header (&rtp, mprtp_ext_header_id,
     (gpointer) & data, sizeof (data));

  databed = fecheader = gst_rtp_buffer_get_payload(&rtp);
  fecheader->F          = 1;
  fecheader->R          = 0;
  fecheader->P          = this->produced_fec_packet[0]>>2;
  fecheader->X          = this->produced_fec_packet[0]>>3;
  fecheader->CC         = this->produced_fec_packet[0]>>4;
  fecheader->M          = this->produced_fec_packet[1];
  fecheader->PT         = this->produced_fec_packet[1]>>1;
  fecheader->reserved   = 0;
  fecheader->N_MASK     = this->protected_packets_num;
  fecheader->M_MASK     = 0;
  fecheader->SSRC_Count = 1;
  fecheader->sn_base    = g_htons(this->produced_sn_base);
  memcpy(&fecheader->TS, &this->produced_fec_packet[4], 4);
  memcpy(&fecheader->length_recovery, &this->produced_fec_packet[8], 2);
  memcpy(databed + sizeof(GstRTPFECHeader), &this->produced_fec_packet[10], this->produced_fec_packet_length - 10);

  gst_rtp_buffer_unmap(&rtp);

  memset(this->produced_fec_packet, 0, MONITORPACKETS_MAX_LENGTH);
  this->produced_fec_packet_length = 0;
  this->protected_packets_num = 0;
  this->produced_sn_base = -1;

  THIS_WRITEUNLOCK(this);
  return result;
}





#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
