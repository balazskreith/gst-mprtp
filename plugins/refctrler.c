/* GStreamer Scheduling tree
 * Copyright (C) 2015 Balázs Kreith (contact: balazs.kreith@gmail.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be ureful,
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
#include "refctrler.h"
#include "streamsplitter.h"
#include "gstmprtcpbuffer.h"
#include "mprtprpath.h"
#include "streamjoiner.h"
#include "ricalcer.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define SR_DELAYS_ARRAY_LENGTH 12

#define _ort0(this) (this->or_moments+this->or_index)
#define _ort1(this) (this->or_moments+1-this->or_index)
#define _irt0(this) (this->ir_moments+this->ir_index)
#define _irt1(this) (this->ir_moments+1-this->ir_index)


GST_DEBUG_CATEGORY_STATIC (refctrler_debug_category);
#define GST_CAT_DEFAULT refctrler_debug_category

G_DEFINE_TYPE (RcvEventBasedController, refctrler, G_TYPE_OBJECT);

#define NORMAL_RIPORT_PERIOD_TIME (5*GST_SECOND)

typedef struct _Subflow Subflow;
typedef struct _ORMoment ORMoment;
typedef struct _IRMoment IRMoment;

//                        ^
//                        | Event
// .---.    .--------.-----------.
// |   |    | T | SystemNotifier |
// |   |    | i |----------------|
// | I |    | c |      ORP       |->Reports
// | R |-E->| k |----------------|
// | P |    | e |   PlayCtrler   |
// |   |    | r |                |
// '---'    '---'----------------'
//                        | Delays
//                        V


struct _ORMoment{
  GstClockTime                  time;
  guint32                       lost_packet_num;
  guint32                       late_discarded_bytes;
  guint32                       received_packet_num;
  guint32                       received_bytes;
  guint32                       received_payload_bytes;
  guint32                       expected_packet_num;
  gdouble                       media_rate;
  guint64                       delay40;
  guint64                       delay80;
  guint64                       last_delay;
  GstClockTime                  min_delay;
  GstClockTime                  max_delay;
  guint64                       median_skew;
  GstClockTime                  min_skew;
  GstClockTime                  max_skew;
  guint16                       cycle_num;
  guint32                       jitter;
  guint16                       HSN;
  guint16                       lost;
  guint16                       expected;
  guint16                       received;
};

struct _IRMoment{
  guint64                       SR_sent_ntp_time;
  guint64                       SR_received_ntp_time;
  guint32                       SR_last_packet_count;
  guint32                       SR_actual_packet_count;
  guint16                       HSN;
};

struct _Subflow
{
  MpRTPRPath*                   path;
  guint8                        id;
  GstClock*                     sysclock;
  GstClockTime                  joined_time;
  ReportIntervalCalculator*     ricalcer;
  IRMoment                      ir_moments[2];
  guint8                        ir_index;
  guint32                       ir_num;
  ORMoment                      or_moments[2];
  guint8                        or_index;
  guint32                       or_num;
  gdouble                       avg_rtcp_size;

};

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
refctrler_finalize (GObject * object);

static void
refctrler_ticker (void *data);

static void
refctrler_add_path (
    gpointer controller_ptr,
    guint8 subflow_id,
    MpRTPRPath * path);


static void
refctrler_rem_path (
    gpointer controller_ptr,
    guint8 subflow_id);

static void
refctrler_report_can_flow (gpointer ptr);

//------------------------- Incoming Report Processor -------------------

static void
refctrler_receive_mprtcp (
    gpointer subflow,
    GstBuffer * buf);

static void
_processing_selector (
    Subflow * this,
    GstMPRTCPSubflowBlock * block);

static void
_processing_srblock_processor (
    Subflow * subflow,
    GstRTCPSRBlock * srb);

static void
_step_ir (Subflow * this);

//------------------------ Outgoing Report Producer -------------------------

static void
_orp_main(RcvEventBasedController * this);

void
_step_or (Subflow * this);

static GstBuffer*
_get_mprtcp_xr_7243_block (
    RcvEventBasedController * this,
    Subflow * subflow,
    guint16 * block_length);

static GstBuffer*
_get_mprtcp_rr_block (
    RcvEventBasedController * this,
    Subflow * subflow,
    guint16 * block_length);

static GstBuffer *
_get_mprtcp_xr_owd_block (
    RcvEventBasedController * this,
    Subflow * subflow,
    guint16 * buf_length);

static void
_setup_xr_rfc2743_late_discarded_report (
    Subflow * this,
    GstRTCPXR_RFC7243 *xr,
    guint32 ssrc);

static void
_setup_rr_report (
    Subflow * this,
    GstRTCPRR * rr,
    guint32 ssrc);

void
_setup_xr_owd_report (
    Subflow * this,
    GstRTCPXR_OWD * xr,
    guint32 ssrc);

//----------------------------- System Notifier ------------------------------
static void
_system_notifier_main(RcvEventBasedController * this);

//----------------------------- Play Controller -----------------------------
static void
_play_controller_main(RcvEventBasedController * this);
void _refresh_subflow_skew(RcvEventBasedController *this,
                                     guint64 skew);
//------------------------- Utility functions --------------------------------
static Subflow*
_make_subflow (
    guint8 id,
    MpRTPRPath * path);

static void
_ruin_subflow (gpointer * subflow);

static void
_reset_subflow (Subflow * subflow);

static Subflow*
_subflow_ctor (void);

static void
_subflow_dtor (Subflow * this);

static guint32
_uint32_diff (
    guint32 a,
    guint32 b);

static gint
_cmp_for_maxtree (
    guint64 x,
    guint64 y);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------


void
refctrler_class_init (RcvEventBasedControllerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = refctrler_finalize;

  GST_DEBUG_CATEGORY_INIT (refctrler_debug_category, "refctrler", 0,
      "MpRTP Receiving Event Flow Reporter");

}


void
refctrler_setup (gpointer ptr, StreamJoiner * joiner)
{
  RcvEventBasedController *this;
  this = REFCTRLER (ptr);
  THIS_WRITELOCK (this);
  this->joiner = joiner;
  THIS_WRITEUNLOCK (this);
}

void
refctrler_finalize (GObject * object)
{
  RcvEventBasedController *this = REFCTRLER (object);
  g_hash_table_destroy (this->subflows);
  gst_task_stop (this->thread);
  gst_task_join (this->thread);
  g_object_unref (this->ricalcer);
  g_object_unref (this->sysclock);
}

//static void
//refctrler_stat_run (void *data);

void
refctrler_init (RcvEventBasedController * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->subflows = g_hash_table_new_full (NULL, NULL,
      NULL, (GDestroyNotify) _ruin_subflow);
  this->ssrc = g_random_int ();
  this->report_is_flowable = FALSE;
  this->subflow_skew_tree = make_bintree(_cmp_for_maxtree);
  this->subflow_skew_index = 0;
  this->ricalcer = make_ricalcer(FALSE);
  g_rw_lock_init (&this->rwmutex);
  g_rec_mutex_init (&this->thread_mutex);
  this->thread = gst_task_new (refctrler_ticker, this, NULL);
  gst_task_set_lock (this->thread, &this->thread_mutex);
  gst_task_start (this->thread);
//
//  g_rec_mutex_init (&this->stat_thread_mutex);
//  this->stat_thread = gst_task_new (refctrler_stat_run, this, NULL);
//  gst_task_set_lock (this->stat_thread, &this->stat_thread_mutex);
//  gst_task_start (this->stat_thread);

}

//
//void
//refctrler_stat_run (void *data)
//{
//  RcvEventBasedController *this;
//  GstClockID clock_id;
//  GHashTableIter iter;
//  gpointer key, val;
//  Subflow *subflow;
//  gboolean started = FALSE;
//  guint32 actual;
//  GstClockTime next_scheduler_time;
//  this = data;
//  THIS_WRITELOCK (this);
////  g_print("# subflow1, subflow 2\n");
//  g_hash_table_iter_init (&iter, this->subflows);
//  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
//    subflow = (Subflow *) val;
//    actual = mprtpr_path_get_total_bytes_received(subflow->path);
//    g_print("%c%u", started?',':' ', actual - subflow->last_stat_payload_bytes);
//    subflow->last_stat_payload_bytes = actual;
//    started = TRUE;
//  }
//  g_print("\n");
//  THIS_WRITEUNLOCK(this);
//
//  next_scheduler_time = gst_clock_get_time(this->sysclock) + GST_SECOND;
//  clock_id = gst_clock_new_single_shot_id (this->sysclock, next_scheduler_time);
//
//  if (gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED) {
//    GST_WARNING_OBJECT (this, "The playout clock wait is interrupted");
//  }
//  gst_clock_id_unref (clock_id);
//}

void
refctrler_ticker (void *data)
{
  GstClockTime now, next_scheduler_time;
  RcvEventBasedController *this;
  GstClockID clock_id;
//  guint64 max_path_skew = 0;
  this = REFCTRLER (data);
  THIS_WRITELOCK (this);
  now = gst_clock_get_time (this->sysclock);

  _play_controller_main(this);
  _orp_main(this);
  _system_notifier_main(this);

//done:
  next_scheduler_time = now + 100 * GST_MSECOND;
  THIS_WRITEUNLOCK (this);
  clock_id = gst_clock_new_single_shot_id (this->sysclock, next_scheduler_time);

  if (gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED) {
    GST_WARNING_OBJECT (this, "The playout clock wait is interrupted");
  }
  gst_clock_id_unref (clock_id);
  //clockshot;
}


void
refctrler_add_path (gpointer controller_ptr, guint8 subflow_id,
    MpRTPRPath * path)
{
  RcvEventBasedController *this;
  Subflow *lookup_result;
  this = REFCTRLER (controller_ptr);
  THIS_WRITELOCK (this);
  lookup_result =
      (Subflow *) g_hash_table_lookup (this->subflows,
      GINT_TO_POINTER (subflow_id));
  if (lookup_result != NULL) {
    GST_WARNING_OBJECT (this, "The requested add operation can not be done "
        "due to duplicated subflow id (%d)", subflow_id);
    goto exit;
  }
  lookup_result = _make_subflow (subflow_id, path);
  g_hash_table_insert (this->subflows, GINT_TO_POINTER (subflow_id),
                       lookup_result);
  lookup_result->ricalcer = this->ricalcer;
exit:
  THIS_WRITEUNLOCK (this);
}

void
refctrler_rem_path (gpointer controller_ptr, guint8 subflow_id)
{
  RcvEventBasedController *this;
  Subflow *lookup_result;
  this = REFCTRLER (controller_ptr);
  THIS_WRITELOCK (this);
  lookup_result =
      (Subflow *) g_hash_table_lookup (this->subflows,
      GINT_TO_POINTER (subflow_id));
  if (lookup_result == NULL) {
    GST_WARNING_OBJECT (this, "The requested remove operation can not be done "
        "due to not existed subflow id (%d)", subflow_id);
    goto exit;
  }
  g_hash_table_remove (this->subflows, GINT_TO_POINTER (subflow_id));
exit:
  THIS_WRITEUNLOCK (this);
}


void
refctrler_set_callbacks (void (**riport_can_flow_indicator) (gpointer),
    void (**controller_add_path) (gpointer, guint8, MpRTPRPath *),
    void (**controller_rem_path) (gpointer, guint8))
{
  if (riport_can_flow_indicator) {
    *riport_can_flow_indicator = refctrler_report_can_flow;
  }
  if (controller_add_path) {
    *controller_add_path = refctrler_add_path;
  }
  if (controller_rem_path) {
    *controller_rem_path = refctrler_rem_path;
  }
}



GstBufferReceiverFunc
refctrler_setup_mprtcp_exchange (RcvEventBasedController * this,
    gpointer data, GstBufferReceiverFunc func)
{
  GstBufferReceiverFunc result;
  THIS_WRITELOCK (this);
  this->send_mprtcp_packet_func = func;
  this->send_mprtcp_packet_data = data;
  result = refctrler_receive_mprtcp;
  THIS_WRITEUNLOCK (this);
  return result;
}

void
refctrler_report_can_flow (gpointer ptr)
{
  RcvEventBasedController *this;
  this = REFCTRLER (ptr);
  GST_DEBUG_OBJECT (this, "RTCP riport can now flowable");
  THIS_WRITELOCK (this);
  this->report_is_flowable = TRUE;
  THIS_WRITEUNLOCK (this);
}


//------------------------- Incoming Report Processor -------------------


void
refctrler_receive_mprtcp (gpointer ptr, GstBuffer * buf)
{
  GstMPRTCPSubflowBlock *block;
  RcvEventBasedController *this = REFCTRLER (ptr);
  guint16 subflow_id;
  guint8 info_type;
  Subflow *subflow;
  GstMapInfo map = GST_MAP_INFO_INIT;

  if (G_UNLIKELY (!gst_buffer_map (buf, &map, GST_MAP_READ))) {
    GST_WARNING_OBJECT (this, "The buffer is not readable");
    return;
  }
  block = (GstMPRTCPSubflowBlock *) map.data;
  THIS_WRITELOCK (this);

  gst_mprtcp_block_getdown (&block->info, &info_type, NULL, &subflow_id);
  if (info_type != MPRTCP_BLOCK_TYPE_RIPORT) {
    goto done;
  }
  subflow =
      (Subflow *) g_hash_table_lookup (this->subflows,
      GINT_TO_POINTER (subflow_id));

  if (subflow == NULL) {
    GST_WARNING_OBJECT (this,
        "MPRTCP riport can not be binded any "
        "subflow with the given id: %d", subflow_id);
    goto done;
  }
  _processing_selector (subflow, block);

done:
  gst_buffer_unmap (buf, &map);
  THIS_WRITEUNLOCK (this);
}


void
_processing_selector (Subflow * this, GstMPRTCPSubflowBlock * block)
{
  guint8 pt;

  gst_rtcp_header_getdown (&block->block_header, NULL, NULL, NULL, &pt, NULL,
      NULL);

  if (pt == (guint8) GST_RTCP_TYPE_SR) {
    _processing_srblock_processor (this,
        &block->sender_riport.sender_block);
  } else {
    GST_WARNING ("Event Based Flow receive controller "
        "can only process MPRTCP SR riports. "
        "The received riport payload type is: %d", pt);
  }
}

void
_processing_srblock_processor (Subflow * this, GstRTCPSRBlock * srb)
{
  guint64 ntptime;
  guint32 SR_new_packet_count;
  GST_DEBUG ("RTCP SR riport arrived for subflow %p->%p", this, srb);
  gst_rtcp_srb_getdown(srb, &ntptime, NULL, &SR_new_packet_count, NULL);
//  gst_print_rtcp_srb(srb);
  if(ntptime < _irt0(this)->SR_sent_ntp_time){
      GST_WARNING_OBJECT(this, "Late SR report arrived");
      goto done;
  }
  _step_ir(this);
//  g_print("Received NTP time for subflow %d is %lu->%lu\n", this->id, ntptime,
//          get_epoch_time_from_ntp_in_ns(NTP_NOW - ntptime));
  _irt0(this)->SR_sent_ntp_time = ntptime;
  _irt0(this)->SR_received_ntp_time = NTP_NOW;
  if(SR_new_packet_count == 0 &&
     SR_new_packet_count == _irt0(this)->SR_actual_packet_count &&
     _irt0(this)->SR_actual_packet_count == _irt0(this)->SR_last_packet_count){
      mprtpr_path_set_state(this->path, MPRTPR_PATH_STATE_PASSIVE);
  }else if(mprtpr_path_get_state(this->path) == MPRTPR_PATH_STATE_PASSIVE){
      mprtpr_path_set_state(this->path, MPRTPR_PATH_STATE_ACTIVE);
  }
  _irt0(this)->SR_last_packet_count = _irt0(this)->SR_actual_packet_count;
  _irt0(this)->SR_actual_packet_count = SR_new_packet_count;
  mprtpr_path_set_delay(this->path, get_epoch_time_from_ntp_in_ns(NTP_NOW - ntptime));
done:
  return;
}


void
_step_ir (Subflow * this)
{
  this->ir_index = 1 - this->ir_index;
  memset ((gpointer) _irt0 (this), 0, sizeof (IRMoment));
}



//------------------------ Outgoing Report Producer -------------------------

static void
_orp_main(RcvEventBasedController * this)
{
  ReportIntervalCalculator* ricalcer;
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  guint16 report_length = 0;
  guint16 block_length = 0;
  GstBuffer *block;
  guint subflows_num = 0;
  gdouble media_rate = 0., avg_rtcp_size = 0.;
//  GstClockTime last_delay;
//  GstClockTime now;
//  guint16 lost=0,expected=0,received=0;

//  now = gst_clock_get_time(this->sysclock);
  ricalcer = this->ricalcer;
  if (!this->report_is_flowable || !ricalcer_do_report_now(ricalcer)) {
      goto done;
  }
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    subflow = (Subflow *) val;

    if(!_irt0(subflow)->SR_sent_ntp_time){
      continue;
    }

//    g_print("S%d report time: %lu\n", subflow->id, GST_TIME_AS_MSECONDS(gst_clock_get_time(this->sysclock)));
    _step_or(subflow);
    block = _get_mprtcp_rr_block (this, subflow, &block_length);
    report_length += block_length;
    if (_ort0(subflow)->late_discarded_bytes !=
        _ort1(subflow)->late_discarded_bytes) {
      GstBuffer *xr;
      xr = _get_mprtcp_xr_7243_block (this, subflow, &block_length);
      block = gst_buffer_append (block, xr);
      report_length += block_length;
    }

    {
      GstBuffer *xr;
      xr = _get_mprtcp_xr_owd_block(this, subflow, &block_length);
      block = gst_buffer_append (block, xr);
      report_length += block_length;
    }

    report_length += 12 /*MPRTCP REPOR HEADER */  +
        (28 << 3) /*UDP Header overhead */ ;

    subflow->avg_rtcp_size += (report_length - subflow->avg_rtcp_size) / 4.;
    this->send_mprtcp_packet_func (this->send_mprtcp_packet_data, block);
    media_rate += _ort0(subflow)->media_rate;
    avg_rtcp_size += subflow->avg_rtcp_size;
    ++subflows_num;
  }
  avg_rtcp_size /= (gdouble)subflows_num;
  ricalcer_refresh_parameters(ricalcer,
                              media_rate,
                              avg_rtcp_size);
  ricalcer_urgent_report_request(ricalcer);
  ricalcer_do_next_report_time(ricalcer);

