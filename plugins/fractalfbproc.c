#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>     /* qsort */
#include <stdio.h>
#include "fractalfbproc.h"
#include "reportproc.h"

#define _now(this) gst_clock_get_time (this->sysclock)
#define _stat(this) (this->stat)

GST_DEBUG_CATEGORY_STATIC (fractalfbprocessor_debug_category);
#define GST_CAT_DEFAULT fractalfbprocessor_debug_category

G_DEFINE_TYPE (FRACTaLFBProcessor, fractalfbprocessor, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void fractalfbprocessor_finalize (GObject * object);
static void _process_rle_discvector(FRACTaLFBProcessor *this, GstMPRTCPXRReportSummary *xr);
static void _process_owd(FRACTaLFBProcessor *this, GstMPRTCPXRReportSummary *xrsummary);
static void _process_stat(FRACTaLFBProcessor *this);

static void _on_BiF_80th_calculated(FRACTaLFBProcessor *this, swpercentilecandidates_t *candidates);
static void _on_owd_50th_calculated(FRACTaLFBProcessor *this, swpercentilecandidates_t *candidates);
static void _on_FL_50th_calculated(FRACTaLFBProcessor *this, swpercentilecandidates_t *candidates);

static void _on_owd_sw_rem(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement);
static void _on_BiF_sw_rem(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement);
static void _on_owd_sw_add(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement);
static void _on_BiF_sw_add(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

static void _owd_logger(FRACTaLFBProcessor *this)
{
  if(_now(this) - 100 * GST_MSECOND < this->last_owd_log){
    return;
  }
  {
    gchar filename[255];
    sprintf(filename, "owd_%d.csv", this->subflow->id);
    mprtp_logger(filename, "%lu,%lu,%lu,%lu\n",
                   GST_TIME_AS_USECONDS(_stat(this)->last_qdelay),
                   GST_TIME_AS_USECONDS(_stat(this)->qdelay_50th),
                   GST_TIME_AS_USECONDS(this->RTT),
                   (GstClockTime)_stat(this)->srtt / 1000
                   );
  }

  this->last_owd_log = _now(this);

}

static void _long_sw_item_sprintf(FRACTaLMeasurement* measurement, gchar* to_string){
  sprintf(to_string, "BiF: %d | FL: %1.2f | OWD: %lu",
      measurement->bytes_in_flight,
      measurement->fraction_lost,
      measurement->owd
  );
}


DEFINE_RECYCLE_TYPE(static, measurement, FRACTaLMeasurement);

static void _measurement_shape(FRACTaLMeasurement* result, gpointer udata)
{
  memset(result, 0, sizeof(FRACTaLMeasurement));
}

static void _on_measurement_ref(FRACTaLFBProcessor* this, FRACTaLMeasurement* measurement){
  ++measurement->ref;
}

static void _on_measurement_unref(FRACTaLFBProcessor* this, FRACTaLMeasurement* measurement){
  if(0 < --measurement->ref){
    return;
  }
  recycle_add(this->measurements_recycle, measurement);
}


StructCmpFnc(_measurement_owd_cmp, FRACTaLMeasurement, owd);
StructCmpFnc(_measurement_BiF_cmp, FRACTaLMeasurement, bytes_in_flight);
StructCmpFnc(_measurement_FL_cmp, FRACTaLMeasurement, fraction_lost);



void
fractalfbprocessor_class_init (FRACTaLFBProcessorClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fractalfbprocessor_finalize;

  GST_DEBUG_CATEGORY_INIT (fractalfbprocessor_debug_category, "fractalfbprocessor", 0,
      "FRACTaLFBProcessor");

}

void
fractalfbprocessor_finalize (GObject * object)
{
  FRACTaLFBProcessor *this;
  this = FRACTALFBPROCESSOR(object);

  g_object_unref(this->long_sw);
  g_object_unref(this->short_sw);

  g_object_unref(this->measurements_recycle);
  g_object_unref(this->sysclock);
  g_object_unref(this->sndtracker);
}

void
fractalfbprocessor_init (FRACTaLFBProcessor * this)
{
  this->sysclock         = gst_system_clock_obtain();
  this->RTT              = 0;

}


FRACTaLFBProcessor *make_fractalfbprocessor(SndTracker* sndtracker, SndSubflow* subflow,
    FRACTaLStat *stat, FRACTaLApprovement* approvement)
{
  FRACTaLFBProcessor *this;
  this = g_object_new (FRACTALFBPROCESSOR_TYPE, NULL);
  this->sndtracker   = g_object_ref(sndtracker);
  this->subflow      = subflow;
  this->stat         = stat;
  this->approvement = approvement;

  this->measurements_recycle = make_recycle_measurement(500, (RecycleItemShaper) _measurement_shape);
  this->short_sw               = make_slidingwindow(100, 5 * GST_SECOND);
//  this->FL_sw                = make_slidingwindow(100, 5 * GST_SECOND);
  this->long_sw               = make_slidingwindow(600, 30 * GST_SECOND);

  slidingwindow_add_on_data_ref_change(this->long_sw,  (ListenerFunc) _on_measurement_ref, (ListenerFunc) _on_measurement_unref, this);
//  slidingwindow_add_on_data_ref_change(this->FL_sw,   (ListenerFunc) _on_measurement_ref, (ListenerFunc) _on_measurement_unref, this);
  slidingwindow_add_on_data_ref_change(this->short_sw,  (ListenerFunc) _on_measurement_ref, (ListenerFunc) _on_measurement_unref, this);

  DISABLE_LINE slidingwindow_setup_debug(this->long_sw, (SlidingWindowItemSprintf)_long_sw_item_sprintf, g_print);

  slidingwindow_add_plugin(this->short_sw,
          make_swpercentile(80, _measurement_BiF_cmp, (ListenerFunc) _on_BiF_80th_calculated, this));

  slidingwindow_add_plugin(this->short_sw,
          make_swpercentile(50, _measurement_FL_cmp, (ListenerFunc) _on_FL_50th_calculated, this));

  slidingwindow_add_plugin(this->long_sw,
        make_swpercentile(50, _measurement_owd_cmp, (ListenerFunc) _on_owd_50th_calculated, this));

//  if(0){
  slidingwindow_add_on_change(this->short_sw,
      (ListenerFunc) _on_BiF_sw_add,
      (ListenerFunc) _on_BiF_sw_rem,
      this);

  slidingwindow_add_on_change(this->long_sw,
      (ListenerFunc) _on_owd_sw_add,
      (ListenerFunc) _on_owd_sw_rem,
      this);
//  }

  return this;
}

void fractalfbprocessor_reset(FRACTaLFBProcessor *this)
{

}

void fractalfbprocessor_time_update(FRACTaLFBProcessor *this){
  _process_stat(this);
}

void fractalfbprocessor_report_update(FRACTaLFBProcessor *this, GstMPRTCPReportSummary *summary)
{
  gboolean process = FALSE;
  GstClockTime now = _now(this);
  if(now - 5 * GST_MSECOND < this->last_report_updated){
    GST_DEBUG_OBJECT(this, "Batched report arrived");
    //HERE you decide weather a batched report would be a problem or not,
    //because at high jitter it may appears a lot of times.
    goto done;
  }

  if(summary->XR.LostRLE.processed){
    _process_rle_discvector(this, &summary->XR);
    process = TRUE;
  }

  if(summary->XR.OWD.processed){
//      PROFILING("_process_owd",_process_owd(this, &summary->XR));
      _process_owd(this, &summary->XR);
      process = TRUE;
  }

  if(summary->XR.DiscardedBytes.processed){
    g_print("Dsicarded byte matric is not used now. Turn it off at producer or implement a handler function");
    process = TRUE;
  }

  if(!process){
    goto done;
  }


  ++this->rcved_fb_since_changed;
  this->last_report_updated = now;

  _process_stat(this);

done:
  return;
}


void fractalfbprocessor_approve_measurement(FRACTaLFBProcessor *this)
{
//  FRACTaLApprovement* approvement = this->approvement;
  FRACTaLMeasurement* measurement = recycle_retrieve_and_shape(this->measurements_recycle, NULL);

  measurement->ref             = 1;
  measurement->bytes_in_flight = this->last_bytes_in_flight;
  measurement->owd             = measurement->qdelay = _stat(this)->last_qdelay;
  measurement->fraction_lost   = _stat(this)->FL_in_1s;

//  if(approvement->owd){
    slidingwindow_add_data(this->long_sw,  measurement);
//  }

//  if(approvement->bytes_in_flight){
    slidingwindow_add_data(this->short_sw,  measurement);
//  }

//  if(approvement->fraction_lost){
//    slidingwindow_add_data(this->FL_sw,  measurement);
//  }

  _on_measurement_unref(this, measurement);
}


void _process_owd(FRACTaLFBProcessor *this, GstMPRTCPXRReportSummary *xrsummary)
{
  if(!xrsummary->OWD.median_delay){
    goto done;
  }
  _stat(this)->last_qdelay = xrsummary->OWD.median_delay;
  if(_stat(this)->last_qdelay <= _stat(this)->qdelay_50th){
    _stat(this)->qdelay_log_corr = 1.;
    goto done;
  }
  {
    gdouble refpoint = MAX(_stat(this)->qdelay_50th,MAX(_stat(this)->qdelay_std, 15 * GST_MSECOND));
    gdouble diff = _stat(this)->last_qdelay - refpoint;
    gdouble ratio = diff / _stat(this)->last_qdelay;
    _stat(this)->qdelay_log_corr = 1.-CONSTRAIN(0., .5, pow(ratio, 3));
  }
//  _stat(this)->qdelay_log_corr = _stat(this)->last_qdelay - _stat(this)->qdelay_50th;
//  _stat(this)->qdelay_log_corr /= _stat(this)->last_qdelay * 4;
//  _stat(this)->qdelay_log_corr = 1.-_stat(this)->qdelay_log_corr;
//
//  if(_stat(this)->qdelay_50th < _stat(this)->last_qdelay){
//    _stat(this)->qdelay_log_corr = .9;
//  }else{
//    _stat(this)->qdelay_log_corr = 1.;
//  }

done:
  return;
}

static void _newly_received_packet(FRACTaLFBProcessor *this, SndPacket* packet)
{
  _stat(this)->newly_acked_bytes += packet->payload_size;
  ++this->newly_acked_packets;
  this->newly_received_packets += packet->lost ? 0 : 1;
}

void _process_rle_discvector(FRACTaLFBProcessor *this, GstMPRTCPXRReportSummary *xr)
{
  SndPacket* packet = NULL;
  guint16 act_seq, end_seq;
  GstClockTime last_packet_sent_time = 0;
  gint i;

  act_seq = xr->LostRLE.begin_seq;
  end_seq = xr->LostRLE.end_seq;
  _stat(this)->newly_acked_bytes = 0;
  _stat(this)->last_FL           = 0.;
  this->newly_acked_packets      = 0;
  this->newly_received_packets   = 0;


  if(act_seq == end_seq){
    goto done;
  }

  //g_print("RLE vector from %hu until %hu\n", act_seq, end_seq);

  for(i=0; act_seq != end_seq; ++act_seq, ++i){
    packet = sndtracker_retrieve_sent_packet(this->sndtracker, this->subflow->id, act_seq);

    if(!packet){
      GST_DEBUG_OBJECT(this, "Packet %hu has not in subflow tracked sequences. "
          "Either too late acknowledged or never sent", act_seq);
      continue;
    }
    if(packet->acknowledged){
      if(packet->lost && xr->LostRLE.vector[i]){
        sndtracker_packet_found(this->sndtracker, packet);
        _newly_received_packet(this, packet);
      }
      continue;
    }
    packet->acknowledged = TRUE;
    packet->lost = !xr->LostRLE.vector[i];
    if(!packet->lost){
      _newly_received_packet(this, packet);
    }

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

  if(this->newly_received_packets < this->newly_acked_packets){
    _stat(this)->last_FL  = this->newly_acked_packets - this->newly_received_packets;
    _stat(this)->last_FL /= (gdouble) this->newly_acked_packets;
  }

done:
  return;
}


void _process_stat(FRACTaLFBProcessor *this)
{
  gdouble lost_fraction_in_1s = 0.;
  SndTrackerStat* sndstat = sndtracker_get_subflow_stat(this->sndtracker, this->subflow->id);
  RTPQueueStat* rtpqstat  = sndtracker_get_rtpqstat(this->sndtracker);
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

  _stat(this)->delay_in_rtpqueue     = rtpqstat->delay_length;
  _stat(this)->bytes_in_flight       = sndstat->bytes_in_flight;
  _stat(this)->sender_bitrate        = sndstat->sent_bytes_in_1s * 8;
  _stat(this)->receiver_bitrate      = sndstat->received_bytes_in_1s * 8;
  _stat(this)->fec_bitrate           = sndstat->sent_fec_bytes_in_1s * 8;
  _stat(this)->FL_in_1s              = lost_fraction_in_1s;

  _stat(this)->measurements_num      = MIN(slidingwindow_get_counter(this->short_sw),
                                           slidingwindow_get_counter(this->long_sw));

  if(_stat(this)->sr_avg){
    _stat(this)->sr_avg = .2 * _stat(this)->sender_bitrate + _stat(this)->sr_avg * .8;
  }else{
    _stat(this)->sr_avg = _stat(this)->sender_bitrate;
  }

  if(_stat(this)->rr_avg){
    _stat(this)->rr_avg = .2 * _stat(this)->receiver_bitrate + _stat(this)->rr_avg * .8;
  }else{
    _stat(this)->rr_avg = _stat(this)->receiver_bitrate;
  }

  DISABLE_LINE _owd_logger(this);
}



void _on_BiF_80th_calculated(FRACTaLFBProcessor *this, swpercentilecandidates_t *candidates)
{
  PercentileResult(FRACTaLMeasurement,   \
                   bytes_in_flight,       \
                   candidates,            \
                   _stat(this)->BiF_80th, \
                   _stat(this)->BiF_min, \
                   _stat(this)->BiF_max,  \
                   0                      \
                   );
}


void _on_owd_50th_calculated(FRACTaLFBProcessor *this, swpercentilecandidates_t *candidates)
{
  PercentileResult(FRACTaLMeasurement,   \
                   owd,                   \
                   candidates,            \
                   _stat(this)->qdelay_50th, \
                   this->owd_min,         \
                   this->owd_max,         \
                   0                      \
                   );
}

void _on_FL_50th_calculated(FRACTaLFBProcessor *this, swpercentilecandidates_t *candidates)
{
  PercentileResult(FRACTaLMeasurement,   \
                   fraction_lost,         \
                   candidates,            \
                   _stat(this)->FL_50th, \
                   this->FL_min,         \
                   this->FL_max,         \
                   0                      \
                   );
}


//static gdouble _calculate_std(FRACTALPlusStdHelper* helper, gdouble new_item)
//{
//  gdouble prev_mean = helper->mean;
//  helper->mean += (new_item - helper->mean) / (gdouble) helper->counter;
//  helper->abs_var  += (new_item - helper->mean) * (new_item - prev_mean);
//  return sqrt(helper->abs_var / (gdouble) helper->counter);
//}

static gdouble _calculate_std(FRACTaLStdHelper* helper, gdouble new_item)
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

void _on_BiF_sw_rem(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement)
{
  --this->BiF_std_helper.counter;
  --this->FL_std_helper.counter;
}

void _on_owd_sw_rem(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement)
{
  --this->qdelay_std_helper.counter;
  this->qdelay_std_helper.std -= measurement->qdelay_std_t;
  this->qdelay_std_helper.sum -= measurement->qdelay;
}

void _on_BiF_sw_add(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement)
{
  ++this->BiF_std_helper.counter;
  _stat(this)->BiF_std = _calculate_std(&this->BiF_std_helper, measurement->bytes_in_flight);

  ++this->FL_std_helper.counter;
  _stat(this)->FL_std = _calculate_std(&this->FL_std_helper, measurement->fraction_lost);
}

void _on_owd_sw_add(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement)
{
  ++this->owd_std_helper.counter;
  _stat(this)->qdelay_std = _calculate_std(&this->owd_std_helper, measurement->owd);

//  {
//    gdouble qdelay;
//    qdelay = measurement->qdelay;
//    this->qdelay_std_helper.sum += measurement->qdelay;
//    if(1 < ++this->qdelay_std_helper.counter){
//      gdouble avg,std;
//      avg = this->qdelay_std_helper.sum / (gdouble)this->qdelay_std_helper.counter;
//      std = qdelay - avg;
//      std *= std;
//      measurement->qdelay_std_t = std;
//    }
//  }
//
//   this->qdelay_std_helper.std += measurement->qdelay_std_t;
//   _stat(this)->qdelay_std = this->qdelay_std_helper.std;

}


