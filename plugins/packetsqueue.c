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
  PacketsQueueNode *at;
  guint16 start;
  guint16 end;
  guint16 total;
  guint16 filled;
};

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void packetsqueue_finalize (GObject * object);
static PacketsQueueNode* _make_node(PacketsQueue *this, guint64 snd_time, guint16 seq_num);
static void _trash_node(PacketsQueue *this, PacketsQueueNode* node);
static guint64 _get_skew(PacketsQueue *this, PacketsQueueNode* act, PacketsQueueNode* nxt);
static void _make_gap(PacketsQueue *this, PacketsQueueNode* at, guint16 start, guint16 end);
static guint64 _packetsqueue_add(PacketsQueue *this, guint64 snd_time, guint16 seq_num);
static gboolean _try_fill_a_gap (PacketsQueue * this, PacketsQueueNode *node);
static Gap* _try_found_a_gap(PacketsQueue *this, guint16 seq_num);
static gint _cmp_seq (guint16 x, guint16 y);

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
  this->sysclock = gst_system_clock_obtain();
}

void packetsqueue_test(void)
{
  PacketsQueue *packets;
  guint64 skew;
  g_print("ADD PACKETS");
  packets = make_packetsqueue();
  packetsqueue_add(packets, epoch_now_in_ns-100, 1);
  skew = packetsqueue_add(packets, epoch_now_in_ns-90, 2);
  g_print("SKEW: %lu\n", skew);
  packetsqueue_prepare_gap(packets);
  skew = packetsqueue_add(packets, epoch_now_in_ns-70, 4);
  g_print("SKEW: %lu\n", skew);
  g_print("FOUND IN GAP: %d\n", packetsqueue_try_found_a_gap(packets, 3));
  packetsqueue_prepare_discarded(packets);
  packetsqueue_add(packets, epoch_now_in_ns - 40, 3);
  {
    PacketsQueueNode* node;
    for(node = packets->head; node; node = node->next)
          g_print("%d->", node->seq_num);
    g_print("\n");
    for(node = packets->head; node; node = node->succ)
          g_print("%d->", node->seq_num);
  }
  packets = NULL;
  packets->counter = 0;

}

PacketsQueue *make_packetsqueue(void)
{
  PacketsQueue *result;
  result = g_object_new (PACKETSQUEUE_TYPE, NULL);
  return result;
}

