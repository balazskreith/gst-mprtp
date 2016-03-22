/* GStreamer Scheduling tree
 * Copyright (C) 2015 Balázs Kreith (contact: balazs.kreith@gmail.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be ureful,
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
#include "fecenc.h"
#include "gstmprtcpbuffer.h"
#include "mprtprpath.h"
#include "streamjoiner.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define THIS_READLOCK(this)
#define THIS_READUNLOCK(this)
#define THIS_WRITELOCK(this)
#define THIS_WRITEUNLOCK(this)

//#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
//#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
//#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
//#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define DATABED_LENGTH 1400

GST_DEBUG_CATEGORY_STATIC (fec_encoder_debug_category);
#define GST_CAT_DEFAULT fec_encoder_debug_category

G_DEFINE_TYPE (FECEncoder, fec_encoder, G_TYPE_OBJECT);

#define _now(this) (gst_clock_get_time (this->sysclock))

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
fec_encoder_finalize (GObject * object);


//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------


void
fec_encoder_class_init (FECEncoderClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fec_encoder_finalize;

  GST_DEBUG_CATEGORY_INIT (fec_encoder_debug_category, "fec_encoder", 0,
      "MpRTP Receiving Controller");

}

void
fec_encoder_finalize (GObject * object)
{
  FECEncoder *this = FECENCODER (object);
  g_object_unref (this->sysclock);
}

void
fec_encoder_init (FECEncoder * this)
{
  g_rw_lock_init (&this->rwmutex);

  this->sysclock   = gst_system_clock_obtain ();
}

FECEncoder*
make_fec_encoder()
{
  FECEncoder *this;
  this = g_object_new (FECENCODER_TYPE, NULL);
  return this;
}

void
fec_encoder_set_payload_type(FECEncoder *this, guint8 payload_type)
{
  THIS_WRITELOCK(this);
  this->payload_type = payload_type;
  THIS_WRITEUNLOCK(this);
}

void
fec_encoder_add_rtp_packet(FECEncoder *this, GstBuffer *buf)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint16 packet_length;
  guint8 tmp[10];
  guint8 *src;
  gint i,c;
  THIS_WRITELOCK(this);
  if(++this->actual_packets_num == this->max_packets_num){
    memset(this->parity_bytes, 0, FECPACKET_MAX_LENGTH);
    this->parity_bytes_length = 0;
    this->actual_packets_num = 1;
    this->sn_base = -1;
  }

  rtpfecbuffer_get_inistring(buf, tmp);
  for(i=0; i<10; ++i){
      this->parity_bytes[i] ^= tmp[i];
  }
  gst_rtp_buffer_map(buf, GST_MAP_READ, &rtp);
  c = gst_rtp_buffer_get_packet_len(&rtp);
  src = gst_rtp_buffer_get_payload(&rtp);
  for(i=0; i<c; ++i){
    this->parity_bytes[i+10] ^= src[i];
  }
  if(this->sn_base == -1){
      this->sn_base = gst_rtp_buffer_get_seq(&rtp);
  }
  gst_rtp_buffer_unmap(&rtp);
  this->parity_bytes_length = MAX(this->parity_bytes_length, c + 10);
  THIS_WRITEUNLOCK(this);
}

GstBuffer*
fec_encoder_get_FEC_packet(FECEncoder *this,
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
  length = this->parity_bytes_length - 10 + sizeof(GstRTPFECHeader);
  result = gst_rtp_buffer_new_allocate (this->parity_bytes_length, 0, 0);
//
  gst_rtp_buffer_map(result, GST_MAP_READWRITE, &rtp);
  gst_rtp_buffer_set_payload_type(&rtp, this->payload_type);
  data.id = subflow_id;
  if (++(this->sequence) == 0) {
    ++(this->cycle);
  }
  data.seq = this->sequence;
  gst_rtp_buffer_set_seq(&rtp, this->sequence);
  gst_rtp_buffer_add_extension_onebyte_header (&rtp, mprtp_ext_header_id,
     (gpointer) & data, sizeof (data));

  databed = fecheader = gst_rtp_buffer_get_payload(&rtp);
  fecheader->F          = 1;
  fecheader->R          = 0;
  fecheader->P          = this->parity_bytes[0]>>2;
  fecheader->X          = this->parity_bytes[0]>>3;
  fecheader->CC         = this->parity_bytes[0]>>4;
  fecheader->M          = this->parity_bytes[1];
  fecheader->PT         = this->parity_bytes[1]>>1;
  fecheader->reserved   = 0;
  fecheader->N_MASK     = this->actual_packets_num;
  fecheader->M_MASK     = 0;
  fecheader->SSRC_Count = 1;
  fecheader->sn_base    = g_htons(this->sn_base);
  memcpy(&fecheader->TS, &this->parity_bytes[4], 4);
  memcpy(&fecheader->length_recovery, &this->parity_bytes[8], 2);
  memcpy(databed + sizeof(GstRTPFECHeader), &this->parity_bytes[10], this->parity_bytes_length - 10);

  gst_rtp_buffer_unmap(&rtp);

  memset(this->parity_bytes, 0, FECPACKET_MAX_LENGTH);
  this->parity_bytes_length = 0;
  this->actual_packets_num = 0;
  this->sn_base = -1;

  THIS_WRITEUNLOCK(this);
  return result;
}


#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
