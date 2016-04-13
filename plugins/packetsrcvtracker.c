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
#include "packetsrcvtracker.h"
#include "mprtplogger.h"
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

#define PACKETSRCVTRACKER_ITEMSBED_LENGTH 10000

#define _now(this) gst_clock_get_time (this->sysclock)

GST_DEBUG_CATEGORY_STATIC (packetsrcvtracker_debug_category);
#define GST_CAT_DEFAULT packetsrcvtracker_debug_category

G_DEFINE_TYPE (PacketsRcvTracker, packetsrcvtracker, G_TYPE_OBJECT);


static void packetsrcvtracker_finalize (GObject * object);

static void _obsolate_discarded(PacketsRcvTracker *this);
static void _add_packet_to_discarded(PacketsRcvTracker *this, PacketsRcvTrackerItem* item);
static void _obsolate_misordered(PacketsRcvTracker *this);
static void _add_packet_to_missed(PacketsRcvTracker *this, PacketsRcvTrackerItem* item);
static void _add_packet(PacketsRcvTracker *this, PacketsRcvTrackerItem *item);
static void _find_misordered_packet(PacketsRcvTracker * this, guint16 seq, guint payload_len);

static PacketsRcvTrackerItem *_make_item(PacketsRcvTracker * this, guint16 sn);
static void _ref_item(PacketsRcvTrackerItem *item);
static void _unref_item(PacketsRcvTrackerItem *item);

#define _trackerstat(this) this->trackerstat


static gint
_cmp_seq (guint16 x, guint16 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 32768) return -1;
  if(x > y && x - y > 32768) return -1;
  if(x < y && y - x > 32768) return 1;
  if(x > y && x - y < 32768) return 1;
  return 0;
}

void
packetsrcvtracker_class_init (PacketsRcvTrackerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = packetsrcvtracker_finalize;

  GST_DEBUG_CATEGORY_INIT (packetsrcvtracker_debug_category, "packetsrcvtracker", 0,
      "PacketsRcvTracker");

}

static void _print_item(PacketsRcvTrackerItem *item)
{
  g_print("Item dump | Sn: %hu | ref: %d | disd: %d | lost: %d | rcvd: %d |\n"
      ,
      item->seq,
      item->ref,
      item->discarded,
      item->lost,
      item->received
      );
}

void
packetsrcvtracker_finalize (GObject * object)
{
  PacketsRcvTracker *this;
  this = PACKETSRCVTRACKER(object);
  g_object_unref(this->sysclock);
}

void
packetsrcvtracker_init (PacketsRcvTracker * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->missed = g_queue_new();
  this->discarded  = g_queue_new();
  this->sysclock = gst_system_clock_obtain();

  this->packets = g_queue_new();

  this->missed = g_queue_new();
  this->discard_treshold = 300 * GST_MSECOND;

  this->discarded = g_queue_new();
  this->lost_treshold = 1000 * GST_MSECOND;

  this->itemsbed = mprtp_malloc(sizeof(PacketsRcvTrackerItem) * PACKETSRCVTRACKER_ITEMSBED_LENGTH);
  this->itemsbed_index = 0;
}

PacketsRcvTracker *make_packetsrcvtracker(void)
{
    PacketsRcvTracker *this;
    this = g_object_new (PACKETSRCVTRACKER_TYPE, NULL);
    return this;
}

void packetsrcvtracker_reset(PacketsRcvTracker *this)
{
  THIS_WRITELOCK (this);
  memset(&this->trackerstat, 0, sizeof(PacketsRcvTrackerStat));
  this->initialized = FALSE;
  THIS_WRITEUNLOCK (this);
}

void packetsrcvtracker_set_lost_treshold(PacketsRcvTracker *this, GstClockTime treshold)
{
  THIS_WRITELOCK (this);
  this->lost_treshold = treshold;
  THIS_WRITEUNLOCK (this);
}

void packetsrcvtracker_set_discarded_treshold(PacketsRcvTracker *this, GstClockTime treshold)
{
  THIS_WRITELOCK (this);
  this->discard_treshold = treshold;
  THIS_WRITEUNLOCK (this);
}


