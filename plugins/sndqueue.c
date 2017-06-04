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
//#define _get_subflow(this, subflow_id) ((Subflow*)(_priv(this)->subflows + subflow_id))

G_DEFINE_TYPE (SndQueue, sndqueue, G_TYPE_OBJECT);


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
}


void
sndqueue_init (SndQueue * this)
{
  this->sysclock = gst_system_clock_obtain();
}

SndQueue *make_sndqueue(SndSubflows* subflows_db)
{
  SndQueue* this;
  gint i;
  this = g_object_new(SNDQUEUE_TYPE, NULL);
  this->subflows = g_object_ref(subflows_db);
  for(i = 0; i < MPRTP_PLUGIN_MAX_SUBFLOW_NUM; ++i){
    this->queues[i] = NULL;
  }
  return this;
}

void sndqueue_on_subflow_joined(SndQueue* this, SndSubflow* subflow)
{
  this->queues[subflow->id] = g_queue_new();
}

void sndqueue_on_subflow_detached(SndQueue* this, SndSubflow* subflow)
{
  g_object_unref(this->queues[subflow->id]);
  this->queues[subflow->id] = NULL;
}

void sndqueue_push_packet(SndQueue * this, SndPacket* packet)
{
  GQueue* queue = this->queues[packet->subflow_id];

  if(!queue){
    GST_WARNING("Sending queue for subflow %d is not available", packet->subflow_id);
    return;
  }
  g_queue_push_tail(queue, packet);

  sndsubflows_get_subflow(this->subflows, packet->subflow_id);
}

static void _clear_helper(SndSubflow* subflow, GQueue** queues){
  GQueue* queue = queues[subflow->id];
  g_queue_clear(queue);
}

void sndqueue_clear(SndQueue * this)
{
  sndsubflows_iterate(this->subflows, (GFunc) _clear_helper, this->queues);
}

typedef struct{
  GstClockTime next_approve;
  guint8       subflow_id;
  SndQueue*    this;
}PopHelperTuple;

static void _pop_helper(SndSubflow* subflow, PopHelperTuple* pop_helper){
  if(g_queue_is_empty(pop_helper->this->queues[subflow->id])){
    return;
  }

  if(!pop_helper->subflow_id || subflow->pacing_time < pop_helper->next_approve){
    pop_helper->subflow_id   = subflow->id;
    pop_helper->next_approve = subflow->pacing_time;
  }
}

SndPacket* sndqueue_pop_packet(SndQueue * this, GstClockTime* next_approve)
{
  SndPacket* result = NULL;
  GQueue* queue;
  PopHelperTuple pop_helper = {*next_approve,0,this};
  GstClockTime now = _now(this);

  sndsubflows_iterate(this->subflows, (GFunc) _pop_helper, &pop_helper);
  if(!pop_helper.subflow_id){
    goto done;
  }else if(now < pop_helper.next_approve){
    if(next_approve){
      *next_approve = MIN(pop_helper.next_approve, *next_approve);
    }
  }

  queue = this->queues[pop_helper.subflow_id];
  result = g_queue_pop_head(queue);
done:
  return result;
}

typedef struct{
  gboolean result;
  SndQueue*    this;
}EmptyTuple;

static void _empty_helper(SndSubflow* subflow, EmptyTuple* empty_tuple)
{
  empty_tuple->result &= g_queue_is_empty(empty_tuple->this->queues[subflow->id]);
}

gboolean sndqueue_is_empty(SndQueue* this)
{
  EmptyTuple empty_tuple = {TRUE,this};
  sndsubflows_iterate(this->subflows, (GFunc) _empty_helper, &empty_tuple);
  return empty_tuple.result;
}


