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
  gint64 skew;
  gdouble fraction_lost;
  gdouble psi;
  gint32  bytes_in_flight;
  GstClockTime rtt;
  guint32 rtt_in_ts;
  gint32 extra_bytes;
}ReferencePoint;

typedef struct {
  gdouble psi;
  gint32  extra_bytes;
}DistortionPoint;


//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void fractalfbprocessor_finalize (GObject * object);
static void _process_cc_rle_discvector(FRACTaLFBProcessor *this, GstMPRTCPXRReportSummary *xr);
static void _process_stat(FRACTaLFBProcessor *this);

static void _refresh_sent_packets_in_ewi_t(FRACTaLFBProcessor* this);
void _push_rcvd_packets_in_ewi(FRACTaLFBProcessor *this, SndPacket* packet);
static void _refresh_rcvd_packets_in_ewi(FRACTaLFBProcessor* this);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

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

static gint
_cmp_ts (guint32 x, guint32 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 2147483648) return -1;
  if(x > y && x - y > 2147483648) return -1;
  if(x < y && y - x > 2147483648) return 1;
  if(x > y && x - y < 2147483648) return 1;
  return 0;
}


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



DEFINE_RECYCLE_TYPE(static, reference_point, ReferencePoint);
DEFINE_RECYCLE_TYPE(static, distortion_point, DistortionPoint);

static void _reference_point_shaper(ReferencePoint* to, ReferencePoint* from) {
  memcpy(to, from, sizeof(ReferencePoint));
}

static void _distortion_point_shaper(DistortionPoint* to, DistortionPoint* from) {
  memcpy(to, from, sizeof(DistortionPoint));
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

  g_object_unref(this->reference_sw);

  g_object_unref(this->reference_point_recycle);
  g_object_unref(this->sysclock);
  g_object_unref(this->sndtracker);
}

void
fractalfbprocessor_init (FRACTaLFBProcessor * this)
{
  this->sysclock         = gst_system_clock_obtain();
}

static void _on_minmax_distortion_point_calculated(gpointer udata, swminmaxstat_t* minmax) {
  DistortionPoint* max = minmax->max;
  FRACTaLStat* stat = udata;
  stat->max_psi = MAX(max->psi, 1.);
  stat->max_extra_bytes = max->extra_bytes;
}

swplugin_define_on_calculated_double(FRACTaLStat, _on_skew_std_calculated, skew_std);
swplugin_define_on_calculated_double(FRACTaLStat, _on_psi_std_calculated, psi_std);

swplugin_define_on_calculated_double(FRACTaLStat, _on_lost_avg_calculated, fl_avg);
swplugin_define_on_calculated_double(FRACTaLStat, _on_lost_std_calculated, fl_std);


swplugin_define_swdataextractor(_BiF_extractor, ReferencePoint, bytes_in_flight);
swplugin_define_on_calculated_double(FRACTaLStat, _on_BiF_std_calculated, BiF_std);
static void _on_skew_80th_calculated(gpointer udata, gpointer result){
  ((FRACTaLStat*)udata)->skew_80th = MAX(0, *(gint64*)result);
}

StructCmpFnc(_cmp_distortion_point_psi, DistortionPoint, psi);
swplugin_define_swdataextractor(_fraction_lost_extractor, ReferencePoint, fraction_lost);
swplugin_define_swdataptrextractor(_skew_ptr_extractor, ReferencePoint, skew);
swplugin_define_swdataextractor(_skew_value_extractor, ReferencePoint, skew);
swplugin_define_swdataextractor(_psi_value_extractor, DistortionPoint, psi);

//swplugin_define_swdataptrextractor(_extra_bytes_ptr_extractor, ReferencePoint, extra_bytes);

swplugin_define_on_calculated_data(FRACTaLStat, _on_srtt_calculated, srtt, gdouble);
swplugin_define_swdataextractor(_rtt_extractor, ReferencePoint, rtt);


