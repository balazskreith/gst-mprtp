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
#include "streamjoiner.h"
#include "gstmprtpbuffer.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>


//Copied from gst-plugins-base/gst-libs//gst/video/video-format.c
#ifndef restrict
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
/* restrict should be available */
#elif defined(__GNUC__) && __GNUC__ >= 4
#define restrict __restrict__
#elif defined(_MSC_VER) &&  _MSC_VER >= 1500
#define restrict __restrict
#else
#define restrict                /* no op */
#endif
#endif


GST_DEBUG_CATEGORY_STATIC (stream_joiner_debug_category);
#define GST_CAT_DEFAULT stream_joiner_debug_category

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)


G_DEFINE_TYPE (StreamJoiner, stream_joiner, G_TYPE_OBJECT);

typedef struct _Subflow Subflow;

struct _Subflow
{
  guint8 id;
  MpRTPRPath  *path;
  gdouble      delay;
  gdouble      skew;
  guint32      jitter;

  guint32      monitored_bytes;
};


struct _FrameNode{
  GstMpRTPBuffer* mprtp;
  guint16         seq;
  FrameNode*      next;
};

struct _Frame
{
  Frame          *next;
  FrameNode      *head;
  FrameNode      *tail;
  guint32         timestamp;
  GstClockTime    playout_time;
  GstClockTime    created;
};

static gpointer _frame_ctor(void)
{
  return g_malloc0(sizeof(Frame));
}

static gpointer _framenode_ctor(void)
{
  return g_malloc0(sizeof(FrameNode));
}


//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
stream_joiner_finalize (
    GObject * object);

static void
stream_joiner_run (
    void *data);

static Subflow *
_make_subflow (
    MpRTPRPath * path);

static void
_ruin_subflow (
    gpointer data);

static gint
_cmp_seq (
    guint16 x,
    guint16 y);

static Frame*
_make_frame(
    StreamJoiner *this,
    GstMpRTPBuffer *mprtp);

static FrameNode *
_make_framenode(
    StreamJoiner *this,
    GstMpRTPBuffer *mprtp);

static void
_add_mprtp_packet(
    StreamJoiner *this,
    GstMpRTPBuffer *mprtp);

static void
_push_into_frame(
    StreamJoiner *this,
    Frame *frame,
    GstMpRTPBuffer *rtp);

static Frame *_try_find(
    StreamJoiner *this,
    guint32 timestamp,
    Frame **predecessor);

#define _trash_frame(this, frame)  \
  pointerpool_add(this->frames_pool, frame); \
  --this->framecounter;

#define _trash_framenode(this, frame) pointerpool_add(this->framenodes_pool, node)


//#define DEBUG_PRINT_TOOLS
#ifdef DEBUG_PRINT_TOOLS
static void _print_frame(Frame *frame)
{
  FrameNode *node;
  g_print("Frame %p created: %lu, srt: %d, rd: %d, m: %d src: %d, h: %p, t: %p\n",
          frame, frame->created, frame->sorted, frame->ready, frame->marked,
          frame->source, frame->head, frame->tail);
  g_print("Items: ")
  for(node = frame->head; node; node = node->next)
    g_print("%p(%hu)->%p|", node, node->seq, node->next);
  g_print("\n");
}
#else
#define _print_frame(frame)
#endif

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
stream_joiner_class_init (StreamJoinerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = stream_joiner_finalize;

  GST_DEBUG_CATEGORY_INIT (stream_joiner_debug_category, "stream_joiner", 0,
      "MpRTP Manual Sending Controller");
}

void
stream_joiner_finalize (GObject * object)
{
  StreamJoiner *this = STREAM_JOINER (object);
  g_hash_table_destroy (this->subflows);
  gst_task_stop (this->thread);
  gst_task_join (this->thread);

  g_object_unref (this->sysclock);
  g_object_unref (this->frames_pool);
  g_object_unref (this->framenodes_pool);
//  g_object_unref(this->playoutgate);
}

