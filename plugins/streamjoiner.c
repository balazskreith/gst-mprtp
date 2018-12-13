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


GST_DEBUG_CATEGORY_STATIC (stream_joiner_debug_category);
#define GST_CAT_DEFAULT stream_joiner_debug_category

#define _now(this) gst_clock_get_time (this->sysclock)

G_DEFINE_TYPE (StreamJoiner, stream_joiner, G_TYPE_OBJECT);

typedef struct{
  guint32      timestamp;
  guint32      created_in_ts;
  GSList*      packets;
}Frame;

DEFINE_RECYCLE_TYPE(static, frame, Frame);

static gint
_cmp_ts (guint32 x, guint32 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 2147483648) return -1;
  if(x > y && x - y > 2147483648) return -1;
  if(x < y && y - x > 2147483648) return 1;
  if(x > y && x - y < 2147483648) return 1;
  return 0;
}

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

static guint32 _delta_ts(guint32 last_ts, guint32 actual_ts) {
  if (last_ts <= actual_ts) {
    return actual_ts - last_ts;
  } else {
    return 4294967296 - last_ts + actual_ts;
  }
}

static gint _packets_cmp(RcvPacket* first, RcvPacket* second)
{
  //It should return 0 if the elements are equal,
  //a negative value if the first element comes
  //before the second, and a positive value
  //if the second element comes before the first.

  return _cmp_seq(first->abs_seq, second->abs_seq);
}

static gint _frames_data_cmp(Frame* first, Frame* second, gpointer udata)
{
  if (first->packets != NULL && second->packets != NULL) {
    return _packets_cmp(first->packets->data, second->packets->data);
  }
  return _cmp_ts(first->timestamp, second->timestamp);
}

//----------------------------------------------------------------------
//-------- Private functions belongs to StreamJoiner object ----------
//----------------------------------------------------------------------

static void
stream_joiner_finalize (
    GObject * object);

static Frame* _make_frame(StreamJoiner* this, RcvPacket* packet);
static void _dispose_frame(StreamJoiner* this, Frame* frame);
static void _update_joining_delay(StreamJoiner* this);

//----------------------------------------------------------------------
//--------- Private functions implementations to StreamJoiner object --------
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
  StreamJoiner *this = STREAMJOINER (object);
  RcvPacket* packet;
  while((packet = g_queue_pop_head(this->playoutq)) != NULL){
    rcvpacket_unref(packet);
  }
  g_queue_free(this->playoutq);
  g_object_unref(this->sysclock);

}

void
stream_joiner_init (StreamJoiner * this)
{
  this->sysclock   = gst_system_clock_obtain ();
  this->made       = _now(this);
}

StreamJoiner*
make_stream_joiner(TimestampGenerator* rtp_ts_generator)
{
  StreamJoiner *result;
  result = (StreamJoiner *) g_object_new (STREAMJOINER_TYPE, NULL);
  result->frames = NULL;
  result->frames_recycle = make_recycle_frame(50, NULL);
  result->rtp_ts_generator = g_object_ref(rtp_ts_generator);
//  result->playout_history = make_slidingwindow_double(200, 2 * GST_SECOND);
  result->playoutq   = g_queue_new();
  result->discardedq = g_queue_new();
  result->desired_buffer_time = 80 * GST_MSECOND;
  result->subflows = g_malloc(sizeof(StreamJoinerSubflow) * MPRTP_PLUGIN_MAX_SUBFLOW_NUM);
  memset(result->subflows, 0, sizeof(StreamJoinerSubflow) * MPRTP_PLUGIN_MAX_SUBFLOW_NUM);

  return result;
}

void stream_joiner_set_desired_buffer_time(StreamJoiner* this, GstClockTime value) {
  this->desired_buffer_time = value;
}

GstClockTime stream_joiner_get_max_join_delay_in_ts(StreamJoiner* this) {
  return this->desired_buffer_time + this->joining_delay;
}

