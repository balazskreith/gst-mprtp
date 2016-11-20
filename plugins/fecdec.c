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
#include <math.h>
#include <string.h>
#include "fecdec.h"


#define _now(this) gst_clock_get_time (this->sysclock)


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
typedef enum{
  FECDECODER_MESSAGE_TYPE_FEC_BUFFER        = 1,
  FECDECODER_MESSAGE_TYPE_REPAIR_REQUEST    = 2,
  FECDECODER_MESSAGE_TYPE_RTP_BUFFER        = 3,
  FECDECODER_MESSAGE_TYPE_RESPONSE_LISTENER = 4,
}MessageTypes;

typedef struct{
  MessageTypes type;
  gchar        content[1024];//TODO increase it if any of the message type exceeds this limit!!!!
}Message;

typedef struct{
  MessageTypes type;
  GstBuffer*   buffer;
}FecBufferMessage;

typedef struct{
  MessageTypes type;
  GstBuffer*   buffer;
  guint16      abs_seq;
  guint32      ssrc;
}RTPBufferMessage;

typedef struct{
  MessageTypes     type;
  guint16          missing_seq;
}RepairRequestMessage;

typedef struct{
  MessageTypes     type;
  ListenerFunc     listener;
  gpointer         udata;
}ReponseListenerMessage;

static void fecdecoder_finalize (GObject * object);
static void _clean(FECDecoder *this);;
static void _fecdec_process(gpointer udata);
static void _add_rtp_packet(FECDecoder *this, RTPBufferMessage *packet);
static void _add_fec_buffer(FECDecoder *this, GstBuffer *buffer);
static GstBuffer* _get_repaired_rtpbuffer(FECDecoder *this, guint16 searched_seq);
static void _add_rtp_packet_to_segment(FECDecoder *this, RTPBufferMessage *packet, FECDecoderSegment *segment);
static FECDecoderSegment *_find_segment_by_seq(FECDecoder *this, guint16 seq_num);
static void _add_rtp_packet_to_items(FECDecoder *this, RTPBufferMessage *packet);
static GstBuffer *_repair_rtpbuf_by_segment(FECDecoder *this, FECDecoderSegment *segment, guint16 seq);
static gint _find_item_by_seq_helper(gconstpointer item_ptr, gconstpointer desired_seq);
static FECDecoderItem * _find_item_by_seq(GList *items, guint16 seq);
static FECDecoderItem * _take_item_by_seq(GList **items, guint16 seq);
static void _dispose_item(gpointer item);
static void _segment_dtor(FECDecoder *this, FECDecoderSegment *segment);
static FECDecoderSegment* _segment_ctor(void);
static FECDecoderItem* _make_item(FECDecoder *this, RTPBufferMessage *packet);

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
      "FEC Decoder Component");

}

void
fecdecoder_finalize (GObject * object)
{
  FECDecoder *this;
  this = FECDECODER(object);

  gst_task_stop(this->thread);
  gst_task_join(this->thread);
  gst_object_unref(this->thread);

  g_object_unref(this->sysclock);
//  g_object_unref(this->messenger);
  g_object_unref(this->on_response);

}

void
fecdecoder_init (FECDecoder * this)
{
  this->on_response = make_notifier("FECDec: on-response");
  this->thread      = gst_task_new (_fecdec_process, this, NULL);
  this->sysclock    = gst_system_clock_obtain();
  this->messenger   = make_messenger(sizeof(Message));

}


void fecdecoder_reset(FECDecoder *this)
{

}

FECDecoder *make_fecdecoder(void)
{
  FECDecoder *this;
  this = g_object_new (FECDECODER_TYPE, NULL);
  this->made = _now(this);

  gst_task_set_lock (this->thread, &this->thread_mutex);
  gst_task_start (this->thread);
  return this;
}

void fecdecoder_add_response_listener(FECDecoder *this, ListenerFunc listener, gpointer udata)
{
  ReponseListenerMessage *msg;
  messenger_lock(this->messenger);

  msg = messenger_retrieve_block_unlocked(this->messenger);
  msg->type = FECDECODER_MESSAGE_TYPE_RESPONSE_LISTENER;

  msg->listener = listener;
  msg->udata    = udata;

  messenger_push_block_unlocked(this->messenger, msg);
  messenger_unlock(this->messenger);
}

