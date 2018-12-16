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
#include "jitterbuffer.h"


GST_DEBUG_CATEGORY_STATIC (jitterbuffer_debug_category);
#define GST_CAT_DEFAULT jitterbuffer_debug_category

#define _now(this) gst_clock_get_time (this->sysclock)

G_DEFINE_TYPE (JitterBuffer, jitterbuffer, G_TYPE_OBJECT);

typedef struct{
  guint32      timestamp;
  guint32      created_in_ts;
  GSList*      packets;
}Frame;

typedef struct {
  gint64 value;
  guint8 subflow_id;
}Skew;

DEFINE_RECYCLE_TYPE(static, frame, Frame);
DEFINE_RECYCLE_TYPE(static, skew, Skew);

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

static gint _cmp_skew(Skew* a, Skew* b) {
  return a->value < b->value ? -1 : b->value < a->value ? 1 : 0;
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

static void _skew_shaper(Skew* to, Skew* from) {
  to->subflow_id = from->subflow_id;
  to->value = from->value;
}

//----------------------------------------------------------------------
//-------- Private functions belongs to JitterBuffer object ----------
//----------------------------------------------------------------------

static void
jitterbuffer_finalize (
    GObject * object);

static Frame* _make_frame(JitterBuffer* this, RcvPacket* packet);
static void _dispose_frame(JitterBuffer* this, Frame* frame);
static void _refresh_playout_delay(JitterBuffer* this);

//----------------------------------------------------------------------
//--------- Private functions implementations to JitterBuffer object --------
//----------------------------------------------------------------------

void
jitterbuffer_class_init (JitterBufferClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = jitterbuffer_finalize;

  GST_DEBUG_CATEGORY_INIT (jitterbuffer_debug_category, "jitterbuffer", 0,
      "MpRTP Manual Sending Controller");
}

void
jitterbuffer_finalize (GObject * object)
{
  JitterBuffer *this = JITTERBUFFER (object);
  RcvPacket* packet;
  while((packet = g_queue_pop_head(this->playoutq)) != NULL){
    rcvpacket_unref(packet);
  }
  g_queue_free(this->playoutq);
  g_object_unref(this->sysclock);

}

void
jitterbuffer_init (JitterBuffer * this)
{
  this->sysclock   = gst_system_clock_obtain ();
  this->made       = _now(this);
}

JitterBuffer*
make_jitterbuffer(TimestampGenerator* rtp_ts_generator)
{
  JitterBuffer *result;
  result = (JitterBuffer *) g_object_new (JITTERBUFFER_TYPE, NULL);
  result->frames = NULL;
  result->frames_recycle = make_recycle_frame(50, NULL);
  result->rtp_ts_generator = g_object_ref(rtp_ts_generator);
//  result->playout_history = make_slidingwindow_double(200, 2 * GST_SECOND);
  result->playoutq   = g_queue_new();
  result->discardedq = g_queue_new();
  result->initial_buffer_time = 500 * GST_MSECOND; // The default buffer time for the jitterbuffer
  result->skews_recycle = make_recycle_skew(50, (RecycleItemShaper)_skew_shaper);
  result->skews = make_slidingwindow_with_data_recycle(500, 2 * GST_SECOND, result->skews_recycle);
  result->subflows = g_malloc(sizeof(JitterBufferSubflow) * MPRTP_PLUGIN_MAX_SUBFLOW_NUM);
  memset(result->subflows, 0, sizeof(JitterBufferSubflow) * MPRTP_PLUGIN_MAX_SUBFLOW_NUM);


  return result;
}

void jitterbuffer_set_initial_buffer_time(JitterBuffer* this, GstClockTime value) {
  this->initial_buffer_time = value;
}

static gboolean _percentile_tracker_filter(guint8* subflow_id, Skew* skew) {
//  g_print("Filter (%d) skew %ld|%d\n", *subflow_id, skew->value, skew->subflow_id);
  return skew->subflow_id == *subflow_id;
}

static void _on_subflow_percentile_calculated(JitterBufferSubflow* this, swpercentilecandidates_t* data) {
  gint64* skew_left = data->left;
  gint64* skew_right = data->right;
//  g_print("_on_subflow_percentile_calculated called by subflow %d\n", this->subflow_id);
  if (data->processed == FALSE) {
    return;
  }
  if (skew_left != NULL && skew_right != NULL) {
    this->median_skew = (*skew_left + *skew_right) / 2;
  } else if (skew_left != NULL) {
    this->median_skew = *skew_left;
  } else if (skew_right != NULL) {
    this->median_skew = *skew_right;
  }
  this->median_skew = MAX(0, this->median_skew);

//  g_print("Max skew on subflow %d is %lu\n", this->subflow_id, GST_TIME_AS_MSECONDS(this->median_skew));
  this->jitter_buffer->max_skew = CONSTRAIN(this->jitter_buffer->max_skew, MAX_JITTER_BUFFER_ALLOWED_SKEW, this->median_skew);
  this->jitter_buffer->max_skew_initialized = TRUE;
}

void jitterbuffer_on_subflow_joined(JitterBuffer* this, RcvSubflow* subflow) {
  JitterBufferSubflow* jitter_buffer_subflow = this->subflows + subflow->id;
  if (jitter_buffer_subflow->active) {
    return;
  }
  jitter_buffer_subflow->active = TRUE;
  jitter_buffer_subflow->subflow_id = subflow->id;
  jitter_buffer_subflow->initialized = FALSE;
  jitter_buffer_subflow->jitter_buffer = this;
  jitter_buffer_subflow->percentile_tracker = make_swpercentile(
      50,
      (bintree3cmp) _cmp_skew,
      (ListenerFunc) _on_subflow_percentile_calculated,
      jitter_buffer_subflow);

  swpercentile_set_filter(
      jitter_buffer_subflow->percentile_tracker,
      (ListenerFilterFunc) _percentile_tracker_filter,
      &subflow->id
  );

  slidingwindow_add_plugin(this->skews, jitter_buffer_subflow->percentile_tracker);
//  g_print("I joined subflow %d for jitterbuffer\n", subflow->id);
}

void jitterbuffer_on_subflow_detached(JitterBuffer* this, RcvSubflow* subflow) {
  JitterBufferSubflow* jitter_buffer_subflow = this->subflows + subflow->id;
  slidingwindow_rem_plugin(this->skews, jitter_buffer_subflow->percentile_tracker);
  jitter_buffer_subflow->active = FALSE;
//  g_print("I detached subflow %d from jitterbuffer\n", subflow->id);
}

gboolean jitterbuffer_is_packet_discarded(JitterBuffer* this, RcvPacket* packet)
{
  if(!this->last_played_seq_init){
    return FALSE;
  }
  return _cmp_seq(packet->abs_seq, this->last_played_seq) < 0;
}

RcvPacket* jitterbuffer_pop_discarded_packet(JitterBuffer* this) {
  RcvPacket* result;
  if (g_queue_is_empty(this->discardedq)) {
    return NULL;
  }
  result = g_queue_pop_head(this->discardedq);
  rcvpacket_unref(result);
  return result;
}


void jitterbuffer_push_packet(JitterBuffer *this, RcvPacket* packet)
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
//      g_print("!!!!!!!! Packet %hu with ts %u is added to the discarded packets queue. last seq:%hu, packet seq: %hu last ts: %u\n",
//          packet->subflow_seq, packet->snd_rtp_ts,
//          this->last_played_seq, packet->abs_seq, this->last_played_sent_ts
//          );
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

  // refresh skews
  {
    JitterBufferSubflow* subflow = this->subflows + packet->subflow_id;
    if (_cmp_seq(subflow->last_subflow_seq, packet->subflow_seq) < 0) {
      if (subflow->initialized) {
        Skew skew;
        guint32 dSnd_in_ts = _delta_ts(subflow->last_snd_ts, packet->snd_rtp_ts);
        guint32 dRcv_ints = _delta_ts(subflow->last_rcv_ts, packet->rcv_rtp_ts);
        gint64 dSending = timestamp_generator_get_time(this->rtp_ts_generator, dSnd_in_ts);
        gint64 dReceiving = timestamp_generator_get_time(this->rtp_ts_generator, dRcv_ints);
        if (dSending < 100 * GST_MSECOND && dReceiving < 100 * GST_MSECOND) {
          skew.value = dReceiving - dSending;
          skew.subflow_id = packet->subflow_id;
//          g_print("Skew %ld|%d is added\n", skew.value, skew.subflow_id);
          slidingwindow_add_data(this->skews, &skew);
        }
      }
      subflow->last_rcv_ts = packet->rcv_rtp_ts;
      subflow->last_snd_ts = packet->snd_rtp_ts;
      subflow->last_subflow_seq = packet->subflow_seq;
      subflow->initialized = TRUE;
    }
  }

  // print
//  for (it = this->frames; it; it = it->next) {
//    Frame* actual = it->data;
//    GSList* jt;
//    g_print("Frame %u, packets: ", actual->timestamp);
//    for (jt = actual->packets; jt; jt = jt->next) {
//      RcvPacket* p = jt->data;
//      g_print("%p->%hu|%d # ", p, p->abs_seq, p->marker);
//    }
//    g_print("\n");
//  }
//  g_print("--------\n");

  //  slidingwindow_add_data(this->packets, packet);
  //done:
  return;

}