done:
  return;
}

void
_step_or (Subflow * this)
{
  this->or_index = 1 - this->or_index;
  memset ((gpointer) _ort0 (this), 0, sizeof (ORMoment));

  _ort0(this)->time = gst_clock_get_time(this->sysclock);
  _ort0(this)->lost_packet_num = _ort1(this)->lost_packet_num;
  _ort0(this)->late_discarded_bytes =
       mprtpr_path_get_total_late_discarded_bytes_num (this->path);
  _ort0(this)->received_packet_num =
       mprtpr_path_get_total_received_packets_num (this->path);
  _ort0(this)->received_bytes =
       mprtpr_path_get_total_bytes_received(this->path);
  _ort0(this)->received_payload_bytes =
       mprtpr_path_get_total_payload_bytes(this->path);

  _ort0(this)->median_skew =
      mprtpr_path_get_drift_window(this->path,
                                   &_ort0(this)->min_skew,
                                   &_ort0(this)->max_skew);

      mprtpr_path_get_ltdelays(this->path,
                            &_ort0(this)->delay40,
                            &_ort0(this)->delay80,
                            &_ort0(this)->min_delay,
                            &_ort0(this)->max_delay);
  _ort0(this)->last_delay = mprtpr_path_get_avg_4last_delay(this->path);

  _ort0(this)->cycle_num = mprtpr_path_get_cycle_num (this->path);
  _ort0(this)->jitter = mprtpr_path_get_jitter (this->path);
  _ort0(this)->HSN = mprtpr_path_get_highest_sequence_number (this->path);

  _ort0(this)->media_rate = 64000.;
  ++this->or_num;
//  g_print("ID: %d\n"
//          "OR NUM: %d\n"
//          "lost_packet_num: %hu\n"
//          "time: %lu\n"
//          "late_discarded_bytes: %u\n"
//          "received_packet_num %u\n"
//          "received_bytes %u\n"
//          "received_payload_bytes %u\n"
//          "media_rate %f\n"
//          "median_delay %lu\n"
//          "min_delay %lu\n"
//          "max_delay %lu\n"
//          "median_skew %lu\n"
//          "skew_bytes %u\n"
//          "min_skew %lu\n"
//          "max_skew %lu\n"
//          "cycle_num %hu\n"
//          "jitter %u\n"
//          "HSN: %hu\n",
//          this->id,
//          this->or_num,
//          _ort0(this)->lost_packet_num,
//          _ort0(this)->time,
//          _ort0(this)->late_discarded_bytes,
//          _ort0(this)->received_packet_num,
//          _ort0(this)->received_bytes,
//          _ort0(this)->received_payload_bytes,
//          _ort0(this)->media_rate,
//          _ort0(this)->median_delay,
//          _ort0(this)->min_delay,
//          _ort0(this)->max_delay,
//          _ort0(this)->median_skew,
//          _ort0(this)->skew_bytes,
//          _ort0(this)->min_skew,
//          _ort0(this)->max_skew,
//          _ort0(this)->cycle_num,
//          _ort0(this)->jitter,
//          _ort0(this)->HSN);
}


