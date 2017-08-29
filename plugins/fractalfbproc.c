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

typedef struct {
//  gint64 skew;
  gdouble fraction_lost;
  gdouble psi;
//  gint32  bytes_in_flight;
  GstClockTime rtt;
  guint32 rtt_in_ts;
  gint32 extra_bytes;
  guint32 dts;
  GstClockTime queue_delay;
}ReferencePoint;

typedef struct {
  gdouble psi;
  gint32  extra_bytes;
}DistortionPoint;


static void fractalfbprocessor_finalize (GObject * object);
static void _process_cc_rle_discvector(FRACTaLFBProcessor *this, GstMPRTCPXRReportSummary *xr);
static void _process_stat(FRACTaLFBProcessor *this);

//void _push_rcvd_packets_in_ewi(FRACTaLFBProcessor *this, SndPacket* packet);

static guint32 _delta_ts(guint32 last_ts, guint32 actual_ts) {
  if (last_ts <= actual_ts) {
    return actual_ts - last_ts;
  } else {
    return 4294967296 - last_ts + actual_ts;
  }
}

//Formally, the number being subtracted is known as the subtrahend
//while the number it is subtracted from is the minuend
#define _subtract_ts(minuend, subtrahend) _delta_ts(subtrahend, minuend)

//static gint
//_cmp_ts (guint32 x, guint32 y)
//{
//  if(x == y) return 0;
//  if(x < y && y - x < 2147483648) return -1;
//  if(x > y && x - y > 2147483648) return -1;
//  if(x < y && y - x > 2147483648) return 1;
//  if(x > y && x - y < 2147483648) return 1;
//  return 0;
//}


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

//static gint
//_cmp_rcv_snd_ts (SndPacket* x, SndPacket *y)
//{
//  guint32 dx = _delta_ts(x->sent_ts, x->rcvd_ts);
//  guint32 dy = _delta_ts(y->sent_ts, y->rcvd_ts);
//  return dx == dy ? 0 : dx < dy ? -1 : 1;
//}



DEFINE_RECYCLE_TYPE(static, reference_point, ReferencePoint);

static void _reference_point_shaper(ReferencePoint* to, ReferencePoint* from) {
  memcpy(to, from, sizeof(ReferencePoint));
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

  g_object_unref(this->reference_point_recycle);
  g_object_unref(this->sysclock);
  g_object_unref(this->sndtracker);
}

void
fractalfbprocessor_init (FRACTaLFBProcessor * this)
{
  this->sysclock         = gst_system_clock_obtain();
}


static void _on_min_dts_calculated(FRACTaLFBProcessor* this, swminmaxstat_t* stat) {
  this->min_dts = ((ReferencePoint*)stat->min)->dts;
}

swplugin_define_on_calculated_double(FRACTaLStat, _on_lost_avg_calculated, fl_avg);
swplugin_define_on_calculated_double(FRACTaLStat, _on_lost_std_calculated, fl_std);

swplugin_define_on_calculated_data(FRACTaLFBProcessor, _on_dts_50th_calculated, dts_50th, guint32);
swplugin_define_on_calculated_data(FRACTaLStat, _on_queue_delay_50th_calculated, queue_delay_50th, GstClockTime);

StructCmpFnc(_cmp_reference_point_dts, ReferencePoint, dts);
StructCmpFnc(_cmp_reference_point_queue_delay, ReferencePoint, queue_delay);
swplugin_define_swdoubleextractor(_fraction_lost_extractor, ReferencePoint, fraction_lost);

swplugin_define_swdoubleextractor(_rtt_extractor, ReferencePoint, rtt);
swplugin_define_on_calculated_data(FRACTaLStat, _on_srtt_calculated, srtt, gdouble);

swplugin_define_swdataextractor(gdouble, _queue_delay_value_extractor, ReferencePoint, queue_delay);
swplugin_define_swdataptrextractor(_queue_delay_ptr_extractor, ReferencePoint, queue_delay);
swplugin_define_on_calculated_data(FRACTaLStat, _on_queue_delay_std_calculated, queue_delay_std, gdouble);

static void _on_packet_sent(FRACTaLFBProcessor* this, SndPacket* packet);
static void _on_psi_sw_rem(FRACTaLFBProcessor *this, SndPacket* packet);
static void _on_psi_postprocessor(FRACTaLFBProcessor *this, SndPacket* packet);
static void _refresh_windows_thresholds(FRACTaLFBProcessor *this);

