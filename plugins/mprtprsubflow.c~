#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include "mprtprsubflow.h"
#include "mprtpssubflow.h"
#include "gstmprtcpbuffer.h"

GST_DEBUG_CATEGORY_STATIC (gst_mprtprsubflow_debug_category);
#define GST_CAT_DEFAULT gst_mprtprsubflow_debug_category

G_DEFINE_TYPE (MPRTPRSubflow, mprtpr_subflow, G_TYPE_OBJECT);


typedef struct _Gap
{
  GList *at;
  guint16 start;
  guint16 end;
  guint16 total;
  guint16 filled;
} Gap;

static void mprtpr_subflow_process_rtpbuffer (MPRTPRSubflow * this,
    GstBuffer * buf, guint16 subflow_sequence);
static void mprtpr_subflow_finalize (GObject * object);
static void mprtpr_subflow_reset (MPRTPRSubflow * this);
static void mprtpr_subflow_proc_mprtcpblock (MPRTPRSubflow * this,
    GstMPRTCPSubflowBlock * block);
static gboolean mprtpr_subflow_is_active (MPRTPRSubflow * this);
static gboolean mprtpr_subflow_is_early_discarded_packets (MPRTPRSubflow *
    this);
static guint64 mprtpr_subflow_get_packet_skew_median (MPRTPRSubflow * this);
static gboolean mprtpr_subflow_do_riport_now (MPRTPRSubflow * this,
    GstClockTime * next_time);
static guint8 mprtpr_subflow_get_id (MPRTPRSubflow * this);
static GList *mprtpr_subflow_get_packets (MPRTPRSubflow * this);
static void mprtpr_subflow_add_packet_skew (MPRTPRSubflow * this,
    guint32 rtptime, guint32 clockrate);
static void mprtpr_subflow_setup_rr_riport (MPRTPRSubflow * this,
    GstMPRTCPSubflowRiport * riport);
static void mprtps_subflow_setup_xr_rfc2743_late_discarded_riport (MPRTPRSubflow
    * this, GstMPRTCPSubflowRiport * riport);
static void _proc_rtcp_sr (MPRTPRSubflow * this, GstRTCPSR * sr);
static void mprtpr_subflow_set_avg_rtcp_size (MPRTPRSubflow * this,
    gsize packet_size);

static guint16
_mprtp_buffer_get_sequence_num (GstRTPBuffer * rtp, guint8 MPRTP_EXT_HEADER_ID);
//static guint16 _mprtp_buffer_get_subflow_id(GstRTPBuffer* rtp,
//              guint8 MPRTP_EXT_HEADER_ID);
static gboolean
_found_in_gaps (GList * gaps, guint16 actual_subflow_sequence,
    guint8 ext_header_id, GList ** result_item, Gap ** result_gap);
static Gap *_make_gap (GList * at, guint16 start, guint16 end);
static gint _cmp_seq (guint16 x, guint16 y);




void
mprtpr_subflow_class_init (MPRTPRSubflowClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = mprtpr_subflow_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_mprtprsubflow_debug_category, "mprtpr_subflow",
      0, "MPRTP Receiver Subflow");
}


MPRTPRSubflow *
make_mprtpr_subflow (guint8 id, guint8 header_ext_id)
{
  MPRTPRSubflow *result;

  result = g_object_new (MPRTPR_SUBFLOW_TYPE, NULL);
  result->id = id;
  result->ext_header_id = header_ext_id;
  return result;
}

void
mprtpr_subflow_reset (MPRTPRSubflow * this)
{
  this->cycle_num = 0;
  this->sysclock = gst_system_clock_obtain ();
  this->skews_write_index = 0;
  this->skews_read_index = 0;
  this->seq_initialized = FALSE;
  this->skew_initialized = FALSE;
  this->jitter = 0;
  this->cum_packet_losts = 0;
  this->packet_losts = 0;
  this->ext_rtptime = -1;
  this->rr_riport_time = 0;
  this->result = NULL;
  this->lost_started_riporting = FALSE;
  this->lost_started_riporting_time = 0;
  this->rr_riport_normal_period_time = 5 * GST_SECOND;
  this->rr_riport_bw = 100000;
  this->allow_early = TRUE;
  this->urgent_riport_is_requested = FALSE;
  this->rr_riport_interval = 0;
  this->rr_riport_timeout_interval = 5 * GST_SECOND;
  this->avg_rtcp_size = 128.;
  this->packet_limit_to_riport = 10;
  this->rr_started = FALSE;
  this->media_bw_avg = 0.;
}

