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
  gboolean     marked;
}Frame;

DEFINE_RECYCLE_TYPE(static, frame, Frame);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
stream_joiner_finalize (
    GObject * object);
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
#define JOINED_PACKETS_LENGTH 256
void
stream_joiner_init (StreamJoiner * this)
{
  this->sysclock           = gst_system_clock_obtain ();
  this->made               = _now(this);
  this->max_join_delay_in_ts = DEFAULT_RTP_TIMESTAMP_GENERATOR_CLOCKRATE / 4; // 250ms
  this->min_join_delay_in_ts = DEFAULT_RTP_TIMESTAMP_GENERATOR_CLOCKRATE / 100; // 10ms
//  this->join_delay_in_ts     = (this->max_join_delay_in_ts + this->min_join_delay_in_ts) / 2;
  this->join_delay_in_ts     = this->min_join_delay_in_ts;
}

static void _rcvpacket_ref(StreamJoiner * this, RcvPacket* packet) {rcvpacket_ref(packet);}
static void _rcvpacket_unref(StreamJoiner * this, RcvPacket* packet) {rcvpacket_unref(packet);}

static guint _get_bucket_index(StreamJoiner* this, RcvPacket* packet) {
  GstClockTime skew_in_ms = GST_TIME_AS_MSECONDS(timestamp_generator_get_time(this->rtp_ts_generator, packet->subflow_skew_in_ts));
  if (40 <= skew_in_ms) {
    return 39;
  }
//  g_print("%d:%ld->%lu\n", packet->subflow_id, packet->subflow_skew_in_ts, skew_in_ms);
  return skew_in_ms;
}

static gboolean _filter_packet_for_histogram(RcvSubflow* subflow, RcvPacket* packet) {
//  g_print("filter: %d -> %d | %ld\n", subflow->id, packet->subflow_id, packet->subflow_skew_in_ts);
  return subflow->id == packet->subflow_id;
}

static void _calculated_percentile_index(GstClockTime* median_store, gint* index) {
  g_print("Median: %d\n", *index);
  *median_store = (*index + 1) * GST_MSECOND;
}

static void _on_subflow_joined(StreamJoiner* this, RcvSubflow* subflow) {
  this->infos[subflow->id].skews_histogram = make_swhistogram(
      NULL, // notifier func about the hist values
      NULL, // notifier func udata
      40, // the histogram size <- 1ms until 40
      (SWHistogramIndex) _get_bucket_index, // the transformation function for index
      this, // the transformer udata
      (ListenerFilterFunc) _filter_packet_for_histogram,
      subflow);

  swhistogram_add_percentiletracker( this->infos[subflow->id].skews_histogram,
      50, // percentile
      (ListenerFunc) _calculated_percentile_index, // callback
      &this->infos[subflow->id].skews_median // callback udata
      );
  this->infos[subflow->id].skews_median = 0;
  slidingwindow_add_plugin(this->packets, this->infos[subflow->id].skews_histogram);
}

static void _on_subflow_detached(StreamJoiner* this, RcvSubflow* subflow) {
  slidingwindow_rem_plugin(this->packets, this->infos[subflow->id].skews_histogram);
  swplugin_dtor(this->infos[subflow->id].skews_histogram);
  this->infos[subflow->id].skews_histogram = NULL;
}

