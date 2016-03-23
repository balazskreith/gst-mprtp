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
static void _add_rtp_packet_to_segment(FECDecoder *this, GstMpRTPBuffer *mprtp, FECDecoderSegment *segment);
static FECDecoderSegment *_find_segment_by_seq(FECDecoder *this, guint16 seq_num);
static void _add_rtp_packet_to_items(FECDecoder *this, GstMpRTPBuffer *mprtp);
static GstBuffer *_repair_rtpbuf_by_segment(FECDecoder *this, FECDecoderSegment *segment, guint16 seq);
static FECDecoderItem * _find_item_by_seq(FECDecoder *this, guint16 seq);
static FECDecoderItem * _take_item_by_seq(FECDecoder *this, guint16 seq);
static FECDecoderRequest * _find_request_by_seq(FECDecoder *this, guint16 seq);
static void _remove_from_request(FECDecoder *this, FECDecoderRequest *request);
static void _segment_dtor(FECDecoderSegment *segment);
static FECDecoderSegment* _segment_ctor(void);


static void
_print_segment(FECDecoderSegment *segment)
{
  gint i;
  if(!segment){
    return;
  }
  g_print(
      "##### Segment #####\n"
      "arrived_length: %d\n"
      "complete:       %d\n"
      "base_sn:        %d\n"
      "high_sn:        %d\n"
      "missing_length: %d\n"
      "missing_num:    %d\n"
      "protected:      %d\n"
      ,
      segment->arrived_length,
      segment->complete,
      segment->base_sn,
      segment->high_sn,
      segment->missing_length,
      segment->missing_num,
      segment->protected
  );
  g_print("----- Arrives -----\n");
  for(i=0; i<segment->arrived_length; ++i){
    g_print("%d| ", segment->arrived[i]);
  }
  g_print("\n----- Missing -----\n");
  for(i=0; i<segment->missing_length; ++i){
    g_print("%d| ", segment->missing[i]);
  }
  g_print("\n###############\n");

}


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
  g_free(this->segments);
}

