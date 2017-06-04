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
#include "fractalfbprod.h"
#include "reportprod.h"

#define _now(this) gst_clock_get_time (this->sysclock)

GST_DEBUG_CATEGORY_STATIC (fractalfbproducer_debug_category);
#define GST_CAT_DEFAULT fractalfbproducer_debug_category

G_DEFINE_TYPE (FRACTaLFBProducer, fractalfbproducer, G_TYPE_OBJECT);

static void fractalfbproducer_finalize (GObject * object);
static gboolean _do_fb(FRACTaLFBProducer* data);;
static gboolean _packet_subflow_filter(FRACTaLFBProducer *this, RcvPacket *packet);
static void _on_discarded_packet(FRACTaLFBProducer *this, RcvPacket *packet);
static void _on_received_packet(FRACTaLFBProducer *this, RcvPacket *packet);
static void _setup_xr_rfc7243(FRACTaLFBProducer * this,ReportProducer* reportproducer);
static void _setup_xr_rfc3611_rle_lost(FRACTaLFBProducer * this,  ReportProducer* reportproducer);
static void _setup_xr_owd(FRACTaLFBProducer * this,  ReportProducer* reportproducer);
static void _setup_xr_cc_fb_rle(FRACTaLFBProducer * this,  ReportProducer* reportproducer);
//static void _setup_afb_reps(FRACTALFBProducer * this, ReportProducer *reportproducer);
static void _on_fb_update(FRACTaLFBProducer *this,  ReportProducer* reportproducer);

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

static guint32 _delta_ts(guint32 last_ts, guint32 actual_ts) {
  if (last_ts <= actual_ts) {
    return actual_ts - last_ts;
  } else {
    return 4294967296 - last_ts + actual_ts;
  }
}

//
//static guint16 _diff_seq(guint16 a, guint16 b)
//{
//  if(a < b) return b-a;
//  if(b < a) return (65536 - a) + b;
//  return 0;
//}

static void _on_rle_sw_rem(FRACTaLFBProducer* this, guint16* seq_num)
{
  this->lost_vector[*seq_num] = FALSE;
  if(_cmp_seq(this->begin_seq, *seq_num) <= 0){
    this->begin_seq = *seq_num + 1;
  }
}

void
fractalfbproducer_class_init (FRACTaLFBProducerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fractalfbproducer_finalize;

  GST_DEBUG_CATEGORY_INIT (fractalfbproducer_debug_category, "fractalfbproducer", 0,
      "FRACTALFBProducer");

}

void
fractalfbproducer_finalize (GObject * object)
{
  FRACTaLFBProducer *this;
  this = FRACTALFBPRODUCER(object);

  rcvtracker_rem_on_received_packet_listener(this->tracker,  (ListenerFunc)_on_received_packet);
  rcvsubflow_rem_on_rtcp_fb_cb(this->subflow, (ListenerFunc) _on_fb_update);

  g_object_unref(this->sysclock);
  g_object_unref(this->tracker);
  mprtp_free(this->lost_vector);
  mprtp_free(this->ecn_vector);
  mprtp_free(this->timestamp_vector);
  mprtp_free(this->ato_vector);


}

void
fractalfbproducer_init (FRACTaLFBProducer * this)
{
  this->sysclock = gst_system_clock_obtain();

  this->lost_vector = g_malloc0(sizeof(gboolean)  * 65536);
  this->ecn_vector = g_malloc0(sizeof(gboolean)  * 65536);
  this->timestamp_vector = g_malloc0(sizeof(guint32) * 65536);
  this->ato_vector = g_malloc0(sizeof(guint32) * 65536);

}


typedef struct{
  guint64 dsnd;
  guint64 drcv;
  gdouble payload_size;
}Skew;

DEFINE_RECYCLE_TYPE(static, skew_data, Skew);

static void _skew_shaper(Skew* to, Skew* from){
  memcpy(to,from,sizeof(Skew));
}

static void _on_skew_add(FRACTaLFBProducer* this, Skew* skew)
{
  if(skew->dsnd <= skew->drcv){
    this->raise += skew->drcv - skew->dsnd;
  }else{
    this->fall += skew->dsnd - skew->drcv;
  }
//  g_print("_on_skew_add\n");
}