void
mprtpr_subflow_init (MPRTPRSubflow * this)
{
  this->process_mprtp_packets = mprtpr_subflow_process_rtpbuffer;
  this->proc_mprtcpblock = mprtpr_subflow_proc_mprtcpblock;
  this->is_active = mprtpr_subflow_is_active;
  this->is_early_discarded_packets = mprtpr_subflow_is_early_discarded_packets;
  this->get_skews_median = mprtpr_subflow_get_packet_skew_median;
  this->add_packet_skew = mprtpr_subflow_add_packet_skew;
  this->get_packets = mprtpr_subflow_get_packets;
  this->set_avg_rtcp_size = mprtpr_subflow_set_avg_rtcp_size;
  this->setup_rr_riport = mprtpr_subflow_setup_rr_riport;
  this->setup_xr_rfc2743_late_discarded_riport =
      mprtps_subflow_setup_xr_rfc2743_late_discarded_riport;
  this->get_id = mprtpr_subflow_get_id;
  this->do_riport_now = mprtpr_subflow_do_riport_now;
  this->active = TRUE;
  this->ssrc = g_random_int ();


  g_mutex_init (&this->mutex);
  mprtpr_subflow_reset (this);

}


void
mprtpr_subflow_finalize (GObject * object)
{
  MPRTPRSubflow *subflow;
  subflow = MPRTPR_SUBFLOW_CAST (object);
  g_object_unref (subflow->sysclock);

}

void
mprtpr_subflow_proc_mprtcpblock (MPRTPRSubflow * this,
    GstMPRTCPSubflowBlock * block)
{
  GstRTCPHeader *header = &block->block_header;
  guint8 type;
  g_mutex_lock (&this->mutex);

  gst_rtcp_header_getdown (header, NULL, NULL, NULL, &type, NULL, NULL);

  if (type == GST_RTCP_TYPE_SR) {
    _proc_rtcp_sr (this, &block->sender_riport);
  }

  g_mutex_unlock (&this->mutex);
}

GList *
mprtpr_subflow_get_packets (MPRTPRSubflow * this)
{
  GList *result, *it;
  Gap *gap;
  GstClockTime now;
  gboolean distortion_was;
  g_mutex_lock (&this->mutex);
  distortion_was = this->distortion;

  now = gst_clock_get_time (this->sysclock);
  result = this->result;
  for (it = this->gaps; it != NULL; it = it->next) {
    gap = it->data;
    if (gap->filled > 0) {
      ++this->early_discarded;
    }
    this->packet_losts += gap->total - gap->filled;
  }
  if (this->distortion && this->distortion_happened + 30 * GST_SECOND < now) {
    this->distortion = FALSE;
  }

  if (this->packet_losts > 0 || this->late_discarded > 0) {
    this->distortion = TRUE;
    this->distortion_happened = now;
  }

  if (!distortion_was && this->distortion) {
    this->urgent_riport_is_requested = TRUE;
  }

  g_list_free_full (this->gaps, g_free);
  this->gaps = NULL;
  this->result = NULL;
  g_mutex_unlock (&this->mutex);
  return result;
}


gboolean
mprtpr_subflow_is_active (MPRTPRSubflow * this)
{
  gboolean result;
  g_mutex_lock (&this->mutex);
  result = this->active;
  g_mutex_unlock (&this->mutex);
  return result;
}

gboolean
mprtpr_subflow_is_early_discarded_packets (MPRTPRSubflow * this)
{
  gboolean result;
  g_mutex_lock (&this->mutex);
  result = this->early_discarded > 0 ? TRUE : FALSE;
  g_mutex_unlock (&this->mutex);
  return result;
}


guint8
mprtpr_subflow_get_id (MPRTPRSubflow * this)
{
  guint16 result;
  g_mutex_lock (&this->mutex);
  result = this->id;
  g_mutex_unlock (&this->mutex);
  return result;
}