void
fecdecoder_init (FECDecoder * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sysclock = gst_system_clock_obtain();
  this->repair_window_max = 400 * GST_MSECOND;
  this->repair_window_min = 20 * GST_MSECOND;
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

void fecdecoder_request_repair(FECDecoder *this, guint16 seq)
{
  FECDecoderRequest *request;
  THIS_WRITELOCK(this);
  request = mprtp_malloc(sizeof(FECDecoderRequest));
  request->added = _now(this);
  request->seq_num = seq;
  this->requests = g_list_prepend(this->requests, request);
  THIS_WRITEUNLOCK(this);
}

gboolean fecdecoder_has_repaired_rtpbuffer(FECDecoder *this, GstBuffer** repairedbuf)
{
  FECDecoderSegment *segment;
  FECDecoderRequest *request;
  GList *it;
  gboolean result = FALSE;
  THIS_WRITELOCK(this);
  for(it = this->requests; it; it = it->next){
    request = it->data;
    if(_now(this) - this->repair_window_min < request->added){
      continue;
    }
//    g_print("Active request for repair: %hu\n", request->seq_num);
    segment = _find_segment_by_seq(this, request->seq_num);
    _print_segment(segment);
    if(!segment || segment->missing_num != 1){
      continue;
    }
    result = TRUE;
    *repairedbuf = _repair_rtpbuf_by_segment(this, segment, request->seq_num);
    gst_print_rtp_buffer(*repairedbuf);
    _remove_from_request(this, request);
    goto done;
  }
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
  FECDecoderSegment *segment;
  FECDecoderRequest *request;
  THIS_WRITELOCK(this);
  request = _find_request_by_seq(this, mprtp->abs_seq);
  if(request){
    _remove_from_request(this, request);
  }

  segment =_find_segment_by_seq(this, mprtp->abs_seq);
  if(!segment){
    _add_rtp_packet_to_items(this, mprtp);
    goto done;
  }
  _add_rtp_packet_to_segment(this, mprtp, segment);
done:
  THIS_WRITEUNLOCK(this);
}

void fecdecoder_add_fec_packet(FECDecoder *this, GstMpRTPBuffer *mprtp)
{
  FECDecoderSegment *segment;
  GstRTPFECHeader      header;
  guint16 seq,c;

  THIS_WRITELOCK(this);
  rtpfecbuffer_cpy_header_data(mprtp->buffer, &header);
  if(header.F != 1){//If it not start with 1 it is an empty packet in my view now.
    goto done;
  }

  segment               = _segment_ctor();
  segment->added        = _now(this);
  segment->fec          = gst_buffer_ref(mprtp->buffer);
  segment->base_sn      = g_ntohs(header.sn_base);
  segment->high_sn      = (guint16)(segment->base_sn + (guint16)(header.N_MASK-1));
  segment->protected    = header.N_MASK;
  segment->items        = NULL;

  //collects the segment already arrived
  for(c = 0, seq = segment->base_sn; seq != (segment->high_sn+1) && c < GST_RTPFEC_MAX_PROTECTION_NUM; ++seq, ++c){
    FECDecoderItem* item;
    item = _take_item_by_seq(this, seq);
    if(!item){
      segment->missing[segment->missing_length++] = seq;
      continue;
    }
    segment->arrived[segment->arrived_length++] = seq;
    segment->items = g_list_prepend(segment->items, item);
  }
  segment->missing_num = segment->missing_length;
  segment->complete = segment->arrived_length == segment->protected;
  this->segments = g_list_prepend(this->segments, segment);
  DISABLE_LINE _print_segment(segment);
done:
  THIS_WRITEUNLOCK(this);
}

void fecdecoder_clean(FECDecoder *this)
{
  GList *new_list, *it;
  GstClockTime now;
  THIS_WRITELOCK(this);
  now = _now(this);

  //filter request elements
  new_list = NULL;
  for(it = this->requests; it; it = it->next){
    FECDecoderRequest *request;
    request = it->data;
    if(request->added < now - this->repair_window_max){
      mprtp_free(request);
      continue;
    }
    new_list = g_list_prepend(new_list, request);
  }
  g_list_free(this->requests);
  this->requests = new_list;

  //filter out items
  new_list = NULL;
  for(it = this->items; it; it = it->next){
    FECDecoderItem *item;
    item = it->data;
    if(item->added < now - this->repair_window_max){
      mprtp_free(item);
      continue;
    }
    new_list = g_list_prepend(new_list, item);
  }
  g_list_free(this->items);
  this->items = new_list;


  //filter out segments
  new_list = NULL;
  for(it = this->segments; it; it = it->next){
    FECDecoderSegment *segment;
    segment = it->data;
    if(segment->added < now - this->repair_window_max || segment->complete){
      _segment_dtor(segment);
      continue;
    }
    new_list = g_list_prepend(new_list, segment);
  }
  g_list_free(this->segments);
  this->segments = new_list;

  THIS_WRITEUNLOCK(this);
}

static gint _find_segment_by_seq_helper(gconstpointer segment_ptr, gconstpointer seq_ptr)
{
  const guint16 *seq = seq_ptr;
  const FECDecoderSegment *segment = segment_ptr;
  if(_cmp_uint16(*seq, segment->base_sn) < 0){
    return -1;
  }
  if(_cmp_uint16(segment->high_sn, *seq) < 0){
    return -1;
  }
  return 0;
}

FECDecoderSegment *_find_segment_by_seq(FECDecoder *this, guint16 seq_num)
{
  GList *it;
  FECDecoderSegment *result = NULL;
  it = g_list_find_custom(this->segments, &seq_num, _find_segment_by_seq_helper);
  if(!it){
    goto done;
  }
  result = it->data;
done:
  return result;
}


void _add_rtp_packet_to_items(FECDecoder *this, GstMpRTPBuffer *mprtp)
{
  FECDecoderItem *item;
  item = _find_item_by_seq(this, mprtp->abs_seq);
  if(item){
      GST_WARNING_OBJECT(this, "Duplicated RTP sequence number found");
      goto done;
  }
  item = g_malloc0(sizeof(FECDecoderItem));
  rtpfecbuffer_setup_bitstring(mprtp->buffer, item->bitstring, &item->bitstring_length);
  item->seq_num = mprtp->abs_seq;
  item->ssrc    = mprtp->ssrc;
  item->added   = _now(this);
  this->items = g_list_prepend(this->items, item);
done:
  return;
}

void _add_rtp_packet_to_segment(FECDecoder *this, GstMpRTPBuffer *mprtp, FECDecoderSegment *segment)
{
  gint i;
  for(i=0; i<segment->arrived_length; ++i){
    if(segment->arrived[i] == mprtp->abs_seq){
      GST_WARNING_OBJECT(this, "Duplicated sequece number %hu for RTP packet", mprtp->abs_seq);
      goto done;
    }
  }
  segment->arrived[segment->arrived_length++] = mprtp->abs_seq;
  rtpfecbuffer_add_rtpbuffer_to_fec_segment(&segment->base, mprtp->buffer);

  //elliminate if missed once, mark obsolate if its full
  for(i=0; i<segment->missing_length; ++i){
    if(segment->missing[i] != mprtp->abs_seq){
      continue;
    }
    break;
  }
  if(i == segment->missing_length){
    GST_WARNING_OBJECT(this, "RTP packet bitstring is added, but sequence num not found in missing lists");
    goto done;
  }
  segment->missing[i] = -1;
  if(0 < segment->missing_num){
    if(--segment->missing_num == 0){
      segment->complete = TRUE;
    }
  }
done:
  return;
}


GstBuffer *_repair_rtpbuf_by_segment(FECDecoder *this, FECDecoderSegment *segment, guint16 seq)
{
  GList *it;
  gint i;
  //only called when repair is possible

  //check weather we have unprocessed items
  for(it = segment->items; it; it = it->next){
    FECDecoderItem *item;
    item = it->data;
    for(i=0; i < item->bitstring_length; ++i){
      segment->base.parity_bytes[i] ^= item->bitstring[i];
    }
  }
  return rtpfecbuffer_get_rtpbuffer_by_fec(&segment->base, segment->fec, seq);
}

static gint _find_item_by_seq_helper(gconstpointer item_ptr, gconstpointer desired_seq)
{
  const guint16 *seq = desired_seq;
  const FECDecoderItem *item = item_ptr;
  return item->seq_num == *seq ? 0 : -1;
}

FECDecoderItem * _find_item_by_seq(FECDecoder *this, guint16 seq)
{
  FECDecoderItem *result = NULL;
  GList *item;
  item = g_list_find_custom(this->items, &seq, _find_item_by_seq_helper);
  if(!item){
    goto done;
  }
  result = item->data;
done:
  return result;
}

FECDecoderItem * _take_item_by_seq(FECDecoder *this, guint16 seq)
{
  FECDecoderItem *result = NULL;
  result = _find_item_by_seq(this, seq);
  if(result){
    this->items = g_list_remove(this->items, result);
  }
  return result;
}

static gint _find_request_by_seq_helper(gconstpointer item_ptr, gconstpointer seq_ptr)
{
  const guint16 *seq = seq_ptr;
  const FECDecoderRequest *request = item_ptr;
  return request->seq_num == *seq ? 0 : -1;
}

FECDecoderRequest * _find_request_by_seq(FECDecoder *this, guint16 seq)
{
  FECDecoderRequest *result = NULL;
  GList *item;
  item = g_list_find_custom(this->requests, &seq, _find_request_by_seq_helper);
  if(!item){
    goto done;
  }
  result = item->data;
done:
  return result;
}

void _remove_from_request(FECDecoder *this, FECDecoderRequest *request)
{
  this->requests = g_list_remove(this->requests, request);
}

void _segment_dtor(FECDecoderSegment *segment)
{
  gst_buffer_unref(segment->fec);
  if(segment->items){
    g_list_free_full(segment->items, mprtp_free);
  }
  mprtp_free(segment);
}

FECDecoderSegment* _segment_ctor(void)
{
  FECDecoderSegment* result;
  result = mprtp_malloc(sizeof(FECDecoderSegment));
  rtpfecbuffer_init_segment(&result->base);
  return result;
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