GstBuffer *
_get_mprtcp_xr_7243_block (RcvEventBasedController * this, Subflow * subflow,
    guint16 * buf_length)
{
  GstMPRTCPSubflowBlock block;
  GstRTCPXR_RFC7243 *xr;
  gpointer dataptr;
  guint16 length;
  guint8 block_length;
  GstBuffer *buf;

  gst_mprtcp_block_init (&block);
  xr = gst_mprtcp_riport_block_add_xr_rfc2743 (&block);
  _setup_xr_rfc2743_late_discarded_report (subflow, xr, this->ssrc);
  gst_rtcp_header_getdown (&xr->header, NULL, NULL, NULL, NULL, &length, NULL);
  block_length = (guint8) length + 1;
  gst_mprtcp_block_setup (&block.info, MPRTCP_BLOCK_TYPE_RIPORT, block_length,
      (guint16) subflow->id);
  length = (block_length + 1) << 2;
  dataptr = g_malloc0 (length);
  memcpy (dataptr, &block, length);
  buf = gst_buffer_new_wrapped (dataptr, length);
  if (buf_length) {
    *buf_length = length;
  }
  //gst_print_mprtcp_block(&block, NULL);
  return buf;
}


GstBuffer *
_get_mprtcp_rr_block (RcvEventBasedController * this, Subflow * subflow,
    guint16 * buf_length)
{
  GstMPRTCPSubflowBlock block;
  GstRTCPRR *rr;
  gpointer dataptr;
  guint16 length;
  guint8 block_length;
  GstBuffer *buf;

  gst_mprtcp_block_init (&block);
  rr = gst_mprtcp_riport_block_add_rr (&block);
  _setup_rr_report (subflow, rr, this->ssrc);
  gst_rtcp_header_getdown (&rr->header, NULL, NULL, NULL, NULL, &length, NULL);
  block_length = (guint8) length + 1;
  gst_mprtcp_block_setup (&block.info, MPRTCP_BLOCK_TYPE_RIPORT, block_length,
      (guint16) subflow->id);
  length = (block_length + 1) << 2;
  dataptr = g_malloc0 (length);
  memcpy (dataptr, &block, length);
  buf = gst_buffer_new_wrapped (dataptr, length);
  if (buf_length) {
    *buf_length = length;
  }
  //gst_print_mprtcp_block(&block, NULL);
//  gst_print_rtcp_rr(rr);
  return buf;
}


