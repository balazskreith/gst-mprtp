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
#include "packetsrcvqueue.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>
#include "mprtpspath.h"
#include "rtpfecbuffer.h"

//#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
//#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
//#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
//#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define _now(this) gst_clock_get_time (this->sysclock)

#define THIS_READLOCK(this)
#define THIS_READUNLOCK(this)
#define THIS_WRITELOCK(this)
#define THIS_WRITEUNLOCK(this)


static gint
_cmp_uint16 (guint16 x, guint16 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 32768) return -1;
  if(x > y && x - y > 32768) return -1;
  if(x < y && y - x > 32768) return 1;
  if(x > y && x - y < 32768) return 1;
  return 0;
}

//static gint
//_cmp_uint32 (guint32 x, guint32 y)
//{
//  if(x == y) return 0;
//  if(x < y && y - x < 2147483648) return -1;
//  if(x > y && x - y > 2147483648) return -1;
//  if(x < y && y - x > 2147483648) return 1;
//  if(x > y && x - y < 2147483648) return 1;
//  return 0;
//}

GST_DEBUG_CATEGORY_STATIC (packetsrcvqueue_debug_category);
#define GST_CAT_DEFAULT packetsrcvqueue_debug_category

G_DEFINE_TYPE (PacketsRcvQueue, packetsrcvqueue, G_TYPE_OBJECT);


//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void packetsrcvqueue_finalize (GObject * object);
//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
packetsrcvqueue_class_init (PacketsRcvQueueClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = packetsrcvqueue_finalize;

  GST_DEBUG_CATEGORY_INIT (packetsrcvqueue_debug_category, "packetsrcvqueue", 0,
      "MpRTP Manual Sending Controller");

}

void
packetsrcvqueue_finalize (GObject * object)
{
  PacketsRcvQueue *this;
  this = PACKETSRCVQUEUE(object);
  g_object_unref(this->sysclock);
  g_object_unref(this->discarded);
  g_object_unref(this->packets);
}


void
packetsrcvqueue_init (PacketsRcvQueue * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sysclock = gst_system_clock_obtain();
  this->discarded = g_queue_new();
  this->packets = g_queue_new();

  this->desired_framenum = 1;
  this->high_watermark = .01 * GST_SECOND;
  this->low_watermark =  .04 * GST_SECOND;
  this->spread_factor = 2.;

}


void packetsrcvqueue_reset(PacketsRcvQueue *this)
{
  THIS_WRITELOCK(this);

  THIS_WRITEUNLOCK(this);
}


PacketsRcvQueue *make_packetsrcvqueue(void)
{
  PacketsRcvQueue *result;
  result = g_object_new (PACKETSRCVQUEUE_TYPE, NULL);
  result->made = _now(result);
  return result;
}

void
packetsrcvqueue_set_playout_allowed(PacketsRcvQueue *this, gboolean playout_permission)
{
  THIS_WRITELOCK (this);
  this->playout_allowed = playout_permission;
  THIS_WRITEUNLOCK (this);
}

void
packetsrcvqueue_set_desired_framenum(PacketsRcvQueue *this, guint desired_framenum)
{
  THIS_WRITELOCK (this);
  this->desired_framenum = desired_framenum;
  THIS_WRITEUNLOCK (this);
}


void
packetsrcvqueue_set_high_watermark(PacketsRcvQueue *this, GstClockTime high_watermark)
{
  THIS_WRITELOCK (this);
  this->high_watermark = high_watermark;
  THIS_WRITEUNLOCK (this);
}

void
packetsrcvqueue_set_low_watermark(PacketsRcvQueue *this, GstClockTime low_watermark)
{
  THIS_WRITELOCK (this);
  this->low_watermark = low_watermark;
  THIS_WRITEUNLOCK (this);
}

void
packetsrcvqueue_flush(PacketsRcvQueue *this)
{
  THIS_WRITELOCK (this);
  this->flush = TRUE;
  THIS_WRITEUNLOCK (this);
}

static gint _mprtp_queue_sort_helper(gconstpointer a, gconstpointer b, gpointer user_data)
{
  const GstMpRTPBuffer *ai = a;
  const GstMpRTPBuffer *bi = b;
  return _cmp_uint16(ai->abs_seq, bi->abs_seq);
}

void packetsrcvqueue_push_discarded(PacketsRcvQueue *this, GstMpRTPBuffer *mprtp)
{
  GstMpRTPBuffer *head;
  THIS_WRITELOCK(this);
  if(g_queue_is_empty(this->packets)){
    g_queue_push_tail(this->packets, mprtp);
    goto done;
  }
  head = g_queue_peek_head(this->packets);
  if(_cmp_uint16(mprtp->abs_seq, head->abs_seq) < 0){
    g_queue_push_tail(this->discarded, mprtp);
    goto done;
  }
  g_queue_insert_sorted(this->packets, mprtp, _mprtp_queue_sort_helper, NULL);
done:
  THIS_WRITEUNLOCK(this);
}


void packetsrcvqueue_push(PacketsRcvQueue *this, GstMpRTPBuffer *mprtp)
{
  THIS_WRITELOCK(this);
//  g_print("%hu is transferred at %d\n", mprtp->abs_seq, mprtp->subflow_id);
  g_queue_push_tail(this->packets, mprtp);
  THIS_WRITEUNLOCK(this);
}

GstMpRTPBuffer* packetsrcvqueue_pop(PacketsRcvQueue *this)
{
  GstMpRTPBuffer *result = NULL;
  gint32 packets_num = 0;
  THIS_WRITELOCK(this);
  if(!this->playout_allowed || g_queue_is_empty(this->packets)){
    goto done;
  }
  if(!this->low_watermark || !this->high_watermark){
    result = g_queue_pop_head(this->packets);
    goto done;
  }

  packets_num = g_queue_get_length(this->packets);
  if(!this->hwmark_reached){
    if(this->high_watermark < packets_num){
       this->hwmark_reached = TRUE;
    }
    goto done;
  }
  if(packets_num < this->low_watermark){
    this->hwmark_reached = FALSE;
  }
  result = g_queue_pop_head(this->packets);
done:
  THIS_WRITEUNLOCK(this);
  return result;
}

GstMpRTPBuffer* packetsrcvqueue_pop_discarded(PacketsRcvQueue *this)
{
  GstMpRTPBuffer *result = NULL;
  THIS_WRITELOCK(this);
  if(!this->playout_allowed || g_queue_is_empty(this->discarded)){
    goto done;
  }
  result = g_queue_pop_head(this->discarded);
done:
  THIS_WRITEUNLOCK(this);
  return result;
}


#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