static void _frame_reset(gpointer inc_data)
{
  Frame *casted_data = inc_data;
  memset(casted_data, 0, sizeof(Frame));
}

static void _framenode_reset(gpointer inc_data)
{
  FrameNode *casted_data = inc_data;
  memset(casted_data, 0, sizeof(FrameNode));
}

void
stream_joiner_init (StreamJoiner * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->subflows = g_hash_table_new_full (NULL, NULL, NULL, _ruin_subflow);
//  this->max_path_skew = 10 * GST_MSECOND;
  this->frames_pool = make_pointerpool(1024, _frame_ctor, g_free, _frame_reset);
  this->framenodes_pool = make_pointerpool(1024, _framenode_ctor, g_free, _framenode_reset);
  this->PHSN = 0;
  this->playout_allowed = TRUE;
  this->playout_halt = FALSE;
  this->playout_halt_time = 100 * GST_MSECOND;
  this->tick_interval = 20 * GST_USECOND;
  this->latency_window = make_percentiletracker(256, 90);
  percentiletracker_set_treshold(this->latency_window, 10 * GST_SECOND);
  g_rw_lock_init (&this->rwmutex);
  g_rec_mutex_init (&this->thread_mutex);
  this->thread = gst_task_new (stream_joiner_run, this, NULL);
  gst_task_set_lock (this->thread, &this->thread_mutex);
  gst_task_start (this->thread);
  this->ticks = make_percentiletracker(1024, 50);
  percentiletracker_add(this->ticks, 2 * GST_MSECOND);
}

StreamJoiner*
make_stream_joiner(gpointer data, void (*func)(gpointer,GstMpRTPBuffer*))
{
  StreamJoiner *result;
  result = (StreamJoiner *) g_object_new (STREAM_JOINER_TYPE, NULL);
  result->send_mprtp_packet_data = data;
  result->send_mprtp_packet_func = func;
//  result->playoutgate = make_playoutgate(data, func);
  return result;
}

void
stream_joiner_run (void *data)
{
  GstClockTime now, next_scheduler_time;
  StreamJoiner *this = STREAM_JOINER (data);
  GstClockID clock_id;
  Frame *frame;
  FrameNode *node;

  THIS_WRITELOCK (this);
  now = gst_clock_get_time (this->sysclock);
  if (this->subflow_num == 0) {
    next_scheduler_time = now + GST_MSECOND;
    goto done;
  }

  if(!this->playout_allowed){
    next_scheduler_time = now + GST_MSECOND;
    goto done;
  }
  if(this->playout_halt){
    next_scheduler_time = now + this->playout_halt_time;
    this->playout_halt = FALSE;
    goto done;
  }
pop_frame:
  frame = this->head;
  if(!frame || now < frame->playout_time) goto next_tick;
  if(this->last_snd_ntp_reference){
    FrameNode* node;
    node = frame->head;
    if(this->last_snd_ntp_reference < node->mprtp->abs_snd_ntp_time){
      GstClockTime interval = get_epoch_time_from_ntp_in_ns(node->mprtp->abs_snd_ntp_time - this->last_snd_ntp_reference);
//      this->estimated_tick = skalmanfilter_measurement_update(this->tick_estimator, interval);
      percentiletracker_add(this->ticks, interval);
//      g_print("Tick: %f\n", this->estimated_tick);
    }
    this->last_snd_ntp_reference = node->mprtp->abs_snd_ntp_time;
  }else{
    FrameNode* node;
    node = frame->head;
    this->last_snd_ntp_reference = node->mprtp->abs_snd_ntp_time;
  }
pop_node:
  node = frame->head;
  this->PHSN = node->seq;
  this->send_mprtp_packet_func(this->send_mprtp_packet_data, node->mprtp);
  frame->head = node->next;
  _trash_framenode(this, node);
  if(frame->head) goto pop_node;
  this->head = frame->next;
  _trash_frame(this, frame);
  goto pop_frame;
next_tick:
  next_scheduler_time = now + GST_MSECOND;
//  next_scheduler_time = now + (guint64)this->estimated_tick;
//  g_print("%lu-%lu\n", now + GST_MSECOND, now + (guint64)this->estimated_tick);
done:
//  g_print("Frame in: %d\n", this->framecounter);
  {
    GstClock *clock = this->sysclock;
    THIS_WRITEUNLOCK (this);
    clock_id = gst_clock_new_single_shot_id (clock, next_scheduler_time);
  }
  if (gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED) {
    GST_WARNING_OBJECT (this, "The playout clock wait is interrupted");
  }
  gst_clock_id_unref (clock_id);
}


