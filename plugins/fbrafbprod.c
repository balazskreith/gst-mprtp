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

static void _refresh_delay_variation(FBRAFBProducer * this, GstMpRTPBuffer *mprtp);
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
  this->dev_delay = stats->dev;
  if(stats->avg < 0.){
      this->stability = 1.;
  }else{
    this->stability = 1. - stats->avg;
  }
  g_print("avg: %f| num: %u| sum:%ld| stability: %f|\n",
          stats->avg, stats->num, stats->sum, this->stability);
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
  mprtp_free(this->blocks);
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

  this->blocks   = mprtp_malloc(sizeof(CorrBlock) * 10);
  this->vector   = mprtp_malloc(sizeof(gboolean)  * 1000);
  this->vector_length = 0;
  {
    gint i;
    for(i=0; i < 6; ++i){
      this->blocks[i].next = &this->blocks[i + 1];
      this->blocks[i].id   = i;
//      this->blocks[i].N    = 64>>i;
      this->blocks[i].N    = 64>>i;
      this->blocks[i].N    = 5;
    }
  }
  this->block_index = 1;
  this->cblocks_counter = 1;

  this->devars = make_numstracker(1000, 2 * GST_SECOND);
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
  this = data;
  THIS_WRITELOCK (this);
  _refresh_delay_variation(this, mprtp);
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
//  sampling_ok = _cmp_seq(this->begin_seq, this->end_seq) <= 0 ? 1 : 0;
  sampling_ok = 1;
//  estimation = 1. - CONSTRAIN(0.0, 1.0, this->blocks[2].g);
//  estimation = 1. - CONSTRAIN(0.0, 1.0, this->blocks[2].g);
  estimation = this->values_sum < 0 ? 1.0 : 1.- (gdouble)this->values_sum / 100.;
//  g_print("estimation: %f\n", estimation);
  report_producer_add_afb_reps(reportproducer, this->ssrc, sampling_ok, estimation);

}


//void _refresh_delay_variation(FBRAFBProducer * this, GstMpRTPBuffer *mprtp)
//{
//  GstClockTime diff;
//  gint cmp;
//
//  if(this->devar.last_timestamp == 0){
//    this->devar.last_delay        = mprtp->delay;
//    this->devar.last_timestamp    = mprtp->timestamp;
//    return;
//  }
//
//  cmp = _cmp_timestamp(mprtp->timestamp, this->devar.last_timestamp);
//  if(cmp < 0){
//    return;
//  }
//
//  if(!cmp){
//    this->devar.last_delay = .5 * this->devar.last_delay +  .5 * mprtp->delay;
//    return;
//  }
//
//  if(!this->devar.last_delay_t1){
//    this->devar.last_delay_t1 = this->devar.last_delay;
//    return;
//  }
//
//  g_print("last delay_t1: %lu last delay: %lu timestamp: %u payload: %u",
//          this->devar.last_delay_t1,
//          this->devar.last_delay,
//          mprtp->timestamp,
//          mprtp->payload_bytes);
//
//  diff = this->devar.last_delay_t1 < this->devar.last_delay ?
//      this->devar.last_delay - this->devar.last_delay_t1 : this->devar.last_delay_t1 - this->devar.last_delay;
//  diff = GST_TIME_AS_MSECONDS(diff);
//
//  this->blocks[0].Iu0 = diff;
//  _execute_corrblocks(this, this->blocks);
//  _execute_corrblocks(this, this->blocks);
//  this->blocks[0].Id1 = diff;
//
//  g_print("diff: %ld| g0: %f| g1: %f| g2: %f| g3: %f\n",
//          diff,
//          this->blocks[0].g,
//          this->blocks[1].g,
//          this->blocks[2].g,
//          this->blocks[3].g);
//
//  this->devar.last_delay_t1  = this->devar.last_delay;
//  this->devar.last_delay     = mprtp->delay;
//  this->devar.last_timestamp = mprtp->timestamp;
//
//}

