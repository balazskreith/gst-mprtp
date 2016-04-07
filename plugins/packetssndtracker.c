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
#include "packetssndtracker.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>     /* qsort */

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

//#define THIS_READLOCK(this)
//#define THIS_READUNLOCK(this)
//#define THIS_WRITELOCK(this)
//#define THIS_WRITEUNLOCK(this)

#define _now(this) gst_clock_get_time (this->sysclock)

GST_DEBUG_CATEGORY_STATIC (packetssndtracker_debug_category);
#define GST_CAT_DEFAULT packetssndtracker_debug_category

typedef struct _PacketsSndTrackerItem{
  guint        ref;
  guint16      seq_num;
  guint32      payload_bytes;
  GstClockTime sent;
}PacketsSndTrackerItem;

typedef struct _PacketsSndTrackerDiscardedItem{
  guint16      seq_num;
  gboolean     discarded;
}PacketsSndTrackerDiscardedItem;

G_DEFINE_TYPE (PacketsSndTracker, packetssndtracker, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void packetssndtracker_finalize (GObject * object);

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
packetssndtracker_class_init (PacketsSndTrackerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = packetssndtracker_finalize;

  GST_DEBUG_CATEGORY_INIT (packetssndtracker_debug_category, "packetssndtracker", 0,
      "PacketsSndTracker");

}

void
packetssndtracker_finalize (GObject * object)
{
  PacketsSndTracker *this;
  this = PACKETSSNDTRACKER(object);
  g_object_unref(this->sysclock);
}

void
packetssndtracker_init (PacketsSndTracker * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sent        = g_queue_new();
  this->sent_in_1s  = g_queue_new();
  this->acked       = g_queue_new();
  this->discards    = g_queue_new();
  this->sysclock = gst_system_clock_obtain();
}

PacketsSndTracker *make_packetssndtracker(void)
{
    PacketsSndTracker *this;
    this = g_object_new (PACKETSSNDTRACKER_TYPE, NULL);
    return this;
}


void packetssndtracker_reset(PacketsSndTracker *this)
{
  THIS_WRITELOCK (this);

  THIS_WRITEUNLOCK (this);
}


void packetssndtracker_add(PacketsSndTracker *this, guint payload_len, guint16 sn)
{
  PacketsSndTrackerItem* item;
  GstClockTime now;
  THIS_WRITELOCK (this);
  now = _now(this);
  //make item
  item = mprtp_malloc(sizeof(PacketsSndTrackerItem));
  item->payload_bytes = payload_len;
  item->sent          = now;
  item->seq_num       = sn;
  item->ref           = 2;

  this->bytes_in_flight+= item->payload_bytes;
  ++this->packets_in_flight;
  g_queue_push_tail(this->sent, item);

  this->sent_bytes_in_1s += item->payload_bytes;
  ++this->sent_packets_in_1s;
  g_queue_push_tail(this->sent_in_1s, item);
  while(!g_queue_is_empty(this->sent_in_1s)){
    item = g_queue_peek_head(this->sent_in_1s);
    if(now - GST_SECOND < item->sent){
      break;
    }
    item = g_queue_pop_head(this->sent_in_1s);
    this->sent_bytes_in_1s -= item->payload_bytes;
    --this->sent_packets_in_1s;
    if(--item->ref == 0){
      mprtp_free(item);
    }
  }

  THIS_WRITEUNLOCK (this);
}

void packetssndtracker_add_discarded_bitvector(PacketsSndTracker *this,
                                            guint16 begin_seq,
                                            guint16 end_seq,
                                            GstRTCPXRBitvectorChunk *chunks)
{
  PacketsSndTrackerDiscardedItem* item;
  gint vec_i,bit_i;
  guint16 actual_seq = begin_seq;
  gint32 first_seq = -1;
  THIS_WRITELOCK (this);
  if(!chunks){
    goto done;
  }
  if(!g_queue_is_empty(this->discards)){
    first_seq = ((PacketsSndTrackerDiscardedItem*) g_queue_peek_tail(this->discards))->seq_num;
  }
  for(vec_i = 0; actual_seq != end_seq ;++vec_i){
    for(bit_i = 0; bit_i<15 && actual_seq != end_seq; ++bit_i, ++actual_seq){
      if(first_seq != -1 && _cmp_uint16(actual_seq, first_seq) <= 0) continue;
      item = mprtp_malloc(sizeof(PacketsSndTrackerDiscardedItem));
      item->discarded = ((chunks[vec_i].bitvector>>bit_i) & 1) == 0;
      item->seq_num = actual_seq;
      g_queue_push_tail(this->discards, item);
    }
  }
done:
  THIS_WRITEUNLOCK (this);
}

guint32 packetssndtracker_get_goodput_bytes_from_acked(PacketsSndTracker *this, gdouble *fraction_utilized)
{
  PacketsSndTrackerDiscardedItem *disc_item;
  PacketsSndTrackerItem *acked_item;
  GList *ait;
  guint32 result;
  guint acked_num = 0, discarded_num = 0;
  THIS_WRITELOCK (this);
  result = this->acked_bytes_in_1s;
  ait = this->acked->head;
again:
  if(g_queue_is_empty(this->discards) || !ait){
    goto done;
  }
  disc_item = g_queue_peek_head(this->discards);
  acked_item     = ait->data;
  if(acked_item->seq_num < disc_item->seq_num){
    ait = ait->next;
    goto again;
  }
  if(disc_item->seq_num < acked_item->seq_num){
    mprtp_free(g_queue_pop_head(this->discards));
    goto again;
  }
  if(disc_item->discarded){
    result -= acked_item->payload_bytes;
    ++discarded_num;
  }
  ++acked_num;
  mprtp_free(g_queue_pop_head(this->discards));
  ait = ait->next;
  goto again;
done:
  if(fraction_utilized){
    if(!acked_num){
      *fraction_utilized = 0.;
    }else{
      *fraction_utilized = (gdouble)(acked_num - discarded_num) / (gdouble) acked_num;
    }
  }
  THIS_WRITEUNLOCK (this);
  return result;
}

void packetssndtracker_update_hssn(PacketsSndTracker *this, guint16 hssn)
{
  PacketsSndTrackerItem* item;
  GstClockTime treshold;

  THIS_WRITELOCK (this);

  while(!g_queue_is_empty(this->sent)){
    item = g_queue_peek_head(this->sent);
    if(_cmp_uint16(hssn, item->seq_num) < 0){
      break;
    }
    item = g_queue_pop_head(this->sent);
    this->bytes_in_flight    -= item->payload_bytes;
    --this->packets_in_flight;
    this->acked_bytes_in_1s  += item->payload_bytes;
    ++this->acked_packets_in_1s;
    g_queue_push_tail(this->acked, item);
  }
  if(g_queue_is_empty(this->acked)){
    goto done;
  }

  treshold = ((PacketsSndTrackerItem*) g_queue_peek_tail(this->acked))->sent - GST_SECOND;
  while(!g_queue_is_empty(this->acked)){
    item = g_queue_peek_head(this->acked);
    if(treshold < item->sent){
      break;
    }
    item = g_queue_pop_head(this->acked);
    this->acked_bytes_in_1s -= item->payload_bytes;
    --this->acked_packets_in_1s;
    if(--item->ref == 0){
      mprtp_free(item);
    }
  }

done:
  THIS_WRITEUNLOCK (this);
}

void
packetssndtracker_get_stats (PacketsSndTracker * this, PacketsSndTrackerStat* result)
{
  THIS_READLOCK (this);
  result->acked_bytes_in_1s     = this->acked_bytes_in_1s;
  result->sent_bytes_in_1s      = this->sent_bytes_in_1s;
  result->bytes_in_flight       = this->bytes_in_flight;
  result->acked_packets_in_1s   = this->acked_packets_in_1s;
  result->sent_packets_in_1s    = this->sent_packets_in_1s;
  result->packets_in_flight     = this->packets_in_flight;
  THIS_READUNLOCK (this);
}



#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
