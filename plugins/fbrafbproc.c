#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>     /* qsort */
#include <stdio.h>
#include "fbrafbproc.h"
#include "reportproc.h"

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

static void _on_BiF_80th_calculated(FBRAFBProcessor *this, swpercentilecandidates_t *candidates);
static void _on_owd_80th_calculated(FBRAFBProcessor *this, swpercentilecandidates_t *candidates);
static void _on_FL_50th_calculated(FBRAFBProcessor *this, swpercentilecandidates_t *candidates);

static void _on_long_sw_rem(FBRAFBProcessor *this, FBRAPlusMeasurement* measurement);
static void _on_short_sw_rem(FBRAFBProcessor *this, FBRAPlusMeasurement* measurement);
static void _on_long_sw_add(FBRAFBProcessor *this, FBRAPlusMeasurement* measurement);
static void _on_short_sw_add(FBRAFBProcessor *this, FBRAPlusMeasurement* measurement);

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
    sprintf(filename, "owd_%d.csv", this->subflow->id);
    mprtp_logger(filename, "%lu,%lu,%lu,%lu\n",
                   GST_TIME_AS_USECONDS(_stat(this)->last_owd),
                   GST_TIME_AS_USECONDS(_stat(this)->owd_80th),
                   GST_TIME_AS_USECONDS(this->RTT),
                   (GstClockTime)_stat(this)->srtt / 1000
                   );
  }

  this->last_owd_log = _now(this);

}

DEFINE_RECYCLE_TYPE(static, measurement, FBRAPlusMeasurement);
StructCmpFnc(_measurement_owd_cmp, FBRAPlusMeasurement, owd);
StructCmpFnc(_measurement_BiF_cmp, FBRAPlusMeasurement, bytes_in_flight);
StructCmpFnc(_measurement_FL_cmp, FBRAPlusMeasurement, fraction_lost);

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

  g_object_unref(this->long_sw);
  g_object_unref(this->short_sw);
  g_object_unref(this->measurements_recycle);
  g_object_unref(this->sysclock);
  g_object_unref(this->sndtracker);
}

void
fbrafbprocessor_init (FBRAFBProcessor * this)
{
  this->sysclock         = gst_system_clock_obtain();
  this-> RTT             = 0;

}


FBRAFBProcessor *make_fbrafbprocessor(SndTracker* sndtracker, SndSubflow* subflow, FBRAPlusStat *stat)
{
  FBRAFBProcessor *this;
  this = g_object_new (FBRAFBPROCESSOR_TYPE, NULL);
  this->on_report_processed = make_notifier();
  this->sndtracker = g_object_ref(sndtracker);
  this->subflow    = subflow;
  this->stat       = stat;

  this->measurements_recycle = make_recycle_measurement(100, NULL);
  this->short_sw = make_slidingwindow(100, 5 * GST_SECOND);
  this->long_sw  = make_slidingwindow(600, 30 * GST_SECOND);

  slidingwindow_add_plugin(this->short_sw,
          make_swpercentile(80, _measurement_BiF_cmp, (ListenerFunc) _on_BiF_80th_calculated, this));

  slidingwindow_add_plugin(this->short_sw,
          make_swpercentile(50, _measurement_FL_cmp, (ListenerFunc) _on_FL_50th_calculated, this));

  slidingwindow_add_plugin(this->long_sw,
        make_swpercentile(80, _measurement_owd_cmp, (ListenerFunc) _on_owd_80th_calculated, this));

//  if(0){
  slidingwindow_add_on_change(this->short_sw,
      (ListenerFunc) _on_short_sw_add,
      (ListenerFunc) _on_short_sw_rem,
      this);

  slidingwindow_add_on_change(this->long_sw,
      (ListenerFunc) _on_long_sw_add,
      (ListenerFunc) _on_long_sw_rem,
      this);
//  }

  return this;
}

void fbrafbprocessor_reset(FBRAFBProcessor *this)
{

}

void fbrafbprocessor_time_update(FBRAFBProcessor *this){
  _process_stat(this);
}

void fbrafbprocessor_report_update(FBRAFBProcessor *this, GstMPRTCPReportSummary *summary)
{
  gboolean process = FALSE;
  GstClockTime now = _now(this);
  if(now - 20 * GST_MSECOND < this->last_report_updated){
    g_warning("batched report arrived");
    goto done;
  }

  if(summary->XR.LostRLE.processed){
    _process_rle_discvector(this, &summary->XR);
    process = TRUE;
  }

  if(summary->XR.OWD.processed){
      PROFILING("_process_owd",_process_owd(this, &summary->XR));
      process = TRUE;
  }

  if(!process){
    goto done;
  }

  ++this->stat->measurements_num;
  ++this->rcved_fb_since_changed;
  this->last_report_updated = now;

  _process_stat(this);

done:
  return;
}

void fbrafbprocessor_approve_measurement(FBRAFBProcessor *this)
{
  FBRAPlusMeasurement *measurement;
  measurement = recycle_retrieve(this->measurements_recycle);
  measurement->bytes_in_flight = this->last_bytes_in_flight;
  measurement->owd             = _stat(this)->last_owd;
  measurement->fraction_lost   = _stat(this)->FL_in_1s;

  slidingwindow_add_data(this->long_sw, measurement);
  slidingwindow_add_data(this->short_sw, measurement);
}


void _process_owd(FBRAFBProcessor *this, GstMPRTCPXRReportSummary *xrsummary)
{
  if(!xrsummary->OWD.median_delay){
    goto done;
  }
  _stat(this)->last_owd = xrsummary->OWD.median_delay;

  if(_stat(this)->owd_80th){
    gdouble corr = log(GST_TIME_AS_MSECONDS(_stat(this)->owd_80th));
    corr /= log(GST_TIME_AS_MSECONDS(_stat(this)->last_owd));
    _stat(this)->owd_log_corr = corr;
  }else{
    _stat(this)->owd_log_corr = 1.;
  }

done:
  return;
}

