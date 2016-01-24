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
#include "bintree.h"

//#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
//#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
//#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
//#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define THIS_READLOCK(this)
#define THIS_READUNLOCK(this)
#define THIS_WRITELOCK(this)
#define THIS_WRITEUNLOCK(this)

#define _cmp_uint16(x,y) ((x==y)?0:((gint16) (x - y)) < 0 ? -1 : 1)

GST_DEBUG_CATEGORY_STATIC (packetsrcvqueue_debug_category);
#define GST_CAT_DEFAULT packetsrcvqueue_debug_category

G_DEFINE_TYPE (PacketsRcvQueue, packetsrcvqueue, G_TYPE_OBJECT);



//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void packetsrcvqueue_finalize (GObject * object);
static PacketsRcvQueueNode* _make_node(PacketsRcvQueue *this,
                                    guint64 snd_time,
                                    guint16 seq_num,
                                    GstClockTime *delay);
#define _trash_node(this, node) pointerpool_add(this->node_pool, node)
static gpointer _node_ctor(void)
{
  return g_malloc0(sizeof(PacketsRcvQueueNode));
}

static guint64 _get_skew(PacketsRcvQueue *this,
                         PacketsRcvQueueNode* act,
                         PacketsRcvQueueNode* nxt);

static guint64 _packetsrcvqueue_add(PacketsRcvQueue *this,
                                 guint64 snd_time,
                                 guint16 seq_num,
                                 GstClockTime *delay);
static void _remove_head(PacketsRcvQueue *this, guint64 *skew);
static guint16 _uint16_diff (guint16 a, guint16 b);
static gint _cmp_for_bintree (guint64 a, guint64 b);

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
  PacketsRcvQueueNode *next;

  this = PACKETSRCVQUEUE(object);
  g_object_unref(this->node_pool);
  while(this->head){
    next = this->head->next;
    _trash_node(this, this->head);
    this->head = next;
  }
  g_object_unref(this->sysclock);

}

static void _node_reset(gpointer inc_data)
{
  PacketsRcvQueueNode *casted_data = inc_data;
  memset(casted_data, 0, sizeof(PacketsRcvQueueNode));
}


void
packetsrcvqueue_init (PacketsRcvQueue * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->jitter = 0;
  this->node_pool = make_pointerpool(64, _node_ctor, g_free, _node_reset);
  this->node_tree = make_bintree(_cmp_for_bintree);
  this->sysclock = gst_system_clock_obtain();
}


void packetsrcvqueue_reset(PacketsRcvQueue *this)
{
  THIS_WRITELOCK(this);
  while(this->head) _remove_head(this, NULL);
  bintree_reset(this->node_tree);
  this->last_seq_init = FALSE;
  THIS_WRITEUNLOCK(this);
}

PacketsRcvQueue *make_packetsrcvqueue(void)
{
  PacketsRcvQueue *result;
  result = g_object_new (PACKETSRCVQUEUE_TYPE, NULL);
  return result;
}

guint64 packetsrcvqueue_add(PacketsRcvQueue *this,
                         guint64 snd_time,
                         guint16 seq_num,
                         GstClockTime *delay)
{
  guint64 result;
  THIS_WRITELOCK(this);
  result = _packetsrcvqueue_add(this, snd_time, seq_num, delay);
  THIS_WRITEUNLOCK(this);
  return result;
}


guint64 _packetsrcvqueue_add(PacketsRcvQueue *this,
                          guint64 snd_time,
                          guint16 seq_num,
                          GstClockTime *delay)
{
//  guint64 skew = 0;
  PacketsRcvQueueNode* node;
  node = _make_node(this, snd_time, seq_num, delay);
  bintree_insert_value(this->node_tree,
                      (((guint64)seq_num)<<48) |
                       (gst_clock_get_time(this->sysclock)>>16));
  if(!this->head) {
      this->head = this->tail = node;
      node->skew  = 0;
      this->counter = 1;
      goto done;
  }
  node->skew = _get_skew(this, this->tail, node);
  this->tail->next = node;
  this->tail = node;
  ++this->counter;
done:
  return node->skew;
}

