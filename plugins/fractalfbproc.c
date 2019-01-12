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

//typedef struct {
//  gdouble fraction_lost;
//  GstClockTime rtt;
//  guint32 rtt_in_ts;
//}ReferencePoint;

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


// https://stackoverflow.com/questions/5083465/fast-efficient-least-squares-fit-algorithm-in-c
// linear regression
#include <stdlib.h>
#include <math.h>                           /* math functions */


static gboolean csv_header_printed = FALSE;
static void _print_packet_stat(FRACTaLFBProcessor *this, SndPacket* packet) {
  gchar result[1024];
  if (packet->subflow_id != this->subflow->id) {
    return;
  }
  memset(result, 0, 1024);

  if (!csv_header_printed) {
    sprintf(result,
        "Subflow ID,"             // 1
        "Elapsed time in sec,"    // 2
        "Minimal dts,"            // 3
        "Queue Delay,"            // 4
        "Seqence Number,"         // 5
        "Arrival State,"          // 6
        "Skew,"                   // 7
        "Ref,"                    // 8
        "Begin Sequence,"         // 9
        "End Sequcence,"          // 10
        "Sending Rate,"           // 11
        "Fraction Lost,"          // 12
        "QSample,"                // 13
        );

    g_print("Packet:%s\n",result);
    csv_header_printed = TRUE;
    memset(result, 0, 1024);
  }

  sprintf(result,
          "%2d,"     // 1
          "%3.1f,"   // 2
          "%6u,"     // 3
          "%6u,"     // 4
          "%6u,"     // 5
          "%1u,"     // 6
          "%6u,"     // 7
          "%2d,"     // 8
          "%6hu,"    // 9
          "%6hu,"    // 10
          "%6d,"     // 11
          "%f,"      // 12
          "%f,"      // 13
          ,
          this->subflow->id,                         // 1
          GST_TIME_AS_MSECONDS(_now(this) - this->made) / 1000., // 2
          this->min_dts,                             // 3
          packet->qts,                               // 4
          packet->subflow_seq,                       // 5
          packet->arrival_status,                    // 6
          packet->subflow_skew,                      // 7
          packet->ref,                               // 8
          this->cc_begin_seq,                        // 9
          this->cc_end_seq,                          // 10
          _stat(this)->sender_bitrate / 1000,        // 11
          _stat(this)->fraction_lost,                // 12
          packet->qsample                            // 13
          );

  g_print("Packet:%s\n",result);
  if (packet->arrival_status == 0) {
    g_print("Unknown: %hu, %hu\n", packet->subflow_seq, packet->subflow_id);
  }

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

  g_object_unref(this->sysclock);
  g_object_unref(this->sndtracker);
//  g_object_unref(this->qdelay_bucket);
}

void
fractalfbprocessor_init (FRACTaLFBProcessor * this)
{
  this->sysclock         = gst_system_clock_obtain();
}

static void _on_packet_sent(FRACTaLFBProcessor* this, SndPacket* packet);
static void _refresh_windows_thresholds(FRACTaLFBProcessor *this);


void fractalfbprocessor_set_evaluation_window_margins(FRACTaLFBProcessor *this, GstClockTime min, GstClockTime max) {

  this->min_ewi_in_ts = timestamp_generator_get_ts_for_time(this->ts_generator, min);
  this->max_ewi_in_ts = timestamp_generator_get_ts_for_time(this->ts_generator, max);
  _refresh_windows_thresholds(this);
}

static GstClockTime _get_fluctuationcalcer_time_validity(FRACTaLFBProcessor* this) {
	return  _stat(this)->srtt * 2;
}

