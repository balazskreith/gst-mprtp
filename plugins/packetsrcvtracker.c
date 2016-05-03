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

#define _now(this) gst_clock_get_time (this->sysclock)

GST_DEBUG_CATEGORY_STATIC (packetsrcvtracker_debug_category);
#define GST_CAT_DEFAULT packetsrcvtracker_debug_category

G_DEFINE_TYPE (PacketsRcvTracker, packetsrcvtracker, G_TYPE_OBJECT);


struct _CorrBlock{
  guint           id,N;
  gdouble         Iu0,Iu1,Id1,Id2,Id3,G01,M0,M1,G_[64],M_[64];
  gint            index;
  gdouble         g;
  CorrBlock*      next;
};


static void packetsrcvtracker_finalize (GObject * object);

static void _obsolate_discarded(PacketsRcvTracker *this);
static void _add_packet_to_discarded(PacketsRcvTracker *this, PacketsRcvTrackerItem* item);
static void _obsolate_misordered(PacketsRcvTracker *this);
static void _add_packet_to_missed(PacketsRcvTracker *this, PacketsRcvTrackerItem* item);
static void _obsolate_packets(PacketsRcvTracker *this);
static void _add_packet(PacketsRcvTracker *this, PacketsRcvTrackerItem *item);
static void _find_misordered_packet(PacketsRcvTracker * this, guint16 seq, guint payload_len);
static void _refresh_delay_variation(PacketsRcvTracker * this, GstMpRTPBuffer *mprtp);

static PacketsRcvTrackerItem *_make_item(PacketsRcvTracker * this, guint16 sn);
static void _ref_item(PacketsRcvTrackerItem *item);
static void _unref_item(PacketsRcvTrackerItem *item);

static void _execute_corrblocks(PacketsRcvTracker *this, CorrBlock *blocks);
static void _execute_corrblock(CorrBlock* this);

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

static gint
_cmp_uint32 (guint32 x, guint32 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 2147483648) return -1;
  if(x > y && x - y > 2147483648) return -1;
  if(x < y && y - x > 2147483648) return 1;
  if(x > y && x - y < 2147483648) return 1;
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

  this->blocks = g_malloc0(sizeof(CorrBlock) * 10);
  {
    gint i;
    for(i=0; i < 6; ++i){
      this->blocks[i].next = &this->blocks[i + 1];
      this->blocks[i].id   = i;
      this->blocks[i].N    = 64>>i;
    }
  }
  this->block_index = 1;
  this->cblocks_counter = 1;

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


void packetsrcvtracker_add(PacketsRcvTracker *this, GstMpRTPBuffer *mprtp)
{
  PacketsRcvTrackerItem *item;
  THIS_WRITELOCK (this);

  ++_trackerstat(this).total_packets_received;
  _trackerstat(this).total_payload_received += mprtp->payload_bytes;

  if(!this->initialized){
    this->initialized = TRUE;
    item = _make_item(this, mprtp->subflow_seq);
    item->payload_len = mprtp->payload_bytes;
    item->received    = TRUE;
    _add_packet(this, item);
    _trackerstat(this).highest_seq = mprtp->subflow_seq;
    goto done;
  }
  if(_cmp_seq(mprtp->subflow_seq, _trackerstat(this).highest_seq) <= 0){
    _find_misordered_packet(this, mprtp->subflow_seq, mprtp->payload_bytes);
    goto done;
  }

  if(_cmp_seq(_trackerstat(this).highest_seq + 1, mprtp->subflow_seq) < 0){
    guint16 seq = _trackerstat(this).highest_seq + 1;
    for(; _cmp_seq(seq, mprtp->subflow_seq) < 0; ++seq){
      item = _make_item(this, seq);
      _add_packet(this, item);
      if(0 < this->discard_treshold){
        _add_packet_to_missed(this, item);
      }else{
        _add_packet_to_discarded(this, item);
      }

    }
  }else{
    //packet is in order
  }

  item = _make_item(this, mprtp->subflow_seq);
  item->payload_len = mprtp->payload_bytes;
  item->received    = TRUE;

  ++_trackerstat(this).good_packets_in_1s;
  _trackerstat(this).good_payload_bytes_in_1s += item->payload_len;

  _add_packet(this, item);

  _refresh_delay_variation(this, mprtp);
  if(65472 < _trackerstat(this).highest_seq && mprtp->subflow_seq < 128){
    ++_trackerstat(this).cycle_num;
  }

  //set the new packet seq as the highest seq
  _trackerstat(this).highest_seq = mprtp->subflow_seq;
done:
  THIS_WRITEUNLOCK (this);
}