GstBuffer *
_get_mprtcp_xr_owd_block (
    RcvEventBasedController * this,
    Subflow * subflow,
    guint16 * buf_length)
{
  GstMPRTCPSubflowBlock block;
  GstRTCPXR_OWD *xr;
  gpointer dataptr;
  guint16 length;
  guint8 block_length;
  GstBuffer *buf;

  gst_mprtcp_block_init (&block);
  xr = gst_mprtcp_riport_block_add_xr_owd (&block);
  _setup_xr_owd_report(subflow, xr, this->ssrc);
  gst_rtcp_header_getdown (&xr->header, NULL, NULL, NULL, NULL, &length, NULL);
  block_length = (guint8) length + 1;
  gst_mprtcp_block_setup (&block.info, MPRTCP_BLOCK_TYPE_RIPORT, block_length,
      (guint16) subflow->id);
  length = (block_length + 1) << 2;
  dataptr = g_malloc0 (length);
  memcpy (dataptr, &block, length);
  buf = gst_buffer_new_wrapped (dataptr, length);
  if (buf_length) {
    *buf_length = length;
  }
  //gst_print_mprtcp_block(&block, NULL);
  return buf;
}


void
_setup_xr_rfc2743_late_discarded_report (Subflow * this,
    GstRTCPXR_RFC7243 * xr, guint32 ssrc)
{
  guint8 flag = RTCP_XR_RFC7243_I_FLAG_INTERVAL_DURATION;
  gboolean early_bit = FALSE;
  guint32 late_discarded_bytes;

  gst_rtcp_header_change (&xr->header, NULL, NULL, NULL, NULL, NULL, &ssrc);
  late_discarded_bytes =
      _uint32_diff (_ort1(this)->late_discarded_bytes,
                    _ort0(this)->late_discarded_bytes);
  gst_rtcp_xr_rfc7243_change (xr, &flag, &early_bit, NULL,
      &late_discarded_bytes);
//  g_print("DISCARDED REPORT SETTED UP\n");
}

