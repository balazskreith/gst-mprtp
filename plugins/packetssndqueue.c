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
#include "packetssndqueue.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>
#include "bintree.h"

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define _now(this) gst_clock_get_time (this->sysclock)

//#define THIS_READLOCK(this)
//#define THIS_READUNLOCK(this)
//#define THIS_WRITELOCK(this)
//#define THIS_WRITEUNLOCK(this)
#define _cmp_uint16(x,y) ((x==y)?0:((gint16) (x - y)) < 0 ? -1 : 1)

GST_DEBUG_CATEGORY_STATIC (packetssndqueue_debug_category);
#define GST_CAT_DEFAULT packetssndqueue_debug_category

G_DEFINE_TYPE (PacketsSndQueue, packetssndqueue, G_TYPE_OBJECT);

typedef enum{
  PACING_DEACTIVE    = 0,
  PACING_ACTIVATED   = 1,
  PACING_ACTIVE      = 2,
  PACING_DEACTIVATED = 3
}PacingTypes;

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void packetssndqueue_finalize (GObject * object);
static PacketsSndQueueNode* _make_node(PacketsSndQueue *this,
                                       GstBuffer *buffer);

//#define _trash_node(this, node) g_slice_free(PacketsSndQueueNode, node)
#define _trash_node(this, node) g_free(node)

static void _packetssndqueue_add(PacketsSndQueue *this,
                                 GstBuffer* buffer);
static void _remove_head(PacketsSndQueue *this);

static void _ticking_process_run (void *data);

static guint64 _pacing_deactive_state(PacketsSndQueue *this);
static guint64 _pacing_activated_state(PacketsSndQueue *this);
static guint64 _pacing_active_state(PacketsSndQueue *this);
static guint64 _pacing_deactivated_state(PacketsSndQueue *this);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
packetssndqueue_class_init (PacketsSndQueueClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = packetssndqueue_finalize;

  GST_DEBUG_CATEGORY_INIT (packetssndqueue_debug_category, "packetssndqueue", 0,
      "MpRTP Manual Sending Controller");

}

void
packetssndqueue_finalize (GObject * object)
{
  PacketsSndQueue *this;
  PacketsSndQueueNode *next;

  this = PACKETSSNDQUEUE(object);
  while(this->head){
    next = this->head->next;
    _trash_node(this, this->head);
    this->head = next;
  }
  g_object_unref(this->sysclock);
  gst_task_stop (this->ticking_thread);
  gst_task_join (this->ticking_thread);
  gst_object_unref (this->ticking_thread);

}

void
packetssndqueue_init (PacketsSndQueue * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sysclock = gst_system_clock_obtain();
  this->obsolation_treshold = 0;
  this->ticking_thread = gst_task_new (_ticking_process_run, this, NULL);
  gst_task_set_lock (this->ticking_thread, &this->ticking_mutex);
  gst_task_start (this->ticking_thread);

}


void packetssndqueue_reset(PacketsSndQueue *this)
{
  THIS_WRITELOCK(this);
  while(this->head) _remove_head(this);
  THIS_WRITEUNLOCK(this);
}

PacketsSndQueue *make_packetssndqueue(BufferProxy proxy, gpointer proxydata)
{
  PacketsSndQueue *result;
  result = g_object_new (PACKETSSNDQUEUE_TYPE, NULL);
  result->proxy     = proxy;
  result->proxydata = proxydata;
  return result;
}

void packetssndqueue_set_bandwidth(PacketsSndQueue *this, gdouble bandwidth)
{
  THIS_WRITELOCK(this);
  this->bandwidth = bandwidth;
  if(this->pacing == PACING_DEACTIVE && 0. < bandwidth){
    this->pacing = PACING_ACTIVATED;
  }else if(this->pacing == PACING_ACTIVE && 0. == bandwidth){
    this->pacing = PACING_DEACTIVATED;
  }
  this->allowed_rate_per_ms = bandwidth / 1000.;
  THIS_WRITEUNLOCK(this);
}

void packetssndqueue_push(PacketsSndQueue *this,
                          GstBuffer *buffer)
{
  THIS_WRITELOCK(this);
  if(0 < (this->pacing & 2)){
    _packetssndqueue_add(this, buffer);
  }else{
    this->proxy(this->proxydata, buffer);
  }
  THIS_WRITEUNLOCK(this);
}

