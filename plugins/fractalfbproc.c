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
  gdouble fraction_lost;
  GstClockTime rtt;
  guint32 rtt_in_ts;
}ReferencePoint;

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


DEFINE_RECYCLE_TYPE(static, reference_point, ReferencePoint);

static void _reference_point_shaper(ReferencePoint* to, ReferencePoint* from) {
  memcpy(to, from, sizeof(ReferencePoint));
}


static BucketList* _make_bucket_list(GstClockTime window_size){
  BucketList* this = g_malloc0(sizeof(BucketList));
  this->pushed_items = g_queue_new();
  this->window_size = window_size;
  return this;
}

static void _free_bucket_list(BucketList* bucket_list) {
  while(!g_queue_is_empty(bucket_list->pushed_items)) {
    g_free(g_queue_pop_head(bucket_list->pushed_items));
  }
  g_free(bucket_list);
}

static gint _get_bucket_index(FRACTaLFBProcessor* this, gdouble new_value) {
  gint bucket_index;
  gdouble actual_bucket = this->first_bucket_size;
  for (bucket_index = 0; bucket_index < BUCKET_LIST_LENGTH; ++bucket_index) {
    if (actual_bucket < new_value) {
      actual_bucket *= 2.;
      continue;
    }
    return bucket_index;
  }
  return BUCKET_LIST_LENGTH - 1;
}
typedef struct {
  GstClockTime added;
  gint index;
}BucketItem;

static void _bucket_list_add(FRACTaLFBProcessor* this, BucketList* bucket_list, gint bucket_index)
{
  BucketItem* item;
  GstClockTime now = _now(this);
  if (!g_queue_is_empty(this->bucket_recycle)) {
    item = g_queue_pop_head(this->bucket_recycle);
  } else {
    item = g_malloc0(sizeof(BucketItem));
  }
  item->index = bucket_index;
  item->added = now;
  ++bucket_list->buckets[item->index];

  g_queue_push_tail(bucket_list->pushed_items, item);

again:
  if (g_queue_is_empty(bucket_list->pushed_items)) {
    return;
  }
  item = g_queue_peek_head(bucket_list->pushed_items);
  if (now - bucket_list->window_size < item->added) {
    return;
  }
  item = g_queue_pop_head(bucket_list->pushed_items);
  --bucket_list->buckets[item->index];
  g_queue_push_tail(this->bucket_recycle, item);
  goto again;
}

static gdouble _get_bucket_cosine_similarity(BucketList* a, BucketList* b) {
  //"compute cosine similarity of v1 to v2: (v1 dot v2)/{||v1||*||v2||)"
  gdouble sumxx = 0., sumxy = 0., sumyy = 0.;
  gint index;
  for (index = 0; index < BUCKET_LIST_LENGTH; ++index) {
    gdouble x = a->buckets[index];
    gdouble y = b->buckets[index];
    sumxx += x*x;
    sumyy += y*y;
    sumxy += x*y;
  }
  if (sumxy == 0.) {
    return 0.;
  } else if (sumxx == 0. || sumyy == 0.) {
    return 1.;
  }
  return sumxy / sqrt(sumxx * sumyy);
}

static void _list_bucket_list(BucketList* list, const gchar* name) {
  gint index;
  gchar buffer[255];
  memset(buffer, 0, 255);
  for (index = 0; index < BUCKET_LIST_LENGTH; ++index) {
    gchar buf[255];
    sprintf(buf, "%d,", list->buckets[index]);
    strcat(buffer, buf);
  }
  g_print("%s items: %s\n", name, buffer);
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
  g_queue_free_full(this->bucket_recycle, g_free);
  _free_bucket_list(this->actual_bucket);
  _free_bucket_list(this->non_congestion_bucket);
  _free_bucket_list(this->congestion_bucket);
}

void
fractalfbprocessor_init (FRACTaLFBProcessor * this)
{
  this->sysclock         = gst_system_clock_obtain();
}

