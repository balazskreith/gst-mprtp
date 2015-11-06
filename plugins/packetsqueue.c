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
#include "packetsqueue.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>
#include "bintree.h"

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)
#define _cmp_uint16(x,y) ((x==y)?0:((gint16) (x - y)) < 0 ? -1 : 1)

GST_DEBUG_CATEGORY_STATIC (packetsqueue_debug_category);
#define GST_CAT_DEFAULT packetsqueue_debug_category

G_DEFINE_TYPE (PacketsQueue, packetsqueue, G_TYPE_OBJECT);



struct _Gap{
  Gap*         next;
  guint16      start;
  guint16      end;
  guint8       filled;
  guint8       size;
  GstClockTime created;
};

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void packetsqueue_finalize (GObject * object);
static PacketsQueueNode* _make_node(PacketsQueue *this,
                                    guint64 snd_time,
                                    guint16 seq_num,
                                    GstClockTime *delay);
static void _trash_node(PacketsQueue *this, PacketsQueueNode* node);
static Gap* _make_gap(PacketsQueue *this,
                                    guint16 start,
                                    guint16 end);
static void _trash_gap(PacketsQueue *this,
                       Gap* gap);

static guint64 _get_skew(PacketsQueue *this,
                         PacketsQueueNode* act,
                         PacketsQueueNode* nxt);

static void _add_gap(PacketsQueue *this,
                     guint16 start,
                     guint16 end);

static void _try_to_fill_a_gap(PacketsQueue *this,
                               guint16 seq);

static guint64 _packetsqueue_add(PacketsQueue *this,
                                 guint64 snd_time,
                                 guint16 seq_num,
                                 GstClockTime *delay);
static void _remove_head(PacketsQueue *this, guint64 *skew);
static guint16 _uint16_diff (guint16 a, guint16 b);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
packetsqueue_class_init (PacketsQueueClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = packetsqueue_finalize;

  GST_DEBUG_CATEGORY_INIT (packetsqueue_debug_category, "packetsqueue", 0,
      "MpRTP Manual Sending Controller");

}

void
packetsqueue_finalize (GObject * object)
{
  PacketsQueue *this;
  PacketsQueueNode *next;
  Gap *gap,*next_gap;

  this = PACKETSQUEUE(object);
  while(!g_queue_is_empty(this->node_pool)){
    g_free(g_queue_pop_head(this->node_pool));
  }

  while(!g_queue_is_empty(this->gaps_pool)){
    g_free(g_queue_pop_head(this->gaps_pool));
  }

  for(gap = this->gaps_head; gap; gap = next_gap){
    next_gap = gap->next;
    g_free(gap);
  }

  while(this->head){
    next = this->head->next;
    _trash_node(this, this->head);
    this->head = next;
  }
  g_object_unref(this->sysclock);

}

void
packetsqueue_init (PacketsQueue * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->jitter = 0;
  this->node_pool = g_queue_new();
  this->gaps_pool = g_queue_new();
  this->sysclock = gst_system_clock_obtain();
}


void packetsqueue_reset(PacketsQueue *this)
{
  THIS_WRITELOCK(this);
  while(this->head) _remove_head(this, NULL);
  THIS_WRITEUNLOCK(this);
}

PacketsQueue *make_packetsqueue(void)
{
  PacketsQueue *result;
  result = g_object_new (PACKETSQUEUE_TYPE, NULL);
  return result;
}

guint64 packetsqueue_add(PacketsQueue *this,
                         guint64 snd_time,
                         guint16 seq_num,
                         GstClockTime *delay)
{
  guint64 result;
  THIS_WRITELOCK(this);
  result = _packetsqueue_add(this, snd_time, seq_num, delay);
  THIS_WRITEUNLOCK(this);
  return result;
}

void packetsqueue_prepare_gap(PacketsQueue *this)
{
  THIS_WRITELOCK(this);
  this->gap_arrive = TRUE;
  THIS_WRITEUNLOCK(this);
}

