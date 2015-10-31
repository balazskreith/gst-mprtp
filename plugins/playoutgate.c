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
#include "playoutgate.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)


GST_DEBUG_CATEGORY_STATIC (playoutgate_debug_category);
#define GST_CAT_DEFAULT playoutgate_debug_category

G_DEFINE_TYPE (PlayoutGate, playoutgate, G_TYPE_OBJECT);


struct _FrameNode{
  GstBuffer *buffer;
  guint16    seq;
  FrameNode *next;
};

struct _Frame
{
  Frame          *next;
  FrameNode      *head;
  FrameNode      *tail;
  guint32         timestamp;
  GstClockTime    created;
  gboolean        ready;
  gboolean        sorted;
  gboolean        diversified;
  guint8          source;
};


//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void playoutgate_finalize (GObject * object);
static Frame* _make_frame(PlayoutGate *this, GstRTPBuffer *rtp, guint8 subflow_id);
static FrameNode * _make_framenode(PlayoutGate *this, GstRTPBuffer *rtp);
static gint _cmp_seq (guint16 x, guint16 y);
static void _playoutgate_push(PlayoutGate *this, GstRTPBuffer *rtp, guint8 subflow_id);
static gboolean _playoutgate_has_frame_to_playout(PlayoutGate *this);
//static Frame *_get_the_oldest(PlayoutWindow *this);
static GstBuffer *_playoutgate_pop(PlayoutGate *this);
static void _push_into_frame(PlayoutGate *this, Frame *frame, GstRTPBuffer *rtp, guint8 subflow_id);
static gboolean _check_if_frame_is_sorted(Frame *frame);
static Frame *_try_find(PlayoutGate *this, guint32 timestamp, Frame **predecessor);
static void _trash_frame(PlayoutGate *this, Frame* gap);
static void _trash_framenode(PlayoutGate *this, FrameNode* framenode);
static gboolean _check_if_frame_is_ready(Frame *frame);
//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------
//
//static void _print_frame(Frame *frame)
//{
//  g_print("Frame timestamp: %u, head: %p, tail: %p, seqs:",
//          frame->timestamp, frame->head, frame->tail);
//  {
//    FrameNode *node;
//
//    for(node = frame->head; node; node = node->next){
//      g_print("(%p)%hu",node,node->seq);
//      if(node->next) g_print("->");
//    }
//  }
//  g_print("\n");
//}

void
playoutgate_class_init (PlayoutGateClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = playoutgate_finalize;

  GST_DEBUG_CATEGORY_INIT (playoutgate_debug_category, "playoutgate", 0,
      "MpRTP Manual Sending Controller");

}

void
playoutgate_finalize (GObject * object)
{
  PlayoutGate *this;
  this = PLAYOUTGATE(object);
  while(!g_queue_is_empty(this->frames_pool)){
    g_free(g_queue_pop_head(this->frames_pool));
  }
  while(!g_queue_is_empty(this->framenodes_pool)){
    g_free(g_queue_pop_head(this->framenodes_pool));
  }

  g_object_unref(this->sysclock);

}

void
playoutgate_init (PlayoutGate * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->frames_pool = g_queue_new();
  this->framenodes_pool = g_queue_new();
  this->sysclock = gst_system_clock_obtain();
}

void playoutgate_test(void)
{

}

gboolean playoutgate_is_diversified(PlayoutGate *this)
{
  gboolean result;
  Frame *frame;
  THIS_READLOCK(this);
  for(frame = this->head; frame && !frame->diversified; frame = frame->next);
  result = !frame;
  THIS_READUNLOCK(this);
  return result;
}

void playoutgate_reset(PlayoutGate *this)
{
  GstBuffer *buffer;
  THIS_WRITELOCK(this);
  while((buffer = _playoutgate_pop(this))) gst_buffer_unref(buffer);
  THIS_WRITEUNLOCK(this);
}

PlayoutGate *make_playoutgate(void)
{
  PlayoutGate *result;
  result = g_object_new (PLAYOUTGATE_TYPE, NULL);
  return result;
}

gboolean playoutgate_has_frame_to_playout(PlayoutGate *this)
{
  gboolean result;
  THIS_READLOCK(this);
  result = _playoutgate_has_frame_to_playout(this);
  THIS_READUNLOCK(this);
  return result;
}