void
_setup_rr_report (Subflow * this, GstRTCPRR * rr, guint32 ssrc)
{
  guint8 fraction_lost;
  guint32 ext_hsn, LSR, DLSR;
  guint16 expected = 0, received = 0, lost = 0;
  gdouble received_bytes, interval;
  GstClockTime obsolate_treshold;
  if(this->or_num < 3) obsolate_treshold = 0;
  else obsolate_treshold=ricalcer_get_obsolate_time(this->ricalcer);

  gst_rtcp_header_change (&rr->header, NULL, NULL, NULL, NULL, NULL, &ssrc);
  mprtpr_path_get_obsolate_stat(this->path,
                                obsolate_treshold,
                                &lost,
                                &received,
                                &expected);
  _ort0(this)->lost_packet_num += lost;
  _ort0(this)->received += received;
  _ort0(this)->expected += expected;
  _ort0(this)->lost += lost;
//  g_print("Sub%d: HSN:%hu->%hu=%hu - received:%u->%u=%u lost: %u->%u=%u\n",
//          this->id, _ort1(this)->HSN, _ort0(this)->HSN, expected,
//          _ort1(this)->received_packet_num,
//          _ort0(this)->received_packet_num,
//          received,
//          _ort1(this)->lost_packet_num,
//          _ort0(this)->lost_packet_num,
//          lost);

  if(_ort0(this)->received < _ort0(this)->expected)
    fraction_lost = (256. * ((gdouble) _ort0(this)->lost ) /
                    ((gdouble) (_ort0(this)->received)));
  else
    fraction_lost = 0;
//  g_print("%d: expected: %hu received: %hu lost: %hu fraction_lost:256.%f/%f=%d\n",
//          this->id,
//          _ort0(this)->expected,
//          _ort0(this)->received,
//          _ort0(this)->lost,
//          (gdouble) _ort0(this)->lost,
//          (gdouble) _ort0(this)->received,
//          (guint8)(256. * ((gdouble) _ort0(this)->lost  /
//          (gdouble) (_ort0(this)->received))));

  ext_hsn = (((guint32) _ort0(this)->cycle_num) << 16) | ((guint32) _ort0(this)->HSN);

//  g_print("Cum lost: %d\n", _ort0(this)->lost_packet_num);

  LSR = (guint32) (_irt0(this)->SR_sent_ntp_time >> 16);

  if (_irt0(this)->SR_sent_ntp_time == 0) {
    DLSR = 0;
  } else {
    guint64 temp;
    temp = NTP_NOW - _irt0(this)->SR_received_ntp_time;
    DLSR = (guint32)(temp>>16);
  }
  gst_rtcp_rr_add_rrb (rr,
                       0,
                       fraction_lost,
                       _ort0(this)->lost_packet_num,
                       ext_hsn,
                       _ort0(this)->jitter,
                       LSR,
                       DLSR);

  received_bytes = (gdouble) (_ort0(this)->received_payload_bytes -
                              _ort1(this)->received_payload_bytes);
  interval =
      (gdouble) GST_TIME_AS_SECONDS (_ort0(this)->time - _ort1(this)->time);
  if (interval < 1.) {
    interval = 1.;
  }
  _ort0(this)->media_rate = received_bytes / interval;

//  gst_print_rtcp_rrb(&rr->blocks);
//  reset
//
//    g_print("this->media_rate = %f / %f = %f\n",
//                   received_bytes,
//                   interval, this->media_rate);
}


