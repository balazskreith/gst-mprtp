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

enum{
  FRAME_SCHEDULING = 1,
  PACKET_SCHEDULING = 2,
};

#define _qstat(this) sndqueue_get_stat(this->sndqueue)
#define _now(this) gst_clock_get_time (this->sysclock)
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------
//Functions realted to stream_splitter
static void
stream_splitter_finalize (
    GObject * object);

static SndSubflow*
_select_next_subflow(
    StreamSplitter * this,
    SndPacket *packet);
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
        (gdouble)subflow->desired_target / (gdouble)this->total_stable_target,
//        (gdouble) this->stable_targets[subflow->id] / 1000,
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

  this->total_target = 0;
  this->total_stable_target = 0;
  this->mode = PACKET_SCHEDULING;
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
  // calculate off
  {
    gint32 total_sending_rate = _qstat(this)->total_bitrate + _qstat(this)->bytes_in_queue * 8;
    this->sending_rate_avg = total_sending_rate * .5 + this->sending_rate_avg * .5;
    if (this->total_target == this->total_stable_target) {
      this->target_off = 1.;
    } else {
      this->target_off = (gdouble) (this->sending_rate_avg - this->total_target);
      this->target_off /= (gdouble) (this->total_target - this->total_stable_target);
      this->target_off = 1 + CONSTRAIN(-1., 0., this->target_off);
    }
  }
  return this->total_target;
}

void
stream_splitter_on_packet_queued(StreamSplitter* this, SndPacket* packet)
{

}

void
stream_splitter_on_subflow_joined(StreamSplitter* this, SndSubflow* subflow)
{

}

void
stream_splitter_on_packet_sent(StreamSplitter* this, SndPacket* packet)
{

}

void
stream_splitter_on_packet_obsolated(StreamSplitter* this, SndPacket* packet)
{

}

void
stream_splitter_on_subflow_state_reported(StreamSplitter* this, SndSubflow* subflow) {
  // Here we calculate everything related to coupled congestion control.
}

void
stream_splitter_on_subflow_stable_target_bitrate_chaned(StreamSplitter* this, SndSubflow* subflow) {
  this->total_stable_target -= this->stable_targets[subflow->id];
  this->stable_targets[subflow->id] = subflow->desired_target;
  this->total_stable_target += this->stable_targets[subflow->id];
}

void
stream_splitter_on_subflow_desired_target_chaned(StreamSplitter* this, SndSubflow* subflow)
{
  this->total_target -= this->desired_targets[subflow->id];
  this->desired_targets[subflow->id] = subflow->desired_target;
  this->total_target += this->desired_targets[subflow->id];
}

void
stream_splitter_on_subflow_state_changed(StreamSplitter* this, SndSubflow* subflow)
{

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

SndSubflow* stream_splitter_select_subflow(StreamSplitter * this, SndPacket *packet) {
  gboolean new_frame = this->last_timestamp == 0 || packet->timestamp != this->last_timestamp;
//  if (new_frame) {
//    // Calculate the avarage packet per frame
//  }
  switch(this->mode) {
    case FRAME_SCHEDULING:
      if (new_frame) {
        return this->last_subflow;
      }
      this->last_subflow = _select_next_subflow(this, packet);
      this->last_timestamp = packet->timestamp;
      return this->last_subflow;
    default:
    case PACKET_SCHEDULING:
      return _select_next_subflow(this, packet);
  }
}

SndSubflow* _select_next_subflow(StreamSplitter * this, SndPacket *packet) {
    SndSubflow *selected = NULL, *subflow;
    GSList* it;
    volatile gint32 drate;
    volatile gint32 selected_drate = 0;
    DISABLE_LINE _print_ratios(this);

    for (it = sndsubflows_get_subflows(this->subflows); it; it = it->next) {
      subflow = it->data;
      drate = _qstat(this)->actual_bitrates[subflow->id] + _qstat(this)->queued_bytes[subflow->id] * 8;
      if (0 < this->stable_targets[subflow->id]) {
        drate -= this->stable_targets[subflow->id] * (1.-this->target_off) + this->desired_targets[subflow->id] * this->target_off;
      } else {
        drate -= this->desired_targets[subflow->id];
      }

      if(!subflow->active){
        continue;
      }

      if(this->keyframe_filtering){
        if(packet->keyframe && subflow->state < this->max_state){
          continue;
        }
      }

      if (!selected || drate < selected_drate) {
        selected = subflow;
        selected_drate = drate;
      }
    }
    return selected;
}