void fractalfbprocessor_set_evaluation_window_margins(FRACTaLFBProcessor *this, GstClockTime min, GstClockTime max) {

  this->min_ewi_in_ts = timestamp_generator_get_ts_for_time(this->ts_generator, min);
  this->max_ewi_in_ts = timestamp_generator_get_ts_for_time(this->ts_generator, max);
  _refresh_windows_thresholds(this);
}


static gboolean _psi_sw_obsolate(FRACTaLFBProcessor *this, SlidingWindowItem* item) {
  SndPacket* head = item->data;
  SndPacket* tail = slidingwindow_peek_newest(this->psi_sw);
  guint32 delta_ts;
  if (!head->acknowledged) {
    delta_ts  = _delta_ts(head->sent_ts, tail->sent_ts);
    return GST_SECOND < timestamp_generator_get_time(this->ts_generator, delta_ts);
  }
  if (!tail->acknowledged || head->lost || tail->lost) {
    delta_ts  = _delta_ts(head->sent_ts, tail->sent_ts);
    return 2 * _stat(this)->srtt < timestamp_generator_get_time(this->ts_generator, delta_ts);
  }
  delta_ts  = _delta_ts(head->rcvd_ts, tail->rcvd_ts);
  return 2 * _stat(this)->srtt < timestamp_generator_get_time(this->ts_generator, delta_ts);
}

static gboolean _swminmax_filter(ReferencePoint* item) {
  return 0 < item->dts;
}

FRACTaLFBProcessor *make_fractalfbprocessor(SndTracker* sndtracker, SndSubflow* subflow, FRACTaLStat *stat)
{
  FRACTaLFBProcessor *this;
  SlidingWindowPlugin* swminmax;
  this = g_object_new (FRACTALFBPROCESSOR_TYPE, NULL);
  this->sndtracker   = g_object_ref(sndtracker);
  this->subflow      = subflow;
  this->stat         = stat;
  this->reference_point_recycle = make_recycle_reference_point(200, (RecycleItemShaper) _reference_point_shaper);
  this->reference_sw = make_slidingwindow_with_data_recycle(200, 15 * GST_SECOND, this->reference_point_recycle);
  this->ewi_sw = make_slidingwindow_uint32(200, GST_SECOND);
  this->psi_sw = make_slidingwindow(500, GST_SECOND);
  this->ts_generator = sndtracker_get_ts_generator(sndtracker);
  this->sent_packets = g_queue_new();

  sndtracker_add_on_packet_sent(this->sndtracker, (ListenerFunc)_on_packet_sent, this);

//  correlator_add_on_correlation_calculated_listener(this->drift_correlator, (ListenerFunc) _on_drift_corr_calculated, stat);
  swminmax = make_swminmax( (GCompareFunc) _cmp_reference_point_dts, (ListenerFunc) _on_min_dts_calculated, this);
  swminmax_set_filter(swminmax, (SWPluginFilterFunc)_swminmax_filter);

  slidingwindow_setup_custom_obsolation(this->psi_sw, (SlidingWindowObsolateFunc) _psi_sw_obsolate, this);
  slidingwindow_add_on_rem_item_cb(this->psi_sw, (ListenerFunc) _on_psi_sw_rem, this);
  slidingwindow_add_postprocessor(this->psi_sw, (ListenerFunc) _on_psi_postprocessor, this);

  slidingwindow_add_plugins(this->reference_sw,
//          make_swpercentile(80, _measurement_drift_cmp, (ListenerFunc) _on_drift_80th_calculated, this),
          make_swpercentile2(50,
              (GCompareFunc) _cmp_reference_point_queue_delay,
              (ListenerFunc) _on_queue_delay_50th_calculated,
              stat,
              (SWExtractorFunc) _queue_delay_ptr_extractor,
              (SWMeanCalcer) swpercentile2_prefer_right_selector,
              (SWEstimator) swpercentile2_prefer_right_selector),
          make_swstd(_on_queue_delay_std_calculated, stat, _queue_delay_value_extractor, 0),
          swminmax,
          make_swavg(_on_lost_avg_calculated, stat, _fraction_lost_extractor),
          make_swstd(_on_lost_std_calculated, stat, _fraction_lost_extractor, 100),
          make_swavg(_on_srtt_calculated, stat, _rtt_extractor),
          NULL);

  slidingwindow_add_plugins(this->ewi_sw,
          make_swpercentile2(50,
              (GCompareFunc) bintree3cmp_uint32,
              (ListenerFunc) _on_dts_50th_calculated,
              this,
              (SWExtractorFunc) swpercentile2_self_extractor,
              (SWMeanCalcer) swpercentile2_prefer_right_selector,
              (SWEstimator) swpercentile2_prefer_right_selector),
          NULL);

  fractalfbprocessor_set_evaluation_window_margins(this, 0.25 * GST_SECOND, 0.5 * GST_SECOND);
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

  if(summary->XR.CongestionControlFeedback.processed){
    _process_cc_rle_discvector(this, &summary->XR);
    process = TRUE;
  }

  if(!process){
    goto done;
  }

  _process_stat(this);
  this->last_report_update = now;
done:
  return;
}