void fecdecoder_request_repair(FECDecoder *this, guint16 missing_seq)
{
  RepairRequestMessage *msg;
  messenger_lock(this->messenger);

  msg = messenger_retrieve_block_unlocked(this->messenger);
  msg->type = FECDECODER_MESSAGE_TYPE_REPAIR_REQUEST;

  msg->missing_seq = missing_seq;

  messenger_push_block_unlocked(this->messenger, msg);
  messenger_unlock(this->messenger);
}

void fecdecoder_add_rtp_buffer(FECDecoder *this, GstBuffer *buffer)
{
  RTPBufferMessage* msg;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  messenger_lock(this->messenger);

  msg = messenger_retrieve_block_unlocked(this->messenger);
  msg->type = FECDECODER_MESSAGE_TYPE_RTP_BUFFER;

  gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp);
  msg->abs_seq = gst_rtp_buffer_get_seq(&rtp);
  msg->ssrc    = gst_rtp_buffer_get_ssrc(&rtp);
  gst_rtp_buffer_unmap(&rtp);
  msg->buffer = buffer;

  messenger_push_block_unlocked(this->messenger, msg);
  messenger_unlock(this->messenger);
}

void fecdecoder_add_fec_buffer(FECDecoder *this, GstBuffer *buffer)
{
  FecBufferMessage *msg;
  messenger_lock(this->messenger);

  msg = messenger_retrieve_block_unlocked(this->messenger);
  msg->type = FECDECODER_MESSAGE_TYPE_FEC_BUFFER;

  msg->buffer = buffer;

  messenger_push_block_unlocked(this->messenger, msg);
  messenger_unlock(this->messenger);
}

static void _fecdec_process(gpointer udata)
{
  FECDecoder* this = udata;
  Message *msg;

  msg = messenger_pop_block_with_timeout(this->messenger, 1000);
  if(!msg){
    goto done;
  }

  switch(msg->type){
    case FECDECODER_MESSAGE_TYPE_RTP_BUFFER:
    {
      RTPBufferMessage* casted_msg = (RTPBufferMessage*)msg;
      _add_rtp_packet(this, casted_msg);
      gst_buffer_unref(casted_msg->buffer);
    }
    break;
    case FECDECODER_MESSAGE_TYPE_FEC_BUFFER:
    {
      FecBufferMessage* casted_msg = (FecBufferMessage*)msg;
      _add_fec_buffer(this, casted_msg->buffer);
      gst_buffer_unref(casted_msg->buffer);
    }
    break;
    case FECDECODER_MESSAGE_TYPE_REPAIR_REQUEST:
    {
      RepairRequestMessage* casted_msg = (RepairRequestMessage*)msg;
      GstBuffer* rtpbuf;
//      g_print("Repair request arrived for seq: %hu\n", casted_msg->missing_seq);
      rtpbuf = _get_repaired_rtpbuffer(this, casted_msg->missing_seq);
      notifier_do(this->on_response, rtpbuf);
    }
    break;
    case FECDECODER_MESSAGE_TYPE_RESPONSE_LISTENER:
    {
      ReponseListenerMessage* casted_msg = (ReponseListenerMessage*)msg;
      notifier_add_listener(this->on_response, casted_msg->listener, casted_msg->udata);
    }
    break;
    default:
      g_warning("Unhandled message at FECDecoder with type %d", msg->type);
    break;
  }
  messenger_throw_block(this->messenger, msg);

done:
  _clean(this);
  return;
}

void _add_rtp_packet(FECDecoder *this, RTPBufferMessage *packet)
{
  FECDecoderSegment *segment;
  segment =_find_segment_by_seq(this, packet->abs_seq);
  if(!segment){
    _add_rtp_packet_to_items(this, packet);
    goto done;
  }
  _add_rtp_packet_to_segment(this, packet, segment);
done:
  return;
}

void _add_fec_buffer(FECDecoder *this, GstBuffer *buffer)
{
  FECDecoderSegment *segment;
  GstRTPFECHeader   *header;
  guint8*           payload;
  gint16            payload_length;
  guint16 seq,c;
  GstRTPBuffer       rtp = GST_RTP_BUFFER_INIT;

  gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp);
  payload = gst_rtp_buffer_get_payload(&rtp);
  payload_length = gst_rtp_buffer_get_payload_len(&rtp);
  header = (GstRTPFECHeader*) payload;
  if(header->SSRC_Count  == 0){
    goto done;
  }

