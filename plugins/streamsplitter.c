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
        (gdouble)subflow->approved_target / (gdouble)this->total_stable_target,
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

  this->total_target = TOTAL_MIN_SENDING_RATE;
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
  this->total_target = MAX(this->total_target, this->min_sending_rate);
  return this->total_target;
}

static void _subflow_report_handler(StreamSplitter* this, SndSubflow* source)
{
  GSList* it;
  SndSubflow* subflow;
  gint32 max_increasement = 0;
  gint32 total_increasement = 0;
  gint32 total_desired_target = 0;
  gint32 total_stable_target = 0;
  gint32 total_sending_rate = 0;
//  gdouble weight;
  gint32 sum_reduction = 0;
  gint32 remaining_increasement = 0;
  gdouble accumulated_influence = 0.;

  this->min_sending_rate = 0;
  for (it = sndsubflows_get_subflows(this->subflows); it; it = it->next) {
    subflow = it->data;
    if (!subflow->active) continue;
    if (subflow->stable_bitrate < subflow->requested_target) {
      gint32 subflow_increasement = subflow->requested_target - subflow->stable_bitrate;
      max_increasement = MAX(max_increasement, subflow_increasement);
      total_increasement += subflow->requested_target - subflow->stable_bitrate;
      accumulated_influence += (gdouble) GST_TIME_AS_MSECONDS(_now(this) - subflow->last_increased_target) / (gdouble) (subflow_increasement / 1000.);
    } else if (subflow->requested_target <= subflow->stable_bitrate){
      sum_reduction += subflow->stable_bitrate - subflow->requested_target;
    }
    total_sending_rate += sndtracker_get_subflow_stat(this->tracker, subflow->id)->sent_bytes_in_1s * 8;
    total_stable_target += subflow->stable_bitrate;
    this->stable_targets[subflow->id] = subflow->stable_bitrate;
    this->min_sending_rate += subflow->min_sending_rate;
  }

  total_desired_target = total_stable_target - sum_reduction + max_increasement;
  remaining_increasement = max_increasement;
//  g_print("Max increasement: %d\n", max_increasement);

  for (it = sndsubflows_get_subflows(this->subflows); it; it = it->next) {
    subflow = it->data;
    if (!subflow->active) continue;
    if (subflow->requested_target <= subflow->stable_bitrate)
    {
      subflow->approved_target = subflow->requested_target;
      this->desired_targets[subflow->id] = subflow->approved_target;
    }
    else if (subflow->stable_bitrate < subflow->requested_target)
    {
      gdouble subflow_increasement = subflow->requested_target - subflow->stable_bitrate;
      gint32 approved_increasement;
      gdouble weight; // = subflow_increasement / (gdouble) total_increasement;
      weight = (gdouble) GST_TIME_AS_MSECONDS(_now(this) - subflow->last_increased_target) / (gdouble) (subflow_increasement / 1000.);
      weight /= accumulated_influence;
      approved_increasement = MIN(max_increasement * weight, subflow_increasement);
      approved_increasement = MIN(approved_increasement, remaining_increasement);
//      subflow->approved_target = subflow->stable_bitrate + max_increasement * weight;
      subflow->approved_target = subflow->stable_bitrate + approved_increasement;
      this->desired_targets[subflow->id] = subflow->approved_target;
      remaining_increasement -= approved_increasement;
//      g_print("Max increasement: %f, %f\n", subflow_increasement, weight);
    }
    else // the desired rate is equal to the sending rate
    {
      this->desired_targets[subflow->id] = subflow->requested_target;
    }

    subflow->approved_target = MAX(subflow->approved_target, subflow->min_sending_rate);
    mediator_set_response(subflow->control_channel, subflow);
  }

  this->total_target = total_desired_target;
//  g_print("this->total_target: %d , %d\n", this->total_target, sum_reduction);

  source->base_db->target_off = this->target_off;
  source->base_db->total_desired_target = total_desired_target;
  source->base_db->total_stable_target = total_stable_target;
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

static gdouble sigma(gdouble x, gdouble x0, gdouble k) {
//  g_print("1/(1+e^(%1.2f*(%d-%d)))\n", k, (gint32)x, (gint32)x0);
  return 1./(1.+exp(k*(x-x0)));
}

static void _refresh_target_off(StreamSplitter * this)
{
  gint32 total_sending_rate = sndtracker_get_stat(this->tracker)->sent_bytes_in_1s * 8;
  gdouble halfway;
  gdouble stepness;
  gdouble distance = abs(this->subflows->total_stable_target - this->subflows->total_desired_target);
  this->sending_rate_avg = total_sending_rate * .5 + this->sending_rate_avg * .5;

  if (this->subflows->total_stable_target == this->subflows->total_desired_target) {
    this->target_off = 1.;
    goto done;
  }
  if (this->subflows->total_stable_target < this->subflows->total_desired_target) {
    halfway = this->subflows->total_stable_target + distance / 2.;
    stepness = -5./distance;
  } else {
    halfway = this->subflows->total_desired_target + distance / 2.;
    stepness = 5./distance;
  }

  this->target_off = sigma(this->sending_rate_avg, halfway, stepness);

//  gdouble stable_off, desired_off;
//  stable_off = (this->subflows->total_stable_target - this->sending_rate_avg) / (gdouble) this->subflows->total_stable_target;
//  if (this->total_stable_target == this->subflows->total_desired_target) {
//    this->target_off = 0.;
//  } else {
//    this->target_off = (gdouble) (this->subflows->total_desired_target - this->sending_rate_avg);
//    this->target_off /= (gdouble) (this->subflows->total_desired_target - this->total_stable_target);
//    this->target_off = CONSTRAIN(0., 1., this->target_off);
//  }
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
        drate -= this->stable_targets[subflow->id] * (1.-this->target_off) + this->desired_targets[subflow->id] * (this->target_off);
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