swplugin_define_on_calculated_data(FRACTaLStat, _on_rcvd_bytes_sum, rcved_bytes_in_ewi, gdouble);
swplugin_define_swdoubleextractor(_rtt_extractor, ReferencePoint, rtt);
swplugin_define_on_calculated_data(FRACTaLStat, _on_srtt_calculated, srtt, gdouble);
swplugin_define_swdataextractor(gdouble, _sndpacket_payload_extractor, SndPacket, payload_size);
//swplugin_define_on_calculated_data(FRACTaLFBProcessor, _on_min_dts_calculated, min_dts, guint32);
static void _on_packet_sent(FRACTaLFBProcessor* this, SndPacket* packet);
static void _refresh_windows_thresholds(FRACTaLFBProcessor *this);

void fractalfbprocessor_set_evaluation_window_margins(FRACTaLFBProcessor *this, GstClockTime min, GstClockTime max) {

  this->min_ewi_in_ts = timestamp_generator_get_ts_for_time(this->ts_generator, min);
  this->max_ewi_in_ts = timestamp_generator_get_ts_for_time(this->ts_generator, max);
  _refresh_windows_thresholds(this);
}

FRACTaLFBProcessor *make_fractalfbprocessor(SndTracker* sndtracker, SndSubflow* subflow, FRACTaLStat *stat)
{
  FRACTaLFBProcessor *this;
  this = g_object_new (FRACTALFBPROCESSOR_TYPE, NULL);
  this->sndtracker   = g_object_ref(sndtracker);
  this->subflow      = subflow;
  this->stat         = stat;
  this->reference_point_recycle = make_recycle_reference_point(200, (RecycleItemShaper) _reference_point_shaper);
  this->reference_sw = make_slidingwindow_with_data_recycle(200, 15 * GST_SECOND, this->reference_point_recycle);
  this->ewi_sw = make_slidingwindow(200, GST_SECOND);
  this->ts_generator = sndtracker_get_ts_generator(sndtracker);
  this->sent_packets = g_queue_new();
  this->dts = 0;
  this->bucket_recycle = g_queue_new();
  this->congestion_bucket = _make_bucket_list(30 * GST_SECOND);
  this->non_congestion_bucket = _make_bucket_list(30 * GST_SECOND);
  this->actual_bucket = _make_bucket_list(GST_SECOND);
  this->first_bucket_size = timestamp_generator_get_ts_for_time(this->ts_generator, 10 * GST_MSECOND);

  sndtracker_add_on_packet_sent(this->sndtracker, (ListenerFunc)_on_packet_sent, this);

  slidingwindow_add_plugins(this->reference_sw,
          make_swavg(_on_srtt_calculated, stat, _rtt_extractor),
          NULL);

  slidingwindow_add_plugins(this->ewi_sw,
          make_swsum(
              (ListenerFunc) _on_rcvd_bytes_sum,
              stat,
              (SWDataExtractor) _sndpacket_payload_extractor),
          NULL);

  fractalfbprocessor_set_evaluation_window_margins(this, 0.25 * GST_SECOND, 0.5 * GST_SECOND);

  this->congestion_bucket->buckets[4] = 20;
  this->congestion_bucket->buckets[3] = 10;
  this->congestion_bucket->buckets[2] = 5;

  this->non_congestion_bucket->buckets[2] = 5;
  this->non_congestion_bucket->buckets[1] = 10;
  this->non_congestion_bucket->buckets[0] = 20;

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
  this->srtt_in_ts = this->srtt_in_ts * .9 + this->rtt_in_ts * .1;
  slidingwindow_add_data(this->reference_sw,  &reference_point);
}



void fractalfbprocessor_set_congestion_reference(FRACTaLFBProcessor* this, gboolean value) {
  this->collect_congestion_reference = value;
}

void fractalfbprocessor_set_non_congestion_reference(FRACTaLFBProcessor* this, gboolean value) {
  this->collect_non_congestion_reference = value;
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
      sndtracker_packet_acked(this->sndtracker, packet);
      continue;
    }
