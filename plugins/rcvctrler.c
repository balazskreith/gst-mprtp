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
#include "rcvctrler.h"
#include "streamsplitter.h"
#include "gstmprtcpbuffer.h"
#include "mprtprpath.h"
#include "streamjoiner.h"
#include "ricalcer.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define SR_DELAYS_ARRAY_LENGTH 12

#define _ort0(this) (this->or_moments+this->or_index)
#define _ort1(this) (this->or_moments+1-this->or_index)
#define _irt0(this) (this->ir_moments+this->ir_index)
#define _irt1(this) (this->ir_moments+1-this->ir_index)

#define _now(this) (gst_clock_get_time (this->sysclock))

GST_DEBUG_CATEGORY_STATIC (rcvctrler_debug_category);
#define GST_CAT_DEFAULT rcvctrler_debug_category

G_DEFINE_TYPE (RcvController, rcvctrler, G_TYPE_OBJECT);

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
  gdouble                       receiving_rate;
  GstClockTime                  median_delay;
  GstClockTime                  min_delay;
  GstClockTime                  max_delay;
  guint16                       discarded;
  guint32                       discarded_bytes;
  guint16                       cycle_num;
  guint32                       jitter;
  guint16                       HSN;
  guint16                       total_lost;
  guint16                       missing;
  guint32                       received;
  guint32                       received_bytes;
  guint16                       received_diff;

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

  guint32                       monitored_bytes;

  //for stats
  guint32                       jitter;
  guint64                       estimated_delay;
  guint32                       rcvd_bytes;
  guint32                       discarded_bytes;

};

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
rcvctrler_finalize (GObject * object);

static void
refctrler_ticker (void *data);

//------------------------- Incoming Report Processor -------------------

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
_orp_main(RcvController * this);

void
_step_or (Subflow * this);

static GstBuffer*
_get_mprtcp_xr_7243_block (
    RcvController * this,
    Subflow * subflow,
    guint16 * block_length);

static GstBuffer *
_get_mprtcp_xr_owd_block (
    RcvController * this,
    Subflow * subflow,
    guint16 * buf_length);

static GstBuffer *
_get_mprtcp_xr_rfc7097_block (
    RcvController * this,
    Subflow * subflow,
    guint16 * buf_length);

static GstBuffer *
_get_mprtcp_xr_rfc3611_block (
    RcvController * this,
    Subflow * subflow,
    guint16 * buf_length);

static GstBuffer *
_get_mprtcp_xr_owd_rle_block (
    RcvController * this,
    Subflow * subflow,
    guint16 * buf_length);

static GstBuffer*
_get_mprtcp_rr_block (
    RcvController * this,
    Subflow * subflow,
    guint16 * block_length);

static void
_setup_xr_rfc7243_late_discarded_report (
    Subflow * this,
    GstRTCPXR_RFC7243 *xr,
    guint32 ssrc);

static void
_setup_rr_report (
    Subflow * this,
    GstRTCPRR * rr,
    guint32 ssrc);

static void
_setup_xr_owd (
    Subflow * this,
    GstRTCPXR_OWD * xr,
    guint32 ssrc);

static GstRTCPXR_RFC7097 *
_setup_xr_rfc7097 (Subflow * this,
                   GstRTCPXR_RFC7097 * xr,
                   guint32 ssrc,
                   guint *chunks_num);

static GstRTCPXR_RFC3611 *
_setup_xr_rfc3611 (Subflow * this,
                   GstRTCPXR_RFC3611 * xr,
                   guint32 ssrc,
                   guint *chunks_num);

static GstRTCPXR_OWD_RLE *
_setup_xr_owd_rle (Subflow * this,
                   GstRTCPXR_OWD_RLE * xr,
                   guint32 ssrc,
                   guint *chunks_num);

//----------------------------- System Notifier ------------------------------
static void
_system_notifier_main(RcvController * this);

//----------------------------- Path Ticker Main -----------------------------
static void
_path_ticker_main(RcvController * this);
//static void _refresh_subflow_skew(RcvEventBasedController *this,
//                                     guint64 skew);
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

static guint16
_uint16_diff (
    guint16 a,
    guint16 b);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------


void
rcvctrler_class_init (RcvControllerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = rcvctrler_finalize;

  GST_DEBUG_CATEGORY_INIT (rcvctrler_debug_category, "rndctrler", 0,
      "MpRTP Receiving Controller");

}


