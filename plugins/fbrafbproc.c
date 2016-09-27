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
#include "fbrafbproc.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>     /* qsort */
#include <stdio.h>
#include "mprtplogger.h"

#define _now(this) gst_clock_get_time (this->sysclock)
#define _stat(this) (this->stat)

GST_DEBUG_CATEGORY_STATIC (fbrafbprocessor_debug_category);
#define GST_CAT_DEFAULT fbrafbprocessor_debug_category

G_DEFINE_TYPE (FBRAFBProcessor, fbrafbprocessor, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void fbrafbprocessor_finalize (GObject * object);
static void _process_rle_discvector(FBRAFBProcessor *this, GstMPRTCPXRReportSummary *xr);
static void _process_owd(FBRAFBProcessor *this, GstMPRTCPXRReportSummary *xrsummary);
static void _process_stat(FBRAFBProcessor *this);
//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

static void _owd_logger(FBRAFBProcessor *this)
{
  if(_now(this) - 100 * GST_MSECOND < this->last_owd_log){
    return;
  }
  {
    gchar filename[255];
    sprintf(filename, "owd_%d.csv", this->subflow_id);
    mprtp_logger(filename, "%lu,%lu,%lu,%lu\n",
                   GST_TIME_AS_USECONDS(this->stat.owd_stt),
                   GST_TIME_AS_USECONDS(this->stat.owd_ltt80),
                   GST_TIME_AS_USECONDS(this->stat.RTT),
                   (GstClockTime)this->stat.srtt / 1000
                   );
  }



}


StructCmpFnc(_measurement_owd_cmp, FBRAPlusMeasurement, owd);
StructCmpFnc(_measurement_BiF_cmp, FBRAPlusMeasurement, bytes_in_flight);

void _on_BiF_80th_calculated(FBRAFBProcessor *this, swpercentilecandidates_t *candidates)
{
  PercentileResult(FBRAPlusMeasurement,   \
                   bytes_in_flight,       \
                   candidates,            \
                   _stat(this)->BiF_80th, \
                   _stat(this)->BiF_min,  \
                   _stat(this)->BiF_max,  \
                   0                      \
                   );
}


void _on_owd_80th_calculated(FBRAFBProcessor *this, swpercentilecandidates_t *candidates)
{
  PercentileResult(FBRAPlusMeasurement,   \
                   bytes_in_flight,       \
                   candidates,            \
                   _stat(this)->owd_80th, \
                   _stat(this)->owd_min,  \
                   _stat(this)->owd_max,  \
                   0                      \
                   );
}


void
fbrafbprocessor_class_init (FBRAFBProcessorClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fbrafbprocessor_finalize;

  GST_DEBUG_CATEGORY_INIT (fbrafbprocessor_debug_category, "fbrafbprocessor", 0,
      "FBRAFBProcessor");

}

void
fbrafbprocessor_finalize (GObject * object)
{
  FBRAFBProcessor *this;
  this = FBRAFBPROCESSOR(object);
  g_object_unref(this->sysclock);
  g_object_unref(this->sndtracker);
}

void
fbrafbprocessor_init (FBRAFBProcessor * this)
{
  this->sysclock         = gst_system_clock_obtain();
  this-> RTT             = 0;
  this->stat.srtt        = 0;

}


FBRAFBProcessor *make_fbrafbprocessor(SndTracker* sndtracker, SndSubflow* subflow, FBRAPlusStat *stat)
{
  FBRAFBProcessor *this;
  this = g_object_new (FBRAFBPROCESSOR_TYPE, NULL);
  this->on_report_processed = make_observer();
  this->sndtracker = g_object_ref(sndtracker);
  this->subflow    = subflow;
  this->stat       = stat;

  this->short_sw = make_slidingwindow(100, 5 * GST_SECOND);
  this->long_sw = make_slidingwindow(600, 30 * GST_SECOND);

  slidingwindow_add_plugin(this->short_sw,
        make_swpercentile(80, _measurement_BiF_cmp, (NotifierFunc) _on_BiF_80th_calculated, this));

  slidingwindow_add_plugin(this->long_sw,
        make_swpercentile(80, _measurement_owd_cmp, (NotifierFunc) _on_owd_80th_calculated, this));

  return this;
}


void fbrafbprocessor_reset(FBRAFBProcessor *this)
{

}

void fbrafbprocessor_update(FBRAFBProcessor *this, GstMPRTCPReportSummary *summary)
{
  gboolean process = FALSE;
  if(summary->XR.LostRLE.processed){
    _process_rle_discvector(this, &summary->XR);
    process = TRUE;
  }
  if(summary->XR.OWD.processed){
      PROFILING("_process_owd",_process_owd(this, &summary->XR));
      process = TRUE;
  }
  if(process){
    ++this->measurements_num;
    _process_stat(this);
    _owd_logger(this);
  }
}

void fbrafbprocessor_approve_measurement(FBRAFBProcessor *this)
{
  FBRAPlusMeasurement *measurement;
  measurement = g_slice_new0(FBRAPlusMeasurement);
  measurement->bytes_in_flight = this->last_bytes_in_flight;
  measurement->owd             = this->last_owd;

  slidingwindow_add_data(this->long_sw, measurement);
  slidingwindow_add_data(this->short_sw, measurement);
}


void _process_owd(FBRAFBProcessor *this, GstMPRTCPXRReportSummary *xrsummary)
{
  if(!xrsummary->OWD.median_delay){
    goto done;
  }
  this->last_owd = xrsummary->OWD.median_delay;

  if(_stat(this)->owd_80th){
    this->stat.owd_log_corr = log(_stat(this)->owd_80th) / log(this->last_owd);
  }else{
    this->stat.owd_log_corr = 1.;
  }

done:
  return;
}


void _process_rle_discvector(FBRAFBProcessor *this, GstMPRTCPXRReportSummary *xr)
{
  RTPPacket* packet;
  guint16 act_seq, end_seq;
  gint i;

  act_seq = xr->LostRLE.begin_seq;
  end_seq = xr->LostRLE.end_seq;
  if(act_seq == end_seq){
    goto done;
  }

  for(i=0; act_seq <= end_seq; ++act_seq, ++i){
    packet = sndtracker_retrieve_sent_packet(this->sndtracker, this->subflow->id, act_seq);
    packet->onsending_info.acknowledged = TRUE;
    packet->onsending_info.lost = !xr->LostRLE.vector[i];
    sndtracker_packet_acked(this, packet);
  }

  {
    GstClockTime now = _now(this);
    GstClockTime rtt = now - packet->forwarded;
    this->RTT = (this->RTT == 0) ? rtt : (rtt * .125 + this->RTT * .875);
    if(this->RTT < now - this->srtt_updated){
      _stat(this)->srtt = (_stat(this)->srtt == 0.) ? this->RTT : this->RTT * .125 + _stat(this)->srtt * .875;
      this->srtt_updated = now;
    }
  }

done:
  return;
}


void _process_stat(FBRAFBProcessor *this)
{
  SndTrackerStat* sndstat = sndtracker_get_subflow_stat(this->sndtracker, this->subflow->id);
  this->last_bytes_in_flight = sndstat->bytes_in_flight;

  if(sndstat->bytes_in_flight < _stat(this)->BiF_80th * 1.2){
    _stat(this)->stalled_bytes = 0;
  }else{
    _stat(this)->stalled_bytes = sndstat->bytes_in_flight - _stat(this)->BiF_80th;
  }

  _stat(this)->received_bitrate = sndstat->received_bytes_in_1s * 8;
  _stat(this)->sender_bitrate   = sndstat->sent_bytes_in_1s * 8;
  _stat(this)->owd_srtt_ratio   = (gdouble) this->last_owd / (gdouble) _stat(this)->srtt;

  _stat(this)->last_updated     = _now(this);
}