void
mprtpr_subflow_set_avg_rtcp_size (MPRTPRSubflow * this, gsize packet_size)
{
  g_mutex_lock (&this->mutex);
  this->avg_rtcp_size +=
      ((gdouble) packet_size - this->avg_rtcp_size) * 1. / 16.;
  g_mutex_unlock (&this->mutex);
}



gboolean
mprtpr_subflow_do_riport_now (MPRTPRSubflow * this, GstClockTime * next_time)
{
  GstClockTime now;
  gboolean result = FALSE;
  GstClockTime t_normal, t_max = 7 * GST_SECOND + 500 * GST_MSECOND;
  GstClockTime t_bw_min;

  gdouble randv = g_random_double_range (0.5, 1.5);
  g_mutex_lock (&this->mutex);
  now = gst_clock_get_time (this->sysclock);
  if (!this->rr_started) {
    this->rr_riport_interval = 1 * GST_SECOND;
    this->rr_riport_time = now + this->rr_riport_interval;
    this->rr_started = TRUE;
    goto mprtpr_subflow_is_riporting_time_done;
  }

  if (this->urgent_riport_is_requested) {
    this->urgent_riport_is_requested = FALSE;
    if (!this->allow_early) {
      goto mprtpr_subflow_is_riporting_time_done;
    }
    this->allow_early = FALSE;
    result = TRUE;
    this->rr_riport_interval = (gdouble) this->rr_riport_interval / 2. * randv;
    if (this->rr_riport_interval < 500 * GST_MSECOND) {
      this->rr_riport_interval = 750. * (gdouble) GST_MSECOND *randv;
    } else if (2 * GST_SECOND < this->rr_riport_interval) {
      this->rr_riport_interval = 2. * (gdouble) GST_SECOND *randv;
    }
    this->rr_riport_time = now + this->rr_riport_interval;
    goto mprtpr_subflow_is_riporting_time_done;
  }

  if (now < this->rr_riport_time) {
    goto mprtpr_subflow_is_riporting_time_done;
  }

  if (this->media_bw_avg > 0.0) {
    this->rr_riport_bw = this->media_bw_avg * 0.01;
    t_bw_min =
        (GstClockTime) (this->avg_rtcp_size * 2. / this->rr_riport_bw *
        (gdouble) GST_SECOND);
    t_normal = MAX (this->rr_riport_normal_period_time, t_bw_min);
  } else {
    t_normal = this->rr_riport_normal_period_time;
  }

  if (this->packet_received < this->packet_limit_to_riport) {
    if (this->last_riport_sent_time + ((GstClockTime) (t_normal * 3)) < now) {
      result = TRUE;
      this->rr_riport_interval = (GstClockTime) ((gdouble) t_normal * randv);
      this->rr_riport_time = now + this->rr_riport_time;
      goto mprtpr_subflow_is_riporting_time_done;
    }
    this->rr_riport_time = now + (GstClockTime) (t_normal *
        (1.1 - this->packet_received / this->packet_limit_to_riport));
    this->rr_riport_interval = this->rr_riport_interval * 1.5;
    if (t_max < this->rr_riport_interval) {
      this->rr_riport_interval = t_max;
    }
    goto mprtpr_subflow_is_riporting_time_done;
  }

  result = TRUE;
  this->allow_early = TRUE;
  if (this->packet_losts > 0 || this->late_discarded > 0) {
    if (!this->rr_paths_congestion_riport_is_started) {
      this->rr_paths_changing_riport_started = now;
    }
    this->rr_paths_congestion_riport_is_started = TRUE;
    if (now - 10 * GST_SECOND < this->rr_paths_changing_riport_started) {
      this->rr_riport_interval =
          (gdouble) this->rr_riport_interval / 2. * randv;
    } else {
      this->rr_riport_interval = (gdouble) t_normal *randv;
    }
    if (this->rr_riport_interval < 500 * GST_MSECOND) {
      this->rr_riport_interval = (gdouble) (750 * GST_MSECOND) * randv;
    }
  } else {
    if (this->rr_paths_congestion_riport_is_started) {
      this->rr_paths_congestion_riport_is_started = FALSE;
      this->rr_paths_changing_riport_started = now;
    }
    if (now - 10 * GST_SECOND < this->rr_paths_changing_riport_started) {
      this->rr_riport_interval =
          (gdouble) this->rr_riport_interval / 2. * randv;
    } else {
      this->rr_riport_interval =
          (gdouble) this->rr_riport_interval * 1.5 * randv;
    }
    if (this->rr_riport_interval < 500 * GST_MSECOND) {
      this->rr_riport_interval = (gdouble) (750 * GST_MSECOND) * randv;
    } else if (t_max < this->rr_riport_interval) {
      this->rr_riport_interval = (gdouble) t_normal *randv;
    }
  }

  this->rr_riport_time = now + this->rr_riport_interval;

mprtpr_subflow_is_riporting_time_done:
//    g_print("subflow %d this->rr_riport_time = %llu + %llu\n", this->id, now,
//                      GST_TIME_AS_MSECONDS(this->rr_riport_interval));
  *next_time = this->rr_riport_time;
  g_mutex_unlock (&this->mutex);
  return result;
}



