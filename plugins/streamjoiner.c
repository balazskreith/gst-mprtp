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
#include <stdio.h>
#include "mprtplogger.h"


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

#define _now(this) gst_clock_get_time (this->sysclock)

G_DEFINE_TYPE (StreamJoiner, stream_joiner, G_TYPE_OBJECT);

typedef struct _Subflow Subflow;

struct _Subflow
{
  guint8 id;
  MpRTPRPath  *path;
  gdouble      skew;
  gdouble      delay;
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
  gboolean        intact;
  guint32         last_seq;
  GstClockTime    playout_time;
  GstClockTime    created;
};


//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
stream_joiner_finalize (
    GObject * object);

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

static GstMpRTPBuffer*
_rem_mprtp(
    StreamJoiner *this);

static void
_logging(
    gpointer data);

static void
_logging2(
    gpointer data);

//#define _trash_frame(this, frame)
//  g_slice_free(Frame, frame);
//  --this->framecounter;


#define _trash_frame(this, frame)  \
  mprtp_free(frame);      \
  --this->framecounter;


//#define _trash_framenode(this, node) g_slice_free(FrameNode, node);
#define _trash_framenode(this, node) mprtp_free(node);


//#define DEBUG_PRINT_TOOLS
#ifdef DEBUG_PRINT_TOOLS
static void _print_frame(Frame *frame)
{
  FrameNode *node;
  g_print("Frame %p created: %lu, srt: %d, rd: %d, m: %d src: %d, h: %p, t: %p\n",
          frame, frame->created, frame->intact, frame->ready, frame->marked,
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
  g_object_unref(this->urgent);
  g_object_unref (this->sysclock);
//  g_object_unref(this->playoutgate);
}

static void _skews_max_pipe(gpointer data, gint64 value)
{
  StreamJoiner* this = data;
  this->max_skew = MAX(0, value);
}

static void _skews_min_pipe(gpointer data, gint64 value)
{
  StreamJoiner* this = data;
  this->min_skew = MAX(0, value);
}

static void _snd_sampling_stat_pipe(gpointer data, PercentileTrackerPipeData *stat)
{
  StreamJoiner* this = data;
  this->sampling_time = stat->percentile;
//  mprtp_logger("snd_samplings.log", "%lu\n", stat->percentile);
}

static void _iterate_subflows(StreamJoiner *this, void(*iterator)(Subflow *, gpointer), gpointer data)
{
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    subflow = (Subflow *) val;
    iterator(subflow, data);
  }
}


void
stream_joiner_init (StreamJoiner * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->subflows = g_hash_table_new_full (NULL, NULL, NULL, _ruin_subflow);
//  this->max_path_skew = 10 * GST_MSECOND;
  this->flushing          = FALSE;
  this->playout_allowed   = TRUE;
  this->playout_halt      = FALSE;
  this->playout_halt_time = 0 * GST_MSECOND;
  this->init_delay        = 500 * GST_MSECOND;
  this->urgent            = g_queue_new();
  this->skews             = make_numstracker(256, 2 * GST_SECOND);
  this->made              = _now(this);
  this->snd_samplings     = make_percentiletracker(100, 50);

  percentiletracker_set_treshold(this->snd_samplings, 2 * GST_SECOND);

  percentiletracker_set_stats_pipe(this->snd_samplings, _snd_sampling_stat_pipe, this);
  numstracker_add_plugin(this->skews,
                         (NumsTrackerPlugin*)make_numstracker_minmax_plugin(_skews_max_pipe, this,
                                                                            _skews_min_pipe, this));

  g_rw_lock_init (&this->rwmutex);

  mprtp_logger_add_logging_fnc(_logging, this, 1, &this->rwmutex);
  mprtp_logger_add_logging_fnc(_logging2, this, 10, &this->rwmutex);
}

StreamJoiner*
make_stream_joiner(void)
{
  StreamJoiner *result;
  result = (StreamJoiner *) g_object_new (STREAM_JOINER_TYPE, NULL);
  return result;
}


