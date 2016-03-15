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
#include "mprtpspath.h"

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

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

//#define _trash_node(this, node) g_slice_free(PacketsSndQueueNode, node)
#define _trash_node(this, node) g_free(node)

static void _packetssndqueue_add(PacketsSndQueue *this, GstBuffer* buffer);
static GstBuffer * _packetssndqueue_rem(PacketsSndQueue *this);
static void _logging(PacketsSndQueue *this);
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
}

void
packetssndqueue_init (PacketsSndQueue * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sysclock = gst_system_clock_obtain();
  this->obsolation_treshold = 400 * GST_MSECOND;
  this->logging_interval = GST_SECOND;
  this->incoming_bytes = make_numstracker(2048, GST_SECOND);
}


void packetssndqueue_reset(PacketsSndQueue *this)
{
  THIS_WRITELOCK(this);
  this->approved_bytes = 0;
  numstracker_reset(this->incoming_bytes);
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

void packetssndqueue_setup(PacketsSndQueue *this, gint32 target_bitrate, gboolean pacing)
{
  THIS_WRITELOCK(this);
  target_bitrate>>=3;
  this->target_rate = target_bitrate * .75;
  if(!this->pacing && pacing){
    this->approved_bytes = target_bitrate / 100;
    this->pacing_started = _now(this);
    this->pacing = TRUE;
  }else if(!pacing && this->pacing){
    this->pacing = _now(this) - this->pacing_started < 2 * GST_SECOND;
  }

  this->allowed_rate_per_ms = target_bitrate / 1000;
  THIS_WRITEUNLOCK(this);
}

void packetssndqueue_approve(PacketsSndQueue *this)
{
  THIS_WRITELOCK(this);
  if(this->pacing){
    this->approved_bytes += this->allowed_rate_per_ms;
  }
  THIS_WRITEUNLOCK(this);
}

gint32 packetssndqueue_get_encoder_bitrate(PacketsSndQueue *this)
{
  gint64 result;
  THIS_READLOCK(this);
  numstracker_get_stats(this->incoming_bytes, &result);
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

void packetssndqueue_push(PacketsSndQueue *this, GstBuffer *buffer)
{
  THIS_WRITELOCK(this);
  if(this->last_logging < _now(this) - this->logging_interval){
    _logging(this);
  }
  _packetssndqueue_add(this, buffer);
  THIS_WRITEUNLOCK(this);
}

GstBuffer * packetssndqueue_pop(PacketsSndQueue *this)
{
  GstBuffer *result = NULL;
  THIS_WRITELOCK(this);
again:
  if(!this->counter){
    goto done;
  }
  if(!this->pacing){
    result = _packetssndqueue_rem(this);
    goto done;
  }
  if(this->items[this->items_read_index].timestamp == this->last_timestamp){
    this->approved_bytes -= this->items[this->items_read_index].size;
    result = _packetssndqueue_rem(this);
    goto done;
  }
  if(this->items[this->items_read_index].added < _now(this) - this->obsolation_treshold){
      GstBuffer *buf;
      buf = _packetssndqueue_rem(this);
      GST_WARNING_OBJECT(this, "A buffer might be dropped due to obsolation");
      gst_buffer_unref(buf);
      this->expected_lost = TRUE;
      goto again;
  }
  if(this->approved_bytes < this->items[this->items_read_index].size){
    goto done;
  }
  this->last_timestamp  = this->items[this->items_read_index].timestamp;
  this->approved_bytes -= this->items[this->items_read_index].size;
  result = _packetssndqueue_rem(this);

done:
  //g_print("approved bytes: %d - counter: %d\n", this->approved_bytes, this->counter);
  THIS_WRITEUNLOCK(this);
  return result;
}

void _packetssndqueue_add(PacketsSndQueue *this, GstBuffer *buffer)
{
  this->items[this->items_write_index].added  = _now(this);
  {
    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
    guint payload_len;
    gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp);
    this->items[this->items_write_index].size = payload_len = gst_rtp_buffer_get_payload_len(&rtp);
    this->items[this->items_write_index].timestamp = gst_rtp_buffer_get_timestamp(&rtp);
    gst_rtp_buffer_unmap(&rtp);

    numstracker_add(this->incoming_bytes, payload_len);
  }
  this->items[this->items_write_index].buffer = gst_buffer_ref(buffer);
  ++this->counter;
  this->bytes+=this->items[this->items_write_index].size;

  if(++this->items_write_index == PACKETSSNDQUEUE_MAX_ITEMS_NUM){
    this->items_write_index = 0;
  }

  if(this->items_write_index == this->items_read_index){
    GstBuffer *buf;
    buf = _packetssndqueue_rem(this);
    if(buf){
      GST_WARNING_OBJECT(this, "A buffer might be dropped due to queue fullness");
      gst_buffer_unref(buf);
      this->expected_lost = TRUE;
    }
  }
  return;
}

GstBuffer * _packetssndqueue_rem(PacketsSndQueue *this)
{
  GstBuffer *result = NULL;
  result = this->items[this->items_read_index].buffer;
  this->bytes-=this->items[this->items_read_index].size;

  this->items[this->items_read_index].buffer = 0;
  this->items[this->items_read_index].size = 0;
  this->items[this->items_read_index].added = 0;
  if(++this->items_read_index == PACKETSSNDQUEUE_MAX_ITEMS_NUM){
    this->items_read_index = 0;
  }
  --this->counter;
  return result;
}

void _logging(PacketsSndQueue *this)
{
  mprtp_logger("logs/packetssnqueue.log",
               "----------------------------------------------------\n"
               "Seconds: %lu, pacing: %d, approved bytes: %d\n"
               "packets in queue: %d bytes in queue: %d\n",
               GST_TIME_AS_SECONDS(_now(this) - this->made), this->pacing, this->approved_bytes,

               this->counter, this->bytes
               );
  this->last_logging = _now(this);
}


#undef DEBUG_PRINT_TOOLS
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
