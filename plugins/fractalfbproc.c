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

//static void _on_drift_80th_calculated(FRACTaLFBProcessor *this, swpercentilecandidates_t *candidates);

static void _on_long_sw_rem(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement);
static void _on_long_sw_add(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement);
static void _on_srtt_sw_add(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement);
static void _on_srtt_sw_rem(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement);


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
    mprtp_logger(filename, "%lu,%lu\n",
                   GST_TIME_AS_USECONDS(this->RTT),
                   (GstClockTime)_stat(this)->srtt / 1000
                   );
  }

  this->last_owd_log = _now(this);

}

//static gint
//_cmp_seq (guint16 x, guint16 y)
//{
//  if(x == y) return 0;
//  if(x < y && y - x < 32768) return -1;
//  if(x > y && x - y > 32768) return -1;
//  if(x < y && y - x > 32768) return 1;
//  if(x > y && x - y < 32768) return 1;
//  return 0;
//}


static void _long_sw_item_sprintf(FRACTaLMeasurement* measurement, gchar* to_string){
  sprintf(to_string, "-");
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


StructCmpFnc(_measurement_drift_cmp, FRACTaLMeasurement, drift);


static GstClockTime* _drift_pointer_extractor(FRACTaLMeasurement* measurement) {
  return &measurement->drift;
}

static gpointer _drift_prefer_right_selector(FRACTaLMeasurement* left, FRACTaLMeasurement* right) {
  return right ? right : left;
}

static void _on_drift_80th_calculated(FRACTaLFBProcessor* this, GstClockTime* drift) {
  _stat(this)->drift_80th = *drift;
}


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

  g_object_unref(this->measurements_recycle);
  g_object_unref(this->sysclock);
  g_object_unref(this->sndtracker);
  g_free(this->measurement);
}

void
fractalfbprocessor_init (FRACTaLFBProcessor * this)
{
  this->sysclock         = gst_system_clock_obtain();
  this->RTT              = 0;
  this->measurement      = NULL;

}
swplugin_define_swdataextractor(_drift_extractor, FRACTaLMeasurement, drift);
swplugin_define_on_calculated_double(FRACTaLStat, _on_drift_std_calculated, drift_std);

swplugin_define_swdataextractor(_lost_extractor, FRACTaLMeasurement, fraction_lost);
swplugin_define_on_calculated_double(FRACTaLStat, _on_lost_avg_calculated, lost_avg);
swplugin_define_on_calculated_double(FRACTaLStat, _on_lost_std_calculated, lost_std);

swplugin_define_swdataextractor(_BiF_extractor, FRACTaLMeasurement, bytes_in_flight);
swplugin_define_on_calculated_double(FRACTaLStat, _on_BiF_std_calculated, BiF_std);



static void _on_packet_sent(FRACTaLFBProcessor* this, SndPacket* packet){

}

static void _on_packet_queued(FRACTaLFBProcessor* this, SndPacket* packet){
  if(packet->subflow_id != this->subflow->id){
    return;
  }
  this->newly_queued_bytes += packet->payload_size;
}

