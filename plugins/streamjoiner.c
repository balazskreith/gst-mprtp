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
  gboolean        marker;
  FrameNode*      next;
};

struct _Frame
{
  Frame          *next;
  FrameNode      *head;
  FrameNode      *tail;
  guint32         timestamp;
  gboolean        ready;
  gboolean        marked;
  gboolean        sorted;
  guint32         last_seq;
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
_cmp_seq32 (
    guint32 x,
    guint32 y);

static gint
_cmp_seq (
    guint16 x,
    guint16 y);

static Frame*
_make_frame(
    StreamJoiner *this,
    FrameNode *node);

static FrameNode *
_make_framenode(
    StreamJoiner *this,
    GstMpRTPBuffer *mprtp);

static gboolean
_add_mprtp_packet(
    StreamJoiner *this,
    GstMpRTPBuffer *mprtp);

static void
_push_into_frame(
    StreamJoiner *this,
    Frame *frame,
    GstMpRTPBuffer *rtp);

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
  g_print("Items: ");
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

static void _skew_max_pipe(gpointer data, gint64 value)
{
  StreamJoiner* this = data;
  this->max_skew = value;
}

static void _delay_max_pipe(gpointer data, gint64 value)
{
  StreamJoiner* this = data;
  this->max_delay = MIN(value, this->min_delay + this->max_delay_diff);
//  g_print("Max delay: %ld, %lu\n", value, this->max_delay);
}

static void _delay_min_pipe(gpointer data, gint64 value)
{
  StreamJoiner* this = data;
  this->min_delay = value;
//  g_print("Max delay: %ld, %lu\n", value, this->max_delay);
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
  this->flushing = FALSE;
  this->playout_allowed = TRUE;
  this->playout_halt = FALSE;
  this->playout_halt_time = 100 * GST_MSECOND;
  this->max_delay_diff = 50 * GST_MSECOND;
  this->min_delay = this->max_delay = 100 * GST_MSECOND;
  this->forced_delay = 150 * GST_MSECOND;
  this->forced_delay = 0;
  this->delays = make_numstracker(256, GST_SECOND);
  numstracker_add_plugin(this->delays,
                           (NumsTrackerPlugin*)make_numstracker_minmax_plugin(_delay_max_pipe, this, _delay_min_pipe, this));
  this->skews = make_numstracker(256, GST_SECOND);
  numstracker_add_plugin(this->skews,
                         (NumsTrackerPlugin*)make_numstracker_minmax_plugin(_skew_max_pipe, this, NULL, NULL));
//  this->latency_window = make_percentiletracker(256, 90);
//  percentiletracker_set_treshold(this->latency_window, 10 * GST_SECOND);
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
  if (this->subflow_num == 0 || !this->playout_allowed) {
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
  if(node->mprtp){
    this->bytes_in_queue -= node->mprtp->payload_bytes;
//    {
//      GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
//      gst_rtp_buffer_map(node->mprtp->buffer, GST_MAP_READ, &rtp);
//      g_print("Playout Abs-Seq: %u Payload-ln: %u, Timestamp: %u - IFrame: %d Ready: %d Originated: %d\n",
//              gst_rtp_buffer_get_seq(&rtp),
//              gst_rtp_buffer_get_payload_len(&rtp),
//              gst_rtp_buffer_get_timestamp(&rtp),
//              !GST_BUFFER_FLAG_IS_SET (rtp.buffer, GST_BUFFER_FLAG_DELTA_UNIT),
//              frame->ready,
//              node->mprtp->subflow_id);
//      gst_rtp_buffer_unmap(&rtp);
//    }
//    g_print("Playout %u\n", gst_mprtp_buffer_get_abs_seq(node->mprtp));
    this->send_mprtp_packet_func(this->send_mprtp_packet_data, node->mprtp);
  }
  frame->head = node->next;
  _trash_framenode(this, node);
  if(frame->head) goto pop_node;
  this->last_played_timestamp = frame->timestamp;
  this->head = frame->next;
  _trash_frame(this, frame);
  //if we want to flush then goto pop_frame
  if(this->flushing) goto pop_frame;
//  g_print("----------------------%d-------------------------\n", this->framecounter);
next_tick:
  next_scheduler_time = now +  GST_MSECOND;
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
  Subflow *subflow;
  THIS_WRITELOCK(this);
  if(!GST_IS_BUFFER(mprtp->buffer)){
    this->send_mprtp_packet_func(this->send_mprtp_packet_data, mprtp);
    goto done;
  }
  subflow = (Subflow *) g_hash_table_lookup (this->subflows, GINT_TO_POINTER (mprtp->subflow_id));

  if(mprtp->payload_type == this->monitor_payload_type){
    //Todo: FEC packet processing here,
    subflow->monitored_bytes += mprtp->payload_bytes;
    this->send_mprtp_packet_func(this->send_mprtp_packet_data, mprtp);
    goto done;
  }

  mprtpr_path_get_joiner_stats(subflow->path,
                              &subflow->delay,
                              &subflow->skew,
                              &subflow->jitter);

  numstracker_add(this->skews, subflow->skew);
  //g_print("Subflow %d delay: %f\n", subflow->id, subflow->delay);
  numstracker_add(this->delays, subflow->delay);
  this->playout_delay *= 124.;
  this->playout_delay += this->max_skew;
  this->playout_delay /= 125.;

  //ssrc filter
  if(this->ssrc != 0 && this->ssrc != mprtp->ssrc){
    this->send_mprtp_packet_func(this->send_mprtp_packet_data, mprtp);
    goto done;
  }

  if(!_add_mprtp_packet(this, mprtp)){
//    g_print("Discarded packet arrived. Seq: %hu Subflow: %d live:%d<-delay:%lu-dur:%lu-ts:%lu-offs:%lu\n",
//            mprtp->abs_seq,
//            mprtp->subflow_id,
//            GST_BUFFER_FLAG_IS_SET (mprtp->buffer, GST_BUFFER_FLAG_LIVE),
//            mprtp->delay,
//            GST_BUFFER_DURATION(mprtp->buffer),
//            GST_BUFFER_TIMESTAMP(mprtp->buffer),
//            GST_BUFFER_OFFSET(mprtp->buffer));

    //The packet should be forwarded and discarded
//      g_print("mprtp timestamp: %u last played timestamp: %u\n", mprtp->timestamp, this->last_played_timestamp);
    if(this->last_played_timestamp != 0 && _cmp_seq32(mprtp->timestamp, this->last_played_timestamp) < 0){
      mprtpr_path_add_discard(subflow->path, mprtp);
    }

    this->send_mprtp_packet_func(this->send_mprtp_packet_data, mprtp);
  }
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
stream_joiner_set_forced_delay(StreamJoiner *this, GstClockTime forced_delay)
{
  THIS_WRITELOCK (this);
  this->forced_delay = forced_delay;
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
                        gdouble *playout_delay,
                        gint32 *playout_buffer_size)
{
  THIS_READLOCK(this);
  if(playout_delay) *playout_delay = this->playout_delay;
  if(playout_buffer_size) *playout_buffer_size = this->bytes_in_queue;
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
  if(x == y) return 0;
  if(x < y && y - x < 32768) return -1;
  if(x > y && x - y > 32768) return -1;
  if(x < y && y - x > 32768) return 1;
  if(x > y && x - y < 32768) return 1;
  return 0;
}

//x < y:-1; x == y: 0; x > y: 1
gint
_cmp_seq32 (guint32 x, guint32 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 2147483648) return -1;
  if(x > y && x - y > 2147483648) return -1;
  if(x < y && y - x > 2147483648) return 1;
  if(x > y && x - y < 2147483648) return 1;
  return 0;
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


gboolean _add_mprtp_packet(StreamJoiner *this,
                         GstMpRTPBuffer *mprtp)
{
  Frame *frame,*prev = NULL;
  guint32 timestamp;
  gint cmp;
  gboolean result = FALSE;
  //If frame timestamp smaller than the last timestamp the packet is discarded.
  //the frame timestamp comparsion muzst be considering the wrap around
  timestamp = mprtp->timestamp;
  cmp = _cmp_seq32(timestamp, this->last_played_timestamp);
  if(this->last_played_timestamp != 0 && cmp <= 0){
    //The packet should be forwarded and discarded
    goto exit;
  }
  if(!this->head) {
    frame = this->head = this->tail = _make_frame(this, _make_framenode(this, mprtp));
    prev = NULL;
    goto done;
  }
  frame = this->head;
  cmp = _cmp_seq32(timestamp, frame->timestamp);
  //a frame arrived should be played out before the head,
  //but it is older than the last played out timestamp.
  if(cmp < 0){
    this->head = _make_frame(this, _make_framenode(this, mprtp));
    this->head->next = frame;
    frame = this->head;
    prev = NULL;
    goto done;
  }
  if(cmp == 0){
    prev = NULL;
    _push_into_frame(this, frame, mprtp);
    goto done;
  }
again:
  prev = frame;
  frame = frame->next;
  //we reached the end
  if(!frame){
    frame = _make_frame(this, _make_framenode(this, mprtp));
    prev->next = frame;
    goto done;
  }
  cmp = _cmp_seq32(timestamp, frame->timestamp);
  //so the actual frame timestamp is smaller than what we have. Great!
  //Prev must exists since the first frame is checked
  //we insert the node into a new frame
  if(cmp < 0){
    prev->next = _make_frame(this, _make_framenode(this, mprtp));
    prev->next->next = frame;
    frame = prev->next;
    goto done;
  }
  //is it the right one?
  if(cmp == 0){
    _push_into_frame(this, frame, mprtp);
    goto done;
  }
  goto again;
done:
  result = TRUE;
  this->bytes_in_queue += mprtp->payload_bytes;
  gst_buffer_ref(mprtp->buffer);
  frame->ready = frame->sorted && frame->marked;
  if(prev) frame->ready&=prev->marked;
exit:
  return result;
}


void _push_into_frame(StreamJoiner *this, Frame *frame, GstMpRTPBuffer *mprtp)
{
  FrameNode *prev,*act, *node;
  gint cmp;
  if(!frame->head){
    //cannot be happened.
   g_warning("Something wrong with the playoutbuffer");
   goto exit;
  }
  act = frame->head;
  node = _make_framenode(this, mprtp);
  //x < y: -1; x == y: 0; x > y: 1
  cmp = _cmp_seq(node->seq, act->seq);
  //check weather the head is the smallest
  if(cmp < 0){
    //new head.
    frame->head = node;
    node->next = act;
    goto done;
  }
again:
  prev = act;
  act = act->next;
  if(!act){
    prev->next = node;
    frame->tail = node;
    goto done;
  }
  cmp = _cmp_seq(node->seq, act->seq);
  if(cmp == 0){
    //do nothing, but its a duplicated packet.
  }
  if(cmp <= 0){
    prev->next = node;
    node->next = act;
    goto done;
  }
  goto again;
done:

  //Check weather the frame is marked
  frame->marked |= node->marker;

  //Check weather the frame is sorted
  for(prev = frame->head, act = frame->head->next;
            !act && (guint16)(prev->seq + 1) == act->seq;
            prev = act, act = act->next);
  frame->sorted = !act;
exit:
  return;
}

Frame* _make_frame(StreamJoiner *this, FrameNode *node)
{
  Frame *result;
  GstClockTime delay_diff;

  result = pointerpool_get(this->frames_pool);
  memset((gpointer)result, 0, sizeof(Frame));
  result->head = result->tail = node;
  result->timestamp = node->mprtp->timestamp;
  result->created = gst_clock_get_time(this->sysclock);
  result->ready = node->marker;
  result->last_seq = result->ready ? node->seq : 0;
  //Todo: Considering the playout delay calculations here.
  delay_diff = 0;
  if(0 < this->forced_delay){
    if(node->mprtp->delay < this->forced_delay){
      delay_diff = this->forced_delay - node->mprtp->delay;
    }
  }else if(node->mprtp->delay < this->max_delay){
    delay_diff = this->max_delay - node->mprtp->delay;
  }
  result->playout_time = gst_clock_get_time(this->sysclock) + delay_diff +  this->playout_delay;
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
  result->seq = mprtp->abs_seq;
  result->marker = mprtp->marker;
  return result;
}



#undef HEAP_CMP
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