FRACTaLFBProcessor *make_fractalfbprocessor(SndTracker* sndtracker, SndSubflow* subflow, FRACTaLStat *stat)
{
  FRACTaLFBProcessor *this;
  this = g_object_new (FRACTALFBPROCESSOR_TYPE, NULL);
  this->sndtracker   = g_object_ref(sndtracker);
  this->subflow      = subflow;
  this->stat         = stat;
  this->ts_generator = sndtracker_get_ts_generator(sndtracker);
  this->qdelay_stability_calcer = make_qdelay_stability_calcer();
  this->fl_stability_calcer = make_fl_stability_calcer();
  this->dts = 0;
  this->made = _now(this);

  this->qd_fluctuationcalcer = make_fluctuationcalcer();
  fluctuationcalcer_setup_time_threshold_provider(this->qd_fluctuationcalcer, (FluctuationCalcerTimeValidityProvider) _get_fluctuationcalcer_time_validity, this);
//  this->qdelay_bucket = make_bucket(QDELAY_BUCKET_LIST_LENGTH, 10 * GST_MSECOND);
//  this->qdelay_devs = make_bucket(2, 0);

  sndtracker_add_on_packet_sent(this->sndtracker, (ListenerFunc)_on_packet_sent, this);
  DISABLE_LINE sndtracker_add_on_packet_obsolated(this->sndtracker, (ListenerFunc)_print_packet_stat, this);

  fractalfbprocessor_set_evaluation_window_margins(this, 0.25 * GST_SECOND, 0.5 * GST_SECOND);


  return this;
}

void fractalfbprocessor_reset(FRACTaLFBProcessor *this)
{
  this->min_dts = 0;
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
  {
    GstClockTime interval = now - this->last_report_update;
    this->fb_interval_avg = this->fb_interval_avg * .8 + interval * .2;
  }
  this->last_report_update = now;
  if (!this->first_report_update) {
    this->first_report_update = now;
  }
done:
  return;
}

void fractalfbprocessor_approve_feedback(FRACTaLFBProcessor *this)
{
  this->srtt_in_ts = this->srtt_in_ts * .9 + this->rtt_in_ts * .1;
  this->subflow->rtt = _stat(this)->srtt = timestamp_generator_get_time(this->ts_generator, this->srtt_in_ts);
}


