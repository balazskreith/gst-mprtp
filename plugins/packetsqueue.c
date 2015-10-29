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

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)


GST_DEBUG_CATEGORY_STATIC (packetsqueue_debug_category);
#define GST_CAT_DEFAULT packetsqueue_debug_category

G_DEFINE_TYPE (PacketsQueue, packetsqueue, G_TYPE_OBJECT);

struct _Gap
{
  GapNode *at;
  guint16 start;
  guint16 end;
};

struct _GapNode{
  PacketsQueueNode *node;
  GapNode *succ,*pred;
  Gap *gap;
};

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void packetsqueue_finalize (GObject * object);
static PacketsQueueNode* _make_node(PacketsQueue *this,
                                    guint64 snd_time,
                                    guint16 seq_num,
                                    GstClockTime *delay);
static GapNode * _make_gapnode(PacketsQueue *this, PacketsQueueNode* at, Gap *gap);
static void _trash_node(PacketsQueue *this, PacketsQueueNode* node);
static void _trash_gap(PacketsQueue *this, Gap* gap);
static void _trash_gapnode(PacketsQueue *this, GapNode* gapnode);
static guint64 _get_skew(PacketsQueue *this, PacketsQueueNode* act, PacketsQueueNode* nxt);
static Gap * _make_gap(PacketsQueue *this, PacketsQueueNode* at, guint16 start, guint16 end);
static guint64 _packetsqueue_add(PacketsQueue *this,
                                 guint64 snd_time,
                                 guint16 seq_num,
                                 GstClockTime *delay);
static Gap* _try_found_a_gap(PacketsQueue *this, guint16 seq_num,
                             gboolean *duplicated, GapNode **insert_after);
static gboolean _try_fill_a_gap (PacketsQueue * this, PacketsQueueNode *node);
static gint _cmp_seq (guint16 x, guint16 y);
static void _remove_head(PacketsQueue *this, guint64 *skew);
static void _remove_gapnode(PacketsQueue *this, GapNode *node);
static void _remove_gap(PacketsQueue *this, Gap *gap);
static void _delete_node(PacketsQueue *this, PacketsQueueNode *node);

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
  this = PACKETSQUEUE(object);
  while(!g_queue_is_empty(this->node_pool)){
    g_free(g_queue_pop_head(this->node_pool));
  }
  while(!g_queue_is_empty(this->gaps_pool)){
    g_free(g_queue_pop_head(this->gaps_pool));
  }
  while(!g_queue_is_empty(this->gapnodes_pool)){
    g_free(g_queue_pop_head(this->gapnodes_pool));
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
  this->node_pool = g_queue_new();
  this->gaps_pool = g_queue_new();
  this->gapnodes_pool = g_queue_new();
  this->sysclock = gst_system_clock_obtain();
}

