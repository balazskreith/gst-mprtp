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

static void _on_long_sw_rem(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement);
static void _on_short_sw_rem(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement);
static void _on_long_sw_add(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement);
static void _on_short_sw_add(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement);

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
  sprintf(to_string, "BiF: %d",
      measurement->bytes_in_flight
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


//StructCmpFnc(_measurement_flaw_cmp, FRACTaLMeasurement, flaw);
StructCmpFnc(_measurement_BiF_cmp, FRACTaLMeasurement, bytes_in_flight);



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
  g_free(this->measurement);
}

void
fractalfbprocessor_init (FRACTaLFBProcessor * this)
{
  this->sysclock         = gst_system_clock_obtain();
  this->RTT              = 0;
  this->measurement      = g_malloc0(sizeof(FRACTaLMeasurement));

}


FRACTaLFBProcessor *make_fractalfbprocessor(SndTracker* sndtracker, SndSubflow* subflow, FRACTaLStat *stat)
{
  FRACTaLFBProcessor *this;
  this = g_object_new (FRACTALFBPROCESSOR_TYPE, NULL);
  this->sndtracker   = g_object_ref(sndtracker);
  this->subflow      = subflow;
  this->stat         = stat;

  this->measurements_recycle = make_recycle_measurement(500, (RecycleItemShaper) _measurement_shape);
  this->short_sw               = make_slidingwindow(100, 2 * GST_SECOND);
  this->srtt_sw               = make_slidingwindow(100, GST_SECOND);

//  this->FL_sw                = make_slidingwindow(100, 5 * GST_SECOND);
  this->long_sw               = make_slidingwindow(600, 30 * GST_SECOND);

  slidingwindow_add_on_data_ref_change(this->long_sw,  (ListenerFunc) _on_measurement_ref, (ListenerFunc) _on_measurement_unref, this);
  slidingwindow_add_on_data_ref_change(this->short_sw,  (ListenerFunc) _on_measurement_ref, (ListenerFunc) _on_measurement_unref, this);
  slidingwindow_add_on_data_ref_change(this->srtt_sw,  (ListenerFunc) _on_measurement_ref, (ListenerFunc) _on_measurement_unref, this);

  DISABLE_LINE slidingwindow_setup_debug(this->long_sw, (SlidingWindowItemSprintf)_long_sw_item_sprintf, g_print);

  slidingwindow_add_plugin(this->short_sw,
          make_swpercentile(80, _measurement_BiF_cmp, (ListenerFunc) _on_BiF_80th_calculated, this));

  slidingwindow_add_on_change(this->srtt_sw,
      (ListenerFunc)_on_srtt_sw_add,
      (ListenerFunc)_on_srtt_sw_rem,
      this);

  slidingwindow_add_on_change(this->short_sw,
      (ListenerFunc) _on_short_sw_add,
      (ListenerFunc) _on_short_sw_rem,
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
  if(now - 5 * GST_MSECOND < this->last_report_updated){
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
  slidingwindow_add_data(this->short_sw, this->measurement);

  ++this->rcved_fb_since_changed;
  this->last_report_updated = now;



done:
  return;
}

void fractalfbprocessor_reset_short_sw(FRACTaLFBProcessor *this)
{
  slidingwindow_clear(this->short_sw);
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


void _process_owd(FRACTaLFBProcessor *this, GstMPRTCPXRReportSummary *xrsummary)
{
  FRACTaLMeasurement* measurement = this->measurement;
  if(!xrsummary->OWD.median_delay){
    goto done;
  }
  this->last_raise = xrsummary->OWD.max_delay;
  this->last_fall  = xrsummary->OWD.min_delay;

  measurement->drift = this->last_raise + this->last_fall;

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
        measurement->newly_received_bytes += packet->payload_size;
      }
      continue;
    }
    packet->acknowledged = TRUE;
    packet->lost = !xr->LostRLE.vector[i];

    if(!packet->lost){
      measurement->newly_received_bytes += packet->payload_size;
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
    }
    this->HSN = last_seq;

    _stat(this)->sent_bytes_in_srtt = 0;
    boundary = last_packet_sent_time - MAX(100 * GST_MSECOND, _stat(this)->srtt);
    //calculate lost packets in srtt
    for(i=0, --end_seq, act_seq = xr->LostRLE.begin_seq; act_seq != end_seq; --end_seq, ++i){
      packet = sndtracker_retrieve_sent_packet(this->sndtracker, this->subflow->id, end_seq);
      if(!packet){
        continue;
      }
      if(packet->sent < boundary){
        break;
      }
      _stat(this)->sent_bytes_in_srtt += packet->payload_size;
      ++sent_packets;
      if(!packet->lost){
        continue;
      }
      ++lost_packets;
    }
    if(0 < sent_packets){
      measurement->fraction_lost = (gdouble) lost_packets / (gdouble) sent_packets;
    }
  }

done:
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

  if(measurement->ref == 1){//to assign only once.
    measurement->bytes_in_flight = sndstat->bytes_in_flight;
    measurement->sr_avg = _stat(this)->sr_avg;
  }

  if(_stat(this)->rr_avg){
    alpha = ewma_factor * .5 + .5;
    _stat(this)->rr_avg = alpha * _stat(this)->receiver_bitrate + _stat(this)->rr_avg * (1.-alpha);
  }else{
    _stat(this)->rr_avg = _stat(this)->receiver_bitrate;
  }

  _stat(this)->rr_sr_corr =  _stat(this)->rr_avg / this->sr_avg_srtt;
//  _stat(this)->rr_sr_corr =  (gdouble)_stat(this)->received_bytes_in_srtt / (gdouble)_stat(this)->sent_bytes_in_srtt;

  DISABLE_LINE _owd_logger(this);

  slidingwindow_refresh(this->srtt_sw);
  slidingwindow_refresh(this->long_sw);
  slidingwindow_refresh(this->short_sw);
}