guint64 packetsqueue_add(PacketsQueue *this, guint64 snd_time, guint16 seq_num)
{
  guint64 result;
  THIS_WRITELOCK(this);
  result = _packetsqueue_add(this, snd_time, seq_num);
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

gboolean packetsqueue_try_found_a_gap(PacketsQueue *this, guint16 seq_num)
{
  gboolean result;
  THIS_READLOCK(this);
  result = _try_found_a_gap(this, seq_num) != NULL;
  THIS_READUNLOCK(this);
  return result;
}

guint64 _packetsqueue_add(PacketsQueue *this, guint64 snd_time, guint16 seq_num)
{
  guint64 skew = 0;
  PacketsQueueNode* node;
  node = _make_node(this, snd_time, seq_num);
  if(!this->head) {
      this->head = this->tail = node;
      skew = 0;
      this->counter = 1;
      goto done;
  }
  node->skew = _get_skew(this, this->tail, node);
  node->added = gst_clock_get_time(this->sysclock);
  if(this->gap_arrive){
    _make_gap(this, this->tail, this->tail->seq_num, seq_num);
    this->gap_arrive = FALSE;
  }
  this->tail->next = node;
  if(this->discarded_arrive){
    _try_fill_a_gap(this, node);
  }
  else this->tail->succ = node;
  this->tail = node;
  ++this->counter;
done:
  return skew;
}

gboolean packetsqueue_head_obsolted(PacketsQueue *this, GstClockTime treshold, guint *skew)
{
  gboolean result = FALSE;
  PacketsQueueNode* node;
  THIS_WRITELOCK(this);
  if(!this->head) goto done;
  node = this->head;
  if(treshold < node->added) goto done;
  result = TRUE;
  this->head = node->next;
  if(skew) *skew = node->skew;
  _trash_node(this, node);
  --this->counter;
done:
  THIS_WRITEUNLOCK(this);
  return result;
}

void _make_gap(PacketsQueue *this, PacketsQueueNode* at, guint16 start, guint16 end)
{
  Gap *gap;
  guint16 counter;
  if (g_queue_is_empty (this->gaps_pool))
   gap = g_malloc0 (sizeof (Gap));
  else
   gap = (Gap *) g_queue_pop_head (this->gaps_pool);

  gap->at = at;
  gap->start = start;
  gap->end = end;
  gap->filled = 0;
  gap->total = 1;
  for (counter = gap->start + 1;
     counter != (guint16) (gap->end - 1); ++counter, ++gap->total);
  this->gaps = g_list_prepend (this->gaps, gap);
  at->gap = gap;
}



gboolean _try_fill_a_gap (PacketsQueue * this, PacketsQueueNode *node)
{
  gboolean result;
  Gap *gap;
  gint cmp;
  PacketsQueueNode *pred;
  PacketsQueueNode *succ;

  result = FALSE;
  gap = _try_found_a_gap(this, node->seq_num);

  if (!gap)
    goto done;

  for (pred = gap->at; pred->seq_num != gap->end; pred = pred->succ) {
    succ = pred->next;
    cmp = _cmp_seq (node->seq_num, succ->seq_num);
    if (cmp > 0)
      break;
    succ = NULL;
  }
g_print("ITT: %p:%d->%p:%d\n", pred, pred?pred->seq_num:0, succ, succ?succ->seq_num:0);
  if (!succ)
    goto done;
  pred->succ = node;
  node->succ = succ;
  result = TRUE;
done:
  return result;
}

Gap* _try_found_a_gap(PacketsQueue *this, guint16 seq_num)
{
  GList *it;
  Gap *gap;
  gint cmp;

  for (it = this->gaps; it; it = it->next, gap = NULL) {
    gap = it->data;
    cmp = _cmp_seq (gap->start, seq_num);
    if (cmp > 0)
      continue;
    if (cmp == 0)
      goto done;

    cmp = _cmp_seq (seq_num, gap->end);
    if (cmp > 0)
      continue;
    if (cmp == 0)
      goto done;
    break;
  }

done:
  return gap;
}

guint64 _get_skew(PacketsQueue *this, PacketsQueueNode* act, PacketsQueueNode* nxt)
{
  guint64 snd_diff, rcv_diff, skew;
  //guint64 received;

  rcv_diff = nxt->rcv_time - act->rcv_time;
  skew = 0;
  if (rcv_diff > 0x8000000000000000) {
    GST_WARNING_OBJECT (this, "The skew between two packets NOT real: "
            "prev snd: %lu act snd: %lu",act->snd_time, nxt->snd_time);
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
  //nxt->skew = g_random_int_range(25000, 50000);
done:
  return skew;
}

PacketsQueueNode* _make_node(PacketsQueue *this, guint64 snd_time, guint16 seq_num)
{
  PacketsQueueNode *result;
  if(!g_queue_is_empty(this->node_pool))
    result = g_queue_pop_head(this->node_pool);
  else
    result = g_malloc0(sizeof(PacketsQueueNode));
  memset((gpointer)result, 0, sizeof(PacketsQueueNode));
  result->rcv_time = epoch_now_in_ns;
  result->seq_num = seq_num;
  result->snd_time = snd_time;
  return result;
}

void _trash_node(PacketsQueue *this, PacketsQueueNode* node)
{
  if(g_queue_get_length(this->node_pool) > 4096)
    g_free(node);
  else
    g_queue_push_tail(this->node_pool, node);
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