static guint16
uint16_diff (guint16 a, guint16 b)
{
  if (a <= b) {
    return b - a;
  }
  return ~((guint16) (a - b));
}

void
mprtpr_subflow_setup_rr_riport (MPRTPRSubflow * this,
    GstMPRTCPSubflowRiport * riport)
{
  GstMPRTCPSubflowBlock *block;
  GstRTCPRR *rr;
  GstClockTime ntptime;
//  guint32 rtptime;
  guint8 fraction_lost;
  guint32 ext_hsn, LSR, DLSR;
  guint16 expected;

  g_mutex_lock (&this->mutex);
  ntptime = gst_clock_get_time (this->sysclock);

//  rtptime = (guint32)(gst_rtcp_ntp_to_unix (ntptime)>>32), //rtptime

  block = gst_mprtcp_riport_add_block_begin (riport, this->id);
  gst_mprtcp_riport_setup (riport, this->ssrc);
  rr = gst_mprtcp_riport_block_add_rr (block);

  expected = uint16_diff (this->HSN, this->actual_seq);

  fraction_lost = (256.0 * (gfloat) this->packet_losts) / ((gfloat) (expected));
  this->packet_lost_rate = (gfloat) this->packet_losts / (gfloat) (expected);
  this->cum_packet_losts += (guint32) this->packet_losts;

  ext_hsn = (((guint32) this->cycle_num) << 16) | ((guint32) this->actual_seq);
  //g_print("this->LSR: %016llX -> %016llX\n", this->LSR, (guint32)(this->LSR>>16));
  LSR = (guint32) (this->LSR >> 16);

  if (this->LSR == 0 || ntptime < this->LSR) {
    DLSR = 0;
  } else {
    DLSR = (guint32) GST_TIME_AS_MSECONDS ((GstClockTime)
        GST_CLOCK_DIFF (this->LSR, ntptime));
  }

  gst_rtcp_rr_add_rrb (rr, 0,
      fraction_lost, this->cum_packet_losts, ext_hsn, this->jitter, LSR, DLSR);
  gst_mprtcp_riport_add_block_end (riport, block);
  //reset
  this->media_bw_avg += (this->avg_rtp_size * (gdouble) this->packet_received /
      (gdouble) (GST_TIME_AS_MSECONDS (ntptime -
              this->last_riport_sent_time) * 1. / 1000.) -
      this->media_bw_avg) * 1. / 16.;

//  g_print("this->media_bw = %f * %f / %f * 1./1000. = %f\n",
//                this->avg_rtp_size, (gdouble)this->packet_received,
//                (gdouble) (GST_TIME_AS_MSECONDS(ntptime - this->last_riport_sent_time)), this->media_bw_avg);
  this->packet_received = 0;
  this->packet_losts = 0;
  this->HSN = this->actual_seq;
  this->last_riport_sent_time = ntptime;
  g_mutex_unlock (&this->mutex);
}



