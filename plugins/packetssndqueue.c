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

//#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
//#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
//#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
//#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define THIS_READLOCK(this)
#define THIS_READUNLOCK(this)
#define THIS_WRITELOCK(this)
#define THIS_WRITEUNLOCK(this)
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

//#define _trash_node(this, node) g_slice_free(PacketsSndQueueNode, node)
#define _trash_node(this, node) g_free(node)

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
  this->sysclock = gst_system_clock_obtain();
  this->obsolation_treshold = 0;
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

void packetssndqueue_push(PacketsSndQueue *this,
                          GstBuffer *buffer)
{
  THIS_WRITELOCK(this);
  //Todo implement pacing
  this->proxy(this->proxydata, buffer);
  if(0) _packetssndqueue_add(this, buffer);
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
  result = g_malloc0(sizeof(PacketsSndQueueNode));
  result->next = NULL;
  result->added = gst_clock_get_time(this->sysclock);
  result->buffer = gst_buffer_ref(buffer);
  return result;
}


#undef DEBUG_PRINT_TOOLS
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
