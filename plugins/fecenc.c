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
#include "fecenc.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>
#include "bintree.h"
#include "mprtpspath.h"

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


GST_DEBUG_CATEGORY_STATIC (fecencoder_debug_category);
#define GST_CAT_DEFAULT fecencoder_debug_category

G_DEFINE_TYPE (FECEncoder, fecencoder, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void fecencoder_finalize (GObject * object);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
fecencoder_class_init (FECEncoderClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fecencoder_finalize;

  GST_DEBUG_CATEGORY_INIT (fecencoder_debug_category, "fecencoder", 0,
      "AAAAAAAAA");

}

void
fecencoder_finalize (GObject * object)
{
  FECEncoder *this;
  this = FECENCODER(object);
  g_object_unref(this->sysclock);
}

void
fecencoder_init (FECEncoder * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sysclock = gst_system_clock_obtain();
  this->max_protection_num = 16;
}


void fecencoder_reset(FECEncoder *this)
{
  THIS_WRITELOCK(this);

  THIS_WRITEUNLOCK(this);
}

FECEncoder *make_fecencoder(void)
{
  FECEncoder *this;
  this = g_object_new (FECENCODER_TYPE, NULL);
  this->made = _now(this);
  return this;
}


void fecencoder_set_payload_type(FECEncoder *this, guint8 payload_type)
{
  THIS_WRITELOCK(this);
  this->payload_type = payload_type;
  THIS_WRITEUNLOCK(this);
}


void fecencoder_add_rtpbuffer(FECEncoder *this, GstBuffer *buf)
{
  GstRTPFECSegment* actual;
  THIS_WRITELOCK(this);
  actual = &this->segment;
  if(++actual->processed_packets_num == this->max_protection_num){
      memset(actual->parity_bytes, 0, GST_RTPFEC_PARITY_BYTES_MAX_LENGTH);
      actual->parity_bytes_length   = 0;
      actual->processed_packets_num = 1;
      actual->base_sn               = -1;
  }
  rtpfecbuffer_add_rtpbuffer_to_fec_segment(&this->segment, buf);
  THIS_WRITEUNLOCK(this);
}



GstBuffer*
fecencoder_get_fec_packet(FECEncoder *this)
{
  GstBuffer*                   result = NULL;
  GstRTPBuffer                 rtp = GST_RTP_BUFFER_INIT;
  GstRTPFECSegment*            actual;
  guint16 length;
  gpointer payload;

  THIS_WRITELOCK(this);
  actual = &this->segment;
  memset(this->tmp, 0, GST_RTPFEC_PARITY_BYTES_MAX_LENGTH);

  rtpfecbuffer_get_rtpfec_payload(actual, this->tmp, &length);

  result = gst_rtp_buffer_new_allocate (length, 0, 0);
  gst_rtp_buffer_map(result, GST_MAP_READWRITE, &rtp);
  gst_rtp_buffer_set_payload_type(&rtp, this->payload_type);
  gst_rtp_buffer_set_seq(&rtp, ++this->seq_num);
  payload = gst_rtp_buffer_get_payload(&rtp);
  memcpy(payload, this->tmp, length);
  gst_rtp_buffer_unmap(&rtp);

  memset(actual->parity_bytes, 0, GST_RTPFEC_PARITY_BYTES_MAX_LENGTH);
  actual->parity_bytes_length   = 0;
  actual->processed_packets_num = 0;
  actual->base_sn               = -1;

  THIS_WRITEUNLOCK(this);
  return result;
}

void
fecencoder_assign_to_subflow (FECEncoder * this,
                  GstBuffer *buf,
                  guint8 mprtp_ext_header_id,
                  guint8 subflow_id)
{
  MPRTPSubflowHeaderExtension data;
  GstRTPBuffer                rtp = GST_RTP_BUFFER_INIT;
  THIS_WRITELOCK(this);
  gst_rtp_buffer_map(buf, GST_MAP_READWRITE, &rtp);
  data.id = subflow_id;
  data.seq = ++this->subflows_seg[subflow_id];
  gst_rtp_buffer_add_extension_onebyte_header (&rtp, mprtp_ext_header_id, (gpointer) &data, sizeof (data));
  gst_rtp_buffer_unmap(&rtp);
  THIS_WRITEUNLOCK(this);
}


#undef DEBUG_PRINT_TOOLS
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