void packetsrcvtracker_add(PacketsRcvTracker *this, guint payload_len, guint16 sn)
{
  PacketsRcvTrackerItem *item;
  THIS_WRITELOCK (this);

  ++_trackerstat(this).total_packets_received;
  _trackerstat(this).total_payload_received += payload_len;

  if(!this->initialized){
    this->initialized = TRUE;
    item = _make_item(this, sn);
    item->payload_len = payload_len;
    item->received    = TRUE;
    _add_packet(this, item);
    _trackerstat(this).highest_seq = sn;
    goto done;
  }

  if(_cmp_seq(sn, _trackerstat(this).highest_seq) <= 0){
    _find_misordered_packet(this, sn, payload_len);
    goto done;
  }

  if(_cmp_seq(_trackerstat(this).highest_seq + 1, sn) < 0){
    guint16 seq = _trackerstat(this).highest_seq + 1;
    for(; _cmp_seq(seq, sn) < 0; ++seq){
      item = _make_item(this, seq);
      _add_packet(this, item);
      _add_packet_to_missed(this, item);
    }
  }else{

    //packet is in order
  }

  item = _make_item(this, sn);
  item->payload_len = payload_len;
  item->received    = TRUE;
  _add_packet(this, item);

  //consider cycle num increase with allowance of a little gap
  if(65472 < _trackerstat(this).highest_seq && sn < 128){
    ++_trackerstat(this).cycle_num;
  }

  //set the new packet seq as the highest seq
  _trackerstat(this).highest_seq = sn;
done:
  THIS_WRITEUNLOCK (this);
}

void packetsrcvtracker_update_reported_sn(PacketsRcvTracker *this, guint16 reported_sn)
{
  PacketsRcvTrackerItem* item;
  THIS_WRITELOCK (this);
again:
  if(g_queue_is_empty(this->packets)){
    goto done;
  }
  item = g_queue_peek_head(this->packets);
  if(_cmp_seq(reported_sn, item->seq) < 0){
    goto done;
  }
  item = g_queue_pop_head(this->packets);
  _unref_item(item);
  goto again;
done:
  THIS_WRITEUNLOCK (this);
}

void packetsrcvtracker_set_bitvectors(PacketsRcvTracker * this,
                                     guint16 *begin_seq,
                                     guint16 *end_seq,
                                     GstRTCPXRChunk *chunks,
                                     guint *chunks_length)
{
  gint vec_i = 0, bit_i = 0;
  GList *it;
  PacketsRcvTrackerItem *item;

  THIS_READLOCK(this);
  if(g_queue_is_empty(this->packets)){
    goto done;
  }
  it = this->packets->head;
  if(begin_seq){
    item = it->data;
    *begin_seq = item->seq;
  }
  chunks[0].Bitvector.chunk_type = TRUE;
again:
  item = it->data;
  if(!item->discarded){
      chunks[vec_i].Bitvector.bitvector |= (guint16)(1<<bit_i);
  }
  if(++bit_i == 15){
    bit_i = 0;
//    g_print("#%X#\n", chunks[vec_i].Bitvector.bitvector);
    ++vec_i;
    chunks[vec_i].Bitvector.chunk_type = TRUE;
  }
  if(it->next){
    it = it->next;
    goto again;
  }
  if(end_seq){
    *end_seq = item->seq;
  }
done:
  if(chunks_length){
    *chunks_length = vec_i + 1;
  }
  THIS_READUNLOCK(this);
}

void
packetsrcvtracker_get_stat (PacketsRcvTracker * this, PacketsRcvTrackerStat* result)
{
  THIS_READLOCK (this);
  memcpy(result, &this->trackerstat, sizeof(PacketsRcvTrackerStat));
  THIS_READUNLOCK (this);
}