void fractalfbprocessor_approve_feedback(FRACTaLFBProcessor *this)
{
  ReferencePoint reference_point;

  reference_point.fraction_lost = CONSTRAIN(0.0, 1.0, _stat(this)->fraction_lost);
  reference_point.rtt = this->rtt;
  reference_point.rtt_in_ts = this->rtt_in_ts;
  reference_point.extra_bytes = _stat(this)->psi_extra_bytes;
  reference_point.dts = this->dts_50th;
  reference_point.queue_delay = _stat(this)->last_queue_delay;
  this->srtt_in_ts = this->srtt_in_ts * .9 + this->rtt_in_ts * .1;
  slidingwindow_add_data(this->reference_sw,  &reference_point);
}

static gdouble _get_ewma_factor(FRACTaLFBProcessor *this){
  if(_stat(this)->last_queue_delay < 1){
    return 0.;
  }
  return  (gdouble) _stat(this)->last_queue_delay / (gdouble)(_stat(this)->last_queue_delay + _stat(this)->queue_delay_std);
}


void _process_cc_rle_discvector(FRACTaLFBProcessor *this, GstMPRTCPXRReportSummary *xr)
{
  SndPacket* packet = NULL;
  guint16 act_seq, end_seq;
  gint i;
  guint32 reference_dts = 0, reference_sent_ts = 0, reference_ato = 0;
  gboolean reference_dts_init = FALSE;
  guint32 act_ato;
  guint32 report_timestamp;

  act_seq = xr->CongestionControlFeedback.begin_seq;
  end_seq = xr->CongestionControlFeedback.end_seq;
  report_timestamp = xr->CongestionControlFeedback.report_timestamp;
  if(act_seq == end_seq){
    goto done;
  }
  _stat(this)->HSN = end_seq;

//  g_print("RLE vector from %hu until %hu\n", act_seq, end_seq);
  for(i=0; act_seq != end_seq; ++act_seq, ++i) {
    packet = sndtracker_retrieve_sent_packet(this->sndtracker, this->subflow->id, act_seq);
    if(!packet){
      GST_WARNING_OBJECT(this, "Packet %hu has not in subflow tracked sequences. "
          "Either too late acknowledged or never sent", act_seq);
      continue;
    }

    act_ato = xr->CongestionControlFeedback.vector[i].ato;

    if(packet->acknowledged) {
      if(packet->lost && xr->CongestionControlFeedback.vector[i].lost) {
        --this->psi_lost_packets;
        ++this->psi_received_packets;
        this->psi_received_bytes += packet->payload_size;
        packet->lost = FALSE;
        packet->rcvd_ts = _subtract_ts(report_timestamp, act_ato);
        sndtracker_packet_found(this->sndtracker, packet);
      }
      continue;
    }

    if (!xr->CongestionControlFeedback.vector[i].lost) {
      packet->acknowledged = TRUE;
      packet->lost = TRUE;
      packet->skew = 0;
      ++this->psi_lost_packets;
      sndtracker_packet_acked(this->sndtracker, packet);
      continue;
    }

    packet->rcvd_ts = _subtract_ts(report_timestamp, act_ato);
    {
      guint32 dts = _delta_ts(packet->sent_ts, packet->rcvd_ts);
      slidingwindow_add_data(this->ewi_sw, &dts);
      if (!reference_dts_init || dts < reference_dts) {
        reference_dts = dts;
        reference_sent_ts = packet->sent_ts;
        reference_ato = act_ato;
        reference_dts_init = TRUE;
      }
    }

    this->psi_received_bytes += packet->payload_size;
    ++this->psi_received_packets;
    packet->acknowledged = TRUE;
    packet->lost = FALSE;
    sndtracker_packet_acked(this->sndtracker, packet);
  }

  if (reference_sent_ts) {
    guint32 current_ts = timestamp_generator_get_ts(this->ts_generator);
    this->rtt_in_ts = _subtract_ts(_subtract_ts(current_ts, reference_sent_ts), reference_ato);
    this->rtt = timestamp_generator_get_time(this->ts_generator, this->rtt_in_ts);

    if (!this->srtt_in_ts) {
      this->srtt_in_ts = this->rtt_in_ts;
    }
  }

  _refresh_windows_thresholds(this);
done:
  return;
}