void
rcvctrler_setup (RcvController *this, StreamJoiner * joiner)
{
  THIS_WRITELOCK (this);
  this->joiner = joiner;
  THIS_WRITEUNLOCK (this);
}

void
rcvctrler_finalize (GObject * object)
{
  RcvController *this = RCVCTRLER (object);
  g_hash_table_destroy (this->subflows);
  gst_task_stop (this->thread);
  gst_task_join (this->thread);
//  g_object_unref (this->ricalcer);
  g_object_unref (this->sysclock);
  g_object_unref(this->report_producer);
}

static void
rcvctrler_stat_run (void *data);

void
rcvctrler_init (RcvController * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->subflows = g_hash_table_new_full (NULL, NULL,
      NULL, (GDestroyNotify) _ruin_subflow);
  this->ssrc = g_random_int ();
  this->report_is_flowable = FALSE;
  g_rw_lock_init (&this->rwmutex);
  g_rec_mutex_init (&this->thread_mutex);
  this->thread = gst_task_new (refctrler_ticker, this, NULL);
  gst_task_set_lock (this->thread, &this->thread_mutex);
  gst_task_start (this->thread);

  g_rec_mutex_init (&this->stat_thread_mutex);
  this->stat_thread = gst_task_new (rcvctrler_stat_run, this, NULL);
  gst_task_set_lock (this->stat_thread, &this->stat_thread_mutex);
  this->report_producer = g_object_new(REPORTPRODUCER_TYPE, NULL);
}

static gboolean file_init = FALSE;
static gchar file_names[10][255];
static FILE *files[10];
static FILE *main_file = NULL;

void
rcvctrler_stat_run (void *data)
{
  RcvController *this;
  GstClockID clock_id;
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  gdouble playout_delay;
  GstClockTime next_scheduler_time;
  FILE *file;
  gint32 playout_buffer_len;

  this = data;
  THIS_WRITELOCK (this);

  if(!this->joiner) goto done;

  if(!file_init){
    gint i = 0;
    for(i=0; i<10; ++i) {
        files[i] = NULL;
        sprintf(file_names[i], "logs/sub_%d_rcv.csv", i);
    }
    file_init = TRUE;
  }

  if( !main_file) main_file=fopen("logs/sub_rcv_sum.csv", "w");
  else main_file=fopen("logs/sub_rcv_sum.csv", "a");

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    guint32 rcvd_bytes;
    guint32 discarded_bytes;
    subflow = (Subflow *) val;
    if( !files[subflow->id]) files[subflow->id]=fopen(file_names[subflow->id], "w");
    else files[subflow->id]=fopen(file_names[subflow->id], "a");

    file = files[subflow->id];

    discarded_bytes = subflow->discarded_bytes;
    rcvd_bytes = subflow->rcvd_bytes;

    mprtpr_path_get_RR_stats(subflow->path,
                             NULL,
                             NULL,
                             &subflow->jitter,
                             NULL,
                             &subflow->rcvd_bytes);

    mprtpr_path_get_XR7243_stats(subflow->path,
                                 NULL,
                                 &subflow->discarded_bytes);

    mprtpr_path_get_XROWD_stats(subflow->path,
                                &subflow->estimated_delay,
                                NULL,
                                NULL);

    rcvd_bytes = subflow->rcvd_bytes - rcvd_bytes;
    discarded_bytes = subflow->discarded_bytes - discarded_bytes;

    fprintf(file,"%u,%u,%lu,%u\n",
            rcvd_bytes * 8,
            discarded_bytes * 8,
            subflow->estimated_delay,
            subflow->jitter);

    fclose(file);
  }

  stream_joiner_get_stats(this->joiner, &playout_delay, &playout_buffer_len);
  fprintf(main_file,"%f,%d\n", playout_delay,playout_buffer_len);

  fclose(main_file);

done:
  THIS_WRITEUNLOCK(this);

  next_scheduler_time = gst_clock_get_time(this->sysclock) + 1000 * GST_MSECOND;
  clock_id = gst_clock_new_single_shot_id (this->sysclock, next_scheduler_time);

  if (gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED) {
    GST_WARNING_OBJECT (this, "The playout clock wait is interrupted");
  }
  gst_clock_id_unref (clock_id);
}

