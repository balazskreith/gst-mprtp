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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "streamjoiner.h"
#include "mprtplogger.h"


GST_DEBUG_CATEGORY_STATIC (stream_joiner_debug_category);
#define GST_CAT_DEFAULT stream_joiner_debug_category

#define _now(this) gst_clock_get_time (this->sysclock)

G_DEFINE_TYPE (StreamJoiner, stream_joiner, G_TYPE_OBJECT);



typedef struct{
  guint32      timestamp;
  guint32      created_in_ts;
  GSList*      packets;
  gboolean     marked;
}Frame;

DEFINE_RECYCLE_TYPE(static, frame, Frame);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
stream_joiner_finalize (
    GObject * object);
static void _refresh_join_delay(StreamJoiner *this, RcvPacket* packet);
static Frame* _make_frame(StreamJoiner* this, RcvPacket* packet);
static void _dispose_frame(StreamJoiner* this, Frame* frame);
static gboolean _frame_is_ready(StreamJoiner* this, Frame* frame);

static guint16
_max_seq (guint16 x, guint16 y)
{
  if(x == y) return x;
  if(x < y && y - x < 32768) return y;
  if(x > y && x - y > 32768) return y;
  if(x < y && y - x > 32768) return x;
  if(x > y && x - y < 32768) return x;
  return x;
}

static guint32 _delta_ts(guint32 last_ts, guint32 actual_ts) {
  if (last_ts <= actual_ts) {
    return actual_ts - last_ts;
  } else {
    return 4294967296 - last_ts + actual_ts;
  }
}

//
//static gint
//_cmp_seq (guint16 x, guint16 y)
//{
//  if(x == y) return 0;
//  if(x < y && y - x < 32768) return -1;
//  if(x > y && x - y > 32768) return -1;
//  if(x < y && y - x > 32768) return 1;
//  if(x > y && x - y < 32768) return 1;
//  return 0;
//}
//
//static gint
//_cmp_ts (guint32 x, guint32 y)
//{
//  if(x == y) return 0;
//  if(x < y && y - x < 2147483648) return -1;
//  if(x > y && x - y > 2147483648) return -1;
//  if(x < y && y - x > 2147483648) return 1;
//  if(x > y && x - y < 2147483648) return 1;
//  return 0;
//}

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
stream_joiner_class_init (StreamJoinerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = stream_joiner_finalize;

  GST_DEBUG_CATEGORY_INIT (stream_joiner_debug_category, "stream_joiner", 0,
      "MpRTP Manual Sending Controller");
}

void
stream_joiner_finalize (GObject * object)
{
  StreamJoiner *this = STREAM_JOINER (object);
  RcvPacket* packet;
  while((packet = g_queue_pop_head(this->outq)) != NULL){
    rcvpacket_unref(packet);
  }
  g_object_unref(this->rtp_ts_generator);
  g_queue_free(this->outq);
  g_object_unref(this->sysclock);
}

void
stream_joiner_init (StreamJoiner * this)
{
  this->sysclock           = gst_system_clock_obtain ();
  this->made               = _now(this);
  this->max_join_delay_in_ts = DEFAULT_RTP_TIMESTAMP_GENERATOR_CLOCKRATE / 2;
  this->min_join_delay_in_ts = DEFAULT_RTP_TIMESTAMP_GENERATOR_CLOCKRATE / 100;
  this->join_delay_in_ts     = (this->max_join_delay_in_ts + this->min_join_delay_in_ts) / 2;
}

StreamJoiner*
make_stream_joiner(TimestampGenerator* rtp_ts_generator)
{
  StreamJoiner *result;
  result = (StreamJoiner *) g_object_new (STREAM_JOINER_TYPE, NULL);

  result->outq   = g_queue_new();
  result->frames = g_queue_new();
  result->frames_recycle = make_recycle_frame(50, NULL);
  result->rtp_ts_generator = g_object_ref(rtp_ts_generator);
//  result->joinq2 = make_bintree3((bintree3cmp) _cmp_rcvpacket_abs_seq);

  return result;
}

guint32 stream_joiner_get_max_join_delay_in_ts(StreamJoiner *this)
{
  return this->max_join_delay_in_ts;
}

void stream_joiner_set_max_join_delay_in_ts(StreamJoiner *this, guint32 max_join_delay_in_ts)
{
  this->max_join_delay_in_ts = max_join_delay_in_ts;
  this->min_join_delay_in_ts = MIN(this->min_join_delay_in_ts, this->max_join_delay_in_ts);
}

guint32 stream_joiner_get_min_join_delay_in_ts(StreamJoiner *this)
{
  return this->min_join_delay_in_ts;
}

void stream_joiner_set_min_join_delay_in_ts(StreamJoiner *this, guint32 min_join_delay_in_ts)
{
  this->min_join_delay_in_ts = min_join_delay_in_ts;
  this->max_join_delay_in_ts = MAX(this->min_join_delay_in_ts, this->max_join_delay_in_ts);
}

guint32 stream_joiner_get_join_delay_in_ts(StreamJoiner *this) {
  return this->join_delay_in_ts;
}

static gint _packets_cmp(RcvPacket* first, RcvPacket* second)
{
  //It should return 0 if the elements are equal,
  //a negative value if the first element comes
  //before the second, and a positive value
  //if the second element comes before the first.

  if(first->abs_seq == second->abs_seq){
    return 0;
  }
  return first->abs_seq < second->abs_seq ? -1 : 1;
}