//
//void _refresh_delay_variation(FBRAFBProducer * this, GstMpRTPBuffer *mprtp)
//{
//  GstClockTime diff;
//
////  if(_now(this) - 10 * GST_MSECOND < this->devar.last_sampling){
////    return;
////  }
//
////  this->devar.last_sampling = _now(this);
////
////  if(!this->devar.last_median){
////    this->devar.last_median  = this->median_delay;
////    return;
////  }
//  //
//  //  diff = this->devar.last_median < this->median_delay ?
//  //      this->median_delay - this->devar.last_median :
//  //      this->devar.last_median - this->median_delay;
//
////    diff = mprtp->delay < this->median_delay ?
////        this->median_delay - mprtp->delay :
////        mprtp->delay - this->median_delay;
////  diff /= 1000;
//  diff = this->median_delay < mprtp->delay ? 1 : 0;
//  this->blocks[0].Iu0 = diff;
//  _execute_corrblocks(this, this->blocks);
//  _execute_corrblocks(this, this->blocks);
//  this->blocks[0].Id1 = diff;
//
//  this->devar.last_median = this->median_delay;
//
//  g_print("diff: %ld| g0: %f| g1: %f| g2: %f| g3: %f\n",
//          diff,
//          this->blocks[0].g,
//          this->blocks[1].g,
//          this->blocks[2].g,
//          this->blocks[3].g);
//
//}


void _refresh_delay_variation(FBRAFBProducer * this, GstMpRTPBuffer *mprtp)
{

  numstracker_add(this->devars, this->median_delay < mprtp->delay ? 1 : -1);

  this->values_sum -= this->values[this->values_index];
  this->values[this->values_index] = (this->median_delay < mprtp->delay) ? 1 : -1;
  this->values_sum += this->values[this->values_index];
  if(++this->values_index == 100){
    this->values_index = 0;
  }

  g_print("actual: %d| sum: %d|\n",  this->values[this->values_index], this->values_sum);
    this->blocks[0].Iu0 = 0;
    _execute_corrblocks(this, this->blocks);
    _execute_corrblocks(this, this->blocks);
    this->blocks[0].Id1 = 0;
}




void _execute_corrblocks(FBRAFBProducer *this, CorrBlock *blocks)
{
  guint32 X = (this->cblocks_counter ^ (this->cblocks_counter-1))+1;
  switch(X){
    case 2:
      _execute_corrblock(blocks);
    break;
    case 4:
      if(3 < this->cblocks_counter) _execute_corrblock(blocks + 1);
      break;
    case 8:
      if(22 < this->cblocks_counter) _execute_corrblock(blocks + 2);
      break;
    case 16:
      if(60 < this->cblocks_counter) _execute_corrblock(blocks + 3);
      break;
    case 32:
      if(136 < this->cblocks_counter) _execute_corrblock(blocks + 4);
      break;
    case 64:
      if(288 < this->cblocks_counter) _execute_corrblock(blocks + 5);
      break;
  }

  ++this->cblocks_counter;
}

void _execute_corrblock(CorrBlock* this)
{
  this->M1   = this->M0;
  this->M0  -= this->M_[this->index];
  this->G01 -= this->G_[this->index];
  this->M0  += this->M_[this->index] = this->Iu0;
  this->G01 += this->G_[this->index] = this->Iu0 * this->Id1;

  if(this->M0 && this->M1){
    this->g  = this->G01 / (gdouble)(this->N-1);
    this->g /= this->M0 / (gdouble)(this->N)  * this->M1 / (gdouble)(this->N-1);
    this->g -= 1.;
  }else{
    this->g = 0.;
  }
  if(++this->index == this->N) {
    this->index = 0;
  }

  if(this->next && this->id < 6){
    CorrBlock *next = this->next;
    next->Iu0 = this->Iu0 + this->Iu1;
    next->Id1 = this->Id2 + this->Id3;
  }

  this->Iu1  = this->Iu0;
  this->Id3  = this->Id2;
  this->Id2  = this->Id1;
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