FRACTaLFBProcessor *make_fractalfbprocessor(SndTracker* sndtracker, SndSubflow* subflow, FRACTaLStat *stat)
{
  FRACTaLFBProcessor *this;
  this = g_object_new (FRACTALFBPROCESSOR_TYPE, NULL);
  this->sndtracker   = g_object_ref(sndtracker);
  this->subflow      = subflow;
  this->stat         = stat;

  this->measurements_recycle = make_recycle_measurement(500, (RecycleItemShaper) _measurement_shape);

  this->srtt_sw     = make_slidingwindow(100, GST_SECOND);
  this->long_sw     = make_slidingwindow(200, 15 * GST_SECOND);
  this->measurement = recycle_retrieve_and_shape(this->measurements_recycle, NULL);

  this->measurement->ref     = 1;

//  this->drift_correlator = make_correlator(5, 30); //should be 5rtt the total window and 1 rtt the delay.

  sndtracker_add_on_packet_sent(this->sndtracker, (ListenerFunc)_on_packet_sent, this);
  sndtracker_add_on_packet_queued(this->sndtracker, (ListenerFunc)_on_packet_queued, this);

//  correlator_add_on_correlation_calculated_listener(this->drift_correlator, (ListenerFunc) _on_drift_corr_calculated, stat);

  slidingwindow_add_processors(this->long_sw,  (ListenerFunc) _on_measurement_ref, (ListenerFunc) _on_measurement_unref, this);
  slidingwindow_add_processors(this->srtt_sw,  (ListenerFunc) _on_measurement_ref, (ListenerFunc) _on_measurement_unref, this);

  DISABLE_LINE slidingwindow_setup_debug(this->long_sw, (SlidingWindowItemSprintf)_long_sw_item_sprintf, g_print);

  slidingwindow_add_plugins(this->long_sw,
//          make_swpercentile(80, _measurement_drift_cmp, (ListenerFunc) _on_drift_80th_calculated, this),
          make_swpercentile2(80,
              (GCompareFunc) _measurement_drift_cmp,
              (ListenerFunc) _on_drift_80th_calculated,
              this,
              (SWExtractorFunc) _drift_pointer_extractor,
              (SWMeanCalcer)_drift_prefer_right_selector,
              (SWEstimator)_drift_prefer_right_selector),
          make_swstd(_on_drift_std_calculated, stat, _drift_extractor, 0),
          make_swstd(_on_BiF_std_calculated, stat, _BiF_extractor, 0),
          make_swavg(_on_lost_avg_calculated, stat, _lost_extractor),
          make_swstd(_on_lost_std_calculated, stat, _lost_extractor, 100),
          make_swstd(_on_BiF_std_calculated, stat, _BiF_extractor, 0),
          NULL);


  slidingwindow_add_on_change(this->srtt_sw,
      (ListenerFunc)_on_srtt_sw_add,
      (ListenerFunc)_on_srtt_sw_rem,
      this);

  slidingwindow_add_on_change(this->long_sw,
      (ListenerFunc) _on_long_sw_add,
      (ListenerFunc) _on_long_sw_rem,
      this);

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
  if(now - 5 * GST_MSECOND < this->last_report_update){
    GST_DEBUG_OBJECT(this, "Batched report arrived");
    //HERE you decide weather a batched report would be a problem or not,
    //because at high jitter it may appears a lot of times.
    goto done;
  }

  if(1 < this->measurement->ref){
    FRACTaLMeasurement* measurement = recycle_retrieve_and_shape(this->measurements_recycle, NULL);
    measurement->ref = 1;
    _on_measurement_unref(this, this->measurement);
    this->measurement = measurement;
  }

  if(summary->XR.LostRLE.processed){
    _process_rle_discvector(this, &summary->XR);
    process = TRUE;
  }

  if(summary->XR.OWD.processed){
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


  _process_stat(this);

  slidingwindow_add_data(this->srtt_sw, this->measurement);

  ++this->newly_rcved_fb;
  this->last_report_update = now;

done:
  return;
}

void fractalfbprocessor_approve_measurement(FRACTaLFBProcessor *this)
{
  slidingwindow_add_data(this->long_sw,  this->measurement);
}

static gdouble _get_ewma_factor(FRACTaLFBProcessor *this){
  if(!_stat(this)->last_drift){
    return 0.;
  }
  return (gdouble) _stat(this)->last_drift / (gdouble)(_stat(this)->last_drift + _stat(this)->drift_std);
}

static void _calculate_drift_corr(FRACTaLFBProcessor *this, gdouble actual_drift_in_ms){
  gdouble avg_drift_in_ms = MAX(30., GST_TIME_AS_MSECONDS(_stat(this)->drift_80th + 4 * _stat(this)->drift_std));
  gdouble actual_corr = actual_drift_in_ms / avg_drift_in_ms;
  if(_stat(this)->drift_corr == 0.){
    _stat(this)->drift_corr = actual_corr;
  }else{
    _stat(this)->drift_corr = _stat(this)->drift_corr * .75 + actual_corr * .25;
  }
  // _stat(this)->drift_corr = MIN(1., _stat(this)->drift_corr);
}

void _process_owd(FRACTaLFBProcessor *this, GstMPRTCPXRReportSummary *xrsummary)
{
  FRACTaLMeasurement* measurement = this->measurement;
  if(!xrsummary->OWD.median_delay){
    goto done;
  }
//  g_print("%lu, %lu\n", xrsummary->OWD.min_delay, xrsummary->OWD.max_delay);
  this->last_raise = xrsummary->OWD.max_delay;
  this->last_fall  = xrsummary->OWD.min_delay;

  measurement->drift = this->last_raise + this->last_fall;
  //
  _calculate_drift_corr(this, GST_TIME_AS_MSECONDS(measurement->drift));
//  correlator_add_sample(this->drift_correlator, GST_TIME_AS_MSECONDS(measurement->drift));
done:
  return;
}


void _process_rle_discvector(FRACTaLFBProcessor *this, GstMPRTCPXRReportSummary *xr)
{
  SndPacket* packet = NULL;
  guint16 act_seq, end_seq;
  GstClockTime last_packet_sent_time = 0;
  gint i;
  guint16 last_seq;
  FRACTaLMeasurement* measurement = this->measurement;

  act_seq = xr->LostRLE.begin_seq;
  end_seq = xr->LostRLE.end_seq;

  if(act_seq == end_seq){
    goto done;
  }

  //g_print("RLE vector from %hu until %hu\n", act_seq, end_seq);

  for(i=0; act_seq != end_seq; ++act_seq, ++i){
    packet = sndtracker_retrieve_sent_packet(this->sndtracker, this->subflow->id, act_seq);

    if(!packet){
      GST_WARNING_OBJECT(this, "Packet %hu has not in subflow tracked sequences. "
          "Either too late acknowledged or never sent", act_seq);
      continue;
    }

    if(packet->acknowledged){
      if(packet->lost && xr->LostRLE.vector[i]){
        sndtracker_packet_found(this->sndtracker, packet);
//        measurement->newly_received_bytes += packet->payload_size;
      }
      continue;
    }
    packet->acknowledged = TRUE;
    packet->lost = !xr->LostRLE.vector[i];

    if(!packet->lost){
//      measurement->newly_received_bytes += packet->payload_size;
    }

    sndtracker_packet_acked(this->sndtracker, packet);
    last_packet_sent_time = packet->sent;
    last_seq = packet->subflow_seq;
  }

  if(0 < last_packet_sent_time)
  {
    GstClockTime now = _now(this);
    GstClockTime rtt = now - last_packet_sent_time;
    GstClockTime boundary;
    gint lost_packets = 0, sent_packets = 0;;
    this->RTT = (this->RTT == 0) ? rtt : (rtt * .125 + this->RTT * .875);
    if(this->RTT < now - this->srtt_updated){
      _stat(this)->srtt = (_stat(this)->srtt == 0.) ? this->RTT : this->RTT * .125 + _stat(this)->srtt * .875;
      this->srtt_updated = now;
      slidingwindow_set_treshold(this->srtt_sw, _stat(this)->srtt);
//      g_print("this->received_fb_in_srtt: %d\n",this->received_fb_in_srtt);
//      correlator_set_tau(this->drift_correlator, this->received_fb_in_srtt);
//      correlator_set_accumulation_length(this->drift_correlator, this->received_fb_in_srtt * 10);
    }
    this->HSN = last_seq;

    boundary = last_packet_sent_time - MAX(100 * GST_MSECOND, _stat(this)->srtt);
    //calculate lost packets in srtt
    _stat(this)->received_bytes_in_srtt = 0;
    for(i=0, --end_seq, act_seq = xr->LostRLE.begin_seq; act_seq != end_seq; --end_seq, ++i){
      packet = sndtracker_retrieve_sent_packet(this->sndtracker, this->subflow->id, end_seq);
      if(!packet){
        continue;
      }
      if(packet->sent < boundary){
        break;
      }
      ++sent_packets;
      if(!packet->lost){
//        measurement->rcved_bytes_in_srtt += packet->payload_size;
        _stat(this)->received_bytes_in_srtt += packet->payload_size;
        continue;
      }
      ++lost_packets;
    }
//    _stat(this)->received_bytes_in_srtt = measurement->rcved_bytes_in_srtt;
    if(0 < sent_packets){
      measurement->fraction_lost = (gdouble) lost_packets / (gdouble) sent_packets;
    }
  }

done:
//  measurement->sent_bytes_in_srtt = this->sent_bytes_in_srtt;
  return;
}

void _process_stat(FRACTaLFBProcessor *this)
{

  SndTrackerStat*     sndstat     = sndtracker_get_subflow_stat(this->sndtracker, this->subflow->id);
  RTPQueueStat*       rtpqstat    = sndtracker_get_rtpqstat(this->sndtracker);
  FRACTaLMeasurement* measurement = this->measurement;

  gdouble ewma_factor         = _get_ewma_factor(this);
  gdouble alpha;

  _stat(this)->rtpq_delay            = rtpqstat->rtpq_delay;
  _stat(this)->bytes_in_flight       = sndstat->bytes_in_flight;
  _stat(this)->sender_bitrate        = sndstat->sent_bytes_in_1s * 8;
  _stat(this)->receiver_bitrate      = sndstat->received_bytes_in_1s * 8;
  _stat(this)->fec_bitrate           = sndstat->sent_fec_bytes_in_1s * 8;
  _stat(this)->measurements_num      = slidingwindow_get_counter(this->long_sw);
  _stat(this)->fraction_lost         = measurement->fraction_lost;
  _stat(this)->last_drift            = measurement->drift;

  if(_stat(this)->sr_avg){
    alpha = ewma_factor * .5 + .5;
    _stat(this)->sr_avg = alpha * _stat(this)->sender_bitrate + _stat(this)->sr_avg * (1.-alpha);
  }else{
    _stat(this)->sr_avg = _stat(this)->sender_bitrate;
  }

  if(_stat(this)->rr_avg){
    alpha = ewma_factor * .5 + .5;
    _stat(this)->rr_avg = alpha * _stat(this)->receiver_bitrate + _stat(this)->rr_avg * (1.-alpha);
  }else{
    _stat(this)->rr_avg = _stat(this)->receiver_bitrate;
  }

  if(measurement->ref == 1){//to assign only once.
    measurement->bytes_in_flight = sndstat->bytes_in_flight;
    measurement->newly_rcved_fb = this->newly_rcved_fb;
    this->newly_rcved_fb = 0;
    measurement->newly_queued_bytes = this->newly_queued_bytes;
    this->newly_queued_bytes = 0;
  }

  DISABLE_LINE _owd_logger(this);

  slidingwindow_refresh(this->srtt_sw);
  slidingwindow_refresh(this->long_sw);
}


/*
void _on_drift_80th_calculated(FRACTaLFBProcessor *this, swpercentilecandidates_t *candidates)
{
  PercentileResult(FRACTaLMeasurement,   \
                   drift,       \
                   candidates,            \
                   _stat(this)->drift_80th, \
                   this->drift_min, \
                   this->drift_max,  \
                   0                      \
                   );
}
*/

void _on_long_sw_rem(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement)
{

}

void _on_long_sw_add(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement)
{

}

void _on_srtt_sw_add(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement)
{
  _stat(this)->queued_bytes_in_srtt += measurement->newly_queued_bytes;
  _stat(this)->received_fb_in_srtt += measurement->newly_rcved_fb;

}

void _on_srtt_sw_rem(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement)
{
  _stat(this)->received_fb_in_srtt -= measurement->newly_rcved_fb;
  _stat(this)->queued_bytes_in_srtt -= measurement->newly_queued_bytes;
}