void packetsqueue_test(void)
{
  PacketsQueue *packets;
  gboolean duplicated;
  g_print("ADD PACKETS 1,2,3,\n");
  packets = make_packetsqueue();
  packetsqueue_add(packets, epoch_now_in_ns-300000, 1, NULL);
  packetsqueue_add(packets, epoch_now_in_ns-200000, 2, NULL);
  packetsqueue_add(packets, epoch_now_in_ns-100000, 3, NULL);
  {
      PacketsQueueNode* node;
      for(node = packets->head; node; node = node->next)
            g_print("%d->", node->seq_num);
  }
  g_print("\nCOUNTER: %d\n", packets->counter);

  packetsqueue_reset(packets);
  g_print("AFTER RESET->COUNTER: %d\n", packets->counter);
  {
      PacketsQueueNode* node;
      GList *it;
      Gap *gap;
      GapNode *gapnode;
      for(node = packets->head; node; node = node->next)
            g_print("%d->", node->seq_num);
      g_print("\nGaps:\n");
      for(it = packets->gaps; it; it = it->next){
        gap = it->data;
        for(gapnode = gap->at; gapnode; gapnode = gapnode->succ){
          node = gapnode->node;
          g_print("%d->", node->seq_num);
        }
      }
      g_print("\n");
  }

  g_print("ADD PACKETS 1,5,3,2,4\n");
  packetsqueue_add(packets, epoch_now_in_ns-500000, 1, NULL);
  packetsqueue_prepare_gap(packets);
  packetsqueue_add(packets, epoch_now_in_ns-400000, 5, NULL);
  packetsqueue_prepare_discarded(packets);
  packetsqueue_add(packets, epoch_now_in_ns-300000, 3, NULL);
  packetsqueue_prepare_discarded(packets);
  packetsqueue_add(packets, epoch_now_in_ns-200000, 2, NULL);
  packetsqueue_prepare_discarded(packets);
  packetsqueue_add(packets, epoch_now_in_ns-100000, 4, NULL);
  {
      PacketsQueueNode* node;
      GList *it;
      Gap *gap;
      GapNode *gapnode;
      for(node = packets->head; node; node = node->next)
            g_print("%d->", node->seq_num);
      g_print("\nGaps:\n");
      for(it = packets->gaps; it; it = it->next){
        gap = it->data;
        for(gapnode = gap->at; gapnode; gapnode = gapnode->succ){
          node = gapnode->node;
          g_print("%d->", node->seq_num);
        }
      }
      g_print("\n");
  }
  g_print("COUNTER: %d\n", packets->counter);
  g_print("DELETE 1,5: %d\n", packets->counter);
  packetsqueue_head_obsolted(packets, gst_clock_get_time(packets->sysclock));
  packetsqueue_head_obsolted(packets, gst_clock_get_time(packets->sysclock));
  g_print("COUNTER: %d\n", packets->counter);
  {
      PacketsQueueNode* node;
      GList *it;
      Gap *gap;
      GapNode *gapnode;
      for(node = packets->head; node; node = node->next)
            g_print("%d->", node->seq_num);
      g_print("\nGaps:\n");
      for(it = packets->gaps; it; it = it->next){
        gap = it->data;
        for(gapnode = gap->at; gapnode; gapnode = gapnode->succ){
          node = gapnode->node;
          g_print("%d->", node->seq_num);
        }
      }
      g_print("\n");
  }
  packetsqueue_reset(packets);
  g_print("AFTER RESET->COUNTER: %d\n", packets->counter);
  g_print("ADD PACKETS 1,5,5,2\n");
  packetsqueue_add(packets, epoch_now_in_ns-500000, 1, NULL);
  packetsqueue_prepare_gap(packets);
  packetsqueue_add(packets, epoch_now_in_ns-400000, 5, NULL);

  packetsqueue_try_found_a_gap(packets, 5, &duplicated);
  g_print("IF 5 is duplicated? %d\n",duplicated);
  packetsqueue_prepare_discarded(packets);
  packetsqueue_add(packets, epoch_now_in_ns-300000, 5, NULL);

  packetsqueue_try_found_a_gap(packets, 2, &duplicated);
  g_print("IF 2 is duplicated? %d\n",duplicated);
  packetsqueue_prepare_discarded(packets);
  packetsqueue_add(packets, epoch_now_in_ns-100000, 2, NULL);
  {
      PacketsQueueNode* node;
      GList *it;
      Gap *gap;
      GapNode *gapnode;
      for(node = packets->head; node; node = node->next)
            g_print("%d->", node->seq_num);
      g_print("\nGaps:\n");
      for(it = packets->gaps; it; it = it->next){
        gap = it->data;
        for(gapnode = gap->at; gapnode; gapnode = gapnode->succ){
          node = gapnode->node;
          g_print("%d->", node->seq_num);
        }
        g_print("|<|>|");
      }
      g_print("\n");
  }
  g_print("ADD PACKETS 6,3,7,10,9\n");
  packetsqueue_add(packets, epoch_now_in_ns-50000, 6, NULL);
  packetsqueue_prepare_discarded(packets);
  packetsqueue_add(packets, epoch_now_in_ns-10000, 3, NULL);
  packetsqueue_add(packets, epoch_now_in_ns-9000, 7, NULL);
  packetsqueue_prepare_gap(packets);
  packetsqueue_add(packets, epoch_now_in_ns-8000, 10, NULL);
  packetsqueue_prepare_discarded(packets);
  packetsqueue_add(packets, epoch_now_in_ns-10000, 9, NULL);
  {
      PacketsQueueNode* node;
      GList *it;
      Gap *gap;
      GapNode *gapnode;
      for(node = packets->head; node; node = node->next)
            g_print("%d->", node->seq_num);
      g_print("\nGaps:\n");
      for(it = packets->gaps; it; it = it->next){
        gap = it->data;
        for(gapnode = gap->at; gapnode; gapnode = gapnode->succ){
          node = gapnode->node;
          g_print("%d->", node->seq_num);
        }
        g_print("|<|>|");
      }
      g_print("\n");
  }
  g_print("COUNTER: %d\n", packets->counter);
  g_print("DELETE 1,5,5\n");
  packetsqueue_head_obsolted(packets, gst_clock_get_time(packets->sysclock));
  packetsqueue_head_obsolted(packets, gst_clock_get_time(packets->sysclock));
  packetsqueue_head_obsolted(packets, gst_clock_get_time(packets->sysclock));
  {
      PacketsQueueNode* node;
      GList *it;
      Gap *gap;
      GapNode *gapnode;
      for(node = packets->head; node; node = node->next)
            g_print("%d->", node->seq_num);
      g_print("\nGaps:\n");
      for(it = packets->gaps; it; it = it->next){
        gap = it->data;
        for(gapnode = gap->at; gapnode; gapnode = gapnode->succ){
          node = gapnode->node;
          g_print("%d->", node->seq_num);
        }
        g_print("|<|>|");
      }
      g_print("\n");
  }
  g_print("DELETE 2,6,3\n");
  packetsqueue_head_obsolted(packets, gst_clock_get_time(packets->sysclock));
  packetsqueue_head_obsolted(packets, gst_clock_get_time(packets->sysclock));
  packetsqueue_head_obsolted(packets, gst_clock_get_time(packets->sysclock));
  {
      PacketsQueueNode* node;
      GList *it;
      Gap *gap;
      GapNode *gapnode;
      for(node = packets->head; node; node = node->next)
            g_print("%d->", node->seq_num);
      g_print("\nGaps:\n");
      for(it = packets->gaps; it; it = it->next){
        gap = it->data;
        for(gapnode = gap->at; gapnode; gapnode = gapnode->succ){
          node = gapnode->node;
          g_print("%d->", node->seq_num);
        }
        g_print("|<|>|");
      }
      g_print("\n");
  }
  g_object_unref(packets);
  packets->counter = 0;

}

