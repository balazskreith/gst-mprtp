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
static gint _find_item_by_seq_helper(gconstpointer item_ptr, gconstpointer desired_seq);
static FECDecoderItem * _find_item_by_seq(GList *items, guint16 seq);
static FECDecoderItem * _take_item_by_seq(GList **items, guint16 seq);
static FECDecoderRequest * _find_request_by_seq(FECDecoder *this, guint16 seq);
static void _remove_from_request(FECDecoder *this, FECDecoderRequest *request);
static void _segment_dtor(FECDecoderSegment *segment);
static FECDecoderSegment* _segment_ctor(void);
static FECDecoderItem* _make_item(FECDecoder *this, GstMpRTPBuffer *mprtp);


static void
_print_segment(FECDecoderSegment *segment)
{
  if(!segment){
    return;
  }
  g_print(
      "##### Segment #####\n"
      "complete:       %d\n"
      "base_sn:        %d\n"
      "high_sn:        %d\n"
      "missing:        %d\n"
      "protected:      %d\n"
      ,
      segment->complete,
      segment->base_sn,
      segment->high_sn,
      segment->missing,
      segment->protected
  );
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
  this->repair_window_max = 300 * GST_MSECOND;
  this->repair_window_min = 10 * GST_MSECOND;
}


void fecdecoder_reset(FECDecoder *this)
{
  THIS_WRITELOCK(this);

  THIS_WRITEUNLOCK(this);
}

void fecdecoder_get_stat(FECDecoder *this,
                         guint32 *early_repaired_bytes,
                         guint32 *total_repaired_bytes,
                         guint32 *total_lost_bytes)
{
  THIS_READLOCK(this);
  if(early_repaired_bytes){
    *early_repaired_bytes = this->total_early_repaired_bytes;
  }

  if(total_repaired_bytes){
    *total_repaired_bytes = this->total_repaired_bytes;
  }

  if(total_lost_bytes){
    *total_lost_bytes = this->total_lost_bytes;
  }
  THIS_READUNLOCK(this);
}

FECDecoder *make_fecdecoder(void)
{
  FECDecoder *this;
  this = g_object_new (FECDECODER_TYPE, NULL);
  this->made = _now(this);
  return this;
}

