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
#include "fbrafbprod.h"
#include "mprtplogger.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>     /* qsort */

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

//#define THIS_READLOCK(this)
//#define THIS_READUNLOCK(this)
//#define THIS_WRITELOCK(this)
//#define THIS_WRITEUNLOCK(this)

#define _now(this) gst_clock_get_time (this->sysclock)

GST_DEBUG_CATEGORY_STATIC (fbrafbproducer_debug_category);
#define GST_CAT_DEFAULT fbrafbproducer_debug_category

G_DEFINE_TYPE (FBRAFBProducer, fbrafbproducer, G_TYPE_OBJECT);


struct _CorrBlock{
  guint           id,N;
  gdouble         Iu0,Iu1,Id1,Id2,Id3,G01,M0,M1,G_[64],M_[64];
  gint            index;
  gdouble         g;
  CorrBlock*      next;
};


static void fbrafbproducer_finalize (GObject * object);

static void _setup_xr_rfc7097(FBRAFBProducer * this, ReportProducer *reportproducer);
static void _setup_xr_owd(FBRAFBProducer * this, ReportProducer *reportproducer);
static void _setup_afb_reps(FBRAFBProducer * this, ReportProducer *reportproducer);
static void _execute_corrblocks(FBRAFBProducer *this, CorrBlock *blocks);
static void _execute_corrblock(CorrBlock* this);

#define _trackerstat(this) this->trackerstat


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
//
//static gint
//_cmp_timestamp (guint32 x, guint32 y)
//{
//  if(x == y) return 0;
//  if(x < y && y - x < 2147483648) return -1;
//  if(x > y && x - y > 2147483648) return -1;
//  if(x < y && y - x > 2147483648) return 1;
//  if(x > y && x - y < 2147483648) return 1;
//  return 0;
//}

static void _owd_stt_pipe(gpointer data, PercentileTrackerPipeData *stats)
{
  FBRAFBProducer *this = data;
  this->median_delay = stats->percentile;
  this->min_delay    = stats->min;
  this->max_delay    = stats->max;
}

static void _stat_pipe(gpointer data, NumsTrackerStatData *stats)
{
  FBRAFBProducer *this = data;
  this->stability = 1.;
  if(0. < stats->avg){
    this->stability = 1. - stats->avg;
  }
  this->sampling_num = MIN(255, stats->num);
}


void
fbrafbproducer_class_init (FBRAFBProducerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fbrafbproducer_finalize;

  GST_DEBUG_CATEGORY_INIT (fbrafbproducer_debug_category, "fbrafbproducer", 0,
      "FBRAFBProducer");

}

void
fbrafbproducer_finalize (GObject * object)
{
  FBRAFBProducer *this;
  this = FBRAFBPRODUCER(object);
  g_object_unref(this->sysclock);
  mprtp_free(this->vector);
}

void
fbrafbproducer_init (FBRAFBProducer * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sysclock = gst_system_clock_obtain();

  this->received_bytes = make_numstracker(1000, GST_SECOND);
  this->owd_stt  = make_percentiletracker(1000, 50);
  percentiletracker_set_treshold(this->owd_stt, 200 * GST_MSECOND);
  percentiletracker_set_stats_pipe(this->owd_stt, _owd_stt_pipe, this);

  this->vector   = mprtp_malloc(sizeof(gboolean)  * 1000);
  this->vector_length = 0;

  this->devars = make_numstracker(200, 2 * GST_SECOND);
  numstracker_add_plugin(this->devars, (NumsTrackerPlugin*)make_numstracker_stat_plugin(_stat_pipe, this));

}

FBRAFBProducer *make_fbrafbproducer(guint32 ssrc)
{
    FBRAFBProducer *this;
    this = g_object_new (FBRAFBPRODUCER_TYPE, NULL);
    this->ssrc = ssrc;
    return this;
}

void fbrafbproducer_reset(FBRAFBProducer *this)
{
  THIS_WRITELOCK (this);
  this->initialized = FALSE;
  THIS_WRITEUNLOCK (this);
}

