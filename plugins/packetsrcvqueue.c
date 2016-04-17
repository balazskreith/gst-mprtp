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
static void _csv_logging(gpointer data);
static void _readable_logging(gpointer data);
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

static void _samplings_stat_pipe(gpointer data, PercentileTrackerPipeData* stat)
{
  PacketsRcvQueue * this = data;
  if(!stat->percentile){
    this->mean_rate = this->max_playoutrate;
    return;
  }

  this->mean_rate = stat->percentile;
}

void
packetsrcvqueue_init (PacketsRcvQueue * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sysclock = gst_system_clock_obtain();
  this->frames = g_queue_new();
  this->urgent = g_queue_new();
  this->normal = g_queue_new();

  this->bytes_in_normal_queue = 0;

  this->dsampling = make_percentiletracker(100, 50);
  percentiletracker_set_treshold(this->dsampling, 60 * GST_SECOND);
  percentiletracker_set_stats_pipe(this->dsampling, _samplings_stat_pipe, this);
  this->desired_framenum = 1;
  this->min_playoutrate = .01 * GST_SECOND;
  this->max_playoutrate =  .04 * GST_SECOND;
  this->spread_factor = 2.;

  mprtp_logger_add_logging_fnc(_csv_logging,this, 1, &this->rwmutex);
  mprtp_logger_add_logging_fnc(_readable_logging,this, 10, &this->rwmutex);

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
packetsrcvqueue_set_desired_framenum(PacketsRcvQueue *this, guint desired_framenum)
{
  THIS_WRITELOCK (this);
  this->desired_framenum = desired_framenum;
  THIS_WRITEUNLOCK (this);
}


void
packetsrcvqueue_set_min_playoutrate(PacketsRcvQueue *this, GstClockTime min_playoutrate)
{
  THIS_WRITELOCK (this);
  this->min_playoutrate = min_playoutrate;
  THIS_WRITEUNLOCK (this);
}

void
packetsrcvqueue_set_max_playoutrate(PacketsRcvQueue *this, GstClockTime max_playoutrate)
{
  THIS_WRITELOCK (this);
  this->max_playoutrate = max_playoutrate;
  THIS_WRITEUNLOCK (this);
}

void
packetsrcvqueue_flush(PacketsRcvQueue *this)
{
  THIS_WRITELOCK (this);
  this->flush = TRUE;
  THIS_WRITEUNLOCK (this);
}

void
packetsrcvqueue_set_spread_factor(PacketsRcvQueue *this, gdouble spread_factor)
{
  THIS_WRITELOCK (this);
  this->spread_factor = spread_factor;
  THIS_WRITEUNLOCK (this);
}

GstClockTime
packetsrcvqueue_get_playout_point(PacketsRcvQueue *this)
{
  GstClockTime result;
  gint actual_framenum;
  gdouble factor;
  THIS_WRITELOCK (this);
  actual_framenum = g_queue_get_length(this->frames);
  if(!this->sampling_t1 || !this->sampling_t2){
    result = _now(this) + this->max_playoutrate;
    goto done;
  }
  if(this->sampling_t2 < this->sampling_t1){
    percentiletracker_add(this->dsampling, this->sampling_t1 - this->sampling_t2);
  }
  if(!actual_framenum){
    result = _now(this) + this->max_playoutrate;
    goto done;
  }
  factor = pow(this->spread_factor, this->desired_framenum - actual_framenum);
  this->playout_rate = CONSTRAIN(this->min_playoutrate, this->max_playoutrate, this->mean_rate * factor);
  result = _now(this) + this->playout_rate;
done:
  THIS_WRITEUNLOCK (this);
  return result;
}



void packetsrcvqueue_push(PacketsRcvQueue *this, GstMpRTPBuffer *mprtp)
{
  Frame *frame;
  GList *it;
  THIS_WRITELOCK(this);
  if(g_queue_is_empty(this->frames)){
    this->bytes_in_normal_queue += mprtp->payload_bytes;
    g_queue_push_tail(this->frames, _make_frame(this, mprtp));
    goto done;
  }
  if(this->played_timestamp && _cmp_uint32(mprtp->timestamp, this->played_timestamp) < 0){
    this->bytes_in_urgent_queue += mprtp->payload_bytes;
    g_queue_push_tail(this->urgent, mprtp);
    goto done;
  }
  this->bytes_in_normal_queue += mprtp->payload_bytes;
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

//Call after the playout point reached
void packetsrcvqueue_refresh(PacketsRcvQueue *this)
{
  GList *it = NULL;
  Frame *head;
  FrameNode* node;
  GstClockTime sndsum = 0;
  guint sndnum = 0;
  THIS_WRITELOCK(this);
again:
  if(g_queue_is_empty(this->frames)){
    this->flush = FALSE;
    goto done;
  }
  head = g_queue_pop_head(this->frames);
  for(it = head->nodes; it; it = it->next){
    node = it->data;
    sndsum += get_epoch_time_from_ntp_in_ns(node->mprtp->abs_snd_ntp_time);
    ++sndnum;
    g_queue_push_tail(this->normal, node->mprtp);
    g_slice_free(FrameNode, node);
  }
  this->sampling_t2 = this->sampling_t1;
  this->sampling_t1 = sndsum / sndnum;
  g_slice_free(Frame, head);
  if(this->flush){
    goto again;
  }
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
  this->bytes_in_normal_queue -= result->payload_bytes;
  this->played_timestamp = result->timestamp;
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
  this->bytes_in_urgent_queue -= result->payload_bytes;
done:
  THIS_WRITEUNLOCK(this);
  return result;
}

Frame* _make_frame(PacketsRcvQueue *this, GstMpRTPBuffer *mprtp)
{
  Frame *result;
  result = g_slice_new0(Frame);
  result->added = _now(this);
  result->timestamp = mprtp->timestamp;
  result->nodes = g_list_prepend(result->nodes, _make_framenode(this, mprtp));
  return result;
}

FrameNode* _make_framenode(PacketsRcvQueue *this, GstMpRTPBuffer *mprtp)
{
  FrameNode *result;
  result = g_slice_new0(FrameNode);
  result->seq = mprtp->abs_seq;
  result->mprtp = mprtp;
  return result;
}

void _csv_logging(gpointer data)
{
  PacketsRcvQueue *this = data;
    mprtp_logger("packetsrcvqueue.csv",
                 "%d,%d,%d,%lu\n"
                 ,
                 g_queue_get_length(this->frames),
                 this->bytes_in_normal_queue,
                 this->bytes_in_urgent_queue,
                 this->playout_rate
                 );
}

void _readable_logging(gpointer data)
{
  PacketsRcvQueue *this = data;
  mprtp_logger("packetsrcvqueue.log",
               "----------------------------------------------------\n"
               "Seconds: %lu\n"
               "Min playoutrate: %lu\n"
               "Max playoutrate: %lu\n"
               "Mean playoutrate: %lu\n"
               "Actual playoutrate: %lu\n"
               "desired framenum: %d\n"
               "actual framenum: %d\n"
               "bytes in normal queue: %d\n"
               "bytes in urgent queue: %d\n"
               "Flush: %d\n"
               "Playout allowed: %d\n"

               ,

               GST_TIME_AS_SECONDS(_now(this) - this->made),
               this->min_playoutrate,
               this->max_playoutrate,
               this->mean_rate,
               this->playout_rate,
               this->desired_framenum,
               g_queue_get_length(this->frames),
               this->bytes_in_normal_queue,
               this->bytes_in_urgent_queue,
               this->flush,
               this->playout_allowed
               );

}

#undef DEBUG_PRINT_TOOLS
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