void
_setup_xr_owd_report (Subflow * this,
    GstRTCPXR_OWD * xr, guint32 ssrc)
{
  guint8 flag = RTCP_XR_RFC7243_I_FLAG_INTERVAL_DURATION;
  guint32 owd, min, max;
  guint16 percentile = 40;
  guint16 invert_percentile = 25;

  min = (guint32) (get_ntp_from_epoch_ns(_ort0(this)->delay40)>>16);
  max = (guint32) (get_ntp_from_epoch_ns(_ort0(this)->delay80)>>16);
  owd = (guint32) (get_ntp_from_epoch_ns(_ort0(this)->last_delay)>>16);

  gst_rtcp_header_change (&xr->header, NULL,NULL, NULL, NULL, NULL, &ssrc);
  gst_rtcp_xr_owd_change(xr, &flag, &ssrc, &percentile, &invert_percentile,
                         &owd, &min, &max);
}

//----------------------------- System Notifier ------------------------------
void
_system_notifier_main(RcvEventBasedController * this)
{

}

//----------------------------- Play Controller -----------------------------
void
_play_controller_main(RcvEventBasedController * this)
{
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  gboolean valid = FALSE;
  guint64 min_delay = 1<<31, max_delay = 0, skew;
//if(1) return;
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    subflow = (Subflow *) val;
    min_delay = MIN(_ort0(subflow)->min_delay, min_delay);
    max_delay = MAX(_ort0(subflow)->delay80, max_delay);
    skew = mprtpr_path_get_drift_window(subflow->path, NULL, NULL);
    if(!skew || !max_delay || !min_delay) {
      continue;
    }
    valid = TRUE;
    _refresh_subflow_skew(this, skew);
  }
  if(!valid) goto done;

  //set playout skew and delay
  {
    guint64 playout_delay;
    guint64 playout_skew;
    playout_delay = max_delay - min_delay;
    playout_delay = MAX(playout_delay, 10 * GST_MSECOND);
    playout_delay = MIN(DELAY_SKEW_MAX, playout_delay);
    playout_skew = bintree_get_top_value(this->subflow_skew_tree);
    playout_skew = MIN(500 * GST_USECOND, playout_skew);
    if((playout_delay>>2) < playout_skew){
      playout_skew = playout_delay>>2;
    }
//    g_print("Set playout MIN: %lu MAX: %lu "
//        "stream delay: %lu tick interval: %lu\n",
//        min_delay, max_delay,
//            playout_delay, playout_skew);
    stream_joiner_set_stream_delay(this->joiner, playout_delay);
    stream_joiner_set_tick_interval(this->joiner, playout_skew);
  }