static gint _cmp_reference_point_skew(gpointer pa, gpointer pb)
{
  return bintree3cmp_int64(&((ReferencePoint*)pa)->skew, &((ReferencePoint*)pb)->skew);
}

static void _on_packet_sent(FRACTaLFBProcessor* this, SndPacket* packet) {
  if(packet->subflow_id != this->subflow->id){
    return;
  }
  g_queue_push_tail(this->sent_packets, sndpacket_ref(packet));
}

static void _on_packet_queued(FRACTaLFBProcessor* this, SndPacket* packet){
  if(packet->subflow_id != this->subflow->id){
    return;
  }
  _stat(this)->queued_bytes_in_srtt += packet->payload_size;
  g_queue_push_tail(this->queued_packets_rtt, sndpacket_ref(packet));
  do {
    SndPacket* head;
    if (g_queue_is_empty(this->queued_packets_rtt)) {
      return;
    }
    head = g_queue_peek_head(this->queued_packets_rtt);
    if (packet->queued - _stat(this)->srtt <= head->queued) {
      return;
    }
    head = g_queue_pop_head(this->queued_packets_rtt);
    _stat(this)->queued_bytes_in_srtt -= head->payload_size;
    sndpacket_unref(head);
  }while(1);
}

static void _refresh_ewi(FRACTaLFBProcessor *this) {
  this->ewi_in_ts = CONSTRAIN(this->min_ewi_in_ts, this->max_ewi_in_ts, this->srtt_in_ts);
  _stat(this)->ewi_in_s = (gdouble) timestamp_generator_get_time(this->ts_generator, this->ewi_in_ts) / (gdouble) GST_SECOND;
  slidingwindow_set_threshold(this->distortions_sw, MAX(2 * _stat(this)->srtt, _stat(this)->ewi_in_s)  * GST_SECOND);
}

void fractalfbprocessor_set_evaluation_window_margins(FRACTaLFBProcessor *this, GstClockTime min, GstClockTime max) {

  this->min_ewi_in_ts = timestamp_generator_get_ts_for_time(this->ts_generator, min);
  this->max_ewi_in_ts = timestamp_generator_get_ts_for_time(this->ts_generator, max);
  _refresh_ewi(this);
}


