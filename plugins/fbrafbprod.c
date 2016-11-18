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
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>     /* qsort */
#include "fbrafbprod.h"
#include "reportprod.h"

#define _now(this) gst_clock_get_time (this->sysclock)

GST_DEBUG_CATEGORY_STATIC (fbrafbproducer_debug_category);
#define GST_CAT_DEFAULT fbrafbproducer_debug_category

G_DEFINE_TYPE (FBRAFBProducer, fbrafbproducer, G_TYPE_OBJECT);



static void fbrafbproducer_finalize (GObject * object);
static gboolean _do_fb(FBRAFBProducer* data);;
static gboolean _receive_packet_filter(FBRAFBProducer *this, RcvPacket *packet);
static void _on_received_packet(FBRAFBProducer *this, RcvPacket *packet);
static void _setup_xr_rfc3611_rle_lost(FBRAFBProducer * this,  ReportProducer* reportproducer);
static void _setup_xr_owd(FBRAFBProducer * this,  ReportProducer* reportproducer);
//static void _setup_afb_reps(FBRAFBProducer * this, ReportProducer *reportproducer);
static void _on_fb_update(FBRAFBProducer *this,  ReportProducer* reportproducer);

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


static guint16 _diff_seq(guint16 a, guint16 b)
{
  if(a < b) return b-a;
  if(b < a) return (65536 - a) + b;
  return 0;
}

static void _on_rle_sw_rem(FBRAFBProducer* this, guint16* seq_num)
{
  this->vector[*seq_num] = FALSE;
  if(_cmp_seq(this->begin_seq, *seq_num) <= 0){
    this->begin_seq = *seq_num + 1;
  }
}

//PercentileResultPipeFnc(_owd_percentile_pipe, FBRAFBProducer, median_delay, min_delay, max_delay, RcvPacket, delay, 0);
PercentileRawResultPipeFnc(_owd_percentile_pipe, FBRAFBProducer, GstClockTime, median_delay, min_delay, max_delay, 0);

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

  rcvtracker_rem_on_received_packet_listener(this->tracker,  (ListenerFunc)_on_received_packet);
  rcvsubflow_rem_on_rtcp_fb_cb(this->subflow, (ListenerFunc) _on_fb_update);

  g_object_unref(this->sysclock);
  g_object_unref(this->tracker);
  mprtp_free(this->vector);


}

void
fbrafbproducer_init (FBRAFBProducer * this)
{
  this->sysclock = gst_system_clock_obtain();

  this->vector   = g_malloc0(sizeof(gboolean)  * 65536);
  this->vector_length = 0;

}

//static void _owd_sprint(gpointer item, gchar *result)
//{
//  sprintf(result, "%lu", GST_TIME_AS_MSECONDS(*(guint64*)item));
//}

FBRAFBProducer *make_fbrafbproducer(RcvSubflow* subflow, RcvTracker *tracker)
{
  FBRAFBProducer *this;
  this = g_object_new (FBRAFBPRODUCER_TYPE, NULL);
  this->subflow         = subflow;
  this->tracker         = g_object_ref(tracker);
  this->owds_sw         = make_slidingwindow_uint64(20, 100 * GST_MSECOND);

  this->rle_sw          = make_slidingwindow_uint16(100, GST_SECOND);

  slidingwindow_add_on_rem_item_cb(this->rle_sw, (ListenerFunc) _on_rle_sw_rem, this);

//  slidingwindow_add_plugin(this->owds_sw, make_swprinter(_owd_sprint));
  slidingwindow_add_plugin(this->owds_sw,
      make_swpercentile(50, bintree3cmp_uint64, (ListenerFunc)_owd_percentile_pipe, this));

  rcvtracker_add_on_received_packet_listener_with_filter(this->tracker,
      (ListenerFunc) _on_received_packet,
      (ListenerFilterFunc) _receive_packet_filter,
      this);

  rcvsubflow_add_on_rtcp_fb_cb(subflow, (ListenerFunc) _on_fb_update, this);

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

gboolean _receive_packet_filter(FBRAFBProducer *this, RcvPacket *packet)
{
  return packet->subflow_id == this->subflow->id;
}

void _on_received_packet(FBRAFBProducer *this, RcvPacket *packet)
{

  slidingwindow_add_data(this->owds_sw, &packet->delay);
  slidingwindow_add_data(this->rle_sw,  &packet->subflow_seq);

  ++this->rcved_packets;
  this->vector[packet->subflow_seq] = TRUE;
  if(!this->initialized){
    this->initialized = TRUE;
    this->begin_seq = this->end_seq = packet->subflow_seq;
    goto done;
  }

  if(_cmp_seq(this->end_seq, packet->subflow_seq) < 0){
    this->end_seq = packet->subflow_seq;
  }

done:
  return;
}


static gboolean _do_fb(FBRAFBProducer *this)
{
  GstClockTime now = _now(this);
  if(now - 20 * GST_MSECOND < this->last_fb){
    return FALSE;
  }
  if(this->last_fb < _now(this) - 100 * GST_MSECOND){
    return TRUE;
  }
  return 3 < this->rcved_packets;
}


void _on_fb_update(FBRAFBProducer *this, ReportProducer* reportproducer)
{
  if(!_do_fb(this)){
    goto done;
  }


  report_producer_begin(reportproducer, this->subflow->id);
  _setup_xr_owd(this, reportproducer);
  _setup_xr_rfc3611_rle_lost(this, reportproducer);

  this->last_fb = _now(this);
  this->rcved_packets = 0;
done:
  return;
}


void _setup_xr_rfc3611_rle_lost(FBRAFBProducer * this,ReportProducer* reportproducer)
{

  if(_cmp_seq(this->end_seq, this->begin_seq) <= 0){
    goto done;
  }

  this->vector_length = _diff_seq(this->begin_seq, this->end_seq) + 1;

  report_producer_add_xr_lost_rle(reportproducer,
                                       FALSE,
                                       0,
                                       this->begin_seq,
                                       this->end_seq,
                                       this->vector + this->begin_seq,
                                       this->vector_length
                                       );

//  g_print("FB creating begin seq: %d end seq: %d, vector length: %d\n", this->begin_seq, this->end_seq, this->vector_length);
  //BAD!
//  memset(this->vector, 0, sizeof(gboolean) * 65536);
//  if(_cmp_seq(this->begin_seq, this->end_seq) < 0){
//    this->begin_seq = this->end_seq + 1;
//  }
done:
  return;
}



void _setup_xr_owd(FBRAFBProducer * this, ReportProducer* reportproducer)
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