void stream_joiner_receive_mprtp(StreamJoiner * this, GstMpRTPBuffer *mprtp)
{
  guint16 abs_seq;
  Subflow *subflow;
  guint64 latency;
  THIS_WRITELOCK(this);
  if(!GST_IS_BUFFER(mprtp->buffer)){
    this->send_mprtp_packet_func(this->send_mprtp_packet_data, mprtp);
    goto done;
  }
//  g_print("Receive a packet with timestamp: %u\n", gst_mprtp_ptr_buffer_get_timestamp(mprtp));
  abs_seq = gst_mprtp_buffer_get_abs_seq(mprtp);
  subflow = (Subflow *) g_hash_table_lookup (this->subflows, GINT_TO_POINTER (mprtp->subflow_id));
//  g_print("%d|", this->monitor_payload_type);
  if(gst_mprtp_buffer_get_payload_type(mprtp) == this->monitor_payload_type){
    subflow->monitored_bytes += mprtp->payload_bytes;
    //now drop it;
    this->send_mprtp_packet_func(this->send_mprtp_packet_data, mprtp);
    goto done;
  }

  mprtpr_path_get_joiner_stats(subflow->path,
                              &subflow->delay,
                              &subflow->skew,
                              &subflow->jitter);

//  g_print("S%d: %f->%lu\n", subflow->id, subflow->skew, (guint64)subflow->skew);
  {
    guint64 jitter,delay,skew;
    delay = MIN(400 * GST_MSECOND, subflow->delay);
    jitter = MIN(delay * .1, subflow->jitter);
    skew = MIN(jitter * .5, subflow->skew);
    latency = (guint64)(delay + skew + (gdouble)(jitter<<2));
  }

  if(latency < 300 * GST_MSECOND) percentiletracker_add(this->latency_window, latency);

  latency = percentiletracker_get_stats(this->latency_window, NULL, NULL, NULL);
  this->latency = (31. * this->latency + (gdouble)latency) / 32.;
  if(this->ssrc != 0 && this->ssrc != gst_mprtp_buffer_get_ssrc(mprtp)){
//    g_print("this->ssrc: %u, gst ssrc: %u\n",
//            this->ssrc, gst_mprtp_ptr_buffer_get_ssrc(mprtp));
    this->send_mprtp_packet_func(this->send_mprtp_packet_data, mprtp);
    goto done;
  }
//g_print("%f+%f+%u=%lu\n", subflow->delay, subflow->skew, subflow->jitter, latency);
  if(_cmp_seq(this->PHSN, abs_seq) < 0){
    this->ssrc = gst_mprtp_buffer_get_ssrc(mprtp);
    _add_mprtp_packet(this, mprtp);
    goto done;
  }

//  g_print("Discard happened on subflow %d with seq %hu, PHSN: %hu\n",
//          subflow->id, abs_seq, this->PHSN);
  mprtpr_path_add_discard(subflow->path, mprtp);
  this->send_mprtp_packet_func(this->send_mprtp_packet_data, mprtp);
done:
  THIS_WRITEUNLOCK(this);
}

void stream_joiner_set_monitor_payload_type(StreamJoiner *this, guint8 monitor_payload_type)
{
  THIS_WRITELOCK(this);
  this->monitor_payload_type = monitor_payload_type;
  THIS_WRITEUNLOCK(this);
}