void playoutgate_set_window_size(PlayoutGate *this, GstClockTime window_size)
{
  THIS_WRITELOCK(this);
  this->window_size = window_size;
  THIS_WRITEUNLOCK(this);
}

GstBuffer *playoutgate_pop(PlayoutGate *this)
{
  GstBuffer *result;
  THIS_WRITELOCK(this);
  result = _playoutgate_pop(this);
  THIS_WRITEUNLOCK(this);
  return result;
}

void playoutgate_push(PlayoutGate *this, GstRTPBuffer *rtp, guint8 subflow_id)
{
  THIS_WRITELOCK(this);
  _playoutgate_push(this, rtp, subflow_id);
  THIS_WRITEUNLOCK(this);
}

gboolean _playoutgate_has_frame_to_playout(PlayoutGate *this)
{
  Frame *frame;
  GstClockTime treshold;
  treshold = gst_clock_get_time(this->sysclock) - this->window_size;
  frame = this->head;
again:
  if(!frame) goto no;
  if(!frame->sorted && frame->ready) goto yes;
  if(frame->created < treshold) goto yes;
  frame = frame->next;
  goto again;
yes:
  return TRUE;
no:
  return FALSE;
}

//
//Frame *_get_the_oldest(PlayoutWindow *this)
//{
//  Frame *node, *oldest;
//  oldest = this->head;
//  for(node = this->head; node; node = node->next){
//    if(node->created < oldest->created) oldest = node;
//  }
//  return oldest;
//}

GstBuffer *_playoutgate_pop(PlayoutGate *this)
{
  GstBuffer *result = NULL;
  Frame *frame;
  FrameNode *node;
  if(!this->head) goto done;
  frame = this->head;
  node = frame->head;
  result = node->buffer;
  _trash_framenode(this, node);
  frame->head = node->next;
  if(frame->head) goto done;
  this->head = frame->next;
  _trash_frame(this, frame);
done:
  return result;
}
//
//void _playoutgate_push(PlayoutWindow *this,
//                         GstRTPBuffer *rtp)
//{
//  guint32 timestamp;
//  Frame *frame,*prev = NULL;
//  if(!this->head) {
//    this->head = this->tail = frame = _make_frame(this, rtp);
//    goto done;
//  }
//  timestamp = gst_rtp_buffer_get_timestamp(rtp);
//  frame = this->head;
//  if(timestamp < frame->timestamp){
//    this->head = _make_frame(this, rtp);
//    this->head->next = frame;
//    goto done;
//  }
//  frame = _try_find(this, timestamp, &prev);
//  if(!frame && !prev){
//    frame = this->tail;
//    this->tail->next = _make_frame(this, rtp);
//    goto done;
//  }
//  if(!frame){
//    Frame *next;
//    next = prev->next;
//    frame = _make_frame(this, rtp);
//    prev->next = frame;
//    frame->next = next;
//    goto done;
//  }
//  _push_into_frame(this, frame, rtp);
//done:
////  _print_frame(frame);
//  return;
//}



void _playoutgate_push(PlayoutGate *this,
                         GstRTPBuffer *rtp,
                         guint8 subflow_id)
{
  Frame *frame,*succ,*prev = NULL;
  guint32 timestamp;
  if(!this->head) {
    frame = this->head = this->tail = _make_frame(this, rtp, subflow_id);
    goto done;
  }
  timestamp = gst_rtp_buffer_get_timestamp(rtp);
  if(timestamp < this->head->timestamp){
    (frame = _make_frame(this, rtp, subflow_id))->next = this->head;
    this->head = frame;
    goto done;
  }
  frame = _try_find(this, timestamp, &prev);
  if(!frame && !prev){
    this->tail->next = frame = _make_frame(this, rtp, subflow_id);
    this->tail = frame;
    goto done;
  }
  if(!frame){
    succ = prev->next;
    prev->next = frame = _make_frame(this, rtp, subflow_id);;
    frame->next = succ;
    goto done;
  }
  _push_into_frame(this, frame, rtp, subflow_id);
done:
  frame->ready = _check_if_frame_is_ready(frame);
//  _print_frame(frame);
  return;
}