void _process_stat(FRACTaLFBProcessor *this)
{

  SndTrackerStat*     sndstat     = sndtracker_get_subflow_stat(this->sndtracker, this->subflow->id);
  RTPQueueStat*       rtpqstat    = sndtracker_get_rtpqstat(this->sndtracker);

  gdouble ewma_factor         = _get_ewma_factor(this);
  gdouble alpha;

  _stat(this)->rtpq_delay            = rtpqstat->rtpq_delay;
  _stat(this)->sender_bitrate        = sndstat->sent_bytes_in_1s * 8;
  _stat(this)->receiver_bitrate      = sndstat->received_bytes_in_1s * 8;
  _stat(this)->fec_bitrate           = sndstat->sent_fec_bytes_in_1s * 8;
  _stat(this)->measurements_num      = slidingwindow_get_counter(this->reference_sw);
  _stat(this)->sent_packets_in_1s    = sndstat->sent_packets_in_1s;


//  g_print("%d %d - %d\n", slidingwindow_get_counter(this->ewi_sw), this->dts_50th, this->min_dts);
  if (0 < this->min_dts && this->min_dts < this->dts_50th) {
    _stat(this)->last_queue_delay =
          GST_TIME_AS_MSECONDS(timestamp_generator_get_time(this->ts_generator, this->dts_50th - this->min_dts));
  } else {
    _stat(this)->last_queue_delay = 0;
  }

  _stat(this)->psi_received_bytes    = this->psi_received_bytes;
  _stat(this)->psi_sent_bytes        = this->psi_sent_bytes;
  _stat(this)->psi_extra_bytes       = MAX(0, this->psi_sent_bytes - this->psi_received_bytes);

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

  // psi thing

  slidingwindow_refresh(this->reference_sw);
  slidingwindow_refresh(this->psi_sw);
  slidingwindow_refresh(this->ewi_sw);

  if (!this->psi_received_packets || !this->psi_lost_packets) {
    _stat(this)->fraction_lost = 0;
  } else {
    _stat(this)->fraction_lost = (gdouble) this->psi_lost_packets /
        (gdouble) (this->psi_lost_packets + this->psi_received_packets);
  }
}



static void _on_packet_sent(FRACTaLFBProcessor* this, SndPacket* packet) {
  if(packet->subflow_id != this->subflow->id){
    return;
  }
  g_queue_push_tail(this->sent_packets, sndpacket_ref(packet));
  do{
    SndPacket* head;
    if (g_queue_is_empty(this->sent_packets)) {
      return;
    }
    head = g_queue_peek_head(this->sent_packets);
    if (packet->sent - head->sent < _stat(this)->srtt) {
      return;
    }
    this->psi_sent_bytes += head->payload_size;
    ++this->psi_sent_packets;
    slidingwindow_add_data(this->psi_sw, g_queue_pop_head(this->sent_packets));
  } while(1);
}


static void _on_psi_sw_rem(FRACTaLFBProcessor *this, SndPacket* packet) {
  this->psi_sent_bytes -= packet->payload_size;
  --this->psi_sent_packets;

  if (packet->lost) {
    --this->psi_lost_packets;
  } else if (packet->acknowledged){
    --this->psi_received_packets;
    this->psi_received_bytes -= packet->payload_size;
//    g_print("%d:\n", this->psi_received_bytes);
  }

}

static void _on_psi_postprocessor(FRACTaLFBProcessor *this, SndPacket* packet) {
  sndpacket_unref(packet);
}

static void _refresh_windows_thresholds(FRACTaLFBProcessor *this) {

  guint32 ewi_in_ts = CONSTRAIN(this->min_ewi_in_ts, this->max_ewi_in_ts, this->srtt_in_ts);
  GstClockTime ewi_in_ns = timestamp_generator_get_time(this->ts_generator, ewi_in_ts);
  guint32 psi_in_ns = MIN(_stat(this)->srtt + _stat(this)->queue_delay_std * 2, GST_SECOND);

  slidingwindow_set_threshold(this->ewi_sw, ewi_in_ns);
//  slidingwindow_set_threshold(this->ewi_sw, .1 * GST_SECOND);
  slidingwindow_set_threshold(this->psi_sw, psi_in_ns);
}