gboolean jitterbuffer_has_repair_request(JitterBuffer* this, guint16 *gap_seq)
{
  if(this->gap_seq == -1){
    return FALSE;
  }
  *gap_seq = this->gap_seq;
  this->gap_seq = -1;
  return TRUE;
}

RcvPacket* jitterbuffer_pop_packet(JitterBuffer *this)
{
  RcvPacket* packet = NULL;
  Frame* frame;
  GstClockTime now = _now(this);
  GSList* it;

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
    if (now < this->first_waiting_started + this->initial_buffer_time) {
      // g_print("First remaining time is %lu\n", GST_TIME_AS_MSECONDS(this->first_waiting_started + 2 * this->desired_buffer_time - now));
      return NULL;
    }
    this->last_played_out_time = now;
    this->first_frame_played = TRUE;
  }
  else
  { // We played out frame before
    guint32 dSn_in_ts;
    GstClockTime dSending;
//    this->playout_delay = 0.;
    if (_cmp_ts(frame->timestamp, this->last_played_sent_ts) < 0) {
       dSending = 40 * GST_MSECOND;
    } else {
      dSn_in_ts = _delta_ts(this->last_played_sent_ts, frame->timestamp);
      dSending = timestamp_generator_get_time(this->rtp_ts_generator, dSn_in_ts);
    }
//    dSending = 40 * GST_MSECOND;
//    g_print("dSending: %lu\n", GST_TIME_AS_MSECONDS(dSending));
    if (now < this->last_played_out_time + dSending + this->playout_delay) {
      return NULL;
    }
    //g_print("Waiting time: %lu, Playout delay: %f, Buffer Time: %lu, Remaining time is %f\n",
            //GST_TIME_AS_MSECONDS(dSending),
            //GST_TIME_AS_MSECONDS(this->playout_delay),
            //GST_TIME_AS_MSECONDS(timestamp_generator_get_time(this->rtp_ts_generator, _delta_ts(frame->created_in_ts, timestamp_generator_get_ts(this->rtp_ts_generator)))),
            //GST_TIME_AS_MSECONDS(this->last_played_out_time + dSending + this->playout_delay - now));

    // TODO: it's a we playing out, so let's refresh the adjustment
    // Check if played out time needs to be reset or not.
    this->last_played_out_time += dSending + this->playout_delay;
    if (this->last_played_out_time < now - this->initial_buffer_time * 2) {
      // If the last played out time is lower than a initial buffer time with a long shot,
      // then we either set an initial waiting if the buffer is empty or set the playout now.
      if (g_list_length(this->frames) < 2) {
//        g_print("We need to reset the jitterbuffer to the initial waiting\n");
        this->first_frame_played = FALSE;
      } else {
//        g_print("We need to set the playout to the current time to jump the shift\n");
        this->last_played_out_time = now;
      }
    }

    _refresh_playout_delay(this);
  }

  for (it = frame->packets; it; it = it->next) {
    packet = it->data;
    // TODO: Discards here!
    g_queue_push_tail(this->playoutq, packet);
  }
  this->last_played_sent_ts = frame->timestamp;
  this->last_played_out_ts = timestamp_generator_get_ts(this->rtp_ts_generator);
