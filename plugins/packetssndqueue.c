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
#define _cmp_uint16(x,y) ((x==y)?0:((gint16) (x - y)) < 0 ? -1 : 1)

GST_DEBUG_CATEGORY_STATIC (packetssndqueue_debug_category);
#define GST_CAT_DEFAULT packetssndqueue_debug_category

G_DEFINE_TYPE (PacketsSndQueue, packetssndqueue, G_TYPE_OBJECT);



//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void packetssndqueue_finalize (GObject * object);
static PacketsSndQueueNode* _make_node(PacketsSndQueue *this,
                                       GstBuffer *buffer);
static void _trash_node(PacketsSndQueue *this, PacketsSndQueueNode* node);


static void _packetssndqueue_add(PacketsSndQueue *this,
                                 GstBuffer* buffer);
static void _remove_head(PacketsSndQueue *this);

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
  while(!g_queue_is_empty(this->node_pool)){
    g_free(g_queue_pop_head(this->node_pool));
  }

  while(this->head){
    next = this->head->next;
    _trash_node(this, this->head);
    this->head = next;
  }
  g_object_unref(this->sysclock);

}

void
packetssndqueue_init (PacketsSndQueue * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->node_pool = g_queue_new();
  this->sysclock = gst_system_clock_obtain();
}


void packetssndqueue_reset(PacketsSndQueue *this)
{
  THIS_WRITELOCK(this);
  while(this->head) _remove_head(this);
  THIS_WRITEUNLOCK(this);
}

PacketsSndQueue *make_packetssndqueue(void)
{
  PacketsSndQueue *result;
  result = g_object_new (PACKETSSNDQUEUE_TYPE, NULL);
  return result;
}

void packetssndqueue_push(PacketsSndQueue *this,
                          GstBuffer *buffer)
{
  THIS_WRITELOCK(this);
  _packetssndqueue_add(this, buffer);
  THIS_WRITEUNLOCK(this);
}

GstBuffer* packetssndqueue_pop(PacketsSndQueue *this)
{
  GstBuffer *result = NULL;
  THIS_WRITELOCK(this);
  if(!this->head) goto done;
  result = this->head->buffer;
  _remove_head(this);
done:
  THIS_WRITEUNLOCK(this);
  return result;
}

gboolean packetssndqueue_has_buffer(PacketsSndQueue *this)
{
  gboolean result;
  THIS_READLOCK(this);
  result = this->head != NULL;
  THIS_READUNLOCK(this);
  return result;
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
  if(!g_queue_is_empty(this->node_pool))
    result = g_queue_pop_head(this->node_pool);
  else
    result = g_malloc0(sizeof(PacketsSndQueueNode));
  memset((gpointer)result, 0, sizeof(PacketsSndQueueNode));
  result->next = NULL;
  result->added = gst_clock_get_time(this->sysclock);
  result->buffer = gst_buffer_ref(buffer);
  return result;
}


void _trash_node(PacketsSndQueue *this, PacketsSndQueueNode* node)
{
  if(g_queue_get_length(this->node_pool) > 4096)
    g_free(node);
  else
    g_queue_push_tail(this->node_pool, node);
}


#undef DEBUG_PRINT_TOOLS
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
