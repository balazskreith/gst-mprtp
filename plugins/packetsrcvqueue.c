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


typedef struct _FrameNode{
  GstMpRTPBuffer* mprtp;
  guint16         seq;
  gboolean        marker;
  FrameNode*      next;
}FrameNode;

typedef struct _Frame
{
  guint32         timestamp;
  gboolean        ready;
  gboolean        marked;
  gboolean        intact;
  guint32         last_seq;
  GstClockTime    added;
  GList*          nodes;
}Frame;

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


static gint _find_frame_helper(gconstpointer ptr2frame, gconstpointer ptr2searched_ts)
{
  const guint32 *timestamp;
  const Frame* frame;
  timestamp = ptr2searched_ts;
  frame = ptr2frame;
  return frame->timestamp == *timestamp ? 0 : -1;
}

static int
_compare_nodes (gconstpointer a, gconstpointer b)
{
  const FrameNode *ai = a;
  const FrameNode *bi = b;

  if(_cmp_uint16(ai->seq, bi->seq) > 0)
    return 1;
  else if (ai->seq == bi->seq)
    return 0;
  else
    return -1;
}

static int
_compare_frames (gconstpointer a, gconstpointer b, gpointer data)
{
  const Frame *ai = a;
  const Frame *bi = b;

  if(_cmp_uint32(ai->timestamp, bi->timestamp) > 0)
    return 1;
  else if (ai->timestamp == bi->timestamp)
    return 0;
  else
    return -1;
}

GST_DEBUG_CATEGORY_STATIC (packetsrcvqueue_debug_category);
#define GST_CAT_DEFAULT packetsrcvqueue_debug_category

G_DEFINE_TYPE (PacketsRcvQueue, packetsrcvqueue, G_TYPE_OBJECT);


//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void packetsrcvqueue_finalize (GObject * object);
static Frame* _make_frame(PacketsRcvQueue *this, GstMpRTPBuffer *mprtp);
static FrameNode* _make_framenode(PacketsRcvQueue *this, GstMpRTPBuffer *mprtp);
static void _logging(gpointer data);
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
  g_object_unref(this->frames);
  g_object_unref(this->urgent);
  g_object_unref(this->normal);
}

void
packetsrcvqueue_init (PacketsRcvQueue * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sysclock = gst_system_clock_obtain();
  this->frames = g_queue_new();
  this->urgent = g_queue_new();
  this->normal = g_queue_new();
  mprtp_logger_add_logging_fnc(_logging,this, 10, &this->rwmutex);
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
packetsrcvqueue_set_initial_delay(PacketsRcvQueue *this, GstClockTime init_delay)
{
  THIS_WRITELOCK (this);
//  this->init_delay = init_delay;
//  this->init_delay_applied = FALSE;
  THIS_WRITEUNLOCK (this);
}



void packetsrcvqueue_push(PacketsRcvQueue *this, GstMpRTPBuffer *mprtp)
{
  Frame *tail, *head, *frame;
  GList *it;
  THIS_WRITELOCK(this);
  head = g_queue_peek_head(this->frames);
  if(_cmp_uint16(mprtp->timestamp, head->timestamp) < 0){
    g_queue_push_tail(this->urgent, mprtp);
    goto done;
  }
  tail = g_queue_peek_tail(this->frames);
  if(_cmp_uint16(tail->timestamp, mprtp->timestamp) < 0){
    g_queue_push_tail(this->frames, _make_frame(this, mprtp));
    goto done;
  }
  it = g_queue_find_custom(this->frames, &mprtp->timestamp, _find_frame_helper);
  if(it){
    frame = it->data;
    frame->nodes = g_list_insert_sorted(frame->nodes, _make_framenode(this, mprtp), _compare_nodes);
    goto done;
  }
  frame = _make_frame(this, mprtp);
  g_queue_insert_sorted(this->frames, frame, _compare_frames, NULL);
done:
  THIS_WRITEUNLOCK(this);
}

void packetsrcvqueue_refresh(PacketsRcvQueue *this)
{
  GList *it = NULL;
  Frame *head;
  FrameNode* node;
  THIS_WRITELOCK(this);
again:
  if(g_queue_is_empty(this->frames)){
    goto done;
  }
  head = g_queue_peek_head(this->frames);

  //Todo: check playout time
  head = g_queue_pop_head(this->frames);
  for(it = head->nodes; it; it = it->next){
    node = it->data;
    g_queue_push_tail(this->normal, node->mprtp);
    g_slice_free(FrameNode, node);
  }
  g_slice_free(Frame, head);
  goto again;
done:
  THIS_WRITEUNLOCK(this);
}

GstMpRTPBuffer* packetsrcvqueue_pop_normal(PacketsRcvQueue *this)
{
  GstMpRTPBuffer *result = NULL;
  THIS_WRITELOCK(this);
  if(!this->playout_allowed || g_queue_is_empty(this->normal)){
    goto done;
  }
  result = g_queue_pop_head(this->normal);
done:
  THIS_WRITEUNLOCK(this);
  return result;
}

GstMpRTPBuffer* packetsrcvqueue_pop_urgent(PacketsRcvQueue *this)
{
  GstMpRTPBuffer *result = NULL;
  THIS_WRITELOCK(this);
  if(!this->playout_allowed || g_queue_is_empty(this->urgent)){
    goto done;
  }
  result = g_queue_pop_head(this->urgent);
done:
  THIS_WRITEUNLOCK(this);
  return result;
}

Frame* _make_frame(PacketsRcvQueue *this, GstMpRTPBuffer *mprtp)
{
  Frame *result;
  result = g_slice_new(Frame);
  result->added = _now(this);
  result->timestamp = mprtp->timestamp;
  result->nodes = g_list_prepend(result->nodes, _make_framenode(this, mprtp));

  return result;
}

FrameNode* _make_framenode(PacketsRcvQueue *this, GstMpRTPBuffer *mprtp)
{
  FrameNode *result;
  result = g_slice_new(FrameNode);
  result->seq = mprtp->abs_seq;
  result->mprtp = mprtp;
  return result;
}

void _logging(gpointer data)
{
  PacketsRcvQueue *this = data;
  mprtp_logger("packetsrcvqueue.log",
               "----------------------------------------------------\n"
               "Seconds: %lu\n",

               GST_TIME_AS_SECONDS(_now(this) - this->made)

               );

}


#undef DEBUG_PRINT_TOOLS
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