StreamJoiner*
make_stream_joiner(TimestampGenerator* rtp_ts_generator, RcvSubflows* subflows)
{
  StreamJoiner *result;
  result = (StreamJoiner *) g_object_new (STREAM_JOINER_TYPE, NULL);

  result->outq   = g_queue_new();
  result->frames = g_queue_new();
  result->frames_recycle = make_recycle_frame(50, NULL);
  result->rtp_ts_generator = g_object_ref(rtp_ts_generator);
//  result->joinq2 = make_bintree3((bintree3cmp) _cmp_rcvpacket_abs_seq);
  result->subflows = subflows;
  result->joined_packets = g_malloc0(sizeof(RcvPacket*)*JOINED_PACKETS_LENGTH);
  result->joined_packets_index = 0;
  result->initial_timeout = 100 * GST_MSECOND;
  result->playout_delay = 0;

  result->packets = make_slidingwindow(200, 2 * GST_SECOND);
  slidingwindow_add_preprocessor(result->packets, (ListenerFunc)_rcvpacket_ref, result);
  slidingwindow_add_postprocessor(result->packets, (ListenerFunc)_rcvpacket_unref, result);

  rcvsubflows_add_on_subflow_joined_cb(result->subflows, (ListenerFunc) _on_subflow_joined, result);
  rcvsubflows_add_on_subflow_detached_cb(result->subflows, (ListenerFunc) _on_subflow_detached, result);

  return result;
}



static gboolean header_printed = FALSE;
static void _stat_print(StreamJoiner *this, RcvPacket* packet)
{
  gchar result[1024];
  memset(result, 0, 1024);

  if (!header_printed) {
    sprintf(result,
        "Elapsed time in sec,"     // 1
        "Min Join Delay,"          // 4
        "Max Join Delay,"          // 5
        "Packet Playout Delay,"    // 6
        "Packet Rcv Ts,"           // 7
        "Last Rcv Ts,"             // 8
        "Packet Subflow Id,"       // 9
        "Packet Abs Seq,"          // 10
        "Packet Subflow Seq,"      // 11
        "Frames Queue Length,"     // 14
        "Outbound Packets Length," // 15
        "Avg Playout Delay,"       // 16
        ""
        );
    g_print("Stat:%s\n",result);
    header_printed = TRUE;
    memset(result, 0, 1024);
  }

  {
    guint32 now_ts = timestamp_generator_get_ts(this->rtp_ts_generator);
    GstClockTime playout_delay = timestamp_generator_get_time(this->rtp_ts_generator,  now_ts - packet->rcv_rtp_ts);

    sprintf(result,
              "%3.1f,"   // 1
              "%5u,"     // 4
              "%5u,"     // 5
              "%1.3f,"   // 6
              "%u,"      // 7
              "%u,"      // 8
              "%hu,"     // 9
              "%hu,"     // 10
              "%hu,"     // 11
              "%d,"      // 14
              "%d,"      // 15
              "%lu,"     // 16
              ,
              GST_TIME_AS_MSECONDS(_now(this) - this->made) / 1000., // 1
              this->min_join_delay_in_ts,  // 4
              this->max_join_delay_in_ts,   // 5
              GST_TIME_AS_MSECONDS(playout_delay) / 1000., // 6
              packet->rcv_rtp_ts,    // 7
              this->last_rcv_rtp_ts, // 8
              packet->subflow_id,    // 9
              packet->abs_seq,       // 10
              packet->subflow_seq,   // 11
              g_queue_get_length(this->frames), // 14
              g_queue_get_length(this->outq),   // 15
              GST_TIME_AS_MSECONDS(this->playout_delay)    // 16
              );
  }

  g_print("Stat:%s\n", result);
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
//  g_print("Packet %hu on %u is pushed\n", packet->abs_seq, packet->subflow_id);
  packet = rcvpacket_ref(packet);
  if(this->max_join_delay_in_ts == 0) {
    g_queue_push_tail(this->outq, packet);
    goto done;
  }

  list = g_queue_find_custom(this->frames, (gconstpointer) packet, _frame_by_packet);
  if(!list) {
    frame = _make_frame(this, packet);
    g_queue_insert_sorted(this->frames, frame, (GCompareDataFunc) _frames_data_cmp, NULL);
  } else {
    frame = list->data;
    frame->packets = g_slist_insert_sorted(frame->packets, packet, (GCompareFunc) _packets_cmp);
    frame->marked |= packet->marker;
  }
  slidingwindow_add_data(this->packets, packet);

done:
  return;
}