void fbrafbproducer_set_owd_treshold(FBRAFBProducer *this, GstClockTime treshold)
{
  THIS_WRITELOCK (this);
  percentiletracker_set_treshold(this->owd_stt, treshold);
  THIS_WRITEUNLOCK (this);
}


void fbrafbproducer_track(gpointer data, GstMpRTPBuffer *mprtp)
{
  FBRAFBProducer *this;
  gint devar;

  this = data;
  THIS_WRITELOCK (this);

  devar = this->median_delay < mprtp->delay ? 1 : -1;
  numstracker_add(this->devars, devar);
  percentiletracker_add(this->owd_stt, mprtp->delay);

  if(!this->initialized){
    this->initialized = TRUE;
    this->begin_seq = this->end_seq = mprtp->subflow_seq;
    goto done;
  }

  numstracker_add(this->received_bytes, mprtp->payload_bytes);

  if(_cmp_seq(mprtp->subflow_seq, this->end_seq) <= 0){
    goto done;
  }

  if(_cmp_seq(this->end_seq + 1, mprtp->subflow_seq) < 0){
    guint16 seq = this->end_seq + 1;
    for(; _cmp_seq(seq, mprtp->subflow_seq) < 0; ++seq){
      this->vector[this->vector_length++] = FALSE;
    }
  }
  this->vector[this->vector_length++] = TRUE;
  this->end_seq = mprtp->subflow_seq;

done:
  THIS_WRITEUNLOCK (this);
}

void fbrafbproducer_setup_feedback(gpointer data, ReportProducer *reportprod)
{
  FBRAFBProducer *this;
  this = data;
  THIS_WRITELOCK (this);

  _setup_xr_owd(this, reportprod);
  _setup_xr_rfc7097(this, reportprod);
  _setup_afb_reps(this, reportprod);

  THIS_WRITEUNLOCK (this);
}

GstClockTime fbrafbproducer_get_interval(gpointer data)
{
  gint64 received_bytes = 0;
  GstClockTime result;
  FBRAFBProducer *this;
  this = data;
  THIS_READLOCK (this);
  numstracker_get_stats(this->received_bytes, &received_bytes);
  result = (1.0/MIN(50,MAX(10,(received_bytes * 8)/20000))) * GST_SECOND;
  THIS_READUNLOCK(this);
  return result;
}

void _setup_xr_rfc7097(FBRAFBProducer * this, ReportProducer *reportproducer)
{
  report_producer_add_xr_discarded_rle(reportproducer,
                                       FALSE,
                                       0,
                                       this->begin_seq,
                                       this->end_seq,
                                       this->vector,
                                       this->vector_length
                                       );

  memset(this->vector, 0, sizeof(gboolean) * 1000);
  this->vector_length = 0;
  if(_cmp_seq(this->begin_seq, this->end_seq) < 0){
    this->begin_seq = this->end_seq + 1;
  }
}

void _setup_xr_owd(FBRAFBProducer * this, ReportProducer *reportproducer)
{
  guint32      u32_median_delay, u32_min_delay, u32_max_delay;

  u32_median_delay = (guint32)(get_ntp_from_epoch_ns(this->median_delay)>>16);
  u32_min_delay    = (guint32)(get_ntp_from_epoch_ns(this->min_delay)>>16);
  u32_max_delay    = (guint32)(get_ntp_from_epoch_ns(this->max_delay)>>16);

  report_producer_add_xr_owd(reportproducer,
                             RTCP_XR_RFC7243_I_FLAG_CUMULATIVE_DURATION,
                             u32_median_delay,
                             u32_min_delay,
                             u32_max_delay);
}

void _setup_afb_reps(FBRAFBProducer * this, ReportProducer *reportproducer)
{
  guint sampling_ok;
  gfloat estimation;
  sampling_ok = 1;
  report_producer_add_afb_reps(reportproducer, this->ssrc, &this->sampling_num, this->stability);

}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