void packetsrcvtracker_update_reported_sn(PacketsRcvTracker *this, guint16 reported_sn)
{
  THIS_WRITELOCK (this);
  this->reported_seq = reported_sn;
  THIS_WRITEUNLOCK (this);
}

gdouble packetsrcvtracker_get_remb(PacketsRcvTracker * this, guint16 *hssn)
{
  gdouble result;
  THIS_READLOCK(this);
  if(hssn){
    *hssn = _trackerstat(this).highest_seq;
  }
  result = _trackerstat(this).good_payload_bytes_in_1s * 8;
//  g_print("g0: %-10f (%-10f) | g1: %-10f | g2: %-10f | g3: %-10f | g4: %-10f | g5: %-10f\n",
//          this->blocks[0].g,
//          this->aaaa,
//          this->blocks[1].g,
//          this->blocks[2].g,
//          this->blocks[3].g,
//          this->blocks[4].g,
//          this->blocks[5].g);
  result *= 1. - CONSTRAIN(0., .8, this->blocks[1].g);
  THIS_READUNLOCK(this);
  return result;
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
  if(this->reported_seq != -1){
    for(item = it->data;
        item && _cmp_seq(item->seq, this->reported_seq) < 0;
        it = it->next, item = it->data);
  }
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
  _ref_item(item);
  item->received  = FALSE;
  item->discarded = TRUE;
  g_queue_push_tail(this->discarded, item);
  ++_trackerstat(this).total_packets_discarded_or_lost;
}

void _obsolate_misordered(PacketsRcvTracker *this)
{
  PacketsRcvTrackerItem* item;
  GstClockTime now;

  _obsolate_discarded(this);

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
  _ref_item(item);
  item->received = FALSE;
  item->missed   = TRUE;
  g_queue_push_tail(this->missed, item);
}

void _obsolate_packets(PacketsRcvTracker *this)
{
  PacketsRcvTrackerItem* item;
  GstClockTime th_1s;

  _obsolate_misordered(this);

  th_1s = _now(this) - GST_SECOND;
  while(!g_queue_is_empty(this->packets)){
    item = g_queue_peek_head(this->packets);
    if(item->added < th_1s){
      item = g_queue_pop_head(this->packets);
      if(!item->missed && !item->discarded){
        --_trackerstat(this).good_packets_in_1s;
        _trackerstat(this).good_payload_bytes_in_1s -= item->payload_len;
      }
      _unref_item(item);
      continue;
    }
    break;
  }
}

