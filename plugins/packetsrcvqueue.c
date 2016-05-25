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


//typedef struct _FrameNode{
//  GstMpRTPBuffer* mprtp;
//  guint16         seq;
//  gboolean        marker;
//}FrameNode;

//typedef struct _Frame
//{
//  guint32         timestamp;
//  gboolean        ready;
//  gboolean        marked;
//  gboolean        intact;
//  guint32         last_seq;
//  GstClockTime    added;
//  GList*          nodes;
//}Frame;

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


GST_DEBUG_CATEGORY_STATIC (packetsrcvqueue_debug_category);
#define GST_CAT_DEFAULT packetsrcvqueue_debug_category

G_DEFINE_TYPE (PacketsRcvQueue, packetsrcvqueue, G_TYPE_OBJECT);


//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void packetsrcvqueue_finalize (GObject * object);
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
  this->urgent = g_queue_new();
  this->normal = g_queue_new();

  this->dsampling = make_percentiletracker(100, 50);
  percentiletracker_set_treshold(this->dsampling, 60 * GST_SECOND);
  percentiletracker_set_stats_pipe(this->dsampling, _samplings_stat_pipe, this);
  this->desired_framenum = 1;
  this->min_playoutrate = .01 * GST_SECOND;
  this->max_playoutrate =  .04 * GST_SECOND;
  this->spread_factor = 2.;

  DISABLE_LINE mprtp_logger_add_logging_fnc(_csv_logging,this, 1, &this->rwmutex);
  DISABLE_LINE mprtp_logger_add_logging_fnc(_readable_logging,this, 10, &this->rwmutex);

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
  gdouble factor = 1.0;
  THIS_WRITELOCK (this);
  if(this->sampling_checked < this->sampling_updated){
    percentiletracker_add(this->dsampling, this->sampling_t1 - this->sampling_t2);
  }
  this->sampling_checked = _now(this);
  if(!this->mean_rate){
    this->last_playout_point = _now(this) + this->max_playoutrate;
    goto done;
  }

  if(this->last_normal_pushed < this->last_playout_point){
    factor = this->spread_factor;
  }

  this->playout_rate = CONSTRAIN(this->min_playoutrate, this->max_playoutrate, this->mean_rate * factor);
  this->last_playout_point = _now(this) + this->playout_rate;
done:
//  g_print("mean rate: %lu mn: %lu mx: %lu play: %lu => factor: %f\n",
//          this->mean_rate,
//          this->min_playoutrate,
//          this->max_playoutrate,
//          this->playout_rate,
//          factor);
  THIS_WRITEUNLOCK (this);
  return this->last_playout_point;
}

void packetsrcvqueue_push_urgent(PacketsRcvQueue *this, GstMpRTPBuffer *mprtp)
{
  THIS_WRITELOCK(this);
  g_queue_push_tail(this->urgent, mprtp);
  THIS_WRITEUNLOCK(this);
}

static gint _mprtp_queue_sort_helper(gconstpointer a, gconstpointer b, gpointer user_data)
{
  const GstMpRTPBuffer *ai = a;
  const GstMpRTPBuffer *bi = b;
  return _cmp_uint16(ai->abs_seq, bi->abs_seq);
}

void packetsrcvqueue_push(PacketsRcvQueue *this, GstMpRTPBuffer *mprtp)
{
  GstMpRTPBuffer *item;
  THIS_WRITELOCK(this);
  //Decide weather it is urgent or not
  if(g_queue_is_empty(this->normal)){
    this->last_normal_pushed = _now(this);
    g_queue_push_tail(this->normal, mprtp);
    goto done;
  }
  item = g_queue_peek_head(this->normal);
  if(_cmp_uint32(mprtp->timestamp, item->timestamp) < 0){
    g_queue_push_tail(this->urgent, mprtp);
    goto done;
  }
  this->last_normal_pushed = _now(this);
  item = g_queue_peek_tail(this->normal);
  if(_cmp_uint16(item->abs_seq + 1, mprtp->abs_seq) == 0){
    g_queue_push_tail(this->normal, mprtp);
  }else{
    g_queue_insert_sorted(this->normal, mprtp, _mprtp_queue_sort_helper, NULL);
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
  if(this->played_timestamp == result->timestamp){
    this->sndsum += get_epoch_time_from_ntp_in_ns(result->abs_snd_ntp_time);
    ++this->sndnum;
  }else{
    this->sampling_t2 = this->sampling_t1;
    this->sampling_t1 = this->sndsum / (gdouble)this->sndnum;
    this->sndnum = 0.;
    this->sndsum = 0;
    this->sampling_updated = this->sampling_t1 && this->sampling_t2 ? _now(this) : 0;
  }
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
done:
  THIS_WRITEUNLOCK(this);
  return result;
}

void _csv_logging(gpointer data)
{
  PacketsRcvQueue *this = data;
    mprtp_logger("packetsrcvqueue.csv",
                 "%lu\n"
                 ,
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
               "Flush: %d\n"
               "Playout allowed: %d\n"

               ,

               GST_TIME_AS_SECONDS(_now(this) - this->made),
               this->min_playoutrate,
               this->max_playoutrate,
               this->mean_rate,
               this->playout_rate,
               this->desired_framenum,
               this->flush,
               this->playout_allowed
               );

}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