static gint _frames_data_cmp(Frame* first, Frame* second, gpointer udata)
{
  //It should return 0 if the elements are equal,
  //a negative value if the first element comes
  //before the second, and a positive value
  //if the second element comes before the first.

  if(first->timestamp == second->timestamp){
    return 0;
  }
  return first->timestamp < second->timestamp ? -1 : 1;
}

static gint _frame_by_packet(gconstpointer item, gconstpointer udata)
{
  const Frame* frame = item;
  const RcvPacket* packet = udata;
  return frame->timestamp == packet->snd_rtp_ts ? 0 : -1;
}


void stream_joiner_push_packet(StreamJoiner *this, RcvPacket* packet)
{
  Frame* frame;
  GList* list;

  if(this->max_join_delay_in_ts == 0){
    g_queue_push_tail(this->outq, packet);
    goto done;
  }

  list = g_queue_find_custom(this->frames, (gconstpointer) packet, _frame_by_packet);
  if(!list){
    frame = _make_frame(this, packet);
    g_queue_insert_sorted(this->frames, frame, (GCompareDataFunc) _frames_data_cmp, NULL);
  }else{
    frame = list->data;
    frame->packets = g_slist_insert_sorted(frame->packets, packet, (GCompareFunc) _packets_cmp);
    frame->marked |= packet->marker;
  }

done:
  return;
}

RcvPacket* stream_joiner_pop_packet(StreamJoiner *this)
{
  RcvPacket* packet = NULL;
  Frame* first = NULL;
  gboolean timeout;
  if(!g_queue_is_empty(this->outq)){
    packet = g_queue_pop_head(this->outq);
    goto done;
  }

  if(this->max_join_delay_in_ts == 0 || g_queue_is_empty(this->frames)){
    goto done;
  }

  first = g_queue_peek_head(this->frames);
  timeout = this->join_delay_in_ts < _delta_ts(first->created_in_ts, timestamp_generator_get_ts(this->rtp_ts_generator));
//  g_print("join_delay_in_ts: %u elapsed_time_in_ts: %u\n",
//      this->join_delay_in_ts, _delta_ts(first->created_in_ts, timestamp_generator_get_ts(this->rtp_ts_generator)));
  if(!_frame_is_ready(this, first) && !timeout){
    goto done;
  }
//  g_print("Timeout: %d - %d - %hu\n", timeout, _frame_is_ready(this, first), this->hsn);
  first = g_queue_pop_head(this->frames);
  {
    GSList* it;
    for(it = first->packets; it; it = it->next){
      RcvPacket* packet = it->data;
      g_queue_push_tail(this->outq, packet);
    }
    packet = g_queue_pop_head(this->outq);
  }

  _dispose_frame(this, first);
done:
  if(!packet) {
    return NULL;
  }

  _refresh_join_delay(this, packet);

  if(this->hsn_init){
    this->hsn = _max_seq(this->hsn, packet->abs_seq);
  }else{
    this->hsn = packet->abs_seq;
  }

  return packet;
}

void _refresh_join_delay(StreamJoiner *this, RcvPacket* packet) {
  gint64 skew;
  if (!this->last_abs_seq) {
    goto done;
  }
  if (packet->subflow_id == this->last_subflow_id) {
    goto done;
  }
  skew = (gint64)_delta_ts(this->last_rcv_rtp_ts, packet->rcv_rtp_ts) - (gint64)_delta_ts(this->last_snd_rtp_ts, packet->snd_rtp_ts);
  this->join_delay_in_ts = (CONSTRAIN(this->min_join_delay_in_ts, this->max_join_delay_in_ts, skew) + 255 * this->join_delay_in_ts) / 256;
done:
  this->last_abs_seq = packet->abs_seq;
  this->last_subflow_id = packet->subflow_id;
  this->last_snd_rtp_ts = packet->snd_rtp_ts;
  this->last_rcv_rtp_ts = packet->rcv_rtp_ts;
}

Frame* _make_frame(StreamJoiner* this, RcvPacket* packet)
{
  Frame* result = recycle_retrieve(this->frames_recycle);
  result->timestamp = packet->snd_rtp_ts;
//  result->created_in_ts = _now(this);
  result->created_in_ts = packet->rcv_rtp_ts;
  result->marked = FALSE;
  result->packets = g_slist_prepend(NULL, packet);
  return result;
}

void _dispose_frame(StreamJoiner* this, Frame* frame)
{
  g_slist_free(frame->packets);
  frame->packets = NULL;
  recycle_add(this->frames_recycle, frame);
}

gboolean _frame_is_ready(StreamJoiner* this, Frame* frame)
{
  guint16 seq = this->hsn;
  GSList* it;

//  g_print("Frame is %-3s marked, ", frame->marked ? "": "not");
  if(!frame->marked){
//    g_print(" FALSE\n");
    return FALSE;
  }
  for(it = frame->packets; it; it = it->next){
    RcvPacket* packet = it->data;
//    g_print("seq %hu, ", packet->abs_seq);
    if(++seq != packet->abs_seq){
//      g_print(" FALSE\n");
      return FALSE;
    }
  }
//  g_print(" TRUE\n");
  return TRUE;
}

