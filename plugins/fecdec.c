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
#include "fecdec.h"
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

static gint
_cmp_uint16 (guint16 x, guint16 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 32768) return -1;
  if(x > y && x - y > 32768) return -1;
  if(x < y && y - x > 32768) return 1;
  if(x > y && x - y < 32768) return 1;
  return 0;
}


GST_DEBUG_CATEGORY_STATIC (fecdecoder_debug_category);
#define GST_CAT_DEFAULT fecdecoder_debug_category

G_DEFINE_TYPE (FECDecoder, fecdecoder, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void fecdecoder_finalize (GObject * object);
static FECDecoderItem *_find_item(FECDecoder *this, guint16 sn);
//#define _trash_node(this, node) g_slice_free(FECDecoderNode, node)
#define _trash_node(this, node) g_free(node)
//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
fecdecoder_class_init (FECDecoderClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fecdecoder_finalize;

  GST_DEBUG_CATEGORY_INIT (fecdecoder_debug_category, "fecdecoder", 0,
      "AAAAAAAAA");

}

void
fecdecoder_finalize (GObject * object)
{
  FECDecoder *this;
  this = FECDECODER(object);
  g_object_unref(this->sysclock);
  g_free(this->items);
}

void
fecdecoder_init (FECDecoder * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sysclock = gst_system_clock_obtain();
  this->items_length = 100;
  this->items = g_malloc0(sizeof(FECDecoderItem) * this->items_length);
}


void fecdecoder_reset(FECDecoder *this)
{
  THIS_WRITELOCK(this);

  THIS_WRITEUNLOCK(this);
}



FECDecoder *make_fecdecoder(void)
{
  FECDecoder *this;
  this = g_object_new (FECDECODER_TYPE, NULL);
  this->made = _now(this);
  return this;
}

GstBuffer* fecdecoder_repair_rtp(FECDecoder *this, guint16 sn)
{
  FECDecoderItem *item;
  GstBuffer* result = NULL;
  THIS_WRITELOCK(this);
  item = _find_item(this, sn);
  if(!item){
    goto done;
  }
  if(item->processed != item->protected - 1){
    goto done;
  }
  result = rtpfecbuffer_get_rtpbuffer_by_fec(&item->segment, item->fec, sn);
done:
  THIS_WRITEUNLOCK(this);
  return result;
}

void fecdecoder_set_payload_type(FECDecoder *this, guint8 payload_type)
{
  THIS_WRITELOCK(this);
  this->payload_type = payload_type;
  THIS_WRITEUNLOCK(this);
}

void fecdecoder_add_rtp_packet(FECDecoder *this, GstMpRTPBuffer *mprtp)
{
  FECDecoderItem *item;
  gint i;
  THIS_WRITELOCK(this);
  item =_find_item(this, mprtp->abs_seq);
  if(!item){
    goto done;
  }
  if(item->protected <= item->processed){
    goto done;
  }

  for(i = 0; item->sequences[i] != -1; ++i){
    if((guint16)item->sequences[i] == mprtp->abs_seq){
      goto done;
    }
  }
  item->sequences[i] = mprtp->abs_seq;
  ++item->processed;
  rtpfecbuffer_add_rtpbuffer_to_fec_segment(&item->segment, mprtp->buffer);
done:
  THIS_WRITEUNLOCK(this);
}

void fecdecoder_add_fec_packet(FECDecoder *this, GstMpRTPBuffer *mprtp)
{
  FECDecoderItem *item;
  GstRTPFECHeader      header;

  THIS_WRITELOCK(this);
  item = this->items + this->items_write_index;
  rtpfecbuffer_cpy_header_data(mprtp->buffer, &header);
  if(header.F != 1){//If it not start with 1 it is an empty packet in my view now.
    goto done;
  }
  item->added        = _now(this);
  item->processed      = 0;
  item->fec          = gst_buffer_ref(mprtp->buffer);
  item->base_sn      = g_ntohs(header.sn_base);
  item->expected_hsn = (guint16)(item->base_sn + (guint16)header.N_MASK);
  item->protected      = header.N_MASK;
  memset(item->sequences, (gint32)-1, sizeof(gint32) * 16);

  if(++this->items_write_index == this->items_length){
    this->items_write_index = 0;
  }
  ++this->counter;
done:
  THIS_WRITEUNLOCK(this);
}

void fecdecoder_obsolate(FECDecoder *this)
{
  FECDecoderItem *item = NULL;
  THIS_WRITELOCK(this);
again:
  if(this->counter < 1){
    goto done;
  }
  item = this->items + this->items_read_index;
  if(_now(this) - 400 * GST_MSECOND < item->added){
    goto done;
  }
  gst_buffer_unref(item->fec);
  memset(item, 0, sizeof(FECDecoderItem));
  --this->counter;
  if(++this->items_read_index == this->items_length){
    this->items_read_index = 0;
  }
  goto again;
done:
  THIS_WRITEUNLOCK(this);
}

FECDecoderItem *_find_item(FECDecoder *this, guint16 sn)
{
  FECDecoderItem *result = NULL;
  gint32 read_index;
  if(this->counter < 1){
    goto done;
  }
  read_index = this->items_read_index;
again:
  result = this->items + read_index;
  if(_cmp_uint16(result->base_sn, sn) <= 0 &&
     _cmp_uint16(sn, result->expected_hsn) <= 0){
    goto done;
  }
  result = NULL;
  if(read_index == this->items_write_index){
    goto done;
  }
  if(++read_index == this->items_length){
    read_index = 0;
  }
  goto again;
done:
  return result;
}

#undef DEBUG_PRINT_TOOLS
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