gboolean packetsrcvqueue_head_obsolted(PacketsRcvQueue *this, GstClockTime treshold)
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



void packetsrcvqueue_get_packets_stat_for_obsolation(PacketsRcvQueue *this,
                                      GstClockTime treshold,
                                      guint16 *lost,
                                      guint16 *received,
                                      guint16 *expected)
{
  guint64 node_value;
  GstClockTime rcvd;
  guint16 act_seq;
  guint16 seq_diff;
  gboolean first_init = FALSE;
  guint16 first = 0, last = 0;
  THIS_READLOCK(this);
again:
  if(!bintree_get_num(this->node_tree)) goto done;
  node_value = bintree_get_bottom_value(this->node_tree);
  act_seq = node_value>>48;
  rcvd = node_value<<16;
  if(treshold < rcvd) goto done;
  if(!this->last_seq_init) goto delete;
  seq_diff = _uint16_diff(this->last_seq, act_seq);
  if(seq_diff > 0) {
      *lost+= seq_diff;
  }
delete:
  if(received){
    ++(*received);
  }
  if(!first_init){
      first_init = TRUE;
      first = act_seq;
  }
  bintree_delete_value(this->node_tree, node_value);
  this->last_seq = last = act_seq;
  this->last_seq_init = TRUE;
  goto again;
done:
  if(expected){
    if(!first_init) *expected = 0;
    else *expected = _uint16_diff(first, last) + 1;
  }
  THIS_READUNLOCK(this);
}

guint32 packetsrcvqueue_get_jitter(PacketsRcvQueue *this)
{
  guint32 result = FALSE;
  THIS_READLOCK(this);
  result = this->jitter;
  THIS_READUNLOCK(this);
  return result;
}

void packetsrcvqueue_remove_head(PacketsRcvQueue *this, guint64 *skew)
{
  THIS_WRITELOCK(this);
  _remove_head(this, skew);
  THIS_WRITEUNLOCK(this);
}

void _remove_head(PacketsRcvQueue *this, guint64 *skew)
{
  PacketsRcvQueueNode *node;
  node = this->head;
  this->head = node->next;
  if(skew) *skew = node->skew;
  _trash_node(this, node);
  --this->counter;
}

guint64 _get_skew(PacketsRcvQueue *this, PacketsRcvQueueNode* act, PacketsRcvQueueNode* nxt)
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

PacketsRcvQueueNode* _make_node(PacketsRcvQueue *this,
                             guint64 snd_time,
                             guint16 seq_num,
                             GstClockTime *delay)
{
  PacketsRcvQueueNode *result;
  result = pointerpool_get(this->node_pool);
  memset((gpointer)result, 0, sizeof(PacketsRcvQueueNode));
  result->rcv_time = NTP_NOW;
  result->seq_num = seq_num;
  result->snd_time = snd_time;
  result->next = NULL;
  result->added = gst_clock_get_time(this->sysclock);
  if(!delay) return result;

  *delay = get_epoch_time_from_ntp_in_ns(result->rcv_time - result->snd_time);
  if(*delay < 60 * GST_SECOND) return result;
  g_print("PROBLEM: S: %lu - R: %lu = %lu:%lu\n",
          result->snd_time,
          result->rcv_time,
          (guint64)(result->rcv_time - result->snd_time),
          *delay);
  return result;
}



guint16
_uint16_diff (guint16 a, guint16 b)
{
  if(a == b) return 0;
  else if (a <= b) return b - a - 1;
  else return ~((guint16) (a - b));
}

gint _cmp_for_bintree (guint64 a, guint64 b)
{
  guint16 x = a>>48;
  guint16 y = b>>48;
  if (y == x) {
    return 0;
  }
  return ((gint16) (x - y)) < 0 ? -1 : 1;
}


#undef _cmp_uint16
#undef DEBUG_PRINT_TOOLS
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
