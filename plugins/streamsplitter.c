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
        (gdouble)this->actual_rates[subflow->id] / (gdouble)this->total_bitrate
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

StreamSplitter* make_stream_splitter(SndSubflows* sndsubflows)
{
  StreamSplitter *this;
  this = g_object_new (STREAM_SPLITTER_TYPE, NULL);
  this->subflows = g_object_ref(sndsubflows);
  return this;
}

void
stream_splitter_finalize (GObject * object)
{
  StreamSplitter *this = STREAM_SPLITTER (object);
  g_object_unref (this->sysclock);
  g_object_unref(this->subflows);
}


void
stream_splitter_init (StreamSplitter * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->made                   = _now(this);

  {
    gint i;
    for (i = 0; i < MPRTP_PLUGIN_MAX_SUBFLOW_NUM; ++i) {
      this->actual_weights[i] = 1.;
    }
  }
}

//static void _refresh_weights_helper(SndSubflow *subflow, StreamSplitter* this)
//{
//  //  this->actual_weights[subflow->id] = (gdouble)subflow->target_bitrate / (gdouble) this->total_target;
//  gdouble target = (gdouble)subflow->target_bitrate / (gdouble) this->total_target;
//  this->actual_weights[subflow->id] = this->actual_weights[subflow->id] * (1.-this->refresh_ratio) + target * this->refresh_ratio;
//}

void
stream_splitter_on_subflow_state_changed(StreamSplitter* this, SndSubflow* subflow)
{
  if (subflow->state == SNDSUBFLOW_STATE_UNDERUSED) {
    this->extra_targets[subflow->id] = subflow->target_bitrate - this->actual_targets[subflow->id];
  } else {
    this->actual_targets[subflow->id] = subflow->target_bitrate;
  }
}

void
stream_splitter_on_target_bitrate_changed(StreamSplitter* this, SndSubflow* subflow)
{
  this->total_target -= this->actual_targets[subflow->id];
  this->actual_targets[subflow->id] = subflow->target_bitrate;
  this->total_target += this->actual_targets[subflow->id];
  {
    SndSubflowState state = sndsubflow_get_state(subflow);
    if (state == SNDSUBFLOW_STATE_STABLE) {
      this->stable_targets[subflow->id] = subflow->
    }
  }
}

gint32 stream_splitter_get_total_target(StreamSplitter* this)
{
  return this->total_target;
}

void
stream_splitter_on_packet_sent(StreamSplitter* this, SndPacket* packet)
{
  this->actual_rates[packet->subflow_id] += packet->payload_size;
  this->total_bitrate += packet->payload_size;
}

void
stream_splitter_on_packet_obsolated(StreamSplitter* this, SndPacket* packet)
{
  this->actual_rates[packet->subflow_id] -= packet->payload_size;
  this->total_bitrate -= packet->payload_size;
}

static void _select_highest_state(SndSubflow *subflow, SndSubflowState *max_state)
{
  *max_state = CONSTRAIN(*max_state, SNDSUBFLOW_STATE_STABLE, subflow->state);
}

void
stream_splitter_on_subflow_state_changed(StreamSplitter* this, SndSubflow* subflow)
{
  if(subflow->state != SNDSUBFLOW_STATE_OVERUSED){
    return;
  }
  sndsubflows_iterate(this->subflows, (GFunc)_select_highest_state, &this->max_state);
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
  gdouble selected_sr, actual_sr;
  gdouble weight;
  gdouble drate;

  DISABLE_LINE _print_ratios(this);

  for(it = sndsubflows_get_subflows(this->subflows); it; it = it->next)
  {
    subflow = it->data;
    drate = this->actual_targets[subflow->id] - this->actual_rates[subflow->id];

    if (drate < 0.) {
//      weight = this->actual_weights[subflow->id];
      weight = (gdouble) MIN(this->actual_rates[subflow->id], this->actual_targets[subflow->id]);
      weight /= (gdouble) MAX(this->total_bitrate, this->total_target);
    } else {
      if (this->actual_rates[subflow->id] < this->actual_targets[subflow->id] * .5 || this->total_target < this->total_bitrate) {
        weight = (gdouble) this->actual_targets[subflow->id] / (gdouble) this->total_target;
      } else {
        weight = (gdouble) this->actual_rates[subflow->id] / (gdouble) this->total_bitrate;
      }
    }
//    g_print("%d weight: %f | drate: %f\n", subflow->id, weight, drate);

    if(!subflow->active || weight == 0.){
      continue;
    }
    if(this->keyframe_filtering){
      if(packet->keyframe && subflow->state < this->max_state){
        continue;
      }
    }

    if(!selected){
      selected = subflow;
      if(this->actual_rates[subflow->id]){
        selected_sr = (gdouble)this->actual_rates[subflow->id] / weight;
      }else{
        selected_sr = 0.;
      }
      continue;
    }

    if(this->actual_rates[subflow->id]){
      actual_sr = (gdouble)this->actual_rates[subflow->id] / weight;
    }else{
      actual_sr = 0.;
    }

    if(actual_sr < selected_sr){
      selected = subflow;
    }
  }

  return selected;
}