GstMpRTPBuffer *stream_joiner_pop(StreamJoiner *this)
{

  GstMpRTPBuffer *result = NULL;
  GstClockTime now;

  THIS_WRITELOCK (this);
  now  = _now(this);

  if(this->last_playout_refresh < now - 20 * GST_MSECOND){
    this->playout_delay = this->playout_delay * .992 + this->max_skew * .008;
    this->playout_delay = CONSTRAIN(0, 50 * GST_MSECOND, this->playout_delay);
    this->last_playout_refresh = now;
  }

  if(!this->playout_allowed){
    goto done;
  }

  if (this->subflow_num == 0 || !this->playout_allowed) {
    goto done;
  }

  if(this->playout_halt){
    this->playout_halt = now < this->playout_halt_time;
    goto done;
  }

  result = _rem_mprtp(this);
  if(result){
    if(!this->last_played_timestamp){
        this->last_snd_sampling = get_epoch_time_from_ntp_in_ns(result->abs_snd_ntp_time);
        this->last_played_timestamp = result->timestamp;
    }else if(_cmp_seq32(this->last_played_timestamp, result->timestamp) < 0){
        GstClockTime actual_snd_epoch;
        actual_snd_epoch = get_epoch_time_from_ntp_in_ns(result->abs_snd_ntp_time);
        percentiletracker_add(this->snd_samplings, actual_snd_epoch - this->last_snd_sampling);
        this->last_snd_sampling = actual_snd_epoch;
        this->last_played_timestamp = result->timestamp;

    }
  }
done:
  THIS_WRITEUNLOCK (this);
  return result;
}