gboolean _check_if_frame_is_ready(Frame *frame)
{
  Frame *succ;
  FrameNode *succ_head, *this_tail;
  if(!frame || !frame->next) goto no;
  succ = frame->next;
  this_tail = frame->tail;
  succ_head = succ->head;
  if((guint16)(this_tail->seq + 1) == succ_head->seq) goto yes;
  else goto no;
yes:
  return TRUE;
no:
  return FALSE;
}

void _push_into_frame(PlayoutGate *this, Frame *frame, GstRTPBuffer *rtp, guint8 subflow_id)
{
  FrameNode *prev,*node, *succ;
  node = _make_framenode(this, rtp);
  if(!frame->head){
   frame->head = frame->tail = node;
   goto done;
  }
  if(!frame->head->next){
    if(_cmp_seq(node->seq, frame->head->seq) <= 0){
      frame->tail = node->next = frame->head;
      frame->head = node;
    }else{
      frame->tail = frame->head->next = node;
    }
    goto done;
  }
  if(_cmp_seq(node->seq, frame->head->seq) <= 0){
    node->next = frame->head;
    frame->head = node;
    goto done;
  }
  prev = frame->head;
again:
  succ = prev->next;
  if(!succ){
    prev->next = frame->tail = node;
    node->next = NULL;
    goto done;
  }
  if(_cmp_seq(node->seq, succ->seq) <= 0){
    prev->next = node;
    node->next = succ;
    goto done;
  }
  prev = succ;
  goto again;

done:
  frame->sorted = _check_if_frame_is_sorted(frame);
  frame->diversified = frame->diversified || frame->source != subflow_id;
  return;
}
//
gboolean _check_if_frame_is_sorted(Frame *frame)
{
  FrameNode *node, *next;
  node = frame->head;
  next = node->next;
again:
  if(!node || !next) goto yes;
  if((guint16)(node->seq + 1) != next->seq) goto no;
  node = next;
  next = node->next;
  goto again;
yes:
  return TRUE;
no:
  return FALSE;

}

Frame *_try_find(PlayoutGate *this, guint32 timestamp, Frame **predecessor)
{
  Frame *act, *prev;
  prev = NULL;
  act = this->head;
again:
  if(!act) goto not_found;
  if(act->timestamp == timestamp) goto found;
  if(timestamp < act->timestamp) goto prev_found;
  prev = act;
  act = act->next;
  goto again;

prev_found:
    if(predecessor) *predecessor = prev;
not_found:
  return NULL;
found:
  return act;

}

Frame* _make_frame(PlayoutGate *this, GstRTPBuffer *rtp, guint8 source)
{
  Frame *result;
  if(!g_queue_is_empty(this->frames_pool))
    result = g_queue_pop_head(this->frames_pool);
  else
    result = g_malloc0(sizeof(Frame));
  memset((gpointer)result, 0, sizeof(Frame));

  result->head = result->tail = _make_framenode(this, rtp);
  result->timestamp = gst_rtp_buffer_get_timestamp(rtp);
  result->sorted = TRUE;
  result->created = gst_clock_get_time(this->sysclock);
  result->ready = FALSE;
  result->diversified = FALSE;
  result->source = source;
  return result;
}

FrameNode * _make_framenode(PlayoutGate *this, GstRTPBuffer *rtp)
{
  FrameNode *result;
  if (g_queue_is_empty (this->framenodes_pool))
    result = g_malloc0 (sizeof (FrameNode));
  else
    result = (FrameNode *) g_queue_pop_head (this->framenodes_pool);
  memset((gpointer)result, 0, sizeof(FrameNode));

  result->buffer = gst_buffer_ref(rtp->buffer);
  result->next = NULL;
  result->seq = gst_rtp_buffer_get_seq(rtp);
  return result;
}


void _trash_frame(PlayoutGate *this, Frame* gap)
{
  if(g_queue_get_length(this->frames_pool) > 128)
    g_free(gap);
  else
    g_queue_push_tail(this->frames_pool, gap);
}

void _trash_framenode(PlayoutGate *this, FrameNode* framenode)
{
  if(g_queue_get_length(this->framenodes_pool) > 4096)
    g_free(framenode);
  else
    g_queue_push_tail(this->framenodes_pool, framenode);
}

gint
_cmp_seq (guint16 x, guint16 y)
{

  if (x == y) {
    return 0;
  }
  return ((gint16) (x - y)) < 0 ? -1 : 1;

}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