void
mprtps_subflow_setup_xr_rfc2743_late_discarded_riport (MPRTPRSubflow * this,
    GstMPRTCPSubflowRiport * riport)
{
  GstMPRTCPSubflowBlock *block;
  GstRTCPXR_RFC7243 *xr;
  guint8 flag = RTCP_XR_RFC7243_I_FLAG_INTERVAL_DURATION;
  gboolean early_bit = FALSE;
  g_mutex_lock (&this->mutex);

  block = gst_mprtcp_riport_add_block_begin (riport, this->id);
  xr = gst_mprtcp_riport_block_add_xr_rfc2743 (block);
  gst_rtcp_header_change (&xr->header, NULL, NULL, NULL, NULL, NULL,
      &this->ssrc);

  gst_rtcp_xr_rfc7243_change (xr, &flag, &early_bit,
      NULL, &this->late_discarded_bytes);

  gst_mprtcp_riport_add_block_end (riport, block);

  //reset
  this->late_discarded_bytes = 0;
  this->late_discarded = 0;
  g_mutex_unlock (&this->mutex);
}




static gint
_cmp_guint32 (gconstpointer a, gconstpointer b, gpointer user_data)
{
  const guint32 *_a = a, *_b = b;
  if (*_a == *_b) {
    return 0;
  }
  return *_a < *_b ? -1 : 1;
}

guint64
mprtpr_subflow_get_packet_skew_median (MPRTPRSubflow * this)
{
  guint64 skews[100];
  gint c;
  GstClockTime treshold;
  guint8 i;
  guint64 result;

  g_mutex_lock (&this->mutex);
  if (this->skews_read_index == this->skews_write_index) {
    result = 0;
    goto mprtpr_subflow_get_packet_skew_median_done;
  }
  treshold = gst_clock_get_time (this->sysclock) - 2 * GST_SECOND;
  for (c = 0; this->skews_read_index != this->skews_write_index;) {
    i = this->skews_read_index;
    if (++this->skews_read_index == 100) {
      this->skews_read_index = 0;
    }
    if (this->received_times[i] < treshold) {
      continue;
    }
    skews[c++] = this->skews[i];
  }
  this->skews_read_index = this->skews_write_index = 0;
  g_qsort_with_data (skews, c, sizeof (guint32), _cmp_guint32, NULL);
  result = skews[c >> 1];

mprtpr_subflow_get_packet_skew_median_done:
  g_mutex_unlock (&this->mutex);
  return result;
}

void
mprtpr_subflow_add_packet_skew (MPRTPRSubflow * this,
    guint32 rtptime, guint32 clockrate)
{
  guint64 packet_skew, send_diff, recv_diff, last_ext_rtptime;
  guint64 received;
  g_mutex_lock (&this->mutex);

  received = gst_clock_get_time (this->sysclock);
  if (this->skew_initialized == FALSE) {
    this->ext_rtptime =
        gst_rtp_buffer_ext_timestamp (&this->ext_rtptime, rtptime);
    //this->last_sent_time = sent;
    this->last_received_time = received;
    this->skew_initialized = TRUE;
    goto mprtpr_subflow_add_packet_skew_done;
  }
  last_ext_rtptime = this->ext_rtptime;
  this->ext_rtptime =
      gst_rtp_buffer_ext_timestamp (&this->ext_rtptime, rtptime);

  recv_diff = received - this->last_received_time;
  if (recv_diff > 0x8000000000000000) {
    recv_diff = 0;
  }

  send_diff = gst_util_uint64_scale_int (this->ext_rtptime - last_ext_rtptime,
      GST_SECOND, clockrate);

  if (send_diff > 0x8000000000000000) {
    send_diff = 0;
  }
  if (send_diff == 0) {
    goto mprtpr_subflow_add_packet_skew_done;
  }
  packet_skew = recv_diff - send_diff;
  if (packet_skew > 0x8000000000000000) {
    packet_skew = this->last_packet_skew;
  }
  //g_print("send dif: %llu, recv diff: %llu, packet skew: %llu\n", send_diff, recv_diff, packet_skew);

  this->jitter = this->jitter +
      (((gfloat) packet_skew - (gfloat) this->jitter) / 16.0);

  //g_print("(%llu-%llu) - (%llu-%llu) = %llu\n",
  //    received, this->last_received_time, sent, this->last_sent_time, packet_skew);

  this->last_packet_skew = packet_skew;
  this->skews[this->skews_write_index] = this->last_packet_skew;

  this->received_times[this->skews_write_index] = received;

  if (++this->skews_write_index == 100) {
    this->skews_write_index = 0;
  }

  if (this->skews_write_index == this->skews_read_index) {
    if (++this->skews_read_index == 100) {
      this->skews_read_index = 0;
    }
  }

  this->ext_rtptime =
      gst_rtp_buffer_ext_timestamp (&this->ext_rtptime, rtptime);
  //this->last_sent_time = sent;
  this->last_received_time = received;

mprtpr_subflow_add_packet_skew_done:
  g_mutex_unlock (&this->mutex);
}