void
refctrler_ticker (void *data)
{
  GstClockTime next_scheduler_time;
  RcvController *this;
  GstClockID clock_id;
//  guint64 max_path_skew = 0;
  this = RCVCTRLER (data);
  THIS_WRITELOCK (this);

  if(!this->enabled) goto done;

  _path_ticker_main(this);
  _orp_main(this);
  _system_notifier_main(this);

done:
  next_scheduler_time = _now(this) + 50 * GST_MSECOND;
  THIS_WRITEUNLOCK (this);
  clock_id = gst_clock_new_single_shot_id (this->sysclock, next_scheduler_time);

  if (gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED) {
    GST_WARNING_OBJECT (this, "The playout clock wait is interrupted");
  }
  gst_clock_id_unref (clock_id);
  //clockshot;
}


void
rcvctrler_add_path (RcvController *this, guint8 subflow_id,
    MpRTPRPath * path)
{
  Subflow *lookup_result;
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
//  lookup_result->ricalcer = this->ricalcer;
exit:
  THIS_WRITEUNLOCK (this);
}

void
rcvctrler_rem_path (RcvController *this, guint8 subflow_id)
{
  Subflow *lookup_result;
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

void rcvctrler_enable_auto_rate_and_cc(RcvController *this)
{
  GST_DEBUG_OBJECT (this, "Enable auto rate and flow controlling");
  THIS_WRITELOCK (this);
  this->enabled = TRUE;
  THIS_WRITEUNLOCK (this);
}

void rcvctrler_disable_auto_rate_and_congestion_control(RcvController *this)
{
  GST_DEBUG_OBJECT (this, "Disable auto rate and flow controlling");
  THIS_WRITELOCK (this);
  this->enabled = FALSE;
  THIS_WRITEUNLOCK (this);
}

void
rcvctrler_setup_callbacks(RcvController * this,
                          gpointer mprtcp_send_data,
                          GstBufferReceiverFunc mprtcp_send_func)
{
  THIS_WRITELOCK (this);
  this->send_mprtcp_packet_func = mprtcp_send_func;
  this->send_mprtcp_packet_data = mprtcp_send_data;
  THIS_WRITEUNLOCK (this);
}

void
rcvctrler_setup_discarding_reports(RcvController * this,
                          gboolean rfc7097_reports,
                          gboolean rfc7243_reports)
{
  THIS_WRITELOCK (this);
  this->rfc7097_enabled = rfc7097_reports;
  this->rfc7243_enabled = rfc7243_reports;
  THIS_WRITEUNLOCK (this);
}

void
rcvctrler_setup_rle_lost_reports(RcvController * this,
                          gboolean enabling)
{
  THIS_WRITELOCK (this);
  this->rfc3611_losts_enabled = enabling;
  THIS_WRITEUNLOCK (this);
}

void rcvctrler_set_logging_flag(RcvController *this, gboolean enable)
{
  THIS_WRITELOCK (this);
  if(this->stat_enabled ^ enable){
    if(enable) {
        gst_task_start (this->stat_thread);
    }
    else {
        gst_task_stop(this->stat_thread);
    }
    this->stat_enabled = enable;
  }
  THIS_WRITEUNLOCK (this);
}

void
rcvctrler_report_can_flow (RcvController *this)
{
  GST_DEBUG_OBJECT (this, "RTCP riport can now flowable");
  THIS_WRITELOCK (this);
  this->report_is_flowable = TRUE;
  THIS_WRITEUNLOCK (this);
}


//------------------------- Incoming Report Processor -------------------


void
rcvctrler_receive_mprtcp (RcvController *this, GstBuffer * buf)
{
  GstMPRTCPSubflowBlock *block;
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

  //Todo Decide weather we need this:
  mprtpr_path_add_delay(this->path, get_epoch_time_from_ntp_in_ns(NTP_NOW - ntptime));

  if(SR_new_packet_count == 0 &&
     SR_new_packet_count == _irt0(this)->SR_actual_packet_count &&
     _irt0(this)->SR_actual_packet_count == _irt0(this)->SR_last_packet_count){
  }
  _irt0(this)->SR_last_packet_count = _irt0(this)->SR_actual_packet_count;
  _irt0(this)->SR_actual_packet_count = SR_new_packet_count;
//  mprtpr_path_add_delay(this->path, get_epoch_time_from_ntp_in_ns(NTP_NOW - ntptime));
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
_orp_main(RcvController * this)
{
  ReportIntervalCalculator* ricalcer;
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  guint16 report_length = 0;
  guint16 block_length = 0;
  GstBuffer *block;


  if (!this->report_is_flowable) {
      //goto done;
  }

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    subflow = (Subflow *) val;
    ricalcer = subflow->ricalcer;
    if(!ricalcer_do_report_now(ricalcer)){
        goto done;
    }

    report_producer_begin(this->report_producer, subflow->id);
    report_producer_add_rr(this->report_producer, 1, 2, 3, 4, 5, 6);
    report_producer_add_xr_rfc7243(this->report_producer, 8);

    {
      GstMapInfo map;
      GstBuffer *buffer;
      GstRTCPXR_Chunk chunks[4];
      chunks[0].run_length = 1;
      chunks[1].run_length = 2;
      chunks[2].run_length = 3;
      report_producer_add_xr_rfc7097(this->report_producer, 9, 10, 11, chunks, 4);
      buffer = report_producer_end(this->report_producer);
      gst_buffer_map(buffer, &map, GST_MAP_READ);
      gst_print_rtcp((GstRTCPHeader*)map.data);
      gst_buffer_unmap(buffer, &map);
    }


//    g_print("Report time: %lu\n", GST_TIME_AS_MSECONDS(_ort0(subflow)->time - _ort1(subflow)->time));
    if(!_irt0(subflow)->SR_sent_ntp_time){
      continue;
    }
//    g_print("S%d report time: %lu\n", subflow->id, GST_TIME_AS_MSECONDS(gst_clock_get_time(this->sysclock)));
    _step_or(subflow);
    block = _get_mprtcp_rr_block (this, subflow, &block_length);
    report_length += block_length;

    //OWD simple report is useful because the min and max values
    {
      GstBuffer *xr;
      xr = _get_mprtcp_xr_owd_block (this, subflow, &block_length);
      block = gst_buffer_append (block, xr);
      report_length += block_length;
    }

    //Note: We can calculate the total discarded packet num
    //by using RLE with RFC7097
    if (_ort0(subflow)->discarded != _ort1(subflow)->discarded && this->rfc7243_enabled)
    {
      GstBuffer *xr;
      xr = _get_mprtcp_xr_7243_block (this, subflow, &block_length);
      block = gst_buffer_append (block, xr);
      report_length += block_length;
    }


    if (_ort0(subflow)->discarded != _ort1(subflow)->discarded && (this->rfc7097_enabled))
    {
      GstBuffer *xr;
      xr = _get_mprtcp_xr_rfc7097_block (this, subflow, &block_length);
      block = gst_buffer_append (block, xr);
      report_length += block_length;
    }

    {
      GstBuffer *xr;
      xr = _get_mprtcp_xr_owd_rle_block (this, subflow, &block_length);
      block = gst_buffer_append (block, xr);
      report_length += block_length;
    }

    if (this->rfc3611_losts_enabled)
    {
      //RFC3611 RLE encoded for better quantification of losts
      GstBuffer *xr;
      xr = _get_mprtcp_xr_rfc3611_block (this, subflow, &block_length);
      block = gst_buffer_append (block, xr);
      report_length += block_length;
    }

    mprtpr_path_set_chunks_reported(subflow->path);

    report_length += 12 /*MPRTCP REPOR HEADER */  +
        (28 << 3) /*UDP Header overhead */ ;

    subflow->avg_rtcp_size += (report_length - subflow->avg_rtcp_size) / 4.;
    this->send_mprtcp_packet_func (this->send_mprtcp_packet_data, block);
    ricalcer_refresh_parameters(ricalcer,
                                _ort0(subflow)->receiving_rate,
                                subflow->avg_rtcp_size);
  }
done:
  return;
}

void
_step_or (Subflow * this)
{
  this->or_index = 1 - this->or_index;
  memset ((gpointer) _ort0 (this), 0, sizeof (ORMoment));

  _ort0(this)->time = gst_clock_get_time(this->sysclock);
  mprtpr_path_get_RR_stats(this->path,
                           &_ort0(this)->HSN,
                           &_ort0(this)->cycle_num,
                           &_ort0(this)->jitter,
                           &_ort0(this)->received,
                           &_ort0(this)->received_bytes);

  mprtpr_path_get_XR7243_stats(this->path,
                               &_ort0(this)->discarded,
                               &_ort0(this)->discarded_bytes);

  mprtpr_path_get_XROWD_stats(this->path,
                               &_ort0(this)->median_delay,
                               &_ort0(this)->min_delay,
                               &_ort0(this)->max_delay);
  _ort0(this)->total_lost = _ort1(this)->total_lost;
//  _ort0(this)->receiving_rate = 64000.;
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
_get_mprtcp_xr_7243_block (RcvController * this, Subflow * subflow,
    guint16 * buf_length)
{
  GstMPRTCPSubflowBlock block;
  GstRTCPXR_RFC7243 *xr;
  gpointer dataptr;
  guint16 length;
  guint8 block_length;
  GstBuffer *buf;

  gst_mprtcp_block_init (&block);
  xr = gst_mprtcp_riport_block_add_xr_rfc7243 (&block);
  _setup_xr_rfc7243_late_discarded_report (subflow, xr, this->ssrc);
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
_get_mprtcp_xr_owd_block (RcvController * this, Subflow * subflow,
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
  _setup_xr_owd(subflow, xr, this->ssrc);
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
_get_mprtcp_xr_rfc7097_block (RcvController * this, Subflow * subflow,
    guint16 * buf_length)
{
  GstMPRTCPSubflowBlock block;
  GstMPRTCPSubflowBlock *new_block;
  GstRTCPXR_RFC7097 *new_xr,*xr;
  guint16 length = 0;
  guint8 block_length;
  guint8 new_block_length;
  GstBuffer *buf = NULL;
  guint chunks_num;

  gst_mprtcp_block_init (&block);
  xr = gst_mprtcp_riport_block_add_xr_rfc7097(&block);
  new_xr = _setup_xr_rfc7097(subflow, xr, this->ssrc, &chunks_num);
  new_block_length = sizeof(GstMPRTCPSubflowInfo) + sizeof(GstRTCPXR_RFC7097) + ((chunks_num-2)<<1);
  new_block = g_malloc0(new_block_length);
  memcpy(new_block, &block, sizeof(GstMPRTCPSubflowInfo));
  memcpy(&new_block->xr_rfc7097_report, new_xr, sizeof(GstRTCPXR_RFC7097) + ((chunks_num-2)<<1));
  gst_rtcp_header_getdown (&new_xr->header, NULL, NULL, NULL, NULL, &length, NULL);
  g_free(new_xr);
  block_length = (guint8) length + 1;
  gst_mprtcp_block_setup (&new_block->info, MPRTCP_BLOCK_TYPE_RIPORT, block_length, (guint16) subflow->id);
  length = (block_length + 1) << 2;
  buf = gst_buffer_new_wrapped (new_block, length);
  if (buf_length) {
    *buf_length = length;
  }
//  gst_print_mprtcp_block(new_block, NULL);
  return buf;
}



GstBuffer *
_get_mprtcp_xr_rfc3611_block (RcvController * this, Subflow * subflow,
    guint16 * buf_length)
{
  GstMPRTCPSubflowBlock block;
  GstMPRTCPSubflowBlock *new_block;
  GstRTCPXR_RFC3611 *new_xr,*xr;
  guint16 length = 0;
  guint8 block_length;
  guint8 new_block_length;
  GstBuffer *buf = NULL;
  guint chunks_num;

  gst_mprtcp_block_init (&block);
  xr = gst_mprtcp_riport_block_add_xr_rfc3611(&block);
  new_xr = _setup_xr_rfc3611(subflow, xr, this->ssrc, &chunks_num);
  new_block_length = sizeof(GstMPRTCPSubflowInfo) + sizeof(GstRTCPXR_RFC3611) + ((chunks_num-2)<<1);
  new_block = g_malloc0(new_block_length);
  memcpy(new_block, &block, sizeof(GstMPRTCPSubflowInfo));
  memcpy(&new_block->xr_rfc3611_report, new_xr, sizeof(GstRTCPXR_RFC3611) + ((chunks_num-2)<<1));
  gst_rtcp_header_getdown (&new_xr->header, NULL, NULL, NULL, NULL, &length, NULL);
  g_free(new_xr);
  block_length = (guint8) length + 1;
  gst_mprtcp_block_setup (&new_block->info, MPRTCP_BLOCK_TYPE_RIPORT, block_length, (guint16) subflow->id);
  length = (block_length + 1) << 2;
  buf = gst_buffer_new_wrapped (new_block, length);
  if (buf_length) {
    *buf_length = length;
  }
//  gst_print_mprtcp_block(new_block, NULL);
  return buf;
}


GstBuffer *
_get_mprtcp_xr_owd_rle_block (RcvController * this, Subflow * subflow,
    guint16 * buf_length)
{
  GstMPRTCPSubflowBlock block;
  GstMPRTCPSubflowBlock *new_block;
  GstRTCPXR_OWD_RLE *new_xr,*xr;
  guint16 length = 0;
  guint8 block_length;
  guint8 new_block_length;
  GstBuffer *buf = NULL;
  guint chunks_num;

  gst_mprtcp_block_init (&block);
  xr = gst_mprtcp_riport_block_add_xr_owd_rle(&block);
  new_xr = _setup_xr_owd_rle(subflow, xr, this->ssrc, &chunks_num);
  new_block_length = sizeof(GstMPRTCPSubflowInfo) + sizeof(GstRTCPXR_OWD_RLE) + ((chunks_num-2)<<1);
  new_block = g_malloc0(new_block_length);
  memcpy(new_block, &block, sizeof(GstMPRTCPSubflowInfo));
  memcpy(&new_block->xr_owd_rle_report, new_xr, sizeof(GstRTCPXR_OWD_RLE) + ((chunks_num-2)<<1));
  gst_rtcp_header_getdown (&new_xr->header, NULL, NULL, NULL, NULL, &length, NULL);
  g_free(new_xr);
  block_length = (guint8) length + 1;
  gst_mprtcp_block_setup (&new_block->info, MPRTCP_BLOCK_TYPE_RIPORT, block_length, (guint16) subflow->id);
  length = (block_length + 1) << 2;
  buf = gst_buffer_new_wrapped (new_block, length);
  if (buf_length) {
    *buf_length = length;
  }
//  gst_print_mprtcp_block(new_block, NULL);
  return buf;
}


GstBuffer *
_get_mprtcp_rr_block (RcvController * this, Subflow * subflow,
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




void
_setup_xr_rfc7243_late_discarded_report (Subflow * this,
    GstRTCPXR_RFC7243 * xr, guint32 ssrc)
{
  guint8 flag = RTCP_XR_RFC7243_I_FLAG_INTERVAL_DURATION;
  gboolean early_bit = FALSE;
  guint32 late_discarded_bytes;

  gst_rtcp_header_change (&xr->header, NULL, NULL, NULL, NULL, NULL, &ssrc);
  late_discarded_bytes =
      _uint32_diff (_ort1(this)->discarded_bytes,
                    _ort0(this)->discarded_bytes);
  gst_rtcp_xr_rfc7243_change (xr, &flag, &early_bit, NULL,
      &late_discarded_bytes);
//  g_print("DISCARDED REPORT SETTED UP\n");
}

void
_setup_xr_owd (Subflow * this,
    GstRTCPXR_OWD * xr, guint32 ssrc)
{
  guint8 flag = RTCP_XR_RFC7243_I_FLAG_INTERVAL_DURATION;
  guint32 median, min, max;

  median = (guint32)(get_ntp_from_epoch_ns(_ort0(this)->median_delay)>>16);
  min = (guint32)(get_ntp_from_epoch_ns(_ort0(this)->min_delay)>>16);
  max = (guint32)(get_ntp_from_epoch_ns(_ort0(this)->max_delay)>>16);
  gst_rtcp_header_change (&xr->header, NULL, NULL, NULL, NULL, NULL, &ssrc);
  gst_rtcp_xr_owd_change(xr,
                         &flag,
                         &ssrc,
                         &median,
                         &min,
                         &max);
}


GstRTCPXR_RFC7097 *
_setup_xr_rfc7097 (Subflow * this,
                   GstRTCPXR_RFC7097 * xr,
                   guint32 ssrc,
                   guint *chunks_num)
{
  GstRTCPXR_RFC7097 *result;
  GstRTCPXR_Chunk *chunks;
  guint chunks_num_;
  guint16 begin_seq, end_seq;
  guint16 header_length, block_length;
  gboolean early_bit = FALSE;
  guint8 thinning = 0;

//  chunks = mprtpr_path_get_XR7097_packet_nums_chunks(this->path, &chunks_num_, &begin_seq, &end_seq);
//  chunks = mprtpr_path_get_XR7097_sum_bytes_chunks(this->path, &chunks_num_, &begin_seq, &end_seq);
  chunks = mprtpr_path_get_discard_chunks(this->path, &chunks_num_, &begin_seq, &end_seq);
  result = g_malloc0(sizeof(*xr) + (chunks_num_ - 2) * 2);
//  g_print("chunks num: %u, size of bytes xr: %lu, total: %lu\n",
//          chunks_num, sizeof(*xr), sizeof(*xr) + (chunks_num - 2) * 2);
  memcpy(result, xr, sizeof(*xr));
  memcpy(result->chunks, chunks, chunks_num_ * 2);
  g_free(chunks);
  gst_rtcp_header_getdown(&result->header, NULL, NULL, NULL, NULL, &header_length, NULL);
  gst_rtcp_xr_block_getdown((GstRTCPXR*) result, NULL, &block_length, NULL);
  block_length+=(chunks_num_-2)>>1;
  header_length+=(chunks_num_-2)>>1;
  gst_rtcp_xr_rfc7097_change(result, &early_bit, &thinning, &ssrc, &begin_seq, &end_seq);
  gst_rtcp_xr_block_change((GstRTCPXR*) result, NULL, &block_length, NULL);
  gst_rtcp_header_change(&result->header, NULL, NULL, NULL, NULL, &header_length, NULL);
  if(chunks_num) *chunks_num = chunks_num_;
  return result;
}


GstRTCPXR_RFC3611 *
_setup_xr_rfc3611 (Subflow * this,
                   GstRTCPXR_RFC3611 * xr,
                   guint32 ssrc,
                   guint *chunks_num)
{
  GstRTCPXR_RFC3611 *result;
  GstRTCPXR_Chunk *chunks;
  guint chunks_num_;
  guint16 begin_seq, end_seq;
  guint16 header_length, block_length;
  gboolean early_bit = FALSE;
  guint8 thinning = 0;

//  chunks = mprtpr_path_get_XR3611_chunks(this->path, &chunks_num_, &begin_seq, &end_seq);
  chunks = mprtpr_path_get_lost_chunks(this->path, &chunks_num_, &begin_seq, &end_seq);
  result = g_malloc0(sizeof(*xr) + (chunks_num_ - 2) * 2);
//  g_print("chunks num: %u, size of bytes xr: %lu, total: %lu\n",
//          chunks_num, sizeof(*xr), sizeof(*xr) + (chunks_num - 2) * 2);
  memcpy(result, xr, sizeof(*xr));
  memcpy(result->chunks, chunks, chunks_num_ * 2);
  g_free(chunks);
  gst_rtcp_header_getdown(&result->header, NULL, NULL, NULL, NULL, &header_length, NULL);
  gst_rtcp_xr_block_getdown((GstRTCPXR*) result, NULL, &block_length, NULL);
  block_length+=(chunks_num_-2)>>1;
  header_length+=(chunks_num_-2)>>1;
  gst_rtcp_xr_rfc3611_change(result, &early_bit, &thinning, &ssrc, &begin_seq, &end_seq);
  gst_rtcp_xr_block_change((GstRTCPXR*) result, NULL, &block_length, NULL);
  gst_rtcp_header_change(&result->header, NULL, NULL, NULL, NULL, &header_length, NULL);
  if(chunks_num) *chunks_num = chunks_num_;
  return result;
}


GstRTCPXR_OWD_RLE *
_setup_xr_owd_rle (Subflow * this,
                   GstRTCPXR_OWD_RLE * xr,
                   guint32 ssrc,
                   guint *chunks_num)
{
  GstRTCPXR_OWD_RLE *result;
  GstRTCPXR_Chunk *chunks;
  guint chunks_num_;
  guint16 begin_seq, end_seq;
  guint16 header_length, block_length;
  gboolean early_bit = FALSE;
  guint8 thinning = 0;

  chunks = mprtpr_path_get_owd_chunks(this->path, &chunks_num_, &begin_seq, &end_seq);
  result = g_malloc0(sizeof(*xr) + (chunks_num_ - 2) * 2);
//  g_print("chunks numb: %u, size of bytes xr: %lu, total: %lu\n",
//          chunks_num, sizeof(*xr), sizeof(*xr) + (chunks_num - 2) * 2);
  memcpy(result, xr, sizeof(*xr));
  memcpy(result->chunks, chunks, chunks_num_ * 2);
  g_free(chunks);
  gst_rtcp_header_getdown(&result->header, NULL, NULL, NULL, NULL, &header_length, NULL);
  gst_rtcp_xr_block_getdown((GstRTCPXR*) result, NULL, &block_length, NULL);
  block_length+=(chunks_num_-2)>>1;
  header_length+=(chunks_num_-2)>>1;
  gst_rtcp_xr_owd_rle_change(result, &early_bit, &thinning, &ssrc, &begin_seq, &end_seq);
  gst_rtcp_xr_block_change((GstRTCPXR*) result, NULL, &block_length, NULL);
  gst_rtcp_header_change(&result->header, NULL, NULL, NULL, NULL, &header_length, NULL);
  if(chunks_num) *chunks_num = chunks_num_;
  return result;
}

void
_setup_rr_report (Subflow * this, GstRTCPRR * rr, guint32 ssrc)
{
  guint8 fraction_lost;
  guint32 ext_hsn, LSR, DLSR;
  guint16 expected;
  guint16 received;
  guint16 discarded;
  guint32 received_bytes;
  gdouble interval;

  gst_rtcp_header_change (&rr->header, NULL, NULL, NULL, NULL, NULL, &ssrc);

  ext_hsn = (((guint32) _ort0(this)->cycle_num) << 16) | ((guint32) _ort0(this)->HSN);
  expected = _uint16_diff(_ort1(this)->HSN, _ort0(this)->HSN);
  received =_uint16_diff(_ort1(this)->received, _ort0(this)->received);
  received_bytes = _uint32_diff(_ort1(this)->received_bytes,_ort0(this)->received_bytes);
  discarded = _uint16_diff(_ort1(this)->discarded, _ort0(this)->discarded);

  _ort0(this)->received_diff = received;

  if(expected < received)
    _ort0(this)->missing = received - expected;
  if(discarded){
    if(discarded < _ort1(this)->missing)
      _ort1(this)->missing-=discarded;
    else
      _ort1(this)->missing = 0;
  }

  if(_ort1(this)->missing){
      _ort0(this)->total_lost += _ort1(this)->missing;
      fraction_lost = (256. * ((gdouble) _ort1(this)->missing ) /
                      ((gdouble) (_ort1(this)->received_diff)));
  }else{
      fraction_lost = 0;
  }

  LSR = (guint32) (_irt0(this)->SR_sent_ntp_time >> 16);

  if (_irt0(this)->SR_sent_ntp_time == 0) {
    DLSR = 0;
  } else {
    guint64 temp;
    temp = NTP_NOW - _irt0(this)->SR_received_ntp_time;
    DLSR = (guint32)(temp>>16);
  }

  received_bytes *= (gdouble)fraction_lost / 256.;

  interval =
      (gdouble) GST_TIME_AS_SECONDS (_ort0(this)->time - _ort1(this)->time);
  if (interval < 1.) {
    interval = 1.;
  }
  _ort0(this)->receiving_rate = received_bytes / interval;
  gst_rtcp_rr_add_rrb (rr,
                         0,
                         fraction_lost,
                         _ort0(this)->total_lost,
                         ext_hsn,
                         _ort0(this)->jitter,
                         LSR,
                         DLSR);
//  gst_print_rtcp_rrb(&rr->blocks);
//  reset
//
//    g_print("this->media_rate = %f / %f = %f\n",
//                   received_bytes,
//                   interval, this->media_rate);
}

//----------------------------- System Notifier ------------------------------
void
_system_notifier_main(RcvController * this)
{

}

//----------------------------- Path Ticker Main -----------------------------
void
_path_ticker_main(RcvController * this)
{
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    subflow = (Subflow *) val;
    mprtpr_path_tick(subflow->path);
  }
}

//void _refresh_subflow_skew(RcvEventBasedController *this,
//                                     guint64 skew)
//{
//  this->subflow_skews[this->subflow_skew_index] = skew;
//  bintree_insert_value(this->subflow_skew_tree, this->subflow_skews[this->subflow_skew_index]);
//  if(this->subflow_skews[++this->subflow_skew_index] > 0){
//      bintree_delete_value(this->subflow_skew_tree, this->subflow_skews[this->subflow_skew_index]);
//  }
//}



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
  result->ricalcer = make_ricalcer(FALSE);
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
  g_object_unref (this->ricalcer);
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
    return end - start;
  }
  return ~((guint32) (start - end));
}

guint16
_uint16_diff (guint16 start, guint16 end)
{
  if (start <= end) {
    return end - start;
  }
  return ~((guint16) (start - end));
}



#undef MAX_RIPORT_INTERVAL
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
