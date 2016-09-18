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
#include "mprtpspath.h"
#include "rtpfecbuffer.h"
#include "lib_swplugins.h"

//#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
//#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
//#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
//#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define THIS_READLOCK(this) g_mutex_lock(&this->mutex)
#define THIS_READUNLOCK(this) g_mutex_unlock(&this->mutex)
#define THIS_WRITELOCK(this) g_mutex_lock(&this->mutex)
#define THIS_WRITEUNLOCK(this) g_mutex_unlock(&this->mutex)

#define _now(this) gst_clock_get_time (this->sysclock)

//#define THIS_READLOCK(this)
//#define THIS_READUNLOCK(this)
//#define THIS_WRITELOCK(this)
//#define THIS_WRITEUNLOCK(this)
//#define _cmp_uint16(x,y) ((x==y)?0:((gint16) (x - y)) < 0 ? -1 : 1)

//static gint
//_cmp_uint16 (guint16 x, guint16 y)
//{
//  if(x == y) return 0;
//  if(x < y && y - x < 32768) return -1;
//  if(x > y && x - y > 32768) return -1;
//  if(x < y && y - x > 32768) return 1;
//  if(x > y && x - y < 32768) return 1;
//  return 0;
//}


GST_DEBUG_CATEGORY_STATIC (packetssndqueue_debug_category);
#define GST_CAT_DEFAULT packetssndqueue_debug_category

G_DEFINE_TYPE (PacketsSndQueue, packetssndqueue, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void packetssndqueue_finalize (GObject * object);

//#define _trash_node(this, node) g_slice_free(PacketsSndQueueNode, node)
#define _trash_node(this, node) g_free(node)

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
  this = PACKETSSNDQUEUE(object);
  g_object_unref(this->sysclock);
  g_object_unref(this->items);
}

void
packetssndqueue_init (PacketsSndQueue * this)
{
//  g_rw_lock_init (&this->rwmutex);
  g_mutex_init(&this->mutex);
  g_cond_init(&this->cond);
  this->sysclock = gst_system_clock_obtain();
  this->obsolation_treshold = 300 * GST_MSECOND;
  this->items = g_queue_new();

}


void packetssndqueue_reset(PacketsSndQueue *this)
{
  THIS_WRITELOCK(this);
  THIS_WRITEUNLOCK(this);
}


gboolean packetssndqueue_expected_lost(PacketsSndQueue *this)
{
  gboolean result;
  THIS_READLOCK(this);
  result = this->expected_lost;
  this->expected_lost = FALSE;
  THIS_READUNLOCK(this);
  return result;
}

PacketsSndQueue *make_packetssndqueue(void)
{
  PacketsSndQueue *result;
  result = g_object_new (PACKETSSNDQUEUE_TYPE, NULL);
  result->made = _now(result);
  return result;
}


gint32 packetssndqueue_get_encoder_bitrate(PacketsSndQueue *this)
{
  gint64 result;
  THIS_READLOCK(this);
  result = 0;
  g_warning("Encoder rate not tracked yet");
  THIS_READUNLOCK(this);
  return result * 8;
}

gint32 packetssndqueue_get_bytes_in_queue(PacketsSndQueue *this)
{
  gint32 result;
  THIS_READLOCK(this);
  result = this->bytes;
  THIS_READUNLOCK(this);
  return result;
}

void packetssndqueue_set_obsolation_treshold(PacketsSndQueue *this, GstClockTime treshold)
{
  THIS_WRITELOCK(this);
  this->obsolation_treshold = treshold;
  THIS_WRITEUNLOCK(this);
}

GstClockTime packetssndqueue_get_obsolation_treshold(PacketsSndQueue *this)
{
  GstClockTime result;
  THIS_READLOCK(this);
  result = this->obsolation_treshold;
  THIS_READUNLOCK(this);
  return result;
}

void packetssndqueue_push(PacketsSndQueue *this, GstBuffer *buffer)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  PacketsSndQueueItem *item;
  THIS_WRITELOCK(this);
  item = g_slice_new0(PacketsSndQueueItem);
  item->added = _now(this);
  item->buffer = gst_buffer_ref(buffer);

  gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp);
  item->size      = gst_rtp_buffer_get_payload_len(&rtp);
  item->timestamp = gst_rtp_buffer_get_timestamp(&rtp);
  gst_rtp_buffer_unmap(&rtp);

  this->bytes+=item->size;
  g_queue_push_tail(this->items, item);
  g_cond_signal(&this->cond);
  THIS_WRITEUNLOCK(this);
}

GstBuffer * packetssndqueue_pop(PacketsSndQueue *this)
{
  GstBuffer *result = NULL;
  PacketsSndQueueItem *item;
  THIS_WRITELOCK(this);
  if(!g_queue_is_empty(this->items)){
    item = g_queue_pop_head(this->items);
    this->bytes-=item->size;
    result = item->buffer;
    g_slice_free(PacketsSndQueueItem, item);
  }
  THIS_WRITEUNLOCK(this);
  return result;
}

void packetssndqueue_wait_until_item(PacketsSndQueue *this)
{
  THIS_READLOCK(this);
  if(!g_queue_is_empty(this->items)){
    goto done;
  }
  g_cond_wait(&this->cond, &this->mutex);
done:
  THIS_READLOCK(this);
}

GstBuffer * packetssndqueue_peek(PacketsSndQueue *this)
{
  GstBuffer *result = NULL;
  PacketsSndQueueItem *item;
  THIS_WRITELOCK(this);
again:
  if(g_queue_is_empty(this->items)){
    goto done;
  }
  item = g_queue_peek_head(this->items);
  if(0 < this->obsolation_treshold && item->added < _now(this) - this->obsolation_treshold){
    item = g_queue_pop_head(this->items);
    this->bytes-=item->size;
    gst_buffer_unref(item->buffer);
    g_slice_free(PacketsSndQueueItem, item);
    this->expected_lost = TRUE;
    goto again;
  }
  result = item->buffer;
done:
  THIS_WRITEUNLOCK(this);
  return result;
}



#undef DEBUG_PRINT_TOOLS
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