void packetsqueue_reset(PacketsQueue *this)
{
  THIS_WRITELOCK(this);
  while(this->head) _remove_head(this, NULL);
  while(this->gaps) this->gaps = g_list_remove(this->gaps, this->gaps->data);
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

void packetsqueue_prepare_discarded(PacketsQueue *this)
{
  THIS_WRITELOCK(this);
  this->discarded_arrive = TRUE;
  THIS_WRITEUNLOCK(this);
}

gboolean packetsqueue_try_found_a_gap(PacketsQueue *this, guint16 seq_num, gboolean *duplicated)
{
  gboolean result;
  THIS_READLOCK(this);
  result = _try_found_a_gap(this, seq_num, duplicated, NULL) != NULL;
  THIS_READUNLOCK(this);
  return result;
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
    _make_gap(this, this->tail, this->tail->seq_num, seq_num);
    this->gap_arrive = FALSE;
  }else if(this->discarded_arrive){
    _try_fill_a_gap(this, node);
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
  _delete_node(this, node);
  --this->counter;
}

void _delete_node(PacketsQueue *this, PacketsQueueNode *node)
{
  if(node->gapnode) _remove_gapnode(this, node->gapnode);
  _trash_node(this, node);
}

void _remove_gapnode(PacketsQueue *this, GapNode *node)
{
  Gap *gap;
  GapNode *succ,*pred;
  //g_print("GAPNODE: %p:%d\n", node, node->node->seq_num);
  succ = node->succ;
  pred = node->pred;
  gap = node->gap;
  if(pred) pred->succ = succ;
  if(succ) succ->pred = pred;
  if(node == gap->at) gap->at = succ;
  _trash_gapnode(this, node);
  if(!gap->at) _remove_gap(this, gap);

}

void _remove_gap(PacketsQueue *this, Gap *gap)
{
  this->gaps = g_list_remove(this->gaps, gap);
  _trash_gap(this, gap);
}

Gap * _make_gap(PacketsQueue *this, PacketsQueueNode* at, guint16 start, guint16 end)
{
  Gap *gap;
  GapNode *pred,*succ;
  if (g_queue_is_empty (this->gaps_pool))
   gap = g_malloc0 (sizeof (Gap));
  else
   gap = (Gap *) g_queue_pop_head (this->gaps_pool);

  pred = _make_gapnode(this, at, gap);
  succ = _make_gapnode(this, at->next, gap);
  gap->at = pred;
  pred->succ = succ;
  succ->pred = pred;
  gap->start = start;
  gap->end = end;
  this->gaps = g_list_prepend (this->gaps, gap);
  at->gapnode = gap->at;
  return gap;
}



Gap* _try_found_a_gap(PacketsQueue *this, guint16 seq_num,
                      gboolean *duplicated, GapNode **insert_after)
{
  GList *it;
  GapNode *gapnode;
  Gap *gap = NULL;
  gint cmp;
  PacketsQueueNode *pos = NULL;

  if(duplicated) *duplicated = FALSE;
  for (it = this->gaps; it; it = it->next, gap = NULL) {
    gap = it->data;
    cmp = _cmp_seq (gap->start, seq_num);
    if (cmp > 0)
      continue;
    if (cmp == 0)
      break;

    cmp = _cmp_seq (seq_num, gap->end);
    if (cmp > 0)
      continue;
    break;
  }

  //g_print("MILYEN GAP EZ? %p: KERESETT: %hu at:%p, start:%hu end:%hu\n", gap, seq_num, gap->at, gap->start, gap->end);
  if(!gap) goto done;

  for(gapnode = gap->at; gapnode ; gapnode = gapnode->succ)
  {
    pos = gapnode->node;
    cmp = _cmp_seq (pos->seq_num, seq_num);
    if(0 <= cmp) break;
  }
  if(gapnode){
    if(insert_after) *insert_after = (!cmp) ? gapnode : gapnode->pred;
//    if(insert_after) g_print("INSERT AFTER: %p:%d\n", *insert_after, (*insert_after)->node->seq_num);
    if(pos->seq_num == seq_num && duplicated) *duplicated = TRUE;
  }

done:
  return gap;
}

gboolean _try_fill_a_gap (PacketsQueue * this, PacketsQueueNode *node)
{
  gboolean result;
  Gap *gap;
  GapNode *pred;
  GapNode *succ;
  GapNode *gapnode;

  result = FALSE;
  gap = _try_found_a_gap(this, node->seq_num, NULL, &pred);

  if (!gap) goto done;

  succ = pred->succ;
  pred->succ = gapnode = _make_gapnode(this, node, gap);
  gapnode->succ = succ;
  if(succ) succ->pred = gapnode;
  gapnode->pred = pred;

  result = TRUE;
done:
  return result;
}

guint64 _get_skew(PacketsQueue *this, PacketsQueueNode* act, PacketsQueueNode* nxt)
{
  guint64 snd_diff, rcv_diff, skew;
  //guint64 received;

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
//  result->rcv_time = epoch_now_in_ns;
  //  result->rcv_time = ((NTP_NOW >> 14) & 0x00ffffff);
  result->rcv_time = NTP_NOW;
  result->seq_num = seq_num;
  result->snd_time = snd_time;
  result->next = NULL;
  result->added = gst_clock_get_time(this->sysclock);
  if(delay) *delay = get_epoch_time_from_ntp_in_ns(result->rcv_time - result->snd_time);
  return result;
}

GapNode * _make_gapnode(PacketsQueue *this, PacketsQueueNode* at, Gap *gap)
{
  GapNode *gapnode;
  if (g_queue_is_empty (this->gapnodes_pool))
    gapnode = g_malloc0 (sizeof (GapNode));
  else
    gapnode = (GapNode *) g_queue_pop_head (this->gapnodes_pool);

  gapnode->node = at;
  gapnode->gap = gap;
  at->gapnode = gapnode;
  return gapnode;
}

void _trash_node(PacketsQueue *this, PacketsQueueNode* node)
{
  if(g_queue_get_length(this->node_pool) > 4096)
    g_free(node);
  else
    g_queue_push_tail(this->node_pool, node);
}

void _trash_gap(PacketsQueue *this, Gap* gap)
{
  if(g_queue_get_length(this->gaps_pool) > 128)
    g_free(gap);
  else
    g_queue_push_tail(this->gaps_pool, gap);
}

void _trash_gapnode(PacketsQueue *this, GapNode* gapnode)
{
  if(g_queue_get_length(this->gapnodes_pool) > 4096)
    g_free(gapnode);
  else
    g_queue_push_tail(this->gapnodes_pool, gapnode);
}

gint
_cmp_seq (guint16 x, guint16 y)
{

  if (x == y) {
    return 0;
  }
  /*
     if(x < y || (0x8000 < x && y < 0x8000)){
     return -1;
     }
     return 1;
   */
  return ((gint16) (x - y)) < 0 ? -1 : 1;

}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
