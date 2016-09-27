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

#define _now(this) gst_clock_get_time (this->sysclock)

GST_DEBUG_CATEGORY_STATIC (fbrafbproducer_debug_category);
#define GST_CAT_DEFAULT fbrafbproducer_debug_category

G_DEFINE_TYPE (FBRAFBProducer, fbrafbproducer, G_TYPE_OBJECT);



static void fbrafbproducer_finalize (GObject * object);
static void _on_received_packet(FBRAFBProducer *this, RTPPacket *packet);
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
PercentileResultPipeFnc(_owd_percentile_pipe, FBRAFBProducer, median_delay, min_delay, max_delay, RTPPacket, received.delay, 0);

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
  this->sysclock = gst_system_clock_obtain();

  this->vector   = mprtp_malloc(sizeof(gboolean)  * 1000);
  this->vector_length = 0;

}

FBRAFBProducer *make_fbrafbproducer(guint32 ssrc,
    guint8 subflow_id,
    RcvTracker *tracker)
{
  FBRAFBProducer *this;
  this = g_object_new (FBRAFBPRODUCER_TYPE, NULL);
  this->ssrc = ssrc;
  this->subflow_id      = subflow_id;
  this->tracker         = g_object_ref(tracker);
  this->owds_sw         = make_slidingwindow_uint64(20, 200 * GST_MSECOND);

  slidingwindow_add_plugin(this->owds_sw,
      make_swpercentile(50, bintree3cmp_uint64, _owd_percentile_pipe, this));

  rcvtracker_subflow_add_on_received_packet_cb(this->tracker, subflow_id,
        (NotifierFunc) _on_received_packet, this);

  rcvtracker_subflow_add_on_received_packet_cb(this->tracker, subflow_id,
        (NotifierFunc) slidingwindow_add_data, this->owds_sw);

  slidingwindow_add_pipes()

  rcvtracker_subflow_add_on_lost_packet_cb(this->tracker, subflow_id,
      _on_packet_lost, this);
  return this;
}

void fbrafbproducer_reset(FBRAFBProducer *this)
{
  this->initialized = FALSE;
}

void fbrafbproducer_set_owd_treshold(FBRAFBProducer *this, GstClockTime treshold)
{
  slidingwindow_set_treshold(this->owds_sw, treshold);
}

void _on_received_packet(FBRAFBProducer *this, RTPPacket *packet)
{

  slidingwindow_add_data(this->owds_sw, &pa->delay);

  if(!this->initialized){
    this->initialized = TRUE;
    this->begin_seq = this->end_seq = packet->subflow_seq;
    goto done;
  }

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
  return;
}

void fbrafbproducer_setup_feedback(gpointer data, ReportProducer *reportprod)
{
  FBRAFBProducer *this;
  this = data;

  _setup_xr_owd(this, reportprod);
  _setup_xr_rfc3611_rle_lost(this, reportprod);
}

gboolean fbrafbproducer_do_fb(gpointer data)
{
  gboolean result = FALSE;
  FBRAFBProducer *this;
  this = data;
  if(_now(this) < this->last_fb + 19 * GST_MSECOND){
    goto done;
  }
  result = 4 < this->rcved_packets || (0 < this->next_fb && this->next_fb < _now(this));
done:
  return result;
}

void fbrafbproducer_fb_sent(gpointer data)
{
  FBRAFBProducer *this;
  this = data;
  this->last_fb = _now(this);
  this->rcved_packets = 0;
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