void fecdecoder_request_repair(FECDecoder *this, guint16 seq, guint payload_bytes)
{
  FECDecoderRequest *request;
  THIS_WRITELOCK(this);
  request = mprtp_malloc(sizeof(FECDecoderRequest));
  request->added         = _now(this);
  request->seq_num       = seq;
  request->payload_bytes = payload_bytes;
  this->requests   = g_list_prepend(this->requests, request);
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
    segment = _find_segment_by_seq(this, request->seq_num);
    if(!segment || segment->missing != 1){
      continue;
    }
    result = TRUE;
    *repairedbuf = _repair_rtpbuf_by_segment(this, segment, request->seq_num);
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
  GstRTPFECHeader   *header;
  guint8*           payload;
  gint16            payload_length;
  guint16 seq,c;
  GstRTPBuffer       rtp = GST_RTP_BUFFER_INIT;

  THIS_WRITELOCK(this);

  gst_rtp_buffer_map(mprtp->buffer, GST_MAP_READ, &rtp);
  payload = gst_rtp_buffer_get_payload(&rtp);
  payload_length = gst_rtp_buffer_get_payload_len(&rtp);
  header = (GstRTPFECHeader*) payload;
  if(header->SSRC_Count  == 0){
    goto done;
  }

  segment               = _segment_ctor();
  segment->added        = _now(this);
  segment->base_sn      = g_ntohs(header->sn_base);
  segment->high_sn      = (guint16)(segment->base_sn + (guint16)(header->N_MASK-1));
  segment->protected    = header->N_MASK;
  segment->items        = NULL;
  segment->missing      = 0;
  segment->ssrc         = g_ntohl(header->ssrc);

  memcpy(segment->fecbitstring, payload, 8);
  memcpy(segment->fecbitstring + 8, &header->length_recovery, 2);
  memcpy(segment->fecbitstring + 10, payload + sizeof(GstRTPFECHeader), payload_length - sizeof(GstRTPFECHeader));

  //collects the segment already arrived
  for(c = 0, seq = segment->base_sn; seq != (segment->high_sn+1) && c < GST_RTPFEC_MAX_PROTECTION_NUM; ++seq, ++c){
    FECDecoderItem* item;
    item = _take_item_by_seq(&this->items, seq);
    if(!item){
      ++segment->missing;
      continue;
    }
    segment->items = g_list_prepend(segment->items, item);
  }
  segment->complete = segment->missing == 0;
  this->segments = g_list_prepend(this->segments, segment);
  DISABLE_LINE _print_segment(segment);
done:
  gst_rtp_buffer_unmap(&rtp);
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
      this->total_lost_bytes+=request->payload_bytes;
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
    if(segment->added < now - this->repair_window_max ||
       segment->complete){
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
  item = _find_item_by_seq(this->items, mprtp->abs_seq);
  if(item){
      GST_WARNING_OBJECT(this, "Duplicated RTP sequence number found");
      goto done;
  }
  item = _make_item(this, mprtp);
  this->items = g_list_prepend(this->items, item);

//  gst_print_rtp_buffer(mprtp->buffer);

done:
  return;
}

void _add_rtp_packet_to_segment(FECDecoder *this, GstMpRTPBuffer *mprtp, FECDecoderSegment *segment)
{
  FECDecoderItem *item = NULL;
  if(_find_item_by_seq(segment->items, mprtp->abs_seq) != NULL){
    GST_WARNING_OBJECT(this, "Duplicated sequece number %hu for RTP packet", mprtp->abs_seq);
    goto done;
  }
  if(--segment->missing < 0){
    GST_WARNING_OBJECT(this, "Duplicated or corrupted FEC segment.");
    goto done;
  }
  item = _make_item(this, mprtp);
  segment->complete = segment->missing == 0;
  segment->items = g_list_prepend(segment->items, item);
  if(segment->repaired){
    this->total_early_repaired_bytes +=
        item->bitstring_length + 12 /*fixed rtp header */ - 10 /*the extra 80 bits at the beginning */;
  }
done:
  return;
}


GstBuffer *_repair_rtpbuf_by_segment(FECDecoder *this, FECDecoderSegment *segment, guint16 seq)
{
  GList *it;
  gint i;
  guint16            length;
  guint8*            databed;
  GstBasicRTPHeader* rtpheader;

  //check weather we have unprocessed items
  for(it = segment->items; it; it = it->next){
    FECDecoderItem *item;
    item = it->data;
    for(i=0; i < item->bitstring_length; ++i){
      segment->fecbitstring[i] ^= item->bitstring[i];
    }
  }
  memcpy(&length, segment->fecbitstring + 8, 2);
  length = g_ntohs(length);
  databed = mprtp_malloc(length + 12);

  memcpy(databed,     segment->fecbitstring,     2);
  memcpy(databed + 4, segment->fecbitstring + 4, 4);

  rtpheader = (GstBasicRTPHeader*) databed;
  rtpheader->version = 2;
  rtpheader->seq_num = g_htons(seq);
  rtpheader->ssrc    = g_htonl(segment->ssrc);
  memcpy(databed + 12, segment->fecbitstring + 10, length);

  this->total_repaired_bytes += length + 12;
  segment->repaired           = TRUE;

  return gst_buffer_new_wrapped(databed, length + 12);
}

gint _find_item_by_seq_helper(gconstpointer item_ptr, gconstpointer desired_seq)
{
  const guint16 *seq = desired_seq;
  const FECDecoderItem *item = item_ptr;
  return item->seq_num == *seq ? 0 : -1;
}

FECDecoderItem * _find_item_by_seq(GList *items, guint16 seq)
{
  FECDecoderItem *result = NULL;
  GList *item;
  item = g_list_find_custom(items, &seq, _find_item_by_seq_helper);
  if(!item){
    goto done;
  }
  result = item->data;
done:
  return result;
}

FECDecoderItem * _take_item_by_seq(GList **items, guint16 seq)
{
  FECDecoderItem *result = NULL;
  result = _find_item_by_seq(*items, seq);
  if(result){
    *items = g_list_remove(*items, result);
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
  if(segment->items){
    g_list_free_full(segment->items, mprtp_free);
  }
  mprtp_free(segment);
}

FECDecoderSegment* _segment_ctor(void)
{
  FECDecoderSegment* result;
  result = mprtp_malloc(sizeof(FECDecoderSegment));
  return result;
}

FECDecoderItem* _make_item(FECDecoder *this, GstMpRTPBuffer *mprtp)
{
  FECDecoderItem* result;
  result = g_malloc0(sizeof(FECDecoderItem));
  rtpfecbuffer_setup_bitstring(mprtp->buffer, result->bitstring, &result->bitstring_length);
  result->seq_num = mprtp->abs_seq;
  result->ssrc    = mprtp->ssrc;
  result->added   = _now(this);
  return result;
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