void
mprtpr_subflow_process_rtpbuffer (MPRTPRSubflow * this, GstBuffer * buf,
    guint16 subflow_sequence)
{
  GList *it;
  Gap *gap;
//  guint64 reception_time;
//  guint32 rtptime;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint payload_size;
  guint packet_size;
  g_mutex_lock (&this->mutex);
  printf ("Packet is received by %d path receiver "
      "with %d relative sequence\n", this->id, subflow_sequence);

  if (this->seq_initialized == FALSE) {
    this->actual_seq = subflow_sequence;
    this->HSN = subflow_sequence;
    this->packet_received = 1;
    this->seq_initialized = TRUE;
    this->result = g_list_prepend (this->result, buf);
    goto mprtpr_subflow_process_rtpbuffer_end;
  }
  //goto mprtpr_subflow_process_rtpbuffer_end;

  //calculate lost, discarded and received packets
  ++this->packet_received;
  if (0x8000 < this->HSN && subflow_sequence < 0x8000 &&
      this->received_since_cycle_is_increased > 0x8888) {
    this->received_since_cycle_is_increased = 0;
    ++this->cycle_num;
  }
  gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp);
  payload_size = gst_rtp_buffer_get_payload_len (&rtp);
  packet_size =
      payload_size + gst_rtp_buffer_get_header_len (&rtp) +
      (28 << 3) /*UDP size */ ;
  this->avg_rtp_size += ((gdouble) packet_size - this->avg_rtp_size) * 1. / 16.;
  gst_rtp_buffer_unmap (&rtp);

  if (_cmp_seq (this->HSN, subflow_sequence) > 0) {
    ++this->late_discarded;
    this->late_discarded_bytes += payload_size;
    goto mprtpr_subflow_process_rtpbuffer_end;
  }
  if (subflow_sequence == (guint16) (this->actual_seq + 1)) {
    ++this->received_since_cycle_is_increased;
    this->result = g_list_prepend (this->result, buf);
    ++this->actual_seq;
    goto mprtpr_subflow_process_rtpbuffer_end;
  }
  if (_cmp_seq (this->actual_seq, subflow_sequence) < 0) {      //GAP
    this->result = g_list_prepend (this->result, buf);
    gap = _make_gap (this->result, this->actual_seq, subflow_sequence);
    this->gaps = g_list_append (this->gaps, gap);
    this->actual_seq = subflow_sequence;
    goto mprtpr_subflow_process_rtpbuffer_end;
  }
  if (_cmp_seq (this->actual_seq, subflow_sequence) > 0 && _found_in_gaps (this->gaps, subflow_sequence, this->ext_header_id, &it, &gap) == TRUE) {     //Discarded
    this->result =
        g_list_insert_before (this->result, it != NULL ? it : gap->at, buf);
    //this->result = dlist_pre_insert(this->result, it != NULL ? it : gap->at, rtp);
    ++gap->filled;
    goto mprtpr_subflow_process_rtpbuffer_end;
  }
  ++this->duplicated;

mprtpr_subflow_process_rtpbuffer_end:
  g_mutex_unlock (&this->mutex);
  return;
}



gint
_cmp_seq (guint16 x, guint16 y)
{

  if (x == y) {
    return 0;
  }
  /*
     if(x < y || (0x8000 < x && y < 0x8000)){
     return -1;
     }
     return 1;
   */
  return ((gint16) (x - y)) < 0 ? -1 : 1;

}