void
stream_joiner_add_path (StreamJoiner * this, guint8 subflow_id,
    MpRTPRPath * path)
{
  Subflow *lookup_result;
  THIS_WRITELOCK (this);
  lookup_result =
      (Subflow *) g_hash_table_lookup (this->subflows,
      GINT_TO_POINTER (subflow_id));
  if (lookup_result != NULL) {
    GST_WARNING_OBJECT (this, "The requested add operation can not be done "
        "due to duplicated subflow id (%d)", subflow_id);
    goto exit;
  }
  g_hash_table_insert (this->subflows, GINT_TO_POINTER (subflow_id),
      _make_subflow (path));
  ++this->subflow_num;
exit:
  THIS_WRITEUNLOCK (this);
}

void
stream_joiner_rem_path (StreamJoiner * this, guint8 subflow_id)
{
  Subflow *lookup_result;
  THIS_WRITELOCK (this);
  lookup_result =
      (Subflow *) g_hash_table_lookup (this->subflows,
      GINT_TO_POINTER (subflow_id));
  if (lookup_result == NULL) {
    GST_WARNING_OBJECT (this, "The requested remove operation can not be done "
        "due to not existed subflow id (%d)", subflow_id);
    goto exit;
  }
  g_hash_table_remove (this->subflows, GINT_TO_POINTER (subflow_id));

  if (--this->subflow_num < 0) {
    this->subflow_num = 0;
  }
exit:
  THIS_WRITEUNLOCK (this);
}


void
stream_joiner_set_playout_allowed(StreamJoiner *this, gboolean playout_permission)
{
  THIS_WRITELOCK (this);
  this->playout_allowed = playout_permission;
  THIS_WRITEUNLOCK (this);
}

void
stream_joiner_set_tick_interval(StreamJoiner *this, GstClockTime tick_interval)
{
  THIS_WRITELOCK (this);
  this->tick_interval = tick_interval;
  THIS_WRITEUNLOCK (this);
}

void
stream_joiner_set_stream_delay(StreamJoiner *this, GstClockTime stream_delay)
{
  THIS_WRITELOCK (this);
  this->stream_delay = stream_delay;
  THIS_WRITEUNLOCK (this);
}

guint32
stream_joiner_get_monitored_bytes(StreamJoiner *this, guint8 subflow_id)
{
  guint32 result;
  Subflow *subflow;
  THIS_READLOCK(this);
  subflow = (Subflow *) g_hash_table_lookup (this->subflows, GINT_TO_POINTER (subflow_id));
  result = subflow->monitored_bytes;
  THIS_READUNLOCK(this);
  return result;
}

void
stream_joiner_get_stats(StreamJoiner *this,
                        gdouble *latency)
{
  THIS_READLOCK(this);
  if(latency) *latency = this->latency;

  THIS_READUNLOCK(this);
}

void
stream_joiner_set_playout_halt_time(StreamJoiner *this, GstClockTime halt_time)
{
  THIS_WRITELOCK (this);
  this->playout_halt = TRUE;
  this->playout_halt_time = halt_time;
  THIS_WRITEUNLOCK (this);
}



gint
_cmp_seq (guint16 x, guint16 y)
{
  if (x == y) {
    return 0;
  }
  if (x < y || (0x8000 < x && y < 0x8000)) {
    return -1;
  }
  return 1;

  //return ((gint16) (x - y)) < 0 ? -1 : 1;
}

Subflow *
_make_subflow (MpRTPRPath * path)
{
  Subflow *result = g_malloc0 (sizeof (Subflow));
  result->path = path;
  result->id = mprtpr_path_get_id (path);
  return result;
}

void
_ruin_subflow (gpointer data)
{
  Subflow *this;
  this = (Subflow *) data;
  GST_DEBUG_OBJECT (this, "Subflow %d destroyed", this->id);
}


