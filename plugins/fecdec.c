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
#include "fecdec.h"
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

GST_DEBUG_CATEGORY_STATIC (fec_decoder_debug_category);
#define GST_CAT_DEFAULT fec_decoder_debug_category

G_DEFINE_TYPE (FECDecoder, fec_decoder, G_TYPE_OBJECT);

#define _now(this) (gst_clock_get_time (this->sysclock))

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
fec_decoder_finalize (GObject * object);


//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

static gint
_cmp_seq (guint16 x, guint16 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 32768) return -1;
  if(x > y && x - y > 32768) return -1;
  if(x < y && y - x > 32768) return 1;
  if(x > y && x - y < 32768) return 1;
  return 0;
}


void
fec_decoder_class_init (FECDecoderClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fec_decoder_finalize;

  GST_DEBUG_CATEGORY_INIT (fec_decoder_debug_category, "fec_decoder", 0,
      "MpRTP Receiving Controller");

}

void
fec_decoder_finalize (GObject * object)
{
  FECDecoder *this = FECDECODER (object);
  g_object_unref (this->sysclock);
}

void
fec_decoder_init (FECDecoder * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->items_read_index  = 0;
  this->items_write_index = 0;
  this->items_counter     = 0;
  this->sysclock          = gst_system_clock_obtain ();
}

FECDecoder*
make_fec_decoder()
{
  FECDecoder *this;
  this = g_object_new (FECDECODER_TYPE, NULL);
  return this;
}

void
fec_decoder_set_payload_type(FECDecoder *this, guint8 payload_type)
{
  THIS_WRITELOCK(this);
  this->payload_type = payload_type;
  THIS_WRITEUNLOCK(this);
}

void
fec_decoder_set_repair_window(FECDecoder *this, GstClockTime repair_window)
{
  THIS_WRITELOCK(this);
  this->repair_window = repair_window;
  THIS_WRITEUNLOCK(this);
}

void
fec_decoder_set_PHSN(FECDecoder *this, guint16 HPSN)
{
  FECPacketItem *item;
  THIS_WRITELOCK(this);
  if(!this->HPSN_init){
    this->HPSN_init = TRUE;
    goto done;
  }
again:
  if(this->items_counter == 0){
    goto done;
  }
  item = this->items + this->items_read_index;
  if(_cmp_seq(this->HPSN, item->sn_base) < 0){
    goto done;
  }
  gst_buffer_unref(item->buffer);
  if(++this->items_read_index == FECPACKETITEMS_LENGTH){
    this->items_read_index = 0;
  }
  --this->items_counter;
  goto again;
done:
  this->HPSN = HPSN;
  THIS_WRITEUNLOCK(this);
}

void
fec_decoder_add_FEC_packet(FECDecoder *this, GstBuffer *buf)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint16 packet_length;
  guint8 tmp[10];
  guint8 *src;
  gint i,c;
  THIS_WRITELOCK(this);
  this->items[this->items_write_index].sn_base = rtpfecbuffer_get_sn_base(buf);
  this->items[this->items_write_index].buffer  = gst_buffer_ref(buf);
  if(++this->items_write_index == FECPACKETITEMS_LENGTH){
      this->items_write_index = 0;
  }
  ++this->items_counter;
  THIS_WRITEUNLOCK(this);
}



#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