void packetsqueue_prepare_discards(PacketsQueue *this)
{
  THIS_WRITELOCK(this);
  this->discarded_arrive = TRUE;
  THIS_WRITEUNLOCK(this);
}

guint64 _packetsqueue_add(PacketsQueue *this,
                          guint64 snd_time,
                          guint16 seq_num,
                          GstClockTime *delay)
{
//  guint64 skew = 0;
  PacketsQueueNode* node;
  node = _make_node(this, snd_time, seq_num, delay);
  if(!this->head) {
      this->head = this->tail = node;
      node->skew  = 0;
      this->counter = 1;
      goto done;
  }
  node->skew = _get_skew(this, this->tail, node);
  this->tail->next = node;
  if(this->gap_arrive){
    _add_gap(this, this->tail->seq_num, node->seq_num);
    this->gap_arrive = FALSE;
  }else if(this->discarded_arrive){
    _try_to_fill_a_gap(this, node->seq_num);
    this->discarded_arrive = FALSE;
  }
  this->tail = node;
  ++this->counter;
done:
  return node->skew;
}

gboolean packetsqueue_head_obsolted(PacketsQueue *this, GstClockTime treshold)
{
  gboolean result = FALSE;
  THIS_READLOCK(this);
  if(!this->head) goto done;
  if(treshold < this->head->added) goto done;
  result = TRUE;
done:
  THIS_READUNLOCK(this);
  return result;
}

guint32 packetsqueue_get_lost_packets(PacketsQueue *this, GstClockTime treshold)
{
  guint32 result = 0;
  Gap *gap;
  THIS_WRITELOCK(this);
again:
  if(!this->gaps_head) goto done;
  gap = this->gaps_head;
  if(treshold < gap->created) goto done;
//  g_print("%p Found gap for possible lost: %hu-%hu %lu-%lu\n",
//          this, gap->start, gap->end, gap->created, treshold);
  if(!gap->next) this->gaps_tail = this->gaps_head = NULL;
  else this->gaps_head = gap->next;
  result+=gap->size - gap->filled;
  _trash_gap(this, gap);
  goto again;
done:
  THIS_WRITEUNLOCK(this);
  return result;
}

guint32 packetsqueue_get_jitter(PacketsQueue *this)
{
  guint32 result = FALSE;
  THIS_READLOCK(this);
  result = this->jitter;
  THIS_READUNLOCK(this);
  return result;
}

void packetsqueue_remove_head(PacketsQueue *this, guint64 *skew)
{
  THIS_WRITELOCK(this);
  _remove_head(this, skew);
  THIS_WRITEUNLOCK(this);
}

void _remove_head(PacketsQueue *this, guint64 *skew)
{
  PacketsQueueNode *node;
  node = this->head;
  this->head = node->next;
  if(skew) *skew = node->skew;
  _trash_node(this, node);
  --this->counter;
}

guint64 _get_skew(PacketsQueue *this, PacketsQueueNode* act, PacketsQueueNode* nxt)
{
  guint64 snd_diff, rcv_diff, skew;

  rcv_diff = nxt->rcv_time - act->rcv_time;
  skew = 0;
  if (rcv_diff > 0x8000000000000000) {
    GST_WARNING_OBJECT (this, "The skew between two packets NOT real: "
            "act rcv: %lu nxt rcv: %lu",act->rcv_time, nxt->rcv_time);
    goto done;
  }

  if(act->snd_time < nxt->snd_time)
    snd_diff = nxt->snd_time - act->snd_time;
  else
    snd_diff = act->snd_time - nxt->snd_time;

  if (rcv_diff < snd_diff)
    skew = snd_diff - rcv_diff;
  else
    skew = rcv_diff - snd_diff;

  this->jitter += (skew - this->jitter) / 16.;
//  g_print("act->snd_time: %lu nxt->snd_time: %lu;\n"
//          "act->rcv_time: %lu nxt->rcv_time: %lu;\n"
//          "skew: %lu\n"
//          ,act->snd_time, nxt->snd_time,
//          act->rcv_time, nxt->rcv_time,
//          skew);
  //nxt->skew = g_random_int_range(25000, 50000);
done:
  //convert it to ns base time:
  skew = get_epoch_time_from_ntp_in_ns(skew);
  return skew;
}

