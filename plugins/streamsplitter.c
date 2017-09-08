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
#include "streamsplitter.h"
#include <string.h>
#include <stdio.h>
#include <math.h>



GST_DEBUG_CATEGORY_STATIC (stream_splitter_debug_category);
#define GST_CAT_DEFAULT stream_splitter_debug_category

/* class initialization */
G_DEFINE_TYPE (StreamSplitter, stream_splitter, G_TYPE_OBJECT);


#define _qstat(this) sndqueue_get_stat(this->sndqueue)
#define _now(this) gst_clock_get_time (this->sysclock)
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------
//Functions realted to stream_splitter
static void
stream_splitter_finalize (
    GObject * object);


//----------------------------------------------------------------------
//---- Private function implementations to Stream Dealer object --------
//----------------------------------------------------------------------


static GstClockTime last_ratio_printed = 0;
static void _print_ratios(StreamSplitter *this){
  GSList* it;
  if(_now(this) < last_ratio_printed  + 200 * GST_MSECOND) return;
  for(it = sndsubflows_get_subflows(this->subflows); it; it = it->next){
    SndSubflow* subflow = it->data;
    g_print("%d: %f-%f | ",
        subflow->id,
        (gdouble)subflow->target_bitrate / (gdouble)this->total_target,
        (gdouble)_qstat(this)->actual_bitrates[subflow->id] / (gdouble)_qstat(this)->total_bitrate
        );
  }
  last_ratio_printed = _now(this);
  g_print("\n");
}


void
stream_splitter_class_init (StreamSplitterClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = stream_splitter_finalize;

  GST_DEBUG_CATEGORY_INIT (stream_splitter_debug_category, "stream_splitter", 0,
      "Stream Splitter");

}

StreamSplitter* make_stream_splitter(SndSubflows* sndsubflows, SndQueue* sndqueue)
{
  StreamSplitter *this;
  this = g_object_new (STREAM_SPLITTER_TYPE, NULL);
  this->subflows = g_object_ref(sndsubflows);
  this->packets = g_queue_new();
  this->sndqueue = sndqueue;
  return this;
}

void
stream_splitter_finalize (GObject * object)
{
  StreamSplitter *this = STREAM_SPLITTER (object);
  g_object_unref (this->sysclock);
  g_object_unref(this->subflows);
  g_object_unref(this->packets);
}


void
stream_splitter_init (StreamSplitter * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->made                   = _now(this);
}


gint32 stream_splitter_get_total_media_rate(StreamSplitter* this)
{
  // TODO: considering EQD here
  return this->total_target;
}

void
stream_splitter_on_packet_queued(StreamSplitter* this, SndPacket* packet)
{
  GstClockTime threshold = _now(this) - GST_SECOND;

  g_queue_push_tail(this->packets, sndpacket_ref(packet));

  while(!g_queue_is_empty(this->packets)) {
    SndPacket* head = g_queue_peek_head(this->packets);
    if (threshold <= head->queued) {
      break;
    }
    head = g_queue_pop_head(this->packets);
    if (_qstat(this)->actual_bitrates[head->subflow_id]  < this->actual_targets[head->subflow_id] - 22800) {
      this->target_is_reached = FALSE;
    }
    sndpacket_unref(head);
  }
}

void
stream_splitter_on_packet_sent(StreamSplitter* this, SndPacket* packet)
{
//  this->actual_rates[packet->subflow_id] += packet->payload_size<<3; //convert bytes to bits
//  this->total_bitrate += packet->payload_size;
}

void
stream_splitter_on_packet_obsolated(StreamSplitter* this, SndPacket* packet)
{
//  this->actual_rates[packet->subflow_id] -= packet->payload_size<<3; //convert bytes to bits
//  this->total_bitrate -= packet->payload_size<<3;
//
//  if (this->actual_rates[packet->subflow_id]  < this->actual_targets[packet->subflow_id]) {
//    this->target_is_reached = FALSE;
//  }
}

static void _operate_on_total_target(StreamSplitter* this, SndSubflow* subflow, gint op) {
  gint32 subflow_total = this->actual_targets[subflow->id] + this->extra_targets[subflow->id];
  this->total_target += op * subflow_total;
}

void
stream_splitter_on_subflow_target_bitrate_chaned(StreamSplitter* this, SndSubflow* subflow)
{
  _operate_on_total_target(this, subflow, -1);

  if (subflow->state == SNDSUBFLOW_STATE_UNDERUSED) {
    this->extra_targets[subflow->id] = subflow->target_bitrate - this->actual_targets[subflow->id];
  } else {
    this->actual_targets[subflow->id] = subflow->target_bitrate;
  }
  _operate_on_total_target(this, subflow, 1);
}

void
stream_splitter_on_subflow_state_changed(StreamSplitter* this, SndSubflow* subflow)
{
  _operate_on_total_target(this, subflow, -1);

  if (subflow->state != SNDSUBFLOW_STATE_UNDERUSED) {
    this->actual_targets[subflow->id] = subflow->target_bitrate;
    this->extra_targets[subflow->id] = 0;
  }
  _operate_on_total_target(this, subflow, 1);
}

void
stream_splitter_on_subflow_state_stat_changed(StreamSplitter* this, SndSubflowsStateStat* state_stat) {
  this->max_state = CONSTRAIN(state_stat->min, SNDSUBFLOW_STATE_STABLE, state_stat->max);
}

void
stream_splitter_set_keyframe_filtering(StreamSplitter* this, gboolean keyframe_filtering)
{
  this->keyframe_filtering = keyframe_filtering;
}


SndSubflow* stream_splitter_select_subflow(StreamSplitter * this, SndPacket *packet)
{
  SndSubflow *selected = NULL, *subflow;
  GSList* it;
//  gdouble selected_sr, actual_sr;
//  gdouble weight;
  volatile gint32 drate;
  volatile gint32 selected_drate = 0;
  gboolean target_is_reached = TRUE;
//  gchar string[255];
//  memset(string, 0, 255);
  DISABLE_LINE _print_ratios(this);

  for (it = sndsubflows_get_subflows(this->subflows); it; it = it->next) {
    subflow = it->data;
    drate = this->actual_targets[subflow->id] - _qstat(this)->actual_bitrates[subflow->id];

    if (_qstat(this)->actual_bitrates[subflow->id] < this->actual_targets[subflow->id] - MAX(11200, this->actual_targets[subflow->id] * .1) /*  1400 bytes * 8 */) {
      target_is_reached = FALSE;
    }

    if (this->target_is_reached) {
      drate += this->extra_targets[subflow->id];
    }

    if(!subflow->active){
      continue;
    }

    if(this->keyframe_filtering){
      if(packet->keyframe && subflow->state < this->max_state){
        continue;
      }
    }
//    sprintf(string, "%s %d: %d-%d=%d, ", string, subflow->id,
//        this->actual_targets[subflow->id], this->actual_rates[subflow->id], drate);

    if (!selected || selected_drate < drate) {
      selected = subflow;
      selected_drate = drate;
    }
  }

//  g_print("%s selected: %d packet seq: %hu\n", string, selected->id, packet->abs_seq);
  this->target_is_reached = target_is_reached;
//  this->last_selected = selected;
  return selected;
}