void _obsolate_discarded(PacketsRcvTracker *this)
{
  PacketsRcvTrackerItem* item;
  GstClockTime now;

  now = _now(this);
  while(!g_queue_is_empty(this->discarded)){
    item = g_queue_peek_head(this->discarded);
    if(item->received){
      item = g_queue_pop_head(this->discarded);
      _unref_item(item);
      continue;
    }
    if(item->added < now - this->lost_treshold){
      item = g_queue_pop_head(this->discarded);
      ++_trackerstat(this).total_packets_lost;
      item->lost = TRUE;
      _unref_item(item);
      continue;
    }
    break;
  }
}

void _add_packet_to_discarded(PacketsRcvTracker *this, PacketsRcvTrackerItem* item)
{
  _obsolate_discarded(this);
  _ref_item(item);
  item->discarded = TRUE;
  g_queue_push_tail(this->discarded, item);
  ++_trackerstat(this).total_packets_discarded_or_lost;
}

void _obsolate_misordered(PacketsRcvTracker *this)
{
  PacketsRcvTrackerItem* item;
  GstClockTime now;

  now = _now(this);
  while(!g_queue_is_empty(this->missed)){
    item = g_queue_peek_head(this->missed);
    if(item->received){
      item = g_queue_pop_head(this->missed);
      _unref_item(item);
      continue;
    }
    if(item->added < now - this->discard_treshold){
      item = g_queue_pop_head(this->missed);
      _add_packet_to_discarded(this, item);
      _unref_item(item);
      continue;
    }
    break;
  }
}

void _add_packet_to_missed(PacketsRcvTracker *this, PacketsRcvTrackerItem* item)
{
  _obsolate_misordered(this);
  _ref_item(item);
  item->received = FALSE;
  g_queue_push_tail(this->missed, item);
}



void _add_packet(PacketsRcvTracker *this, PacketsRcvTrackerItem *item)
{
  g_queue_push_tail(this->packets, item);
}

static gint _find_packet_helper(gconstpointer ptr2item, gconstpointer ptr2searched_seq)
{
  const guint16 *seq;
  const PacketsRcvTrackerItem*item;
  seq = ptr2searched_seq;
  item = ptr2item;
  return item->seq == *seq ? 0 : -1;
}

void _find_misordered_packet(PacketsRcvTracker * this, guint16 seq, guint payload_len)
{
  GList *it;
  PacketsRcvTrackerItem *item;
  it = g_queue_find_custom(this->missed, &seq, _find_packet_helper);
  if(it != NULL){
    item = it->data;
    item->received = TRUE;
    item->payload_len = payload_len;
    goto done;
  }

  it = g_queue_find_custom(this->discarded, &seq, _find_packet_helper);
  if(it != NULL){
    item = it->data;
    item->received = TRUE;
    item->payload_len = payload_len;
    ++_trackerstat(this).total_packets_discarded;
    _trackerstat(this).total_payload_discarded+=payload_len;
    goto done;
  }

done:
  return;
}

PacketsRcvTrackerItem *_make_item(PacketsRcvTracker * this, guint16 sn)
{
  PacketsRcvTrackerItem *result = NULL;
  result = &this->itemsbed[this->itemsbed_index];
  if(0 < result->ref){
    g_warning("Itemsbed seems to be small for Packetsrcvtracker.");
    goto done;
  }
  this->itemsbed_index = (this->itemsbed_index + 1) % PACKETSRCVTRACKER_ITEMSBED_LENGTH;
  memset(result, 0, sizeof(PacketsRcvTrackerItem));

  result->seq = sn;
  result->added = _now(this);
  result->ref = 1;

//  g_print("make item index at %d for %hu\n", this->itemsbed_index, sn);
done:
  return result;
}

void _ref_item(PacketsRcvTrackerItem *item)
{
  if(item->ref == 0){
      GST_WARNING("Too few ref for an item");
  }else{
    ++item->ref;
  }
}

void _unref_item(PacketsRcvTrackerItem *item)
{
  if(0 < item->ref){
    --item->ref;
DISABLE_LINE    _print_item(item);
  }else{
      GST_WARNING("Too many unref for items");
  }
}


#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
