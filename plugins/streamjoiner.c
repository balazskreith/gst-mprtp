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

  GQueue*      packets;
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


static gint
_cmp_seq (guint16 x, guint16 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 32768) return -1;
  if(x > y && x - y > 32768) return -1;
  if(x < y && y - x > 32768) return 1;
  if(x > y && x - y < 32768) return 1;
  return 0;
}

static gint
_cmp_ts (guint32 x, guint32 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 2147483648) return -1;
  if(x > y && x - y > 2147483648) return -1;
  if(x < y && y - x > 2147483648) return 1;
  if(x > y && x - y < 2147483648) return 1;
  return 0;
}


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
  g_object_unref(this->frame);
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
  this->join_delay = CONSTRAIN(this->join_min_treshold, this->join_max_treshold, join_delay - stat->min);

//  g_print("join delay: min_th: %lu max_th: %lu jd: mn: %lu mx: %lu perc: %lu - %lu %lu\n",
//          this->join_min_treshold,
//          this->join_max_treshold,
//          stat->min,
//          stat->max,
//          stat->percentile,
//          join_delay,
//          this->join_delay);
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
  this->frame             = g_queue_new();
  this->HSSN_initialized  = FALSE;
  g_rw_lock_init (&this->rwmutex);

  this->delays = make_percentiletracker(4096, 80);
  percentiletracker_set_treshold(this->delays, 60 * GST_SECOND);
  percentiletracker_set_stats_pipe(this->delays, _delays_stat_pipe, this);

  DISABLE_LINE mprtp_logger_add_logging_fnc(_logging, this, 1, &this->rwmutex);
}

StreamJoiner*
make_stream_joiner(PacketsRcvQueue *rcvqueue)
{
  StreamJoiner *result;
  result = (StreamJoiner *) g_object_new (STREAM_JOINER_TYPE, NULL);
  result->rcvqueue = g_object_ref(rcvqueue);
  return result;
}

static void _get_ts_packet(Subflow *subflow, gpointer data)
{
  StreamJoiner *this = data;
  GstMpRTPBuffer *mprtp;
  if(g_queue_is_empty(subflow->packets) || this->ts_packet){
    goto done;
  }
  mprtp = g_queue_peek_head(subflow->packets);
  if(mprtp->timestamp != this->next_ts){
    goto done;
  }
  this->ts_packet = g_queue_pop_head(subflow->packets);
done:
  return;
}


static gint _frame_queue_sort_helper(gconstpointer a, gconstpointer b, gpointer user_data)
{
  const GstMpRTPBuffer *ai = a;
  const GstMpRTPBuffer *bi = b;
  return _cmp_seq(ai->abs_seq, bi->abs_seq);
}

static gboolean _frame_complete(StreamJoiner *this)
{
  guint16 hssn;
  GList *it;
  GstMpRTPBuffer *mprtp;
  if(g_queue_is_empty(this->frame)){
    return FALSE;
  }
  if(this->frame_is_dirty){
    g_queue_sort(this->frame, _frame_queue_sort_helper, NULL);
    this->frame_is_dirty = FALSE;
  }
  hssn = this->HSSN;
  if(!this->HSSN_initialized){
    mprtp = g_queue_peek_head(this->frame);
    hssn = mprtp->abs_seq;
    --hssn;
  }
  for(it = g_queue_peek_head_link(this->frame); it; it = it->next){
    //Todo: check weather this goes to the right direction
    mprtp = it->data;
    if((guint16)(hssn + 1) != mprtp->abs_seq){
      return FALSE;
    }
    ++hssn;
  }

//  g_print("frame is complete and the frame is ended: %d->HSSN: %hu frame first: %hu last: %hu\n",
//          this->frame_ended,
//          this->HSSN,
//          ((GstMpRTPBuffer*) g_queue_peek_head(this->frame))->abs_seq,
//          ((GstMpRTPBuffer*) g_queue_peek_tail(this->frame))->abs_seq);

  return TRUE;
}

static void _set_next_ts(Subflow *subflow, gpointer data)
{
  StreamJoiner *this = data;
  GstMpRTPBuffer *mprtp;
  if(g_queue_is_empty(subflow->packets)){
    goto done;
  }
  mprtp = g_queue_peek_head(subflow->packets);
  if(this->next_ts == 0 || _cmp_ts(mprtp->timestamp, this->next_ts) < 0){
    this->next_ts = mprtp->timestamp;
  }
done:
  return;
}