void stream_joiner_on_subflow_joined(StreamJoiner* this, RcvSubflow* subflow) {
  StreamJoinerSubflow* jitter_buffer_subflow = this->subflows + subflow->id;
  if (jitter_buffer_subflow->active) {
    return;
  }
  jitter_buffer_subflow->active = TRUE;
  jitter_buffer_subflow->subflow_id = subflow->id;
  jitter_buffer_subflow->stream_joiner = this;

  ++this->joined_subflows;
}

void stream_joiner_on_subflow_detached(StreamJoiner* this, RcvSubflow* subflow) {
  StreamJoinerSubflow* stream_joiner_subflow = this->subflows + subflow->id;
  if (!stream_joiner_subflow->active) {
    return;
  }
  stream_joiner_subflow->active = FALSE;

  --this->joined_subflows;
}

gboolean stream_joiner_is_packet_discarded(StreamJoiner* this, RcvPacket* packet)
{
  if(!this->last_played_seq_init){
    return FALSE;
  }
  return _cmp_seq(packet->abs_seq, this->last_played_seq) < 0;
}

RcvPacket* stream_joiner_pop_discarded_packet(StreamJoiner* this) {
  RcvPacket* result;
  if (g_queue_is_empty(this->discardedq)) {
    return NULL;
  }
  result = g_queue_pop_head(this->discardedq);
  rcvpacket_unref(result);
  return result;
}

static void _update_subflow_sampling(StreamJoiner *this, RcvPacket* packet) {
  StreamJoinerSubflow* subflow = this->subflows + packet->subflow_id;
  if (!subflow->active) {
    // already detached or not joined subflows' packets are ignored completly
    return;
  }
  if (subflow->played) {
    // If we have a measurement from a played sequence,
    // that one, which matters
    return;
  }
  if (subflow->sampled) {
    // if it is sampled before due to discarded packet, we don't care anymore.
    return;
  }
  if (this->joining_delay_first_updated && _cmp_seq(packet->abs_seq, this->last_joining_delay_update_HSN) < 0) {
    // If the last joining delay refresh has a HSN at that time
    // which is higher than this packet, we do not want to consider
    // this measurement as a sample
    return;
  }
  g_print("Discarded packet is considered as a sample on subflow %hu (abs: %hu) \n",
      packet->subflow_id, packet->abs_seq);
  // Okay so not we do take an account
  // we increase the sampled subflows and set the
  // sampled to TRUE, which means
  // the joining delay update process can be initiated
  subflow->sampled = TRUE;
  if (this->joined_subflows <= ++this->sampled_subflows) {
    // update joining delay
    _update_joining_delay(this);
  }
}

void stream_joiner_push_packet(StreamJoiner *this, RcvPacket* packet)
{
  Frame* frame = NULL;
  GList* it;
  if (!packet) {
    return;
  }
  if (this->reference_points_initialized && this->first_frame_played_out) {
    if (_cmp_seq(packet->abs_seq, this->last_played_seq) < 0 ||
        0
        // We turned this off, because in test we used a video loop
        // turned out to be given the same timestamp, which was used before.
//        _cmp_ts(packet->snd_rtp_ts, this->last_played_sent_ts) < 0
        )
    {
      _update_subflow_sampling(this, packet);
      g_print("!!!!!!!! Packet %hu with ts %u is added to the discarded packets queue. last seq:%hu, packet seq: %hu last ts: %u\n",
          packet->subflow_seq, packet->snd_rtp_ts,
          this->last_played_seq, packet->abs_seq, this->last_played_sent_ts
          );
      g_queue_push_tail(this->discardedq, packet);
      return;
    }
  } else if(!this->reference_points_initialized){
    if (this->last_pushed_seq != 0) {
      if ((guint16) (this->last_pushed_seq + 1) == packet->abs_seq) {
        ++this->consecutive_good_seq;
      } else {
        this->consecutive_good_seq = 0;
      }
      if (3 < this->consecutive_good_seq) {
        this->reference_points_initialized = TRUE;
      }
    }
    this->last_pushed_seq = packet->abs_seq;

  }

  // because it seems g_list_find_custom does not work?????
  for (it = this->frames; it; it = it->next) {
    Frame* actual = it->data;
    if (actual->timestamp == packet->snd_rtp_ts) {
      frame = actual;
      break;
    }
  }

  packet = rcvpacket_ref(packet);
  if(!frame) {
    frame = _make_frame(this, packet);
    this->frames = g_list_insert_sorted_with_data(this->frames, frame, (GCompareDataFunc) _frames_data_cmp, NULL);
  } else {
    frame->packets = g_slist_insert_sorted(frame->packets, packet, (GCompareFunc) _packets_cmp);
  }

  if (_cmp_seq(this->HSN, packet->abs_seq) < 0) {
    this->HSN = packet->abs_seq;
  }

  return;
}

