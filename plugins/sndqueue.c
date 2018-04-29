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

static gint _cmp_packet_queued(SndPacket *a, SndPacket* b, gpointer udata)
{
  if (a->queued == b->queued) {
    return 0;
  }
  return a->queued < b->queued ? -1 : 1;
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
  g_object_unref(this->unqueued_packets);
}


void
sndqueue_init (SndQueue * this)
{
  this->threshold = .5;
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
  this->unqueued_packets = g_queue_new();
  this->queued_bytes_considered = TRUE;
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
sndqueue_on_subflow_state_changed(SndQueue* this, SndSubflow* subflow)
{
  if (subflow->state == SNDSUBFLOW_STATE_CONGESTED) {
    this->queued_bytes_considered = FALSE;
    ++this->num_subflow_overused;
  } else if (this->tracked_states[subflow->id] == SNDSUBFLOW_STATE_CONGESTED){
    if (--this->num_subflow_overused < 1) {
      this->queued_bytes_considered = TRUE;
    }
  }
  this->tracked_states[subflow->id] = subflow->state;
}

void
sndqueue_on_subflow_target_bitrate_changed(SndQueue* this, SndSubflow* subflow)
{
  this->total_target -= this->actual_targets[subflow->id];
  this->actual_targets[subflow->id] = subflow->approved_target;
  this->total_target += this->actual_targets[subflow->id];
}

static void _refresh_unqueued_packets(SndQueue* this) {
  SndPacket *head;
  GstClockTime threshold = _now(this) - GST_SECOND;
again:
  if (g_queue_is_empty(this->unqueued_packets)) {
    return;
  }
  head = g_queue_peek_head(this->unqueued_packets);
  if (threshold < head->queued) {
    return;
  }
  head = g_queue_pop_head(this->unqueued_packets);
  this->stat.actual_bitrates[head->subflow_id] -= head->payload_size<<3;
  this->stat.total_bitrate -= head->payload_size<<3;
  sndpacket_unref(head);
  goto again;
}

static gboolean _is_queue_over_threshold(SndQueue* this, SndSubflow* subflow) {
  gint32 boundary;
  if (_stat(this)->queued_bytes[subflow->id] < 15000) {
    return FALSE;
  } else if (subflow->state != SNDSUBFLOW_STATE_CONGESTED) {
    boundary = MAX(this->actual_targets[subflow->id], 0) * 3;
  } else {
    boundary = MAX(this->actual_targets[subflow->id], 0);
  }
//  g_print("queued bytes: %d < %f\n", _stat(this)->queued_bytes[subflow->id]<<3, boundary * this->threshold);
//  return FALSE;
  return  boundary * this->threshold < (_stat(this)->queued_bytes[subflow->id]<<3);
}

static gboolean _is_queue_below_threshold(SndQueue* this, SndSubflow* subflow) {
  gint32 boundary;
  if (_stat(this)->queued_bytes[subflow->id] < 15000) {
    return FALSE;
  } else {
    boundary = this->actual_targets[subflow->id];
  }
//  g_print("%d < %f\n", _stat(this)->queued_bytes[subflow->id]<<3, boundary * this->threshold);
//  return FALSE;
  return  (_stat(this)->queued_bytes[subflow->id]<<3) < boundary * (this->threshold / 2);
}

static gint32 _get_dropping_policy(SndQueue* this, SndSubflow* subflow) {
  gdouble off;
  gdouble fullness;
  if (1. < this->threshold) {
    return 1;
  }
  fullness = (_stat(this)->queued_bytes[subflow->id]<<3);
  fullness /= (gdouble) this->actual_targets[subflow->id];
  fullness = MIN(1., fullness);
  off = (fullness - (this->threshold / 2)) / fullness;
  return 4 * (1.-off) + 1 * off;
}

void sndqueue_push_packet(SndQueue * this, SndPacket* packet)
{
  GQueue* queue = this->packets[packet->subflow_id];
  SndSubflow* subflow;

  if(!queue){
    GST_WARNING("Sending queue for subflow %d is not available", packet->subflow_id);
    return;
  }
  ++this->stat.total_pushed_packets;

  g_queue_push_tail(queue, sndpacket_ref(packet));
  this->empty = FALSE;
  this->stat.actual_bitrates[packet->subflow_id] += packet->payload_size<<3;
  this->stat.total_bitrate += packet->payload_size<<3;
  _stat(this)->queued_bytes[packet->subflow_id] += packet->payload_size;
  _stat(this)->bytes_in_queue += packet->payload_size;

  ++_stat(this)->total_packets_in_queue;
  ++_stat(this)->packets_in_queue[packet->subflow_id];

  packet->queued = _now(this);
  notifier_do(this->on_packet_queued, packet);

  subflow = sndsubflows_get_subflow(this->subflows, packet->subflow_id);

  if (0 < this->dropping_policy) {
    if (this->stat.total_pushed_packets % this->dropping_policy == 0 && !g_queue_is_empty(queue)) {
      SndPacket* head = g_queue_pop_head(queue);
      _stat(this)->queued_bytes[head->subflow_id] -= head->payload_size;
      _stat(this)->bytes_in_queue -= head->payload_size;
     --_stat(this)->total_packets_in_queue;
     --_stat(this)->packets_in_queue[subflow->id];
      sndpacket_unref(head);
    }
    if (_is_queue_below_threshold(this, subflow)) {
      this->dropping_policy = 0;
    } else {
      this->dropping_policy = _get_dropping_policy(this, subflow);
    }
  } else if (_is_queue_over_threshold(this, subflow)) {
    this->dropping_policy = _get_dropping_policy(this, subflow);
  }
  _refresh_unqueued_packets(this);
}

typedef struct{
  GstClockTime next_approve;
  guint8       subflow_id;
  SndQueue*    this;
}PopHelperTuple;

static void _pop_helper(SndSubflow* subflow, PopHelperTuple* pop_helper) {
  if (g_queue_is_empty(pop_helper->this->packets[subflow->id])) {
    return;
  }

  if (!pop_helper->subflow_id || subflow->pacing_time < pop_helper->next_approve) {
    pop_helper->subflow_id   = subflow->id;
    pop_helper->next_approve = subflow->pacing_time;
  }
}

static void _set_pacing_time(SndQueue * this, guint8 subflow_id, SndPacket* packet) {
  SndSubflow* subflow = sndsubflows_get_subflow(this->subflows, subflow_id);
  gdouble pacing_interval_in_s;
  gdouble alpha = 1. / (gdouble)_stat(this)->packets_in_queue[subflow->id];
  volatile gint32* pacing_bitrate = this->pacing_bitrate + subflow->id;

  if (*pacing_bitrate == 0) {
    *pacing_bitrate = this->actual_targets[subflow_id] / 8;
  } else if (subflow->state == SNDSUBFLOW_STATE_CONGESTED || !this->queued_bytes_considered) {
    *pacing_bitrate = this->actual_targets[subflow_id] / 8;
  } else if (subflow->state == SNDSUBFLOW_STATE_STABLE) {
    *pacing_bitrate = *pacing_bitrate * alpha +
        (MAX(this->stat.actual_bitrates[subflow_id], this->actual_targets[subflow_id]) / 7) * (1.-alpha);
//    *pacing_bitrate = (1.0 * this->actual_targets[subflow_id]) / 8;
  } else if (subflow->state == SNDSUBFLOW_STATE_INCREASING) {
    *pacing_bitrate = *pacing_bitrate * alpha +
            (MAX(this->stat.actual_bitrates[subflow_id], this->actual_targets[subflow_id]) / 7) * (1.-alpha);
//    *pacing_bitrate = (1.2 * this->actual_targets[subflow_id]) / 8;
  }

  pacing_interval_in_s = (gdouble) packet->payload_size / (gdouble) *pacing_bitrate;
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
    // TODO: here we can switch pacing back.
//    if(next_approve){
//      *next_approve = MIN(pop_helper.next_approve, *next_approve);
//    }
//    if (*next_approve < now - GST_MSECOND) {
//      goto done;
//    }
  }

  queue = this->packets[pop_helper.subflow_id];
  result = g_queue_pop_head(queue);
  _set_pacing_time(this, pop_helper.subflow_id, result);

  _stat(this)->queued_bytes[result->subflow_id] -= result->payload_size;
  _stat(this)->bytes_in_queue -= result->payload_size;

 --_stat(this)->total_packets_in_queue;
 --_stat(this)->packets_in_queue[result->subflow_id];

 g_queue_insert_sorted(this->unqueued_packets, result, (GCompareDataFunc) _cmp_packet_queued, NULL);
 _refresh_unqueued_packets(this);

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