FRACTaLFBProcessor *make_fractalfbprocessor(SndTracker* sndtracker, SndSubflow* subflow, FRACTaLStat *stat)
{
  FRACTaLFBProcessor *this;
  this = g_object_new (FRACTALFBPROCESSOR_TYPE, NULL);
  this->sndtracker   = g_object_ref(sndtracker);
  this->subflow      = subflow;
  this->stat         = stat;
  this->reference_point_recycle = make_recycle_reference_point(200, (RecycleItemShaper) _reference_point_shaper);
  this->distortion_point_recycle = make_recycle_distortion_point(100, (RecycleItemShaper) _distortion_point_shaper);
  this->reference_sw = make_slidingwindow_with_data_recycle(200, 15 * GST_SECOND, this->reference_point_recycle);
  this->distortions_sw = make_slidingwindow_with_data_recycle(100, GST_SECOND, this->distortion_point_recycle);
  this->ts_generator = sndtracker_get_ts_generator(sndtracker);
  this->sent_packets = g_queue_new();
  this->rcvd_packets_ewi = g_queue_new();
  this->queued_packets_rtt = g_queue_new();

  sndtracker_add_on_packet_sent(this->sndtracker, (ListenerFunc)_on_packet_sent, this);
  sndtracker_add_on_packet_queued(this->sndtracker, (ListenerFunc)_on_packet_queued, this);

//  correlator_add_on_correlation_calculated_listener(this->drift_correlator, (ListenerFunc) _on_drift_corr_calculated, stat);

  slidingwindow_add_plugins(this->reference_sw,
//          make_swpercentile(80, _measurement_drift_cmp, (ListenerFunc) _on_drift_80th_calculated, this),
          make_swpercentile2(80,
              (GCompareFunc) _cmp_reference_point_skew,
              (ListenerFunc) _on_skew_80th_calculated,
              stat,
              (SWExtractorFunc) _skew_ptr_extractor,
              (SWMeanCalcer) swpercentile2_prefer_right_selector,
              (SWEstimator) swpercentile2_prefer_right_selector),
          make_swstd(_on_skew_std_calculated, stat, _skew_value_extractor, 0),
          make_swavg(_on_lost_avg_calculated, stat, _fraction_lost_extractor),
          make_swstd(_on_lost_std_calculated, stat, _fraction_lost_extractor, 100),
          make_swavg(_on_srtt_calculated, stat, _rtt_extractor),
          make_swstd(_on_BiF_std_calculated, stat, _BiF_extractor, 0),
          NULL);

  slidingwindow_add_plugins(this->distortions_sw,
          make_swminmax(
              (GCompareFunc) _cmp_distortion_point_psi,
              (ListenerFunc) _on_minmax_distortion_point_calculated,
              stat),
          make_swstd(_on_psi_std_calculated, stat, _psi_value_extractor, 100),
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

  if(summary->XR.DiscardedBytes.processed){
    g_print("Dsicarded byte matric is not used now. Turn it off at producer or implement a handler function");
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
  reference_point.bytes_in_flight = _stat(this)->bytes_in_flight;
  reference_point.fraction_lost = _stat(this)->fraction_lost;
  reference_point.skew = _stat(this)->last_skew;
  reference_point.rtt = this->rtt;
  reference_point.rtt_in_ts = this->rtt_in_ts;
  reference_point.extra_bytes = _stat(this)->extra_bytes;
//  g_print("extra_bytes: %d\n", _stat(this)->extra_bytes);
  slidingwindow_add_data(this->reference_sw,  &reference_point);
  _refresh_ewi(this);

  this->srtt_in_ts = this->srtt_in_ts * .9 + this->rtt_in_ts * .1;
}

static gdouble _get_ewma_factor(FRACTaLFBProcessor *this){
  if(_stat(this)->last_skew < 1){
    return 0.;
  }
  return  (gdouble) _stat(this)->last_skew / (gdouble)(_stat(this)->last_skew + _stat(this)->skew_std) ;
}

static SndPacket* _get_tail_rcved_packet(FRACTaLFBProcessor *this) {
  GList* it;
  for (it = this->rcvd_packets_ewi->tail; it; it = it->next) {
    SndPacket* packet = it->data;
    if (!packet->lost) {
      return packet;
    }
  }
  return NULL;
}

void _process_cc_rle_discvector(FRACTaLFBProcessor *this, GstMPRTCPXRReportSummary *xr)
{
  SndPacket* packet = NULL, *last_packet = NULL;
  guint16 act_seq, end_seq;
  gint i;
  guint32 reference_skew = 0, reference_sent_ts = 0, reference_ato = 0;
  gboolean reference_skew_init = FALSE;
  guint32 act_ato;
  guint32 report_timestamp;

  act_seq = xr->CongestionControlFeedback.begin_seq;
  end_seq = xr->CongestionControlFeedback.end_seq;
  report_timestamp = xr->CongestionControlFeedback.report_timestamp;
  if(act_seq == end_seq){
    goto done;
  }

//  g_print("RLE vector from %hu until %hu\n", act_seq, end_seq);
  last_packet = _get_tail_rcved_packet(this);
  for(i=0; act_seq != end_seq; ++act_seq, ++i) {

    packet = sndtracker_retrieve_sent_packet(this->sndtracker, this->subflow->id, act_seq);

    if(!packet){
      GST_WARNING_OBJECT(this, "Packet %hu has not in subflow tracked sequences. "
          "Either too late acknowledged or never sent", act_seq);
      continue;
    }

    act_ato = xr->CongestionControlFeedback.vector[i].ato;

    if(packet->acknowledged){
      if(packet->lost && xr->CongestionControlFeedback.vector[i].lost) {
        --this->lost_packets_num_in_ewi;
        this->rcvd_bytes_in_ewi += packet->payload_size;
        packet->lost = FALSE;
        packet->rcvd_ts = _subtract_ts(report_timestamp, act_ato);
        _push_rcvd_packets_in_ewi(this, packet);
        sndtracker_packet_found(this->sndtracker, packet);
      }
      continue;
    }

    if (!xr->CongestionControlFeedback.vector[i].lost) {
      packet->acknowledged = TRUE;
      packet->lost = TRUE;
      packet->skew = 0;
      _push_rcvd_packets_in_ewi(this, packet);
      sndtracker_packet_acked(this->sndtracker, packet);
      continue;
    }

    packet->rcvd_ts = _subtract_ts(report_timestamp, act_ato);
    if (last_packet && last_packet->timestamp == packet->timestamp) {
      guint32 drcv_ts = _delta_ts(last_packet->rcvd_ts, packet->rcvd_ts);
      guint32 dsnd_ts = _delta_ts(last_packet->sent_ts, packet->sent_ts);
      gint32 skew =  (gint32)drcv_ts - (gint32)dsnd_ts;
      gint64 abs_skew = skew < 0 ? -1 * skew : skew;
      if (!reference_skew_init || abs_skew < reference_skew) {
        reference_skew_init = TRUE;
        reference_skew = abs_skew;
        reference_sent_ts = packet->sent_ts;
        reference_ato = xr->CongestionControlFeedback.vector[i].ato;
      }
      packet->skew = dsnd_ts < this->ewi_in_ts ? skew : 0;
    }
    last_packet = packet;
    packet->acknowledged = TRUE;
    packet->lost = FALSE;
    _push_rcvd_packets_in_ewi(this, packet);
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

done:
  _refresh_rcvd_packets_in_ewi(this);
//  g_print("subflow %d sent in ewi: %d (%d) "
//          "rcvd in ewi: %d (%d)\n",
//          this->subflow->id,
//          this->sent_bytes_in_ewi_t, this->sent_packets_num_in_ewi_t,
//          this->rcvd_bytes_in_ewi, this->rcvd_packets_num_in_ewi);
  if (this->sent_bytes_in_ewi_t < 10 || !this->rcvd_bytes_in_ewi) {
    _stat(this)->psi = 1.;
  } else {
    _stat(this)->psi = (gdouble) this->sent_bytes_in_ewi_t / (gdouble) this->rcvd_bytes_in_ewi;
  }

  if (!this->rcvd_packets_num_in_ewi || !this->lost_packets_num_in_ewi) {
    _stat(this)->fraction_lost = 0;
  } else {
    _stat(this)->fraction_lost = (gdouble) this->lost_packets_num_in_ewi /
        (gdouble) (this->lost_packets_num_in_ewi + this->rcvd_packets_num_in_ewi);
  }

  _stat(this)->extra_bytes = MAX(0, this->sent_bytes_in_ewi_t - this->rcvd_bytes_in_ewi + this->extra_bytes);

  {
    DistortionPoint distortion_point;
    distortion_point.psi = _stat(this)->psi;
    distortion_point.extra_bytes = _stat(this)->extra_bytes;
    slidingwindow_add_data(this->distortions_sw, &distortion_point);
  }
  return;
}


void _process_stat(FRACTaLFBProcessor *this)
{

  SndTrackerStat*     sndstat     = sndtracker_get_subflow_stat(this->sndtracker, this->subflow->id);
  RTPQueueStat*       rtpqstat    = sndtracker_get_rtpqstat(this->sndtracker);

  gdouble ewma_factor         = _get_ewma_factor(this);
  gdouble alpha;

  _stat(this)->rtpq_delay            = rtpqstat->rtpq_delay;
  _stat(this)->bytes_in_flight       = sndstat->bytes_in_flight;
  _stat(this)->sender_bitrate        = sndstat->sent_bytes_in_1s * 8;
  _stat(this)->receiver_bitrate      = sndstat->received_bytes_in_1s * 8;
  _stat(this)->fec_bitrate           = sndstat->sent_fec_bytes_in_1s * 8;
  _stat(this)->reference_num         = slidingwindow_get_counter(this->reference_sw);
  _stat(this)->received_bytes_in_ewi = this->rcvd_bytes_in_ewi;
  _stat(this)->sent_packets_in_1s    = sndstat->sent_packets_in_1s;

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

  slidingwindow_refresh(this->reference_sw);
  slidingwindow_refresh(this->distortions_sw);
}

void _refresh_sent_packets_in_ewi_t(FRACTaLFBProcessor* this) {
  //ewi - evaluation window interval
  SndPacket* rcvd_head;
  SndPacket* sent_head;
  SndPacket* sent_tail;
  GList* it;
  if (g_queue_is_empty(this->rcvd_packets_ewi)) {
    return;
  }
  rcvd_head = g_queue_peek_head(this->rcvd_packets_ewi);

  do {
    if(g_queue_is_empty(this->sent_packets)) {
      return;
    }
    sent_head = g_queue_peek_head(this->sent_packets);
    if (_cmp_seq(rcvd_head->subflow_seq, sent_head->subflow_seq) <= 0) {
      break;
    }
    sndpacket_unref(g_queue_pop_head(this->sent_packets));
  }while(1);

  sent_tail = this->sent_packets->tail->data;

  this->sent_bytes_in_ewi_t = 0;
  this->sent_packets_num_in_ewi_t = 0;
  for(it = this->sent_packets->head; it; it = it->next) {
    SndPacket* packet = it->data;
    if (this->ewi_in_ts <= _delta_ts(sent_head->sent_ts, packet->sent_ts)) {
      break;
    }
    if (_delta_ts(packet->sent_ts, sent_tail->sent_ts) < this->srtt_in_ts) {
      break;
    }
    this->sent_bytes_in_ewi_t += packet->payload_size;
    ++this->sent_packets_num_in_ewi_t;
  }

  if (!it) {
    return;
  }

  this->extra_bytes = 0;
  for(; it; it = it->next) {
    SndPacket* packet = it->data;
    if (_delta_ts(packet->sent_ts, sent_tail->sent_ts) <= this->rtt_in_ts) {
      break;
    }
    this->extra_bytes += packet->payload_size;
  }
}

void _push_rcvd_packets_in_ewi(FRACTaLFBProcessor *this, SndPacket* packet) {
  if (packet->lost) {
    ++this->lost_packets_num_in_ewi;
  } else {
    ++this->rcvd_packets_num_in_ewi;
    this->rcvd_bytes_in_ewi += packet->payload_size;
    _stat(this)->last_skew += packet->skew;
  }
  g_queue_push_tail(this->rcvd_packets_ewi, sndpacket_ref(packet));
}

void _refresh_rcvd_packets_in_ewi(FRACTaLFBProcessor* this) {
  //ewi - evaluation window interval
  SndPacket* head;
  SndPacket* tail;
  guint32 dts;
  guint32 dts_threshold = this->ewi_in_ts;
  do {
    if(g_queue_is_empty(this->rcvd_packets_ewi)) {
      break;
    }
    head = g_queue_peek_head(this->rcvd_packets_ewi);
    tail = g_queue_peek_tail(this->rcvd_packets_ewi);
    dts = _delta_ts(head->rcvd_ts, tail->rcvd_ts);
    if (_cmp_ts(dts, dts_threshold) < 0) {
      break;
    }
    if (head->lost) {
      --this->lost_packets_num_in_ewi;
    } else {
      --this->rcvd_packets_num_in_ewi;
      this->rcvd_bytes_in_ewi -= head->payload_size;
      _stat(this)->last_skew -= head->skew;
    }
    sndpacket_unref(g_queue_pop_head(this->rcvd_packets_ewi));
  }while(1);
  _refresh_sent_packets_in_ewi_t(this);
}