//  gst_print_rtpfec_buffer(buffer);

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

  //collects the items already arrived
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
}

void _clean(FECDecoder *this)
{
  GList *new_list, *it;
  GstClockTime now;

  now = _now(this);

  //filter out items
  new_list = NULL;
  for(it = this->items; it; it = it->next){
    FECDecoderItem *item;
    item = it->data;
    if(item->added < now - 300 * GST_MSECOND){
      _dispose_item(item);
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
    if(segment->added < now - 300 * GST_MSECOND ||
       segment->complete){
      _segment_dtor(this, segment);
      continue;
    }
    new_list = g_list_prepend(new_list, segment);
  }
  g_list_free(this->segments);
  this->segments = new_list;

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


GstBuffer* _get_repaired_rtpbuffer(FECDecoder *this, guint16 searched_seq)
{
  FECDecoderSegment *segment;
  GList *segi;
  GstBuffer* result = NULL;
  for(segi = this->segments; segi; segi = segi->next){
    guint16 item_seq;
    gint32 missing_seq = -1;
    segment = segi->data;

    if(segment->missing != 1 || segment->repaired) {
      continue;
    }

    if(_cmp_uint16(searched_seq, segment->base_sn) < 0 ||
       _cmp_uint16(segment->base_sn + segment->protected, searched_seq) < 0){
      continue;
    }

    for(item_seq = segment->base_sn; item_seq != (segment->high_sn+1); ++item_seq){
      if(_find_item_by_seq(segment->items, item_seq) == NULL){
        missing_seq = item_seq;
      }
    }

    if(missing_seq != searched_seq){
      break;
    }

    result = _repair_rtpbuf_by_segment(this, segment, missing_seq);
//    gst_print_rtp_buffer(result);

    segment->repaired = TRUE;
    break;
  }

  return result;
}

void _add_rtp_packet_to_items(FECDecoder *this, RTPBufferMessage *packet)
{
  FECDecoderItem *item;
  item = _find_item_by_seq(this->items, packet->abs_seq);
  if(item){
      GST_WARNING_OBJECT(this, "Duplicated RTP sequence number found");
      goto done;
  }
  item = _make_item(this, packet);
  this->items = g_list_prepend(this->items, item);

done:
  return;
}

void _add_rtp_packet_to_segment(FECDecoder *this, RTPBufferMessage *packet, FECDecoderSegment *segment)
{
  FECDecoderItem *item = NULL;
  if(_find_item_by_seq(segment->items, packet->abs_seq) != NULL){
      g_warning("Duplicated sequece number %hu for RTP packet", packet->abs_seq);
    goto done;
  }
  if(--segment->missing < 0){
    g_warning("Duplicated or corrupted FEC segment.");
    goto done;
  }
  item = _make_item(this, packet);
  segment->complete = segment->missing == 0;
  segment->items = g_list_prepend(segment->items, item);

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
  GstBuffer*         result = NULL;
//  GstRTPBuffer       rtp = GST_RTP_BUFFER_INIT;

//  _print_segment(segment);

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

  segment->repaired           = TRUE;
  result = gst_buffer_new_wrapped(databed, length + 12);
  return result;
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

void _dispose_item(gpointer item)
{
  g_slice_free(FECDecoderItem, item);
}

void _segment_dtor(FECDecoder *this, FECDecoderSegment *segment)
{
  if(segment->items){
    g_list_free_full(segment->items, _dispose_item);
//    g_list_free_full(segment->items, mprtp_free);
  }
  g_slice_free(FECDecoderSegment, segment);
}

FECDecoderSegment* _segment_ctor(void)
{
  FECDecoderSegment* result;
//  result = mprtp_malloc(sizeof(FECDecoderSegment));
  result = g_slice_new0(FECDecoderSegment);
  return result;
}

FECDecoderItem* _make_item(FECDecoder *this, RTPBufferMessage *packet)
{
  FECDecoderItem* result;
//  result = g_malloc0(sizeof(FECDecoderItem));
  result = g_slice_new0(FECDecoderItem);
  rtpfecbuffer_setup_bitstring(packet->buffer, result->bitstring, &result->bitstring_length);
  result->seq_num = packet->abs_seq;
  result->ssrc    = packet->ssrc;
  result->added   = _now(this);
  return result;
}


