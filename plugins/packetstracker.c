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
#include "packetstracker.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>     /* qsort */

//#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
//#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
//#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
//#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define THIS_READLOCK(this)
#define THIS_READUNLOCK(this)
#define THIS_WRITELOCK(this)
#define THIS_WRITEUNLOCK(this)

#define _now(this) gst_clock_get_time (this->sysclock)

GST_DEBUG_CATEGORY_STATIC (packetstracker_debug_category);
#define GST_CAT_DEFAULT packetstracker_debug_category

typedef struct _PacketsTrackerItem{
  guint        ref;
  guint16      seq_num;
  guint32      payload_bytes;
  GstClockTime sent;
}PacketsTrackerItem;

G_DEFINE_TYPE (PacketsTracker, packetstracker, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void packetstracker_finalize (GObject * object);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------


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

void
packetstracker_class_init (PacketsTrackerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = packetstracker_finalize;

  GST_DEBUG_CATEGORY_INIT (packetstracker_debug_category, "packetstracker", 0,
      "PacketsTracker");

}

void
packetstracker_finalize (GObject * object)
{
  PacketsTracker *this;
  this = PACKETSTRACKER(object);
  g_object_unref(this->sysclock);
}

void
packetstracker_init (PacketsTracker * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sent     = g_queue_new();
  this->acked = g_queue_new();
  this->sysclock = gst_system_clock_obtain();
}

PacketsTracker *make_packetstracker(void)
{
    PacketsTracker *this;
    this = g_object_new (PACKETSTRACKER_TYPE, NULL);
    return this;
}


void packetstracker_reset(PacketsTracker *this)
{
  THIS_WRITELOCK (this);

  THIS_WRITEUNLOCK (this);
}


void packetstracker_add(PacketsTracker *this, GstRTPBuffer *rtp, guint16 sn)
{
  PacketsTrackerItem* item;
  GstClockTime now;
  THIS_WRITELOCK (this);
  now = _now(this);
  //make item
  item = mprtp_malloc(sizeof(PacketsTrackerItem));
  item->payload_bytes = gst_rtp_buffer_get_payload_len(rtp);
  item->sent          = now;
  item->seq_num       = sn;
  item->ref           = 2;

  this->bytes_in_flight+= item->payload_bytes;
  g_queue_push_tail(this->sent, item);

  this->sent_in_1s_sum += item->payload_bytes;
  g_queue_push_tail(this->sent_in_1s, item);

  while(!g_queue_is_empty(this->sent_in_1s)){
    item = g_queue_peek_head(this->sent_in_1s);
    if(now - GST_SECOND < item->sent){
      break;
    }
    item = g_queue_pop_head(this->sent_in_1s);
    this->sent_in_1s_sum -= item->payload_bytes;
    if(--item->ref == 0){
      mprtp_free(item);
    }
  }

  THIS_WRITEUNLOCK (this);
}

void packetstracker_feedback_update(PacketsTracker *this, GstMPRTCPReportSummary *summary)
{
  PacketsTrackerItem* item;
  GstClockTime treshold;

  THIS_WRITELOCK (this);

  while(!g_queue_is_empty(this->sent)){
    item = g_queue_peek_head(this->sent);
    if(_cmp_uint16(summary->RR.HSSN, item->seq_num) < 0){
      break;
    }
    item = g_queue_pop_head(this->sent);
    this->bytes_in_flight -= item->payload_bytes;
    this->acked_in_1s     += item->payload_bytes;
    g_queue_push_tail(this->acked, item);
  }
  if(g_queue_is_empty(this->acked)){
    goto done;
  }

  treshold = ((PacketsTrackerItem*) g_queue_peek_tail(this->acked))->sent - GST_SECOND;
  while(!g_queue_is_empty(this->acked)){
    item = g_queue_peek_head(this->acked);
    if(treshold < item->sent){
      break;
    }
    item = g_queue_pop_head(this->acked);
    this->acked_in_1s -= item->payload_bytes;
    if(--item->ref == 0){
      mprtp_free(item);
    }
  }
  this->received_in_1s = this->acked_in_1s * (1. - summary->RR.lost_rate);

  if(summary->XR_RFC7097.processed){
    this->goodput_in_1s = this->received_in_1s - summary->XR_RFC7097.total;
  }else if(summary->XR_RFC7243.processed){
    this->goodput_in_1s = this->received_in_1s - summary->XR_RFC7243.discarded_bytes;
  }

done:
  THIS_WRITEUNLOCK (this);
}

void
packetstracker_get_stats (PacketsTracker * this, PacketsTrackerStat* result)
{
  THIS_READLOCK (this);
  result->acked_in_1s    = this->acked_in_1s;
  result->received_in_1s = this->received_in_1s;
  result->goodput_in_1s  = this->goodput_in_1s;
  result->sent_in_1s = this->sent_in_1s_sum;
  THIS_READUNLOCK (this);
}



#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