void _process_rle_discvector(FBRAFBProcessor *this, GstMPRTCPXRReportSummary *xr)
{
  SndPacket* packet = NULL;
  guint16 act_seq, end_seq;
  GstClockTime last_packet_sent_time = 0;
  gint i;

  act_seq = xr->LostRLE.begin_seq;
  end_seq = xr->LostRLE.end_seq;
  if(act_seq == end_seq){
    goto done;
  }
  for(i=0; act_seq <= end_seq; ++act_seq, ++i){
    packet = sndtracker_retrieve_sent_packet(this->sndtracker, this->subflow->id, act_seq);

    if(!packet || packet->acknowledged){
      continue;
    }
    packet->acknowledged = TRUE;
    packet->lost = !xr->LostRLE.vector[i];

    sndtracker_packet_acked(this->sndtracker, packet);
    last_packet_sent_time = packet->sent;
  }

  if(0 < last_packet_sent_time)
  {
    GstClockTime now = _now(this);
    GstClockTime rtt = now - last_packet_sent_time;
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
  gdouble lost_fraction_in_1s = 0.;
  SndTrackerStat* sndstat = sndtracker_get_subflow_stat(this->sndtracker, this->subflow->id);
  this->last_bytes_in_flight = sndstat->bytes_in_flight;

  if(sndstat->bytes_in_flight < _stat(this)->BiF_80th){
    _stat(this)->stalled_bytes = 0;
  }else{
    _stat(this)->stalled_bytes = sndstat->bytes_in_flight - _stat(this)->BiF_80th;
  }

  if(sndstat->received_packets_in_1s < sndstat->acked_packets_in_1s){
    lost_fraction_in_1s  = sndstat->acked_packets_in_1s - sndstat->received_packets_in_1s;
    lost_fraction_in_1s /= (gdouble) sndstat->acked_packets_in_1s;
  }

  _stat(this)->bytes_in_flight       = sndstat->bytes_in_flight;
  _stat(this)->sender_bitrate        = sndstat->sent_bytes_in_1s * 8;
  _stat(this)->receiver_bitrate      = sndstat->received_bytes_in_1s * 8;
  _stat(this)->fec_bitrate           = sndstat->sent_fec_bytes_in_1s * 8;
  _stat(this)->FL_in_1s              = lost_fraction_in_1s;

  _owd_logger(this);
}



void _on_BiF_80th_calculated(FBRAFBProcessor *this, swpercentilecandidates_t *candidates)
{
  PercentileResult(FBRAPlusMeasurement,   \
                   bytes_in_flight,       \
                   candidates,            \
                   _stat(this)->BiF_80th, \
                   this->BiF_min,         \
                   _stat(this)->BiF_max,  \
                   0                      \
                   );
}


void _on_owd_80th_calculated(FBRAFBProcessor *this, swpercentilecandidates_t *candidates)
{
  PercentileResult(FBRAPlusMeasurement,   \
                   owd,                   \
                   candidates,            \
                   _stat(this)->owd_80th, \
                   this->owd_min,         \
                   this->owd_max,         \
                   0                      \
                   );
}

void _on_FL_50th_calculated(FBRAFBProcessor *this, swpercentilecandidates_t *candidates)
{
  PercentileResult(FBRAPlusMeasurement,   \
                   fraction_lost,         \
                   candidates,            \
                   _stat(this)->FL_50th, \
                   this->FL_min,         \
                   this->FL_max,         \
                   0                      \
                   );
}


//static gdouble _calculate_std(FBRAPlusStdHelper* helper, gdouble new_item)
//{
//  gdouble prev_mean = helper->mean;
//  helper->mean += (new_item - helper->mean) / (gdouble) helper->counter;
//  helper->abs_var  += (new_item - helper->mean) * (new_item - prev_mean);
//  return sqrt(helper->abs_var / (gdouble) helper->counter);
//}

static gdouble _calculate_std(FBRAPlusStdHelper* helper, gdouble new_item)
{
  gdouble prev_mean = helper->mean;
  gdouble dprev = new_item - prev_mean;
  gdouble dact;
  gdouble n = helper->counter;
  helper->mean += dprev / n;
  dact = new_item - helper->mean;
  if(helper->counter < 2){
    return 0.;
  }
  helper->emp *= (n - 2.) / (n - 1.);
  helper->emp += pow(dprev, 2) / n;
  helper->var = ( (n-1.) * helper->var + dprev * dact ) / n;

  return sqrt(helper->var);
}

void _on_short_sw_rem(FBRAFBProcessor *this, FBRAPlusMeasurement* measurement)
{
  --this->BiF_std_helper.counter;
}

void _on_long_sw_rem(FBRAFBProcessor *this, FBRAPlusMeasurement* measurement)
{
  --this->owd_std_helper.counter;
  recycle_add(this->measurements_recycle, measurement);
}

void _on_short_sw_add(FBRAFBProcessor *this, FBRAPlusMeasurement* measurement)
{
  ++this->BiF_std_helper.counter;
  _stat(this)->BiF_std = _calculate_std(&this->BiF_std_helper, measurement->bytes_in_flight);
}

void _on_long_sw_add(FBRAFBProcessor *this, FBRAPlusMeasurement* measurement)
{
  ++this->owd_std_helper.counter;
//  _stat(this)->owd_in_ms_std = GST_TIME_AS_MSECONDS(_calculate_std(&this->owd_std_helper, measurement->owd));
  _stat(this)->owd_std = _calculate_std(&this->owd_std_helper, measurement->owd);
}