void stream_joiner_push(StreamJoiner * this, GstMpRTPBuffer *mprtp)
{
  Subflow *subflow;

  THIS_WRITELOCK(this);
  subflow = (Subflow *) g_hash_table_lookup (this->subflows, GINT_TO_POINTER (mprtp->subflow_id));

  mprtpr_path_process_rtp_packet(subflow->path, mprtp);

  mprtpr_path_get_joiner_stats(subflow->path,
                              &subflow->delay,
                              &subflow->skew);

  numstracker_add(this->skews, subflow->skew);

  mprtp->buffer = gst_buffer_ref(mprtp->buffer);
  if(!_add_mprtp_packet(this, mprtp)){
    g_queue_push_tail(this->urgent, mprtp);
  }
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
stream_joiner_set_initial_delay(StreamJoiner *this, GstClockTime init_delay)
{
  THIS_WRITELOCK (this);
  this->init_delay = init_delay;
  this->init_delay_applied = FALSE;
  THIS_WRITEUNLOCK (this);
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
  this->playout_halt_time = _now(this) + halt_time;
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
  Subflow *result = mprtp_malloc (sizeof (Subflow));
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



GstMpRTPBuffer *_rem_mprtp(StreamJoiner *this)
{
  Frame          *frame;
  FrameNode      *node;
  GstMpRTPBuffer *result = NULL;

  if(!g_queue_is_empty(this->urgent)){
    result = g_queue_pop_head(this->urgent);
    goto done;
  }

  frame = this->head;
  if(!frame) {
    goto done;
  }
  if(!this->init_delay_applied){
    this->init_delay_applied = TRUE;
    frame->playout_time = _now(this) + this->init_delay;
    goto done;
  }
  if(!this->flushing && _now(this) < frame->playout_time) {
    goto done;
  }
  node = frame->head;
  result = node->mprtp;
  this->bytes_in_queue -= node->mprtp->payload_bytes;
  frame->head = node->next;
  _trash_framenode(this, node);
  if(!frame->head){
    this->head = frame->next;
    _trash_frame(this, frame);
    if(this->head){
      frame = this->head;
      frame->playout_time = _now(this) + 20 * GST_MSECOND + this->playout_delay;
    }
  }

  this->last_playout = _now(this);
done:
  return result;
}

gboolean _add_mprtp_packet(StreamJoiner *this,
                         GstMpRTPBuffer *mprtp)
{
  Frame *frame,*prev = NULL;
  guint32 timestamp;
  gint cmp;
  gboolean result = FALSE;
  //If frame timestamp smaller than the last timestamp the frame is already played out.
  //the frame timestamp comparsion must considering the wrap around the sequence nums
  timestamp = mprtp->timestamp;
  cmp = _cmp_seq32(timestamp, this->last_played_timestamp);
  if(this->last_played_timestamp != 0 && cmp <= 0){
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
  frame->ready = frame->intact && frame->marked;
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

//Todo: bookmark <- byte scheduling joining time might be considered here.
  //refresh playout time
  frame->playout_time+=0;

  //Check weather the frame is marked
  if(node->marker){
    frame->last_seq = node->seq;
    frame->marked   = TRUE;
  }

  //Check weather the frame is sorted
  for(prev = frame->head, act = frame->head->next;
            !act && (guint16)(prev->seq + 1) == act->seq;
            prev = act, act = act->next);
  frame->intact = !act;
exit:
  return;
}

Frame* _make_frame(StreamJoiner *this, FrameNode *node)
{
  Frame *result;
  //  result = g_slice_new0(Frame);
  result = mprtp_malloc(sizeof(Frame));
  result->head = result->tail = node;
  result->timestamp = node->mprtp->timestamp;
  result->created = gst_clock_get_time(this->sysclock);
  result->ready = node->marker;
  result->last_seq = result->ready ? node->seq : 0;
  result->playout_time = _now(this) + this->sampling_time + this->playout_delay;
  ++this->framecounter;

  return result;
}

FrameNode * _make_framenode(StreamJoiner *this, GstMpRTPBuffer *mprtp)
{
  FrameNode *result;
//  result = g_slice_new0(FrameNode);
  result = mprtp_malloc(sizeof(FrameNode));
  result->mprtp = mprtp;
  result->next = NULL;
  result->seq = mprtp->abs_seq;
  result->marker = mprtp->marker;
  return result;
}

static void _logging_helper(Subflow *subflow, gpointer data)
{
  gchar filename[255];
  sprintf(filename, "path_%d_skews.csv", subflow->id);
  mprtp_logger(filename, "%f\n", subflow->skew);
}

void _logging(gpointer data)
{
  StreamJoiner *this = data;

  mprtp_logger("streamjoiner.csv",
               "%f,%d\n",
               this->playout_delay,
               this->bytes_in_queue);

  _iterate_subflows(this, _logging_helper, this);

  mprtp_logger("streamjoiner.log",
                 "###############################################################\n"
                 "Seconds: %lu\n"
                 "framecounter: %d | bytes_in_queue: %d\n"
                 "initial_delay: %lu\n"
                 "flushing: %d | playout_allowed: %d | playout_halt: %d\n"
                 "halt_time: %lu | \n"
                 ,
                 GST_TIME_AS_SECONDS(_now(this) - this->made),
                 this->framecounter,
                 this->bytes_in_queue,
                 this->init_delay,
                 this->flushing,
                 this->playout_allowed,
                 this->playout_halt,
                 this->playout_halt_time
                 );

}

void _logging2(gpointer data)
{
  StreamJoiner *this = data;

  mprtp_logger("streamjoiner.log",
                 "###############################################################\n"
                 "Seconds: %lu\n"
                 "framecounter: %d | bytes_in_queue: %d\n"
                 "initial_delay: %lu\n"
                 "flushing: %d | playout_allowed: %d | playout_halt: %d\n"
                 "halt_time: %lu | \n"
                 ,
                 GST_TIME_AS_SECONDS(_now(this) - this->made),
                 this->framecounter,
                 this->bytes_in_queue,
                 this->init_delay,
                 this->flushing,
                 this->playout_allowed,
                 this->playout_halt,
                 this->playout_halt_time
                 );

}


#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