static gdouble _get_ewma_factor(FRACTaLFBProcessor *this){
  if(this->qts_std < 1){
    return 0.;
  }
  return  (gdouble) this->qts_std / (gdouble)(this->qts_std +
      timestamp_generator_get_ts_for_time(this->ts_generator, 10 * GST_MSECOND));
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
  gint number_of_qd_packets = 0;

  this->cc_begin_seq = act_seq = xr->CongestionControlFeedback.begin_seq;
  this->cc_end_seq = end_seq = xr->CongestionControlFeedback.end_seq;
  report_timestamp = xr->CongestionControlFeedback.report_timestamp;
//  g_print("CC ack at %d begin: %hu end: %hu\n",
//        this->subflow->id, act_seq, end_seq);
  if(act_seq == end_seq){
    goto done;
  }
  _stat(this)->HSN = end_seq;
  this->min_dts += timestamp_generator_get_ts_for_time(this->ts_generator, 5 * GST_MSECOND);
  this->min_qts += timestamp_generator_get_ts_for_time(this->ts_generator, 1 * GST_MSECOND);

  _stat(this)->lost_or_discarded = 0;
  _stat(this)->avg_qd = 0.0;
  _stat(this)->qd_min = 1000;
  _stat(this)->qd_max = 0;

//  g_print("Received chunks: %d (%d->%d): ", this->subflow->id, act_seq, end_seq);
//  for(i=0; act_seq != end_seq; ++act_seq, ++i) {
  for (i = 0; i < xr->CongestionControlFeedback.vector_length; ++i, ++act_seq) {
    packet = sndtracker_retrieve_sent_packet(this->sndtracker, this->subflow->id, act_seq);
    if(!packet){
      GST_WARNING_OBJECT(this, "Packet %hu has not in subflow tracked sequences. "
          "Either too late acknowledged or never sent", act_seq);
      continue;
    }
//    g_print("(%d, %d) ", (guint16)(packet->subflow_seq), xr->CongestionControlFeedback.vector[i].lost);

    act_ato = xr->CongestionControlFeedback.vector[i].ato;

    if(packet->acknowledged) {
      if(packet->lost && xr->CongestionControlFeedback.vector[i].lost) {
        packet->lost = FALSE;
        packet->rcvd_ts = _subtract_ts(report_timestamp, act_ato);
        sndtracker_packet_found(this->sndtracker, packet);
        packet->arrival_status |= 4;
      }
      continue;
    }

    if (!xr->CongestionControlFeedback.vector[i].lost) {
      packet->acknowledged = TRUE;
      packet->lost = TRUE;
      packet->skew = 0;
      sndtracker_packet_acked(this->sndtracker, packet);
      packet->arrival_status |= 1;
      ++_stat(this)->lost_or_discarded;
      continue;
    }
    ++number_of_qd_packets;
    packet->arrival_status |= 2;

    packet->rcvd_ts = _subtract_ts(report_timestamp, act_ato);
    {
      guint32 dts = _delta_ts(packet->sent_ts, packet->rcvd_ts);
      guint32 qts = 0;
      if (!this->min_dts || _cmp_ts(dts, this->min_dts) < 0) {
        this->min_dts = dts;
      }
      if (this->min_dts < dts) {
        qts = _delta_ts(this->min_dts, dts);
      }
      if (qts < this->min_qts) {
        this->min_qts = qts;
      }

  	  if (qts < this->qts_std * 2.) {
	    fluctuationcalcer_add_good_measurement(this->qd_fluctuationcalcer, 1.);
	  } else {
	    gdouble value = 1.;
	    if (4 * this->qts_std < qts) {
		  value *= 2;
	    }
	  fluctuationcalcer_add_bad_measurement(this->qd_fluctuationcalcer, value);
	}
	this->try_qd_fluctuation = fluctuationcalcer_get_stability_score(this->qd_fluctuationcalcer);

//      g_print("bucket index: %d, qts: %u, min_dts: %u, dts: %u, dts_std: %f \n", bucket_index, qts, this->min_dts, dts, this->qts_std);
//      bucket_add_value(this->qdelay_bucket, qts);
        qdelay_stability_calcer_add_ts(this->qdelay_stability_calcer, qts);

//      std_calcer_add_value(this->qts_std_calcer, qts);
      {
        guint32 qts_dist = this->last_qts < qts ? qts - this->last_qts : this->last_qts - qts;
        guint32 min_qts_dist = this->min_qts < qts ? qts - this->min_qts : this->min_qts - qts;
        this->last_qts = qts;
        this->qts_std = (qts_dist + 31. * this->qts_std) / 32.;
        this->min_qts_std = (min_qts_dist + 255. * this->min_qts_std) / 256.;
      }

      _stat(this)->avg_qd += qts;
      _stat(this)->qd_max = MAX(_stat(this)->qd_max, qts);
      _stat(this)->qd_min = MIN(_stat(this)->qd_min, qts);

      packet->dts = dts;
      packet->qts = qts;

      if (0 < this->last_dts) {
        packet->subflow_skew = dts < this->last_dts ? this->last_dts - dts : dts - this->last_dts;
      }
      this->last_dts = dts;

      if (!reference_dts_init || dts < reference_dts) {
        reference_dts = dts;
        reference_sent_ts = packet->sent_ts;
        reference_ato = act_ato;
        reference_dts_init = TRUE;
      }
    }

    packet->acknowledged = TRUE;
    packet->lost = FALSE;
    sndtracker_packet_acked(this->sndtracker, packet);
  }
  _stat(this)->arrived_packets = number_of_qd_packets;

  if (reference_sent_ts) {
    guint32 current_ts = timestamp_generator_get_ts(this->ts_generator);
    this->rtt_in_ts = _subtract_ts(_subtract_ts(current_ts, reference_sent_ts), reference_ato);
    this->rtt = timestamp_generator_get_time(this->ts_generator, this->rtt_in_ts);

    if (!this->srtt_in_ts) {
      this->srtt_in_ts = this->rtt_in_ts;
    }
  }
  _stat(this)->avg_qd /= (gdouble) number_of_qd_packets;

  _refresh_windows_thresholds(this);
done:
  ++_stat(this)->measurements_num;
  return;
}