void _on_BiF_80th_calculated(FRACTaLFBProcessor *this, swpercentilecandidates_t *candidates)
{
  PercentileResult(FRACTaLMeasurement,   \
                   bytes_in_flight,       \
                   candidates,            \
                   _stat(this)->BiF_80th, \
                   this->BiF_min, \
                   this->BiF_max,  \
                   0                      \
                   );
}

static gdouble _calculate_std(FRACTaLStatHelper* helper, gdouble new_item)
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

static gdouble  _calculate_avg(FRACTaLStatHelper* helper, gint64 new_item, gint multiplier){
  helper->counter += 1 * multiplier;
  helper->sum += new_item * multiplier;
  if(0 < helper->counter){
    return (gdouble)helper->sum / (gdouble)helper->counter;
  }
  return 0.;
}

static gdouble  _calculate_avg_double(FRACTaLStatHelper* helper, gdouble new_item, gint multiplier){
  helper->counter += 1 * multiplier;
  helper->sum += new_item * multiplier;
  if(0 < helper->counter){
    return (gdouble)helper->sum / (gdouble)helper->counter;
  }
  return 0.;
}

void _on_short_sw_rem(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement)
{
  _stat(this)->BiF_avg = _calculate_avg(&this->BiF_stat_helper, measurement->bytes_in_flight, -1);
}

void _on_short_sw_add(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement)
{
  _stat(this)->BiF_avg = _calculate_avg(&this->BiF_stat_helper, measurement->bytes_in_flight, 1);
  _stat(this)->BiF_std = _calculate_std(&this->BiF_stat_helper, measurement->bytes_in_flight);
}

void _on_long_sw_rem(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement)
{
  _stat(this)->drift_avg = _calculate_avg(&this->drift_stat_helper, measurement->drift, -1);
  _stat(this)->lost_avg = _calculate_avg_double(&this->lost_stat_helper, measurement->fraction_lost, -1);
}

void _on_long_sw_add(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement)
{
  _stat(this)->drift_avg = _calculate_avg(&this->drift_stat_helper, measurement->drift, 1);
  _stat(this)->drift_std = _calculate_std(&this->drift_stat_helper, measurement->drift);

  _stat(this)->lost_avg = _calculate_avg_double(&this->lost_stat_helper, measurement->fraction_lost, 1);
  _stat(this)->lost_std = _calculate_std(&this->lost_stat_helper, measurement->fraction_lost);

}


void _on_srtt_sw_add(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement)
{
  _stat(this)->received_bytes_in_srtt += measurement->newly_received_bytes;
}

void _on_srtt_sw_rem(FRACTaLFBProcessor *this, FRACTaLMeasurement* measurement)
{
  _stat(this)->received_bytes_in_srtt -= measurement->newly_received_bytes;
  this->sr_avg_srtt = measurement->sr_avg;
}