gboolean stream_joiner_has_repair_request(StreamJoiner* this, guint16 *gap_seq)
{
  if(this->gap_seq == -1){
    return FALSE;
  }
  *gap_seq = this->gap_seq;
  this->gap_seq = -1;
  return TRUE;
}

static void _update_subflow_waiting_time(StreamJoinerSubflow* subflow, guint16 abs_seq, GstClockTime waiting_time) {
  StreamJoiner* this = subflow->stream_joiner;

  if (this->joining_delay_first_updated && _cmp_seq(abs_seq, this->last_joining_delay_update_HSN) < 0) {
    // If the last joining delay refresh has a HSN at that time
    // which is higher than this packet, we do not want to consider
    // this measurement as a sample
    return;
  }

  if (!subflow->played) {
    subflow->waiting_time = waiting_time;
    subflow->played = TRUE;
    ++this->played_subflows;
  } else {
    subflow->waiting_time = MIN(subflow->waiting_time, waiting_time);
  }

//  g_print("Subflow %hu (abs: %hu) is updated by a waiting time of %lu, the minimal waiting time is %lu, the nr of played subflows: %d\n",
//      subflow->subflow_id, abs_seq, GST_TIME_AS_MSECONDS(waiting_time), GST_TIME_AS_MSECONDS(subflow->waiting_time),
//      this->played_subflows);

  if (!subflow->sampled) {
    subflow->sampled = TRUE;
  }
}



RcvPacket* stream_joiner_pop_packet(StreamJoiner *this)
{
  RcvPacket* packet = NULL;
  Frame* frame;
  GstClockTime now = _now(this);
  GSList* it;
  GstClockTime dSending = 0;

  if(!g_queue_is_empty(this->playoutq)){
    goto playout_from_queue;
  }

  if (this->frames == NULL) { // We have no frame to playout
    return NULL;
  }
  frame = g_list_first(this->frames)->data;

  // We have frame to playout
  if (this->first_frame_played == FALSE) { // is this the first frame to playout?
    if (!this->initial_time_initialized) {
      this->first_waiting_started = now;
      this->initial_time_initialized = TRUE;
      return NULL;
    }
    if (now < this->first_waiting_started + this->desired_buffer_time) {
//      g_print("First remaining time is %lu\n", GST_TIME_AS_MSECONDS(this->first_waiting_started + 2 * this->desired_buffer_time - now));
      return NULL;
    }
    this->first_frame_played = TRUE;
  }
  else
  {
    guint32 dSn_in_ts;
    dSn_in_ts = _delta_ts(this->last_played_sent_ts, frame->timestamp);
    dSending = timestamp_generator_get_time(this->rtp_ts_generator, dSn_in_ts);
    if (now < this->last_played_out_time + dSending + this->joining_delay) {
      return NULL;
    }

    g_print("Waiting time: %lu, Joining delay: %f, Buffer Time: %lu, Remaining time is %f Frames we have: %d\n",
            GST_TIME_AS_MSECONDS(dSending),
            GST_TIME_AS_MSECONDS(this->joining_delay),
            GST_TIME_AS_MSECONDS(timestamp_generator_get_time(this->rtp_ts_generator, _delta_ts(frame->created_in_ts, timestamp_generator_get_ts(this->rtp_ts_generator)))),
            GST_TIME_AS_MSECONDS(this->last_played_out_time + dSending + this->joining_delay - now),
            g_list_length(this->frames));

    // Check if played out time needs to be reset or not.
    this->last_played_out_time += dSending + this->joining_delay;
  }

  for (it = frame->packets; it; it = it->next) {
    packet = it->data;
    g_queue_push_tail(this->playoutq, packet);
    {
      GstClockTime waiting_time = timestamp_generator_get_time(this->rtp_ts_generator, _delta_ts(packet->rcv_rtp_ts, timestamp_generator_get_ts(this->rtp_ts_generator)));
      _update_subflow_waiting_time(this->subflows + packet->subflow_id, packet->abs_seq, waiting_time);
    }
  }
  if (this->joined_subflows <= ++this->sampled_subflows) {
    // update joining delay
    _update_joining_delay(this);
  }
  this->last_played_out_time = now;
  this->last_played_sent_ts = frame->timestamp;
  this->last_played_out_ts = timestamp_generator_get_ts(this->rtp_ts_generator);
//  this->last_played_out_time = now;

  g_slist_free(frame->packets);
  frame->packets = NULL;
  this->frames = g_list_remove(this->frames, frame);
  _dispose_frame(this, frame);
  this->first_frame_played_out = TRUE;

playout_from_queue:
  packet = g_queue_pop_head(this->playoutq);
  if(!this->last_played_seq_init){
    this->last_played_seq = packet->abs_seq;
    this->last_played_seq_init = TRUE;
  } else if((guint16)(this->last_played_seq + 2) == packet->abs_seq) {
    this->gap_seq = this->last_played_seq + 1;
  }


  this->last_played_seq = packet->abs_seq;
  rcvpacket_unref(packet);
  return packet;
}

