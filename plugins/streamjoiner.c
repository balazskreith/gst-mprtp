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
};

typedef struct _Packet{
  gboolean        timegrab;
  GstMpRTPBuffer *mprtp;
}Packet;

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
_cmp_seq (guint16 x, guint16 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 32768) return -1;
  if(x > y && x - y > 32768) return -1;
  if(x < y && y - x > 32768) return 1;
  if(x > y && x - y < 32768) return 1;
  return 0;
}

//static gint
//_cmp_ts (guint32 x, guint32 y)
//{
//  if(x == y) return 0;
//  if(x < y && y - x < 2147483648) return -1;
//  if(x > y && x - y > 2147483648) return -1;
//  if(x < y && y - x > 2147483648) return 1;
//  if(x > y && x - y < 2147483648) return 1;
//  return 0;
//}


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
  g_object_unref(this->rcvqueue);
}
//
//static void _iterate_subflows(StreamJoiner *this, void(*iterator)(Subflow *, gpointer), gpointer data)
//{
//  GHashTableIter iter;
//  gpointer key, val;
//  Subflow *subflow;
//
//  g_hash_table_iter_init (&iter, this->subflows);
//  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
//    subflow = (Subflow *) val;
//    iterator(subflow, data);
//  }
//}

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
  this->sysclock           = gst_system_clock_obtain ();
  this->subflows           = g_hash_table_new_full (NULL, NULL, NULL, _ruin_subflow);
  this->made               = _now(this);
  this->join_delay         = 0;
  this->join_max_treshold  = MAX_TRESHOLD_TIME;
  this->join_min_treshold  = MIN_TRESHOLD_TIME;
  this->betha              = BETHA_FACTOR;
  this->HFSN_initialized   = FALSE;
  this->flush              = FALSE;
  this->packets_by_arrival = g_queue_new();
  this->packets_by_seq     = g_queue_new();
  g_rw_lock_init (&this->rwmutex);

  this->delays = make_percentiletracker(4096, 80);
  percentiletracker_set_treshold(this->delays, 60 * GST_SECOND);
  percentiletracker_set_stats_pipe(this->delays, _delays_stat_pipe, this);

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
  GstClockTime tail_rcvd, head_rcvd;
  Packet* packet;

  THIS_WRITELOCK (this);
  if(g_queue_is_empty(this->packets_by_arrival)){
    goto transfer;
  }
  packet = g_queue_peek_tail(this->packets_by_arrival);
  mprtp  = packet->mprtp;
  tail_rcvd = get_epoch_time_from_ntp_in_ns(mprtp->abs_rcv_ntp_time);

  while(!g_queue_is_empty(this->packets_by_arrival)){
    packet = g_queue_peek_head(this->packets_by_arrival);
    mprtp = packet->mprtp;
    head_rcvd = get_epoch_time_from_ntp_in_ns(mprtp->abs_rcv_ntp_time);
    if(tail_rcvd - head_rcvd < this->join_delay && !this->flush){
      break;
    }
    packet->timegrab = FALSE;
    g_queue_pop_head(this->packets_by_arrival);
  }

transfer:
  while(!g_queue_is_empty(this->packets_by_seq)){
    packet = g_queue_peek_head(this->packets_by_seq);
    if(packet->timegrab){
      break;
    }
    packet = g_queue_pop_head(this->packets_by_seq);
    mprtp = packet->mprtp;
    g_slice_free(Packet, packet);
    this->HFSN = mprtp->abs_seq;
    this->HFSN_initialized = TRUE;
    packetsrcvqueue_push(this->rcvqueue, mprtp);
  }
  THIS_WRITEUNLOCK (this);
}

static gint _packets_queue_sort_helper(gconstpointer a, gconstpointer b, gpointer user_data)
{
  const Packet *ai = a;
  const Packet *bi = b;
  return _cmp_seq(ai->mprtp->abs_seq, bi->mprtp->abs_seq);
}

void stream_joiner_push(StreamJoiner * this, GstMpRTPBuffer *mprtp)
{
  Subflow *subflow;
  Packet* packet;

  THIS_WRITELOCK(this);
  mprtp->buffer = gst_buffer_ref(mprtp->buffer);
  subflow = (Subflow *) g_hash_table_lookup (this->subflows, GINT_TO_POINTER (mprtp->subflow_id));
  if(!subflow){
//      g_print("subflow is not found %d\n", mprtp->subflow_id);
    GST_WARNING_OBJECT(this, "the incoming packet belongs to a subflow (%d), which is not added to StreamJoiner", mprtp->subflow_id);
    packetsrcvqueue_push(this->rcvqueue, mprtp);
    goto done;
  }
  mprtpr_path_process_rtp_packet(subflow->path, mprtp);
//  g_print("inserted %hu - %d\n", mprtp->abs_seq, mprtp->subflow_id);
//  subflow->payload_bytes += mprtp->payload_bytes;
//  g_print("%d bytes: %u\n", subflow->id, subflow->payload_bytes);
  if(this->HFSN_initialized && _cmp_seq(mprtp->abs_seq, this->HFSN) < 0){
    packetsrcvqueue_push_discarded(this->rcvqueue, mprtp);
    goto done;
  }
  packet = g_slice_new0(Packet);
  packet->timegrab = TRUE;
  packet->mprtp    = mprtp;
  g_queue_push_tail(this->packets_by_arrival, packet);
  g_queue_insert_sorted(this->packets_by_seq, packet, _packets_queue_sort_helper, NULL);

  if(!mprtpr_path_is_in_spike_mode(subflow->path)){
//      g_print("path not in spike mode: %d\n", subflow->id);
    percentiletracker_add(this->delays, mprtp->delay);
  }else{
//      g_print("path in spike mode: %d\n", subflow->id);
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
  return result;
}

void
_ruin_subflow (gpointer data)
{
  Subflow *this;
  this = (Subflow *) data;
  GST_DEBUG_OBJECT (this, "Subflow %d destroyed", this->id);
}

#undef MAX_TRESHOLD_TIME
#undef MIN_TRESHOLD_TIME
#undef BETHA_FACTOR

#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