//  this->last_played_out_time = now;

  g_slist_free(frame->packets);
  frame->packets = NULL;
  this->frames = g_list_remove(this->frames, frame);
  _dispose_frame(this, frame);

playout_from_queue:
  packet = g_queue_pop_head(this->playoutq);
  if(!this->last_played_seq_init){
    this->last_played_seq = packet->abs_seq;
    this->last_played_seq_init = TRUE;
  } else if((guint16)(this->last_played_seq + 2) == packet->abs_seq) {
    this->gap_seq = this->last_played_seq + 1;
  }
  this->last_played_seq = packet->abs_seq;
  this->first_frame_played_out = TRUE;
  rcvpacket_unref(packet);
  return packet;
}

void _refresh_playout_delay(JitterBuffer* this) {
  if (this->max_skew_initialized == FALSE) {
    this->playout_delay = 0;
    goto done;
  }
  if (this->playout_delay_initialized == FALSE) {
    this->playout_delay = this->max_skew;
    this->playout_delay_initialized = TRUE;
  } else {
    this->playout_delay = (this->playout_delay * 124 + this->max_skew) / 125;
  }

done:
  this->max_skew = 0;
  return;
}


Frame* _make_frame(JitterBuffer* this, RcvPacket* packet)
{
  Frame* result = recycle_retrieve(this->frames_recycle);
  result->timestamp = packet->snd_rtp_ts;
  result->created_in_ts = packet->rcv_rtp_ts;
  result->packets = g_slist_prepend(NULL, packet);
  return result;
}

void _dispose_frame(JitterBuffer* this, Frame* frame)
{
  if (frame->packets) {
    g_slist_free(frame->packets);
  }
//  frame->packets = NULL;
  memset(frame, 0, sizeof(Frame));
  recycle_add(this->frames_recycle, frame);
}