void stream_joiner_transfer(StreamJoiner *this)
{
  GstMpRTPBuffer *mprtp = NULL;
  GstClockTime join_th;

  THIS_WRITELOCK (this);
  if(!this->next_ts){
    goto done;
  }
  do{
    this->ts_packet = NULL;
    _iterate_subflows(this, _get_ts_packet, this);
    if(!this->ts_packet){
      break;
    }
    if(g_queue_is_empty(this->frame)){
      this->frame_started = _now(this);
    }
    mprtp = this->ts_packet;
    this->frame_ended |= mprtp->marker;
    g_queue_push_tail(this->frame, mprtp);
    this->frame_is_dirty = TRUE;
  }while(this->ts_packet);

  join_th = _now(this) - this->join_delay;
  g_print("frame elapsed: %lu - join delay: %lu frame complete: %d frame ended: %d ts: %d\n",
          _now(this) - this->frame_started, this->join_delay,
          _frame_complete(this), this->frame_ended, this->next_ts);
  if(join_th < this->frame_started){
    if(!this->frame_ended || !_frame_complete(this)){
      goto done;
    }
  }else if(this->frame_is_dirty){
    g_queue_sort(this->frame, _frame_queue_sort_helper, NULL);
    this->frame_is_dirty = FALSE;
  }
  g_print("playout point %lu\n", _now(this));
  while(!g_queue_is_empty(this->frame)){
    mprtp = g_queue_pop_head(this->frame);
    this->HSSN = mprtp->abs_seq;
    this->HSSN_initialized = TRUE;
    packetsrcvqueue_push(this->rcvqueue, mprtp);
  }
  this->next_ts = 0;
  _iterate_subflows(this, _set_next_ts, this);
  this->frame_ended = FALSE;

done:
  THIS_WRITEUNLOCK (this);
}

static gint _packets_queue_sort_helper(gconstpointer a, gconstpointer b, gpointer user_data)
{
  const GstMpRTPBuffer *ai = a;
  const GstMpRTPBuffer *bi = b;
  return _cmp_seq(ai->abs_seq, bi->abs_seq);
}

void stream_joiner_push(StreamJoiner * this, GstMpRTPBuffer *mprtp)
{
  Subflow *subflow;
  GstMpRTPBuffer *item = NULL;

  THIS_WRITELOCK(this);
  mprtp->buffer = gst_buffer_ref(mprtp->buffer);
  subflow = (Subflow *) g_hash_table_lookup (this->subflows, GINT_TO_POINTER (mprtp->subflow_id));
  if(!subflow){
    GST_WARNING_OBJECT(this, "the incoming packet belongs to a subflow (%d) not added to StreamJoiner", mprtp->subflow_id);
    packetsrcvqueue_push(this->rcvqueue, mprtp);
    goto done;
  }
  mprtpr_path_process_rtp_packet(subflow->path, mprtp);
  if(this->HSSN_initialized && _cmp_seq(mprtp->abs_seq, this->HSSN) < 0){
      packetsrcvqueue_push_urgent(this->rcvqueue, mprtp);
    goto done;
  }
  g_queue_insert_sorted(subflow->packets, mprtp, _packets_queue_sort_helper, NULL);

  item = g_queue_peek_head(subflow->packets);
  if(this->next_ts == 0 || _cmp_ts(item->timestamp, this->next_ts) < 0){
    this->next_ts = item->timestamp;
  }
//
//  g_print("queue for subflow %d length: %d bytes: %d\n",
//          subflow->id,
//          subflow->packets_in_queue,
//          subflow->bytes_in_queue
//  );

  subflow->bytes_in_queue += mprtp->payload_bytes;
  ++subflow->packets_in_queue;
  if(!mprtpr_path_is_in_spike_mode(subflow->path)){
    percentiletracker_add(this->delays, mprtp->delay);
  }

done:
  THIS_WRITEUNLOCK(this);
}

void
stream_joiner_set_min_treshold (StreamJoiner * this, GstClockTime treshold)
{
  THIS_WRITELOCK (this);
  this->join_min_treshold = treshold;
  this->join_delay = MAX(this->join_min_treshold, this->join_delay);
  THIS_WRITEUNLOCK (this);
}


void
stream_joiner_set_max_treshold (StreamJoiner * this, GstClockTime treshold)
{
  THIS_WRITELOCK (this);
  this->join_max_treshold = treshold;
  this->join_delay = MIN(this->join_max_treshold, this->join_delay);
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
  result->packets = g_queue_new();
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