static void _select_max_median_delay(RcvSubflow* subflow, StreamJoiner* this) {
  this->max_median_delay = MAX(this->max_median_delay, this->infos[subflow->id].skews_median);
}

RcvPacket* stream_joiner_pop_packet(StreamJoiner *this)
{
  RcvPacket* packet = NULL;
  Frame* first = NULL;
  GstClockTime now = _now(this);
  if(!g_queue_is_empty(this->outq)) {
    packet = g_queue_pop_head(this->outq);
    goto done;
  }

  if(this->max_join_delay_in_ts == 0 || g_queue_is_empty(this->frames)){
    goto done;
  }

  // initial timout delay is here.
  if (!this->initial_timeout_initializes) {
    this->initial_timeout_start = now;
    this->initial_timeout_initializes = TRUE;
    goto done;
  } else if (now < this->initial_timeout_start + this->initial_timeout) {
    goto done;
  } else if (now < this->last_frame_sent + this->avg_frame_interval_time + this->playout_delay ) {
//    g_print("A %lu, %lu\n",  this->avg_frame_interval_time / GST_MSECOND, this->playout_delay / GST_MSECOND);
    goto done;
  }

  first = g_queue_peek_head(this->frames);
//  timeout = this->join_frame_nr * this->frame_inter_arrival_avg_in_ts + playout_delay_in_ts < _delta_ts(first->created_in_ts, timestamp_generator_get_ts(this->rtp_ts_generator));
  DISABLE_LINE _frame_is_ready(this, first);
//  if(!timeout) {
//    goto done;
//  }
//  g_print("Timeout: %d - %d - %hu\n", timeout, _frame_is_ready(this, first), this->hsn);
  first = g_queue_pop_head(this->frames);
  {
    GSList* it;
    guint32 last_snd_rtp_ts = this->last_snd_rtp_ts;
    for(it = first->packets; it; it = it->next) {
      RcvPacket* packet = it->data;
      this->last_abs_seq = packet->abs_seq;
      this->last_subflow_id = packet->subflow_id;
      this->last_rcv_rtp_ts = packet->rcv_rtp_ts;
      this->last_snd_rtp_ts = packet->snd_rtp_ts;
      g_queue_push_tail(this->outq, packet);
    }
    this->last_frame_sent = now;
    // Frame interval refresh
    if (last_snd_rtp_ts != 0 && this->last_snd_rtp_ts != 0) {
      guint32 frame_interval_in_ts = _delta_ts(last_snd_rtp_ts, this->last_snd_rtp_ts);
      GstClockTime frame_interval_in_ns = timestamp_generator_get_time(this->rtp_ts_generator, frame_interval_in_ts);
      if (frame_interval_in_ns < 50 * GST_MSECOND) {
        this->avg_frame_interval_time = this->avg_frame_interval_time * .98 + frame_interval_in_ns * .02;
      }
    }
    // TODO: playout delay recalc
    slidingwindow_refresh(this->packets);
    {
      this->max_median_delay = 0;
      rcvsubflows_iterate(this->subflows, (GFunc)_select_max_median_delay, this);
      this->max_median_delay = MIN(this->max_median_delay, 40 * GST_MSECOND);
      if (!this->playout_delay) {
        this->playout_delay = this->max_median_delay;
      } else {
        this->playout_delay = (this->max_median_delay + 254 * this->playout_delay) / 255;
      }
    }
    packet = g_queue_pop_head(this->outq);
  }

  _dispose_frame(this, first);

done:
  if(!packet) {
    return NULL;
  }
  rcvpacket_unref(packet);

  if(this->hsn_init) {
    this->hsn = _max_seq(this->hsn, packet->abs_seq);
  } else {
    this->hsn = packet->abs_seq;
  }
  DISABLE_LINE _stat_print(this, packet);
  _stat_print(this, packet);
  return packet;
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