void _process_stat(FRACTaLFBProcessor *this)
{

  SndTrackerStat*     sndstat     = sndtracker_get_subflow_stat(this->sndtracker, this->subflow->id);
  RTPQueueStat*       rtpqstat    = sndtracker_get_rtpqstat(this->sndtracker);

  gdouble ewma_factor         = _get_ewma_factor(this);
  gdouble alpha;

  _stat(this)->rtpq_delay            = (gdouble) rtpqstat->bytes_in_queue * 8 / (gdouble) this->subflow->allocated_target;
  _stat(this)->sender_bitrate        = sndstat->sent_bytes_in_1s * 8;
  _stat(this)->receiver_bitrate      = sndstat->received_bytes_in_1s * 8;
  _stat(this)->fec_bitrate           = sndstat->sent_fec_bytes_in_1s * 8;
  _stat(this)->sent_packets_in_1s    = sndstat->sent_packets_in_1s;


  _stat(this)->qdelay_stability = qdelay_stability_calcer_do(this-> qdelay_stability_calcer, &_stat(this)->qdelay_is_stable);

//  g_print("SR avg: %f <| %d\n", _stat(this)->sr_avg, _stat(this)->sender_bitrate);
  if(0. < _stat(this)->sr_avg){
//    alpha = ewma_factor * .5 + .5;
    alpha = .5;
    _stat(this)->sr_avg = alpha * _stat(this)->sender_bitrate + _stat(this)->sr_avg * (1.-alpha);
  }else{
    _stat(this)->sr_avg = _stat(this)->sender_bitrate;
  }

  if(0. < _stat(this)->rr_avg){
    alpha = ewma_factor * .5 + .5;
    _stat(this)->rr_avg = alpha * _stat(this)->receiver_bitrate + _stat(this)->rr_avg * (1.-alpha);
  }else{
    _stat(this)->rr_avg = _stat(this)->receiver_bitrate;
  }

  if (0 < sndstat->lost_packets_in_1s) {
    _stat(this)->fraction_lost = (gdouble) sndstat->lost_packets_in_1s / (gdouble) (sndstat->lost_packets_in_1s + sndstat->received_packets_in_1s);
    fl_stability_calcer_add_sample(this->fl_stability_calcer, _stat(this)->fraction_lost);
    _stat(this)->fl_stability = fl_stability_calcer_do(this->fl_stability_calcer);
  } else {
    _stat(this)->fraction_lost = 0.;
  }

  {
    gint32 drate = (sndstat->sent_bytes_in_1s - sndstat->received_bytes_in_1s) * 8;
    gdouble alpha = this->fb_interval_avg / (gdouble) (200 * GST_MSECOND);
    alpha = CONSTRAIN(.1, .5, alpha);
    _stat(this)->drate_avg = drate * alpha + _stat(this)->drate_avg * (1.-alpha);
  }

  {
//    gdouble alpha = .98;
//    _stat(this)->FL_th = _stat(this)->FL_th * alpha + _stat(this)->fraction_lost * (1.0 - alpha);
    _stat(this)->FL_th = MIN(.1, this->fl_stability_calcer->std * 2);
  }

  if (0. < _stat(this)->fraction_lost) {
    gdouble alpha = .98;
    _stat(this)->fraction_lost_avg = _stat(this)->fraction_lost_avg * alpha + _stat(this)->fraction_lost * (1.-alpha);
  }
}



static void _on_packet_sent(FRACTaLFBProcessor* this, SndPacket* packet) {
  if(packet->subflow_id != this->subflow->id){
    return;
  }
}


static void _refresh_windows_thresholds(FRACTaLFBProcessor *this) {

  guint32 ewi_in_ts = CONSTRAIN(this->min_ewi_in_ts, this->max_ewi_in_ts, this->srtt_in_ts);
  GstClockTime ewi_in_ns = timestamp_generator_get_time(this->ts_generator, ewi_in_ts);
  qdelay_stability_calcer_set_time_threshold(this->qdelay_stability_calcer, 2 * ewi_in_ns);
  fl_stability_calcer_set_time_threshold(this->fl_stability_calcer, ewi_in_ns);
}