done:
  return;
}




void _refresh_subflow_skew(RcvEventBasedController *this,
                                     guint64 skew)
{
  this->subflow_skews[this->subflow_skew_index] = skew;
  bintree_insert_value(this->subflow_skew_tree, this->subflow_skews[this->subflow_skew_index]);
  if(this->subflow_skews[++this->subflow_skew_index] > 0){
      bintree_delete_value(this->subflow_skew_tree, this->subflow_skews[this->subflow_skew_index]);
  }
}



//------------------------- Utility functions --------------------------------

Subflow *
_make_subflow (guint8 id, MpRTPRPath * path)
{
  Subflow *result = _subflow_ctor ();
  g_object_ref (path);
  result->sysclock = gst_system_clock_obtain ();
  result->path = path;
  result->id = id;
  result->joined_time = gst_clock_get_time (result->sysclock);
  _reset_subflow (result);
  return result;
}


void
_ruin_subflow (gpointer * subflow)
{
  Subflow *this;
  g_return_if_fail (subflow);
  this = (Subflow *) subflow;
  g_object_unref (this->sysclock);
  g_object_unref (this->path);
  _subflow_dtor (this);
}

void
_reset_subflow (Subflow * this)
{
  this->avg_rtcp_size = 1024.;
}


Subflow *
_subflow_ctor (void)
{
  Subflow *result;
  result = g_malloc0 (sizeof (Subflow));
  return result;
}

void
_subflow_dtor (Subflow * this)
{
  g_return_if_fail (this);
  g_free (this);
}


guint32
_uint32_diff (guint32 start, guint32 end)
{
  if (start <= end) {
    return end - start - 1;
  }
  return ~((guint32) (start - end));
}

gint
_cmp_for_maxtree (guint64 x, guint64 y)
{
  return x == y ? 0 : x < y ? -1 : 1;
}



#undef MAX_RIPORT_INTERVAL
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