Gap *
_make_gap (GList * at, guint16 start, guint16 end)
{
  Gap *result = g_new0 (Gap, 1);
  guint16 counter;

  result->at = at;
  result->start = start;
  result->end = end;
  //_mprtp_buffer_get_sequence_num(at->data, &result->end);
  //result->active = BOOL_TRUE;
  result->total = 1;
  for (counter = result->start + 1;
      counter != (guint16) (result->end - 1); ++counter, ++result->total);
  //printf("Make Gap: start: %d - end: %d, missing: %d\n",result->start, result->end, result->total);
  return result;
}

gboolean
_found_in_gaps (GList * gaps,
    guint16 actual_subflow_sequence,
    guint8 ext_header_id, GList ** result_item, Gap ** result_gap)
{

  Gap *gap;
  GList *it;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  GstBuffer *buf;
  gint32 cmp;
  guint16 rtp_subflow_sequence;
  for (it = gaps; it != NULL; it = it->next) {
    gap = (Gap *) it->data;
    /*
       printf("\nGap total: %d; Filled: %d Start seq:%d End seq:%d\n",
       gap->total, gap->filled, gap->start, gap->end);
     */
    //if(gap->active == BOOL_FALSE){
    if (gap->filled == gap->total) {
      continue;
    }
    if (_cmp_seq (gap->start, actual_subflow_sequence) <= 0
        && _cmp_seq (actual_subflow_sequence, gap->end) <= 0) {
      break;
    }
  }
  if (it == NULL) {
    return FALSE;
  }
  if (result_gap != NULL) {
    *result_gap = gap;
  }

  for (it = gap->at; it != NULL; it = it->next) {
    //rtp = it->data;
    buf = it->data;
    gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp);
    rtp_subflow_sequence = _mprtp_buffer_get_sequence_num (&rtp, ext_header_id);
    cmp = _cmp_seq (rtp_subflow_sequence, actual_subflow_sequence);
    gst_rtp_buffer_unmap (&rtp);
    //printf("packet_s: %d, actual_s: %d\n",packet->sequence, actual->sequence);
    if (cmp > 0) {
      continue;
    }
    if (cmp == 0) {
      return FALSE;
    }
    break;
  }
  if (result_item != NULL) {
    *result_item = it;
    //printf("result_item: %d",((packet_t*)it->next->data)->sequence);
  }
  return TRUE;
}

//
//guint16 _mprtp_buffer_get_subflow_id(GstRTPBuffer* rtp, guint8 MPRTP_EXT_HEADER_ID)
//{
//      gpointer pointer = NULL;
//      guint size = 0;
//      MPRTPSubflowHeaderExtension *ext_header;
//      if(!gst_rtp_buffer_get_extension_onebyte_header(rtp, MPRTP_EXT_HEADER_ID, 0, &pointer, &size)){
//        GST_WARNING("The requested rtp buffer doesn't contain one byte header extension with id: %d", MPRTP_EXT_HEADER_ID);
//        return FALSE;
//      }
//      ext_header = (MPRTPSubflowHeaderExtension*) pointer;
//      return ext_header->id;
//}

guint16
_mprtp_buffer_get_sequence_num (GstRTPBuffer * rtp, guint8 MPRTP_EXT_HEADER_ID)
{
  gpointer pointer = NULL;
  guint size = 0;
  MPRTPSubflowHeaderExtension *ext_header;
  if (!gst_rtp_buffer_get_extension_onebyte_header (rtp, MPRTP_EXT_HEADER_ID, 0,
          &pointer, &size)) {
    GST_WARNING
        ("The requested rtp buffer doesn't contain one byte header extension with id: %d",
        MPRTP_EXT_HEADER_ID);
    return FALSE;
  }
  ext_header = (MPRTPSubflowHeaderExtension *) pointer;
  return ext_header->seq;
}

void
_proc_rtcp_sr (MPRTPRSubflow * this, GstRTCPSR * sr)
{
//  GstRTCPSRBlock *srblock = &sr->sender_block;
  this->LSR = gst_clock_get_time (this->sysclock);
}