void _add_packet(PacketsRcvTracker *this, PacketsRcvTrackerItem *item)
{
  _obsolate_packets(this);
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


void _refresh_delay_variation(PacketsRcvTracker * this, GstMpRTPBuffer *mprtp)
{
  gint64 devar, drcv, dsnd;

  if(this->devar.last_ntp_rcv_time == 0){
    this->devar.last_timestamp    = mprtp->timestamp;
    this->devar.last_ntp_snd_time = mprtp->abs_snd_ntp_time;
    this->devar.last_ntp_rcv_time = mprtp->abs_rcv_ntp_time;
    this->devar.last_delay        = mprtp->delay;
    return;
  }

  DISABLE_LINE _cmp_uint32(mprtp->timestamp, this->devar.last_timestamp);

  if(mprtp->abs_snd_ntp_time < this->devar.last_ntp_snd_time){
    return;
  }
  if(mprtp->abs_snd_ntp_time < this->devar.last_ntp_snd_time  + get_ntp_from_epoch_ns(5 * GST_MSECOND)){
    return;
  }

  drcv = (mprtp->abs_rcv_ntp_time - this->devar.last_ntp_rcv_time);
  dsnd = (mprtp->abs_snd_ntp_time - this->devar.last_ntp_snd_time);
  if(mprtp->abs_rcv_ntp_time < this->devar.last_ntp_rcv_time ||
     mprtp->abs_snd_ntp_time < this->devar.last_ntp_snd_time)
  {
      g_warning("PROBLEMS WITH RCV OR SND NTP TIME");
  }

  devar = drcv - dsnd;
  this->blocks[0].Iu0 = MAX(devar, -1 * devar);
  _execute_corrblocks(this, this->blocks);
  _execute_corrblocks(this, this->blocks);
  this->blocks[0].Id1 = MAX(devar, -1 * devar);


  //mprtp_logger("devars.csv", "%lu\n", MIN(this->devar.last_delay - mprtp->delay, mprtp->delay - this->devar.last_delay));
  this->devar.last_timestamp    = mprtp->timestamp;
  this->devar.last_ntp_snd_time = mprtp->abs_snd_ntp_time;
  this->devar.last_ntp_rcv_time = mprtp->abs_rcv_ntp_time;
  this->devar.last_delay        = mprtp->delay;
}


PacketsRcvTrackerItem *_make_item(PacketsRcvTracker * this, guint16 sn)
{
  PacketsRcvTrackerItem *result = NULL;
  result = mprtp_malloc(sizeof(PacketsRcvTrackerItem));
  result = g_slice_new0(PacketsRcvTrackerItem);
  result->seq = sn;
  result->added = _now(this);
  result->ref = 1;
  result->payload_len = 0;
  result->discarded = FALSE;
  result->lost = FALSE;
  result->missed = FALSE;

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
  if(1 < item->ref){
    --item->ref;
    DISABLE_LINE _print_item(item);
    return;
  }
  g_slice_free(PacketsRcvTrackerItem, item);
}



void _execute_corrblocks(PacketsRcvTracker *this, CorrBlock *blocks)
{
  guint32 X = (this->cblocks_counter ^ (this->cblocks_counter-1))+1;
  switch(X){
    case 2:
      _execute_corrblock(blocks);
    break;
    case 4:
          _execute_corrblock(blocks + 1);
        break;
    case 8:
          _execute_corrblock(blocks + 2);
        break;
    case 16:
          _execute_corrblock(blocks + 3);
        break;
    case 32:
          _execute_corrblock(blocks + 4);
        break;
    case 64:
          _execute_corrblock(blocks + 5);
        break;
    case 128:
//          _execute_corrblock(blocks + 6);
        break;
    case 256:
//          _execute_corrblock(blocks + 7);
        break;
    default:
//      g_print("not execute: %u\n", X);
      break;
  }

  ++this->cblocks_counter;
}

void _execute_corrblock(CorrBlock* this)
{
  this->M1   = this->M0;
  this->M0  -= this->M_[this->index];
  this->G01 -= this->G_[this->index];
  this->M0  += this->M_[this->index] = this->Iu0;
  this->G01 += this->G_[this->index] = this->Iu0 * this->Id1;

  if(this->M0 && this->M1){
    this->g  = this->G01 / (gdouble)(this->N-1);
    this->g /= this->M0 / (gdouble)(this->N)  * this->M1 / (gdouble)(this->N-1);
    this->g -= 1.;
  }else{
    this->g = 0.;
  }
  if(++this->index == this->N) {
    this->index = 0;
  }

  if(this->next && this->id < 6){
    CorrBlock *next = this->next;
    next->Iu0 = this->Iu0 + this->Iu1;
    next->Id1 = this->Id2 + this->Id3;
  }

  this->Iu1  = this->Iu0;
  this->Id3  = this->Id2;
  this->Id2  = this->Id1;
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
