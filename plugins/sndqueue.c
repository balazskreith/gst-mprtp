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
#include <gst/gst.h>
#include <stdlib.h>
#include <string.h>
#include "sndqueue.h"

GST_DEBUG_CATEGORY_STATIC (sndqueue_debug_category);
#define GST_CAT_DEFAULT sndqueue_debug_category

#define _now(this) gst_clock_get_time (this->sysclock)
#define _priv(this) ((Private*)(this->priv))
#define _stat(this) ((RTPQueueStat*)(&this->stat))
//#define _get_subflow(this, subflow_id) ((Subflow*)(_priv(this)->subflows + subflow_id))

G_DEFINE_TYPE (SndQueue, sndqueue, G_TYPE_OBJECT);

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

#define _tqueue(this, subflow_id) \
  ((TransmissionQueue*)this->tqueues[subflow_id])
//----------------------------------------------------------------------
//-------- Private functions belongs to the object ----------
//----------------------------------------------------------------------

static void
sndqueue_finalize (
    GObject * object);


//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
sndqueue_class_init (SndQueueClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = sndqueue_finalize;

  GST_DEBUG_CATEGORY_INIT (sndqueue_debug_category, "sndqueue", 0,
      "MpRTP Sending Rate Distributor");
}

void
sndqueue_finalize (GObject * object)
{
  SndQueue * this;
  this = SNDQUEUE(object);

  g_object_unref(this->subflows);
  g_object_unref(this->sysclock);
  g_object_unref(this->on_packet_queued);
  g_object_unref(this->tmp_queue);
}


void
sndqueue_init (SndQueue * this)
{
  this->threshold = .75;
  this->sysclock = gst_system_clock_obtain();
  this->on_packet_queued = make_notifier("SndQueue: on-packet-queued");
}

SndQueue *make_sndqueue(SndSubflows* subflows_db)
{
  SndQueue* this;
  gint i;
  this = g_object_new(SNDQUEUE_TYPE, NULL);
  this->subflows = g_object_ref(subflows_db);
  for(i = 0; i < MPRTP_PLUGIN_MAX_SUBFLOW_NUM; ++i){
    this->packets[i] = NULL;
  }
  this->tmp_queue = g_queue_new();
  return this;
}

void sndqueue_add_on_packet_queued(SndQueue * this, ListenerFunc callback, gpointer udata)
{
  notifier_add_listener(this->on_packet_queued, callback, udata);
}

void sndqueue_on_subflow_joined(SndQueue* this, SndSubflow* subflow)
{
  this->packets[subflow->id] = g_queue_new();
}

void sndqueue_on_subflow_detached(SndQueue* this, SndSubflow* subflow)
{
  g_object_unref(this->packets[subflow->id]);
  this->packets[subflow->id] = NULL;
}

void
sndqueue_on_packet_sent(SndQueue* this, SndPacket* packet)
{
  this->actual_rates[packet->subflow_id] += packet->payload_size<<3; //convert bytes to bits
  this->total_bitrate += packet->payload_size<<3;
}

void
sndqueue_on_packet_obsolated(SndQueue* this, SndPacket* packet)
{
  this->actual_rates[packet->subflow_id] -= packet->payload_size<<3; //convert bytes to bits
  this->total_bitrate -= packet->payload_size<<3;
}

void
sndqueue_on_subflow_target_bitrate_chaned(SndQueue* this, SndSubflow* subflow)
{
  this->total_target -= this->actual_targets[subflow->id];
  this->actual_targets[subflow->id] = subflow->target_bitrate;
  this->total_target += this->actual_targets[subflow->id];
}

static void _is_full_helper(SndSubflow* subflow, SndQueue* this) {
  if (_stat(this)->queued_bytes[subflow->id] < 15000) {
    return;
  } else if (subflow->state != SNDSUBFLOW_STATE_OVERUSED) {
    return;
  }
  {
    gint32 boundary = MAX(this->actual_targets[subflow->id], this->actual_rates[subflow->id]);
    if ((_stat(this)->queued_bytes[subflow->id]<<3) < boundary * this->threshold) {
      return;
    }
  }
  g_queue_push_head(this->tmp_queue, subflow);
}

static void _is_clear_helper(SndSubflow* subflow, SndQueue* this) {
  GQueue* target = this->packets[subflow->id];
  SndPacket* packet;
again:
  if (g_queue_is_empty(target)) {
    return;
  }
  packet = g_queue_peek_head(target);
  if (_cmp_ts(packet->timestamp, this->clear_end_ts) <= 0) {
    packet = g_queue_pop_head(target);
    _stat(this)->queued_bytes[subflow->id] -= packet->payload_size;
    _stat(this)->bytes_in_queue -= packet->payload_size;

    --_stat(this)->packets_in_queue;
  }
  goto again;
}


