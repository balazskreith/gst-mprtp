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
#include <stdio.h>
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



static void fbrafbproducer_finalize (GObject * object);

static void _setup_xr_rfc3611_rle_lost(FBRAFBProducer * this, ReportProducer *reportproducer);
static void _setup_xr_owd(FBRAFBProducer * this, ReportProducer *reportproducer);
//static void _setup_afb_reps(FBRAFBProducer * this, ReportProducer *reportproducer);


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

static void _owd_percentile_pipe(gpointer data, swpercentilecandidates_t *candidates)
{
  FBRAFBProducer *this = data;
  if(!candidates->processed){
    return;
  }
  if(!candidates->left){
    this->median_delay = *(GstClockTime*)candidates->right;
  }else if(!candidates->right){
    this->median_delay = *(GstClockTime*)candidates->left;
  }else{
    this->median_delay = *(GstClockTime*)candidates->left;
    this->median_delay += *(GstClockTime*)candidates->right;
    this->median_delay>>=1;
  }

  this->min_delay = *(GstClockTime*)candidates->min;
  this->max_delay = *(GstClockTime*)candidates->max;
}

//static void _tendency_add_pipe(gpointer udata, gpointer itemptr)
//{
//  FBRAFBProducer* this;
//  gint32* item;
//  item = itemptr;
//  this = udata;
//
//  ++this->tendency.counter;
//  this->tendency.sum += *item;
//
//}
//
//static void _tendency_rem_pipe(gpointer udata, gpointer itemptr)
//{
//  FBRAFBProducer* this;
//  gint32* item;
//  item = itemptr;
//  this = udata;
//
//  --this->tendency.counter;
//  this->tendency.sum -= *item;
//
//}

static void _payloads_add_pipe(gpointer udata, gpointer itemptr)
{
  FBRAFBProducer* this;
  gint32* item;
  item = itemptr;
  this = udata;

  this->received_bytes += *item;
  ++this->rcved_packets;

}

static void _payloads_rem_pipe(gpointer udata, gpointer itemptr)
{
  FBRAFBProducer* this;
  gint32* item;
  item = itemptr;
  this = udata;

  this->received_bytes -= *item;

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

  this->vector   = mprtp_malloc(sizeof(gboolean)  * 1000);
  this->vector_length = 0;


  this->owds_sw         = make_slidingwindow_uint64(50, 200 * GST_MSECOND);
//  this->tendency_sw     = make_slidingwindow_int32(100, GST_SECOND);
  this->payloadbytes_sw = make_slidingwindow_int32(2000, GST_SECOND);

  slidingwindow_add_plugin(this->owds_sw,        make_swpercentile(50, bintree3cmp_uint64, _owd_percentile_pipe, this));
//  slidingwindow_add_pipes(this->tendency_sw,     _tendency_rem_pipe, this, _tendency_add_pipe, this);
  slidingwindow_add_pipes(this->payloadbytes_sw, _payloads_rem_pipe, this, _payloads_add_pipe, this);

}

FBRAFBProducer *make_fbrafbproducer(guint32 ssrc, guint8 subflow_id)
{
    FBRAFBProducer *this;
    this = g_object_new (FBRAFBPRODUCER_TYPE, NULL);
    this->ssrc = ssrc;
    this->subflow_id = subflow_id;
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
  slidingwindow_set_treshold(this->owds_sw, treshold);
  THIS_WRITEUNLOCK (this);
}

//static void _refresh_devar(FBRAFBProducer *this, GstMpRTPBuffer *mprtp)
//{
//  if(mprtp->delay <= this->median_delay){
//    slidingwindow_add_int(this->tendency_sw, -1);
//  }else{
//    slidingwindow_add_int(this->tendency_sw, 1);
//  }
//}

void fbrafbproducer_track(gpointer data, GstMpRTPBuffer *mprtp)
{
  FBRAFBProducer *this;


  this = data;
  THIS_WRITELOCK (this);

//  _refresh_devar(this, mprtp);
  slidingwindow_add_data(this->owds_sw, &mprtp->delay);

  if(!this->initialized){
    this->initialized = TRUE;
    this->begin_seq = this->end_seq = mprtp->subflow_seq;
    goto done;
  }

  slidingwindow_add_int(this->payloadbytes_sw, mprtp->payload_bytes);

  if(_cmp_seq(mprtp->subflow_seq, this->end_seq) <= 0){
    goto done;
  }

  if(_cmp_seq(this->end_seq + 1, mprtp->subflow_seq) < 0){
    guint16 seq = this->end_seq + 1;
    for(; _cmp_seq(seq, mprtp->subflow_seq) < 0; ++seq){
//      g_print("marked as discarded: %d\n", seq);
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
  _setup_xr_rfc3611_rle_lost(this, reportprod);
//  _setup_afb_reps(this, reportprod);

  THIS_WRITEUNLOCK (this);
}

gboolean fbrafbproducer_do_fb(gpointer data)
{
  gboolean result = FALSE;
  FBRAFBProducer *this;
  this = data;
  THIS_READLOCK(this);
  if(_now(this) < this->last_fb + 19 * GST_MSECOND){
    goto done;
  }
  result = 4 < this->rcved_packets || (0 < this->next_fb && this->next_fb < _now(this));
done:
  THIS_READUNLOCK(this);
  return result;
}

void fbrafbproducer_fb_sent(gpointer data)
{
  FBRAFBProducer *this;
  this = data;
  THIS_WRITELOCK(this);
  this->last_fb = _now(this);
  this->next_fb = this->last_fb + 100 * GST_MSECOND;
  this->rcved_packets = 0;
//  slidingwindow_set_act_limit(this->tendency_sw, (this->received_bytes * 8) / 50000);
  THIS_WRITEUNLOCK(this);
}

void _setup_xr_rfc3611_rle_lost(FBRAFBProducer * this, ReportProducer *reportproducer)
{
  report_producer_add_xr_lost_rle(reportproducer,
                                       FALSE,
                                       0,
                                       this->begin_seq,
                                       this->end_seq,
                                       this->vector,
                                       this->vector_length
                                       );

//  g_print("FB creating begin seq: %d end seq: %d, vector length: %d\n", this->begin_seq, this->end_seq, this->vector_length);
  memset(this->vector, 0, sizeof(gboolean) * 1000);
//  if(_cmp_seq(this->begin_seq, this->end_seq) < 0){
//    this->begin_seq = this->end_seq + 1;
//  }
  if(0 < this->vector_length){
    this->begin_seq = this->end_seq + 1;
    this->vector_length = 0;
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

//void _setup_afb_reps(FBRAFBProducer * this, ReportProducer *reportproducer)
//{
//  guint8 sampling_num;
//  gfloat tendency = 0.;
//  sampling_num = CONSTRAIN(0, 255, this->tendency.counter);
//  if(0 < sampling_num){
//    tendency = (gfloat) this->tendency.sum / (gfloat) this->tendency.counter;
//  }
//  report_producer_add_afb_reps(reportproducer, this->ssrc, sampling_num, tendency);
//}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