void _add_mprtp_packet(StreamJoiner *this,
                         GstMpRTPBuffer *mprtp)
{
  Frame *frame,*succ,*prev = NULL;
  guint32 timestamp;
  if(!this->head) {
    frame = this->head = this->tail = _make_frame(this, mprtp);
    goto done;
  }
  timestamp = gst_mprtp_buffer_get_timestamp(mprtp);
  if(timestamp < this->head->timestamp){
    (frame = _make_frame(this, mprtp))->next = this->head;
    this->head = frame;
    goto done;
  }
  frame = _try_find(this, timestamp, &prev);
  if(!frame && !prev){
    this->tail->next = frame = _make_frame(this, mprtp);
    this->tail = frame;
    goto done;
  }
  if(!frame){
    succ = prev->next;
    prev->next = frame = _make_frame(this, mprtp);
    frame->next = succ;
    goto done;
  }
  _push_into_frame(this, frame, mprtp);
done:
  gst_buffer_ref(mprtp->buffer);
//  if(frame->late){
//    g_print("PlayoutGate: Late seq %hu, PHSN %hu, max_delay: %lums\n",
//            frame->head->seq, this->PHSN, GST_TIME_AS_MSECONDS(this->max_delay));
//  }
//  _print_frame(frame);
  return;
}


void _push_into_frame(StreamJoiner *this, Frame *frame, GstMpRTPBuffer *mprtp)
{
  FrameNode *prev,*node, *succ;
  node = _make_framenode(this, mprtp);
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
//g_print("Push into frame\n");
  return;
}

Frame *_try_find(StreamJoiner *this, guint32 timestamp, Frame **predecessor)
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

Frame* _make_frame(StreamJoiner *this, GstMpRTPBuffer *mprtp)
{
  Frame *result;
  GstClockTime latency = 0;

  result = pointerpool_get(this->frames_pool);
  memset((gpointer)result, 0, sizeof(Frame));
//  latency = percentiletracker_get_stats(this->latency_window, NULL, NULL, NULL);
  latency = this->latency;// + GST_MSECOND;
//  g_print("%f,0,0\n", this->latency / 1000.);
  if(mprtp->delay < latency)
    latency = latency - mprtp->delay;
  else
    latency = 0;
  latency += percentiletracker_get_stats(this->ticks, NULL, NULL, NULL);
//  latency += 10 * GST_MSECOND;
  result->head = result->tail = _make_framenode(this, mprtp);
  result->timestamp = gst_mprtp_buffer_get_timestamp(mprtp);
  result->created = gst_clock_get_time(this->sysclock);
  result->playout_time = gst_clock_get_time(this->sysclock) + latency;
//  if(subflow->id == 1)
//    g_print("%lu+%lu\n",
//            GST_TIME_AS_MSECONDS(subflow->delay),
//            GST_TIME_AS_MSECONDS(latency));
//  if(uskew)
//      g_print("delay: %lu (%lu)\n",
//              GST_TIME_AS_MSECONDS(result->playout_time - gst_clock_get_time(this->sysclock)),
//              uskew);
  ++this->framecounter;
  return result;
}

FrameNode * _make_framenode(StreamJoiner *this, GstMpRTPBuffer *mprtp)
{
  FrameNode *result;
  result = pointerpool_get(this->framenodes_pool);
  memset((gpointer)result, 0, sizeof(FrameNode));
  result->mprtp = mprtp;
  result->next = NULL;
  result->seq = gst_mprtp_buffer_get_abs_seq(mprtp);
  return result;
}

//
//void _trash_frame(StreamJoiner *this, Frame* frame)
//{
//  if(g_queue_get_length(this->frames_pool) > 128)
//    g_free(frame);
//  else
//    g_queue_push_tail(this->frames_pool, frame);
//}
//
//void _trash_framenode(StreamJoiner *this, FrameNode* framenode)
//{
//  if(g_queue_get_length(this->framenodes_pool) > 4096)
//    g_free(framenode);
//  else
//    g_queue_push_tail(this->framenodes_pool, framenode);
//}



#undef HEAP_CMP
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