static void _check_packets_queue(SndQueue * this) {
  if (this->threshold == 0.) {
    return;
  }
  sndsubflows_iterate(this->subflows, (GFunc) _is_full_helper, this);
  if (g_queue_get_length(this->tmp_queue) < 1) {
    return;
  }
  while(!g_queue_is_empty(this->tmp_queue)) {
    SndSubflow* subflow = g_queue_pop_tail(this->tmp_queue);
    GQueue* target = this->packets[subflow->id];
    SndPacket* packet;
    _stat(this)->bytes_in_queue -= _stat(this)->queued_bytes[subflow->id];
    _stat(this)->queued_bytes[subflow->id] = 0;
    if (g_queue_is_empty(target)) {
      continue;
    }
    packet = g_queue_peek_tail(target);
    if (this->clear_end_ts == 0 || _cmp_ts(this->clear_end_ts, packet->timestamp) < 0) {
      this->clear_end_ts = packet->timestamp;
    }
    _stat(this)->packets_in_queue -= g_queue_get_length(target);
    g_queue_clear(target);
  }
  sndsubflows_iterate(this->subflows, (GFunc) _is_clear_helper, this);
  g_print("clear rtp queue\n");
}

void sndqueue_push_packet(SndQueue * this, SndPacket* packet)
{
  GQueue* queue = this->packets[packet->subflow_id];

  if(!queue){
    GST_WARNING("Sending queue for subflow %d is not available", packet->subflow_id);
    return;
  }
  g_queue_push_tail(queue, packet);
  this->empty = FALSE;
  _stat(this)->queued_bytes[packet->subflow_id] += packet->payload_size;
  _stat(this)->bytes_in_queue += packet->payload_size;

  ++_stat(this)->packets_in_queue;

  packet->queued = _now(this);
  notifier_do(this->on_packet_queued, packet);

  _check_packets_queue(this);
}

typedef struct{
  GstClockTime next_approve;
  guint8       subflow_id;
  SndQueue*    this;
}PopHelperTuple;

static void _pop_helper(SndSubflow* subflow, PopHelperTuple* pop_helper){
  if(g_queue_is_empty(pop_helper->this->packets[subflow->id])){
    return;
  }

  if(!pop_helper->subflow_id || subflow->pacing_time < pop_helper->next_approve){
    pop_helper->subflow_id   = subflow->id;
    pop_helper->next_approve = subflow->pacing_time;
  }
}

static void _set_pacing_time(SndQueue * this, guint8 subflow_id, SndPacket* packet) {
  SndSubflow* subflow = sndsubflows_get_subflow(this->subflows, subflow_id);
  gdouble pacing_interval_in_s;
  gint32 pacing_bitrate;
  switch (subflow->state) {
    case SNDSUBFLOW_STATE_OVERUSED:
      pacing_bitrate = this->actual_targets[subflow_id] / 8;
      break;
    case SNDSUBFLOW_STATE_UNDERUSED:
      pacing_bitrate = MAX(this->actual_rates[subflow_id] + _stat(this)->queued_bytes[subflow_id] * 8,
          this->actual_targets[subflow_id]) / 5;
      break;
    default:
    case SNDSUBFLOW_STATE_STABLE:
      pacing_bitrate = MAX(this->actual_rates[subflow_id] + _stat(this)->queued_bytes[subflow_id]  * 4,
          this->actual_targets[subflow_id]);
      break;
  }
  pacing_interval_in_s = (gdouble) packet->payload_size / (gdouble) pacing_bitrate;
  subflow->pacing_time = _now(this) + pacing_interval_in_s * GST_SECOND;
}

SndPacket* sndqueue_pop_packet(SndQueue * this, GstClockTime* next_approve)
{
  SndPacket* result = NULL;
  GQueue* queue;
  PopHelperTuple pop_helper = {*next_approve,0,this};
  GstClockTime now = _now(this);

  sndsubflows_iterate(this->subflows, (GFunc) _pop_helper, &pop_helper);

  if(!pop_helper.subflow_id) {
    this->empty = TRUE;
    goto done;
  }else if(now < pop_helper.next_approve){
    if(next_approve){
      *next_approve = MIN(pop_helper.next_approve, *next_approve);
    }
    goto done;
  }

  queue = this->packets[pop_helper.subflow_id];
  result = g_queue_pop_head(queue);
  _set_pacing_time(this, pop_helper.subflow_id, result);
  _stat(this)->queued_bytes[pop_helper.subflow_id] -= result->payload_size;
  _stat(this)->bytes_in_queue -= result->payload_size;
  this->last_ts = result->timestamp;
  --_stat(this)->packets_in_queue;

done:
  return result;
}


gboolean sndqueue_is_empty(SndQueue* this)
{
  return this->empty;
}

RTPQueueStat* sndqueue_get_stat(SndQueue* this) {
  return _stat(this);
}