static void _on_skew_rem(FRACTaLFBProducer* this, Skew* skew)
{
  if(skew->dsnd <= skew->drcv){
    this->raise -= skew->drcv - skew->dsnd;
  }else{
    this->fall -= skew->dsnd - skew->drcv;
  }
//  g_print("_on_skew_rem\n");
}


FRACTaLFBProducer *make_fractalfbproducer(RcvSubflow* subflow, RcvTracker *tracker)
{
  FRACTaLFBProducer *this;
  this = g_object_new (FRACTALFBPRODUCER_TYPE, NULL);
  this->subflow         = subflow;
  this->tracker         = g_object_ref(tracker);
  this->ts_generator    = g_object_ref(rcvtracker_get_cc_ts_generator(tracker));
  this->rle_sw          = make_slidingwindow_uint16(100, .33 * GST_SECOND);


  slidingwindow_add_on_rem_item_cb(this->rle_sw, (ListenerFunc) _on_rle_sw_rem, this);

  rcvtracker_add_on_received_packet_listener_with_filter(this->tracker,
      (ListenerFunc) _on_received_packet,
      (ListenerFilterFunc) _packet_subflow_filter,
      this);

  rcvtracker_add_on_discarded_packet_listener_with_filter(this->tracker,
      (ListenerFunc) _on_discarded_packet,
      (ListenerFilterFunc) _packet_subflow_filter,
      this);

  rcvsubflow_add_on_rtcp_fb_cb(subflow, (ListenerFunc) _on_fb_update, this);

  this->skew_recycle = make_recycle_skew_data(100, (RecycleItemShaper) _skew_shaper);
  this->skew_sw = make_slidingwindow(500, 200 * GST_MSECOND);
  slidingwindow_set_data_recycle(this->skew_sw, this->skew_recycle);

  slidingwindow_add_on_change(this->skew_sw, (ListenerFunc)_on_skew_add, (ListenerFunc)_on_skew_rem, this);

  return this;
}

void fractalfbproducer_reset(FRACTaLFBProducer *this)
{
  this->initialized = FALSE;
}

gboolean _packet_subflow_filter(FRACTaLFBProducer *this, RcvPacket *packet)
{
  return packet->subflow_id == this->subflow->id;
}

void _on_discarded_packet(FRACTaLFBProducer *this, RcvPacket *packet)
{
  this->discarded_bytes += packet->payload_size;
}

static void _add_new_skew(FRACTaLFBProducer *this, RcvPacket* packet)
{
  gdouble drcv, dsnd;
  if(this->prev_rcv == 0){
    this->prev_rcv      = packet->abs_rcv_ntp_time;
    this->prev_snd      = packet->abs_snd_ntp_chunk;
    this->prev_seq      = packet->abs_seq;
    return;
  }

  if(0 < _cmp_seq(this->prev_seq, packet->abs_seq)){
    return;
  }
  this->prev_seq = packet->abs_seq;

  if(packet->abs_snd_ntp_chunk < this->prev_snd){//turnaround
    dsnd = (gint64)(0x0000004000000000ULL - this->prev_snd) + (gint64)packet->abs_snd_ntp_chunk;
  }else{
    dsnd = (gint64)packet->abs_snd_ntp_chunk - (gint64)this->prev_snd;
  }
  drcv = (gint64)packet->abs_rcv_ntp_time - (gint64)this->prev_rcv;
  this->prev_rcv = packet->abs_rcv_ntp_time;
  this->prev_snd = packet->abs_snd_ntp_chunk;

  {
    Skew skew = {dsnd,drcv,(gdouble)packet->payload_size};
    slidingwindow_add_data(this->skew_sw, &skew);
//    g_print("%-5hu: %-10lu | %-10lu | %-10lu | %-10lu\n", packet->subflow_seq, skew.drcv, skew.dsnd, this->fall, this->raise);
  }

}