//    _list_bucket_list(this->congestion_bucket, "congestion bucket");
//    _list_bucket_list(this->non_congestion_bucket, "non congestion bucket");
    DISABLE_LINE _list_bucket_list(this->actual_bucket, "actual bucket");
    packet->rcvd_ts = _subtract_ts(report_timestamp, act_ato);
    {
      guint32 dts = _delta_ts(packet->sent_ts, packet->rcvd_ts);
      guint32 qts = 0;
      gint bucket_index;
      if (!this->min_dts || _cmp_ts(dts, this->min_dts) < 0) {
        this->min_dts = dts;
      }
      if (this->min_dts < dts) {
        qts = _delta_ts(this->min_dts, dts);
      }
      bucket_index = _get_bucket_index(this, qts);
//      g_print("bucket index: %d, qts: %u, min_dts: %u, dts: %u, dts_std: %f \n", bucket_index, qts, this->min_dts, dts, this->qts_std);
      if (this->collect_congestion_reference) {
        _bucket_list_add(this, this->congestion_bucket, bucket_index);
      } else if (this->collect_non_congestion_reference) {
        _bucket_list_add(this, this->non_congestion_bucket, bucket_index);
      }
      _bucket_list_add(this, this->actual_bucket, bucket_index);
      {
        guint32 qts_dist = this->last_qts < qts ? qts - this->last_qts : this->last_qts - qts;
        this->last_qts = qts;
        this->qts_std = (qts_dist + 31. * this->qts_std) / 32.;
      }

      // TODO: add sending packets instead of guint32 to ewi
//      slidingwindow_add_data(this->ewi_sw, &dts);
      slidingwindow_add_data(this->ewi_sw, packet);

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

  if (reference_sent_ts) {
    guint32 current_ts = timestamp_generator_get_ts(this->ts_generator);
    this->rtt_in_ts = _subtract_ts(_subtract_ts(current_ts, reference_sent_ts), reference_ato);
    this->rtt = timestamp_generator_get_time(this->ts_generator, this->rtt_in_ts);

    if (!this->srtt_in_ts) {
      this->srtt_in_ts = this->rtt_in_ts;
    }
  }

  this->first_bucket_size = MAX(timestamp_generator_get_ts_for_time(this->ts_generator, 10 * GST_MSECOND), this->qts_std * 2);
//  g_print("first bucket size: %f\n", this->first_bucket_size);
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
  gdouble qdelay_influence = 1. - _get_bucket_cosine_similarity(this->congestion_bucket, this->non_congestion_bucket);

  _stat(this)->rtpq_delay            = (gdouble) rtpqstat->bytes_in_queue * 8 / (gdouble) this->subflow->target_bitrate;
  _stat(this)->sender_bitrate        = sndstat->sent_bytes_in_1s * 8;
  _stat(this)->receiver_bitrate      = sndstat->received_bytes_in_1s * 8;
  _stat(this)->fec_bitrate           = sndstat->sent_fec_bytes_in_1s * 8;
  _stat(this)->measurements_num      = slidingwindow_get_counter(this->reference_sw);
  _stat(this)->sent_packets_in_1s    = sndstat->sent_packets_in_1s;

  this->qdelay_inf_avg = this->qdelay_inf_avg * .8 + qdelay_influence * .2;
  _stat(this)->qdelay_dinfluence = (qdelay_influence - _stat(this)->qdelay_influence) * this->qdelay_inf_avg;
  _stat(this)->qdelay_congestion = _get_bucket_cosine_similarity(this->congestion_bucket, this->actual_bucket);
  _stat(this)->qdelay_non_congestion = _get_bucket_cosine_similarity(this->non_congestion_bucket, this->actual_bucket);
  _stat(this)->qdelay_influence = qdelay_influence;
  _stat(this)->qdelay_std = this->qts_std;

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

  if (0 < sndstat->lost_packets_in_1s) {
    _stat(this)->fraction_lost = (gdouble)sndstat->lost_packets_in_1s / (gdouble) (sndstat->lost_packets_in_1s + sndstat->received_packets_in_1s);
  } else {
    _stat(this)->fraction_lost = 0.;
  }

  slidingwindow_refresh(this->reference_sw);
  slidingwindow_refresh(this->ewi_sw);

}



static void _on_packet_sent(FRACTaLFBProcessor* this, SndPacket* packet) {
  if(packet->subflow_id != this->subflow->id){
    return;
  }
}



static void _refresh_windows_thresholds(FRACTaLFBProcessor *this) {

  guint32 ewi_in_ts = CONSTRAIN(this->min_ewi_in_ts, this->max_ewi_in_ts, this->srtt_in_ts);
  GstClockTime ewi_in_ns = timestamp_generator_get_time(this->ts_generator, ewi_in_ts);
  this->actual_bucket->window_size = ewi_in_ns;
  slidingwindow_set_threshold(this->ewi_sw, ewi_in_ns);
}


