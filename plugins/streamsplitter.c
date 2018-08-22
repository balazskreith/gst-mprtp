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
static void _print_ratios(StreamSplitter *this) {
  GSList* it;
  if(_now(this) < last_ratio_printed  + 200 * GST_MSECOND) return;
  for(it = sndsubflows_get_subflows(this->subflows); it; it = it->next){
    SndSubflow* subflow = it->data;
    g_print("%d: %f-%f | ",
        subflow->id,
        (gdouble)subflow->allocated_target / (gdouble)this->stable_rate,
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

StreamSplitter* make_stream_splitter(SndSubflows* sndsubflows, SndTracker* tracker, SndQueue* sndqueue)
{
  StreamSplitter *this;
  this = g_object_new (STREAM_SPLITTER_TYPE, NULL);
  this->subflows = g_object_ref(sndsubflows);
  this->packets = g_queue_new();
  this->sndqueue = sndqueue;
  this->tracker = tracker;

  this->media_rate = TOTAL_MIN_SENDING_RATE;
  this->stable_rate = 0;
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
  // NOT HERE, BUT WHEN UPDATE IS REGULAR IN TIME
  gint32 targets_dif = abs(this->media_rate - this->stable_rate);
  if (!this->media_rate || !this->stable_rate) {
    return 0;
  }
  if (this->media_rate_std == 0) {
    this->media_rate_std = targets_dif;
  } else {
    this->media_rate_std = (this->media_rate_std * 31 + targets_dif) / 32;
  }

  this->media_rate = MAX(this->media_rate, this->min_sending_rate);
  return this->media_rate;
}


static void _subflow_report_handler(StreamSplitter* this, SndSubflow* source)
{
  GSList* it;
  SndSubflow* subflow;
//  gint32 ongoing_ramp_up = 0;
  gint32 max_ramp_up = 0;
  gdouble total_weight = 0.0;
  gint32 sum_reduction = 0;
//  gint32 remaining_increase = 0;

  this->stable_rate = 0;
  this->min_sending_rate = 0;
  for (it = sndsubflows_get_subflows(this->subflows); it; it = it->next) {
    subflow = it->data;
    if (!subflow->active) continue;
//    if (subflow->stable_bitrate < subflow->allocated_target)
//    {
//      ongoing_ramp_up += subflow->allocated_target - subflow->stable_bitrate;
//    }
//    else
    if (subflow->stable_bitrate < subflow->estimated_target)
    {
      gint32 ramp_up = subflow->estimated_target - subflow->stable_bitrate;
      max_ramp_up = MAX(max_ramp_up, ramp_up);
      if (0. < subflow->rtt) {
        gdouble rtt_in_s = (gdouble) GST_TIME_AS_SECONDS(subflow->rtt * 10) / 10000.;
        total_weight += (gdouble)ramp_up / rtt_in_s;
      } else {
        total_weight += ramp_up;
      }
    }
    else if (subflow->estimated_target <= subflow->stable_bitrate)
    {
      sum_reduction += subflow->stable_bitrate - subflow->estimated_target;
      this->allocated_targets[subflow->id] = subflow->allocated_target = MAX(subflow->min_sending_rate, subflow->estimated_target);
    }

    this->stable_rate += subflow->stable_bitrate;
    this->stable_targets[subflow->id] = subflow->stable_bitrate;
    this->min_sending_rate += subflow->min_sending_rate;

    // Note: Not necessary, since subflow->allocated_target is considered in subflow cong controller
    //mediator_set_response(subflow->control_channel, subflow);
  }

  this->media_rate = this->stable_rate - sum_reduction;
//  if (0 < ongoing_ramp_up) {
//    this->media_rate += ongoing_ramp_up;
//    goto done;
//  } else if (!max_ramp_up) { // no ramping up request
//    goto done;
//  }
  if (!max_ramp_up) {
    goto done;
  }

//  g_print("new ramping up: %d\n", max_ramp_up);
  // New ramping up.
  this->media_rate += max_ramp_up;

  for (it = sndsubflows_get_subflows(this->subflows); it; it = it->next) {
    gdouble weight;
    gint32 subflow_ramp_up;
    gint32 allocated_ramp_up = MAX(0, subflow->allocated_target - subflow->stable_bitrate);
    subflow = it->data;
    if (!subflow->active || subflow->estimated_target <= subflow->stable_bitrate) continue;

    subflow_ramp_up = subflow->estimated_target - subflow->stable_bitrate;
    if (0. < subflow->rtt) {
      gdouble rtt_in_s = (gdouble) GST_TIME_AS_SECONDS(subflow->rtt * 10) / 10000.;
      weight = (gdouble)subflow_ramp_up / rtt_in_s;
    } else {
      weight = subflow_ramp_up;
    }

    if (0 < allocated_ramp_up) {
      allocated_ramp_up = MIN(allocated_ramp_up, subflow_ramp_up * weight / total_weight);
    } else {
      allocated_ramp_up = subflow_ramp_up * weight / total_weight;
    }

    this->allocated_targets[subflow->id] = subflow->allocated_target = MAX(subflow->min_sending_rate, subflow->stable_bitrate + allocated_ramp_up);
  }

done:
  source->base_db->target_off = this->target_off;
  source->base_db->total_desired_target = this->media_rate;
  source->base_db->total_stable_target = this->stable_rate;
  source->base_db->total_sending_rate = this->sending_rate_avg;
}




void
stream_splitter_on_packet_queued(StreamSplitter* this, SndPacket* packet)
{

}

void
stream_splitter_on_subflow_joined(StreamSplitter* this, SndSubflow* subflow)
{
  g_object_ref(subflow->control_channel);
  mediator_set_request_handler(subflow->control_channel, (ListenerFunc) _subflow_report_handler, this);
}

void
stream_splitter_on_subflow_detached(StreamSplitter* this, SndSubflow* subflow)
{
  g_object_unref(subflow->control_channel);
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
stream_splitter_on_subflow_stable_target_bitrate_chaned(StreamSplitter* this, SndSubflow* subflow)
{
  // TODO: change this, because it will be elliminated
//  this->total_stable_target -= this->stable_targets[subflow->id];
//  this->stable_targets[subflow->id] = subflow->approved_target;
//  this->total_stable_target += this->stable_targets[subflow->id];
}

void
stream_splitter_on_subflow_desired_target_chaned(StreamSplitter* this, SndSubflow* subflow)
{
  // TODO: change this, because it will be elliminated
//  this->total_target -= this->desired_targets[subflow->id];
//  this->desired_targets[subflow->id] = subflow->approved_target;
//  this->total_target += this->desired_targets[subflow->id];
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

static void _refresh_target_off(StreamSplitter * this)
{
  gint32 total_sending_rate = sndtracker_get_stat(this->tracker)->sent_bytes_in_1s * 8;
  gdouble alpha;
  gdouble numerator;
  gdouble divider;
  if (this->media_rate_std == 0 || abs(this->media_rate - this->stable_rate) < 1) {
    alpha = 1.;
  } else {
    alpha = MIN(.5, (gdouble)this->media_rate_std / (2. * abs(this->media_rate - this->stable_rate)));
  }
//  g_print("SR avg: %d <| %d .. %f\n", this->sending_rate_avg, total_sending_rate, alpha);
  this->sending_rate_avg = total_sending_rate * alpha + this->sending_rate_avg * (1.-alpha);

  if (this->media_rate == this->stable_rate) {
    this->target_off = 1.;
    goto done;
  } else if (this->stable_rate < this->media_rate) {
    numerator = MAX(0, this->media_rate - this->sending_rate_avg);
    divider = this->media_rate - this->stable_rate;
  } else { // media_rate < stable_rate
    numerator = MAX(0, this->sending_rate_avg - this->media_rate);
    divider =  this->stable_rate - this->media_rate;
  }
  this->target_off = 1. - MIN(1., numerator / divider);

done:
  this->subflows->target_off = this->target_off;
  this->subflows->total_sending_rate = this->sending_rate_avg;
}


SndSubflow* stream_splitter_select_subflow(StreamSplitter * this, SndPacket *packet) {
  gboolean new_frame = this->last_timestamp == 0 || packet->timestamp != this->last_timestamp;
//  if (new_frame) {
//    // Calculate the avarage packet per frame
//  }

  _refresh_target_off(this);

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
        drate -= this->stable_targets[subflow->id] * (1.-this->target_off) + this->allocated_targets[subflow->id] * (this->target_off);
      } else {
        drate -= this->allocated_targets[subflow->id];
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