void _on_received_packet(FRACTaLFBProducer *this, RcvPacket *packet)
{
  _add_new_skew(this, packet);
  slidingwindow_add_data(this->rle_sw,  &packet->subflow_seq);

  ++this->rcved_packets;
  this->lost_vector[packet->subflow_seq] = TRUE;
  this->timestamp_vector[packet->subflow_seq] = timestamp_generator_get_ts(this->ts_generator);
//  g_print("%hu = %u %u\n", packet->subflow_seq, this->ato_vector[packet->subflow_seq], timestamp_generator_get_ts(this->ts_generator));
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


static gboolean _do_fb(FRACTaLFBProducer *this)
{
  GstClockTime now = _now(this);
  slidingwindow_refresh(this->skew_sw);

  if(now - 20 * GST_MSECOND < this->last_fb){
    return FALSE;
  }
  if(this->last_fb < _now(this) - 100 * GST_MSECOND){
    return TRUE;
  }
  return 3 < this->rcved_packets;
}


void _on_fb_update(FRACTaLFBProducer *this, ReportProducer* reportproducer)
{
  if(!_do_fb(this)){
    goto done;
  }
//PROFILING("report_producer_begin",
  report_producer_begin(reportproducer, this->subflow->id);
//);
  //Okay, so discarded byte metrics indicate incipient congestion,
  //which is in fact indicated by the qdelay distoration either.
  //another point is the fact that if competing with tcp, tcp pushes the netqueue
  //until its limitation, thus discard metrics always appear, and also
  //if jitter is high discard metrics appear naturally
  //so now on we try to not to rely on this metric, but for qdelay and losts.
  DISABLE_LINE _setup_xr_rfc7243(this, reportproducer);

//  PROFILING("_setup_xr_owd",
  _setup_xr_owd(this, reportproducer);
//  );

//  PROFILING("_setup_xr_rfc3611_rle_lost",
  DISABLE_LINE _setup_xr_rfc3611_rle_lost(this, reportproducer);
//  );

  _setup_xr_cc_fb_rle(this, reportproducer);

  this->last_fb = _now(this);
  this->rcved_packets = 0;
done:
  return;
}

void _setup_xr_rfc7243(FRACTaLFBProducer * this,ReportProducer* reportproducer)
{
  gboolean interval_metric_flag = TRUE;
  gboolean early_bit = FALSE;

  report_producer_add_xr_discarded_bytes(reportproducer,
                                         interval_metric_flag,
                                         early_bit,
                                         this->discarded_bytes
                                        );
  this->discarded_bytes = 0;
}

void _setup_xr_rfc3611_rle_lost(FRACTaLFBProducer * this,ReportProducer* reportproducer)
{

  if(_cmp_seq(this->end_seq, this->begin_seq) <= 0){
    goto done;
  }

  report_producer_add_xr_lost_rle(reportproducer,
                                       FALSE,
                                       0,
                                       this->begin_seq,
                                       this->end_seq,
                                       this->lost_vector
                                       );
done:
  return;
}

void _setup_xr_cc_fb_rle(FRACTaLFBProducer * this,  ReportProducer* reportproducer) {
  guint32 report_timestamp;
  guint16 act_seq;
  guint32 report_count = 1;
  if(_cmp_seq(this->end_seq, this->begin_seq) <= 0){
    goto done;
  }
  report_timestamp = timestamp_generator_get_ts(this->ts_generator);
  for (act_seq = this->begin_seq; act_seq != this->end_seq; ++act_seq) {
    if (!this->lost_vector[act_seq]) {
      this->ato_vector[act_seq] = 0;
    } else {
      this->ato_vector[act_seq] = _delta_ts(this->timestamp_vector[act_seq], report_timestamp);
    }
  }
  report_producer_add_xr_cc_rle_fb(reportproducer,
      report_count,
      report_timestamp,
      this->begin_seq,
      this->end_seq,
      this->lost_vector,
      this->ecn_vector,
      this->ato_vector
      );
done:
  return;
}


void _setup_xr_owd(FRACTaLFBProducer * this, ReportProducer* reportproducer)
{
  guint32      u32_median_delay, u32_min_delay, u32_max_delay;

//  u32_median_delay = this->median_delay >> 16;
  u32_median_delay = 1; //temporary, for processing at the other end
//  u32_min_delay    = this->min_skew >> 16;
  u32_min_delay    = this->fall >> 16;
//  u32_max_delay    = this->max_skew >> 16;
  u32_max_delay    = this->raise >> 16;

  report_producer_add_xr_owd(reportproducer,
                             RTCP_XR_RFC7243_I_FLAG_CUMULATIVE_DURATION,
                             u32_median_delay,
                             u32_min_delay,
                             u32_max_delay);


}



