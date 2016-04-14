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

#define MAX_TRESHOLD_TIME 200 * GST_MSECOND
#define MIN_TRESHOLD_TIME 10 * GST_MSECOND
#define BETHA_FACTOR 1.2

G_DEFINE_TYPE (StreamJoiner, stream_joiner, G_TYPE_OBJECT);

typedef struct _Subflow Subflow;

struct _Subflow
{
  guint8 id;
  MpRTPRPath  *path;
  guint        bytes_in_queue;
  guint        packets_in_queue;
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

static void
_logging(
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
          frame, frame->added, frame->intact, frame->ready, frame->marked,
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
  g_object_unref (this->sysclock);
  g_object_unref(this->retained_buffers);
  g_object_unref(this->rcvqueue);
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

static void _delays_stat_pipe(gpointer data, PercentileTrackerPipeData* stat)
{
  StreamJoiner * this = data;
  GstClockTime join_delay;
  if(_now(this) - 20 * GST_MSECOND < this->last_join_refresh){
    return;
  }

  this->last_join_refresh = _now(this);
  join_delay = stat->percentile * this->betha;
  this->join_delay = CONSTRAIN(this->join_min_treshold, this->join_max_treshold, join_delay);
}

void
stream_joiner_init (StreamJoiner * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->subflows = g_hash_table_new_full (NULL, NULL, NULL, _ruin_subflow);
  this->made              = _now(this);
  this->join_delay        = 0;
  this->join_max_treshold = MAX_TRESHOLD_TIME;
  this->join_min_treshold = MIN_TRESHOLD_TIME;
  this->betha             = BETHA_FACTOR;
  this->retained_buffers  = g_queue_new();
  g_rw_lock_init (&this->rwmutex);

  this->delays = make_percentiletracker(4096, 80);
  percentiletracker_set_treshold(this->delays, 60 * GST_SECOND);
  percentiletracker_set_stats_pipe(this->delays, _delays_stat_pipe, this);

  mprtp_logger_add_logging_fnc(_logging, this, 1, &this->rwmutex);
}

StreamJoiner*
make_stream_joiner(PacketsRcvQueue *rcvqueue)
{
  StreamJoiner *result;
  result = (StreamJoiner *) g_object_new (STREAM_JOINER_TYPE, NULL);
  result->rcvqueue = g_object_ref(rcvqueue);
  return result;
}


void stream_joiner_transfer(StreamJoiner *this)
{

  GstMpRTPBuffer *mprtp = NULL;
  GstClockTime now;
  Subflow *subflow;
  guint c,i;

  THIS_WRITELOCK (this);
  now  = _now(this);
  if(g_queue_is_empty(this->retained_buffers)){
    goto done;
  }
  c = g_queue_get_length(this->retained_buffers);
  for(i = 0; i < c; ++i){
    mprtp = g_queue_pop_head(this->retained_buffers);
    if(now - get_epoch_time_from_ntp_in_ns(mprtp->abs_snd_ntp_time) < this->join_delay){
      g_queue_push_tail(this->retained_buffers, mprtp);
      continue;
    }
    subflow = (Subflow *) g_hash_table_lookup (this->subflows, GINT_TO_POINTER (mprtp->subflow_id));
    if(subflow){//in case of path remove before the buffer is dried
      subflow->bytes_in_queue -= mprtp->payload_bytes;
      --subflow->packets_in_queue;
    }
    this->bytes_in_queue -= mprtp->payload_bytes;
    --this->packets_in_queue;
    packetsrcvqueue_push(this->rcvqueue, mprtp);
  }

done:
  THIS_WRITEUNLOCK (this);
}

void stream_joiner_push(StreamJoiner * this, GstMpRTPBuffer *mprtp)
{
  Subflow *subflow;

  THIS_WRITELOCK(this);
  mprtp->buffer = gst_buffer_ref(mprtp->buffer);
  if(this->join_delay < mprtp->delay){
    packetsrcvqueue_push(this->rcvqueue, mprtp);
    goto done;
  }

  g_queue_push_tail(this->retained_buffers, mprtp);
  this->bytes_in_queue += mprtp->payload_bytes;
  ++this->packets_in_queue;

  subflow = (Subflow *) g_hash_table_lookup (this->subflows, GINT_TO_POINTER (mprtp->subflow_id));
  if(subflow){
    subflow->bytes_in_queue += mprtp->payload_bytes;
    ++subflow->packets_in_queue;
    if(!mprtpr_path_is_in_spike_mode(subflow->path)){
      percentiletracker_add(this->delays, mprtp->delay);
    }
  }

done:
  THIS_WRITEUNLOCK(this);
}

void
stream_joiner_set_min_treshold (StreamJoiner * this, GstClockTime treshold)
{
  THIS_WRITELOCK (this);
  this->join_min_treshold = treshold;
  THIS_WRITEUNLOCK (this);
}


void
stream_joiner_set_max_treshold (StreamJoiner * this, GstClockTime treshold)
{
  THIS_WRITELOCK (this);
  this->join_max_treshold = treshold;
  THIS_WRITEUNLOCK (this);
}

void
stream_joiner_set_window_treshold (StreamJoiner * this, GstClockTime treshold)
{
  THIS_WRITELOCK (this);
  percentiletracker_set_treshold(this->delays, treshold);
  THIS_WRITEUNLOCK (this);
}

void
stream_joiner_set_betha_factor (StreamJoiner * this, gdouble betha)
{
  THIS_WRITELOCK (this);
  this->betha = betha;
  THIS_WRITEUNLOCK (this);
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

static void _logging_helper(Subflow *subflow, gpointer data)
{
  gchar filename[255];
  sprintf(filename, "path_%d_joindat.csv", subflow->id);
  mprtp_logger(filename, "%d,%d\n", subflow->bytes_in_queue, subflow->packets_in_queue);
}

void _logging(gpointer data)
{
  StreamJoiner *this = data;

  mprtp_logger("streamjoiner.csv",
               "%lu,%d\n",
               this->join_delay,
               this->bytes_in_queue);

  _iterate_subflows(this, _logging_helper, this);

  mprtp_logger("streamjoiner.log",
                 "###############################################################\n"
                 "Seconds: %lu\n"
                 "packets_in_queue: %d | bytes_in_queue: %d\n"
                 ,
                 GST_TIME_AS_SECONDS(_now(this) - this->made),
                 this->packets_in_queue,
                 this->bytes_in_queue
                 );

}

#undef MAX_TRESHOLD_TIME
#undef MIN_TRESHOLD_TIME
#undef BETHA_FACTOR

#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