void _update_joining_delay(StreamJoiner* this) {
  gdouble alpha;
  gdouble dWaiting;
  guint8 i;
  GstClockTime min_waiting_time = -1;
  for (i = 0; i < MPRTP_PLUGIN_MAX_SUBFLOW_NUM; ++i) {
    StreamJoinerSubflow* subflow = this->subflows + i;
    if (!subflow->active) {
      continue;
    }
    min_waiting_time = MIN(min_waiting_time, subflow->waiting_time);
    // We also do a reset for flags
    subflow->sampled = FALSE;
    subflow->played = FALSE;
  }

  // if it is positive, it means the minimal_waiting_time for the slowest subflow
  // is not reached the desired_buffer_time,
  // so we need to increase the joining delay
  // if it is negative, it means we force the slowest subflow to wait more than the desired
  // buffer time, so we need to decrease the joining delay
  dWaiting =  (gint64) this->desired_buffer_time - (gint64) min_waiting_time;
  dWaiting = CONSTRAIN(-.5 * this->desired_buffer_time, .5 * this->desired_buffer_time, dWaiting);
  // by how drastically we need to increase depends on the
  // distance from the desired buffer time
  alpha = MIN(1.0, pow(dWaiting / (gdouble) this->desired_buffer_time, 2.0));

  // then we need to decide how much we trust for this measurement
  alpha *= (this->joined_subflows <= this->played_subflows) ? 1./16. : 1./32.;

  this->joining_delay += dWaiting * alpha;
//  g_print("Update the joining delay. min_waiting_time is %lu, sampled- (%d) and played subflows (%d), "
//      "dWaiting is %f, alpha is %.2f new joining delay is %f\n",
//      GST_TIME_AS_MSECONDS(min_waiting_time), this->sampled_subflows, this->played_subflows,
//      GST_TIME_AS_MSECONDS(dWaiting), alpha, GST_TIME_AS_MSECONDS(this->joining_delay));

  // Reset the counter for the next update
  this->sampled_subflows = 0;
  this->played_subflows = 0;
  this->last_joining_delay_update_HSN = this->HSN;
  this->joining_delay_first_updated = TRUE;
  return;
}


Frame* _make_frame(StreamJoiner* this, RcvPacket* packet)
{
  Frame* result = recycle_retrieve(this->frames_recycle);
  result->timestamp = packet->snd_rtp_ts;
  result->created_in_ts = packet->rcv_rtp_ts;
  result->packets = g_slist_prepend(NULL, packet);
  return result;
}

void _dispose_frame(StreamJoiner* this, Frame* frame)
{
  if (frame->packets) {
    g_slist_free(frame->packets);
  }
//  frame->packets = NULL;
  memset(frame, 0, sizeof(Frame));
  recycle_add(this->frames_recycle, frame);
}

