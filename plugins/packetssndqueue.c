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
                                       GstBuffer *buffer,
                                       guint32 payload_bytes);

#define _trash_node(this, node) g_slice_free(PacketsSndQueueNode, node)

static void _packetssndqueue_add(PacketsSndQueue *this,
                                 GstBuffer* buffer,
                                 guint32 payload_bytes);
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

guint32 packetssndqueue_get_num(PacketsSndQueue *this, guint32 *bytes_in_queue)
{
  guint32 result;
  THIS_READLOCK(this);
  result = this->counter;
  if(bytes_in_queue) *bytes_in_queue = this->bytes_in_queue;
  THIS_READUNLOCK(this);
  return result;
}

PacketsSndQueue *make_packetssndqueue(void)
{
  PacketsSndQueue *result;
  result = g_object_new (PACKETSSNDQUEUE_TYPE, NULL);
  return result;
}

void packetssndqueue_push(PacketsSndQueue *this,
                          GstBuffer *buffer,
                          guint32 payload_bytes)
{
  THIS_WRITELOCK(this);
  _packetssndqueue_add(this, buffer, payload_bytes);
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

void packetssndqueue_set_obsolation_treshold(PacketsSndQueue *this,
                                    GstClockTime obsolation_treshold)
{
  THIS_WRITELOCK(this);
  this->obsolation_treshold = obsolation_treshold;
  THIS_WRITEUNLOCK(this);
}

gboolean packetssndqueue_has_buffer(PacketsSndQueue *this,
                                    guint32 *payload_bytes,
                                    gboolean *expected_lost)
{
  gboolean result = FALSE;
  THIS_READLOCK(this);
again:
  if(!this->head) goto done;
  if(payload_bytes) *payload_bytes = this->head->payload_bytes;
  if(0 < this->obsolation_treshold){
    if(this->head->added < gst_clock_get_time(this->sysclock) - this->obsolation_treshold){
      _remove_head(this);
      if(expected_lost) *expected_lost = TRUE;
      goto again;
    }
  }
  result = TRUE;
done:
  THIS_READUNLOCK(this);
  return result;
}

void _packetssndqueue_add(PacketsSndQueue *this,
                             GstBuffer *buffer,
                             guint32 payload_bytes)
{
//  guint64 skew = 0;
  PacketsSndQueueNode* node;
  node = _make_node(this, buffer, payload_bytes);
  if(!this->head) {
      this->head = this->tail = node;
      this->counter = 1;
      this->bytes_in_queue = payload_bytes;
      goto done;
  }
  this->tail->next = node;
  this->tail = node;
  ++this->counter;
  this->bytes_in_queue += payload_bytes;
done:
  return;
}

void _remove_head(PacketsSndQueue *this)
{
  PacketsSndQueueNode *node;
  guint32 payload_bytes;
  node = this->head;
  payload_bytes = node->payload_bytes;
  this->head = node->next;
  _trash_node(this, node);
  this->bytes_in_queue-=payload_bytes;
  --this->counter;
}


PacketsSndQueueNode* _make_node(PacketsSndQueue *this, GstBuffer *buffer, guint32 payload_bytes)
{
  PacketsSndQueueNode *result;
  result = g_slice_new0(PacketsSndQueueNode);
  result->next = NULL;
  result->added = gst_clock_get_time(this->sysclock);
  result->buffer = gst_buffer_ref(buffer);
  result->payload_bytes = payload_bytes;
  return result;
}


#undef DEBUG_PRINT_TOOLS
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