void _packetssndqueue_add(PacketsSndQueue *this,
                             GstBuffer *buffer)
{
//  guint64 skew = 0;
  PacketsSndQueueNode* node;
  node = _make_node(this, buffer);
  if(!this->head) {
      this->head = this->tail = node;
      this->counter = 1;
      goto done;
  }
  this->tail->next = node;
  this->tail = node;
  ++this->counter;
  if(0) _remove_head(this);
done:
  return;
}

void _remove_head(PacketsSndQueue *this)
{
  PacketsSndQueueNode *node;
  node = this->head;
  this->head = node->next;
  _trash_node(this, node);
  --this->counter;
}


PacketsSndQueueNode* _make_node(PacketsSndQueue *this, GstBuffer *buffer)
{
  PacketsSndQueueNode *result;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  result = g_malloc0(sizeof(PacketsSndQueueNode));
  result->next = NULL;
  result->added = gst_clock_get_time(this->sysclock);
  result->buffer = gst_buffer_ref(buffer);

  if (G_UNLIKELY (!gst_rtp_buffer_map (buffer, GST_MAP_READWRITE, &rtp))) {
    GST_WARNING_OBJECT (this, "The RTP packet is not writeable");
    goto done;
  }
  result->size = gst_rtp_buffer_get_payload_len(&rtp);
  gst_rtp_buffer_unmap(&rtp);
done:
  return result;
}

void
_ticking_process_run (void *data)
{
  PacketsSndQueue *this;
  GstClockID clock_id;
  GstClockTime next_scheduler_time;

  this = (PacketsSndQueue *) data;
  next_scheduler_time = _now(this) + 100 * GST_MSECOND;

  THIS_WRITELOCK (this);
  if(this->pacing == PACING_ACTIVATED){
    next_scheduler_time = _pacing_activated_state(this);
  }else if(this->pacing == PACING_ACTIVE){
    next_scheduler_time = _pacing_active_state(this);
  }else if(this->pacing == PACING_DEACTIVATED){
    next_scheduler_time = _pacing_deactivated_state(this);
  }else if(this->pacing == PACING_DEACTIVE){
    next_scheduler_time = _pacing_deactive_state(this);
  }
  clock_id = gst_clock_new_single_shot_id (this->sysclock, next_scheduler_time);
  THIS_WRITEUNLOCK (this);

  if (gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED) {
    GST_WARNING_OBJECT (this, "The scheduler clock wait is interrupted");
  }
  gst_clock_id_unref (clock_id);
}

guint64 _pacing_deactive_state(PacketsSndQueue *this)
{
  return _now(this) + 100 * GST_MSECOND;
}

guint64 _pacing_activated_state(PacketsSndQueue *this)
{
  this->pacing = PACING_ACTIVE;
  return _now(this) + 1 * GST_MSECOND;
}

guint64 _pacing_active_state(PacketsSndQueue *this)
{
  guint64              next_scheduler_time = _now(this) + 1 * GST_MSECOND;
  PacketsSndQueueNode* node;
again:
  if(!this->head){
    goto done;
  }
  node = this->head;
  if(node->added < _now(this) - 400 * GST_MSECOND){
     //Todo: Set expected losts flag here
    _remove_head(this);
    goto again;
  }
  node->allowed_size += this->allowed_rate_per_ms;
  if(node->allowed_size < node->size){
    goto done;
  }
  this->proxy(this->proxydata, node->buffer);
  _remove_head(this);
//  next_scheduler_time = now + MAX(0.001,(node->size*8)/MAX(50000,this->bandwidth)) * GST_SECOND;
done:
  return next_scheduler_time;
}

guint64 _pacing_deactivated_state(PacketsSndQueue *this)
{
  guint64              next_scheduler_time = _now(this) + 100 * GST_MSECOND;
  PacketsSndQueueNode* node;
again:
  if(!this->head){
    goto done;
  }
  node = this->head;
  if(node->added < _now(this) - 400 * GST_MSECOND){
     //Todo: Set expected losts flag here
    _remove_head(this);
  }else{
    this->proxy(this->proxydata, node->buffer);
  }
  goto again;
done:
  this->pacing = PACING_DEACTIVE;
  return next_scheduler_time;
}


#undef DEBUG_PRINT_TOOLS
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
