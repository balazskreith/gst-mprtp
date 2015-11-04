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


//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void packetsqueue_finalize (GObject * object);
static PacketsQueueNode* _make_node(PacketsQueue *this,
                                    guint64 snd_time,
                                    guint16 seq_num,
                                    GstClockTime *delay);
static void _trash_node(PacketsQueue *this, PacketsQueueNode* node);
static guint64 _get_skew(PacketsQueue *this, PacketsQueueNode* act, PacketsQueueNode* nxt);
static guint64 _packetsqueue_add(PacketsQueue *this,
                                 guint64 snd_time,
                                 guint16 seq_num,
                                 GstClockTime *delay);
static gint _cmp_seq_for_tree(guint64 a, guint64 b);
static void _remove_head(PacketsQueue *this, guint64 *skew);

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
  this->sysclock = gst_system_clock_obtain();
  this->discards_tree = make_bintree(_cmp_seq_for_tree);
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

gint _cmp_seq_for_tree(guint64 a, guint64 b)
{
  guint16 x, y;
  x = a; y = b;
  return _cmp_uint16(x,y);
  //g_print("Compare: %hu, %hu, %d\n",x,y,((gint16) (x - y)) < 0 ? -1 : 1);
//  if (x == y) {
//    return 0;
//  }
////  return ((gint16) (x - y)) < 0 ? -1 : 1;
//  return ((x==y)?0:((gint16) (x - y)) < 0 ? -1 : 1);
}

#undef _cmp_uint16
#undef DEBUG_PRINT_TOOLS
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