void _add_gap(PacketsQueue *this, guint16 start, guint16 end)
{
  Gap *gap;
  if(!this->gaps_head){
    this->gaps_head = this->gaps_tail = _make_gap(this, start, end);
    goto done;
  }
  if(_cmp_uint16(start, this->gaps_tail->end) < 0){
    goto done;
  }
  gap = _make_gap(this, start, end);
  this->gaps_tail->next = gap;
  this->gaps_tail = gap;
  done:
  return;
}

void _try_to_fill_a_gap(PacketsQueue *this, guint16 seq)
{
  Gap *gap,*prev = NULL;
  if(!this->gaps_head) return;
  gap = this->gaps_head;
  for(gap = this->gaps_head; gap; prev = gap, gap = gap->next){
    if(_cmp_uint16(seq, gap->start) < 0)  break;
    if(_cmp_uint16(gap->end, seq) < 0) continue;
//    g_print("Gap %hu-%hu is found for %hu. size: %hu, filled: %hu\n",
//            gap->start, gap->end, seq, gap->size, gap->filled);
    if(++gap->filled < gap->size) break;
    if(gap == this->gaps_head) this->gaps_head = gap->next;
    if(gap == this->gaps_tail) this->gaps_head = prev;
    if(prev) prev->next = gap->next;
//    g_print("%p Gap %hu-%hu is filled\n", this, gap->start, gap->end);
    _trash_gap(this, gap);
    break;
  }
}

PacketsQueueNode* _make_node(PacketsQueue *this,
                             guint64 snd_time,
                             guint16 seq_num,
                             GstClockTime *delay)
{
  PacketsQueueNode *result;
  if(!g_queue_is_empty(this->node_pool))
    result = g_queue_pop_head(this->node_pool);
  else
    result = g_malloc0(sizeof(PacketsQueueNode));
  memset((gpointer)result, 0, sizeof(PacketsQueueNode));
  result->rcv_time = NTP_NOW;
  result->seq_num = seq_num;
  result->snd_time = snd_time;
  result->next = NULL;
  result->added = gst_clock_get_time(this->sysclock);
  if(delay) *delay = get_epoch_time_from_ntp_in_ns(result->rcv_time - result->snd_time);
  return result;
}


void _trash_node(PacketsQueue *this, PacketsQueueNode* node)
{
  if(g_queue_get_length(this->node_pool) > 4096)
    g_free(node);
  else
    g_queue_push_tail(this->node_pool, node);
}

Gap* _make_gap(PacketsQueue *this,
                            guint16 start,
                            guint16 end)
{
  Gap *result;
  if(!g_queue_is_empty(this->gaps_pool))
    result = g_queue_pop_head(this->gaps_pool);
  else
    result = g_malloc0(sizeof(Gap));
  memset((gpointer)result, 0, sizeof(Gap));
  result->start = start;
  result->end = end;
  result->created = gst_clock_get_time(this->sysclock);
  result->size = _uint16_diff (start, end);
  result->filled = 0;
//  g_print("%p Gap %hu-%hu is created\n", this, result->start, result->end);
  return result;
}

void _trash_gap(PacketsQueue *this, Gap* gap)
{
  if(g_queue_get_length(this->gaps_pool) > 1024)
    g_free(gap);
  else
    g_queue_push_tail(this->gaps_pool, gap);
}



guint16
_uint16_diff (guint16 a, guint16 b)
{
  if(a == b) return 0;
  else if (a <= b) return b - a - 1;
  else return ~((guint16) (a - b));
}

#undef _cmp_uint16
#undef DEBUG_PRINT_TOOLS
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
