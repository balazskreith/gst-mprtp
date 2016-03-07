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
#include "mprtplogger.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define MIN_MEDIA_RATE 50000

#define _now(this) (gst_clock_get_time (this->sysclock))

GST_DEBUG_CATEGORY_STATIC (rcvctrler_debug_category);
#define GST_CAT_DEFAULT rcvctrler_debug_category

G_DEFINE_TYPE (RcvController, rcvctrler, G_TYPE_OBJECT);

#define NORMAL_RIPORT_PERIOD_TIME (5*GST_SECOND)

typedef struct _Subflow Subflow;

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

struct _Subflow
{
  guint8                        id;
  MpRTPRPath*                   path;
  GstClock*                     sysclock;
  GstClockTime                  joined_time;
  ReportIntervalCalculator*     ricalcer;

  gdouble                       avg_rtcp_size;
  guint32                       total_lost;
  guint32                       total_received;
  guint16                       HSN;
  guint64                       LSR;

  gchar                        *logfile;

  guint32                       discarded_bytes;
  guint32                       rcvd_bytes[10];
  guint                         rcvd_bytes_index;
  guint32                       rcvd_bytes_sum;

};

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
rcvctrler_finalize (GObject * object);

static void
refctrler_ticker (void *data);

static void
_logging (RcvController *this);
//------------------------ Outgoing Report Producer -------------------------

static void
_orp_main(RcvController * this);

static void
_orp_add_rr(
    RcvController *this,
    Subflow *subflow);

static void
_orp_add_xr_rfc7243(
    RcvController *this,
    Subflow *subflow);

static void
_orp_add_xr_owd(
    RcvController *this,
    Subflow *subflow);

static void
_orp_add_xr_owd_rle(
    RcvController * this,
    Subflow *subflow);

static void
_orp_add_xr_rfc3611(
    RcvController * this,
    Subflow *subflow);

static void
_orp_add_xr_rfc7097(
    RcvController * this,
    Subflow *subflow);

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

  this->report_producer = g_object_new(REPORTPRODUCER_TYPE, NULL);
  this->report_processor = g_object_new(REPORTPROCESSOR_TYPE, NULL);
}

void
_logging (RcvController *this)
{
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  gdouble playout_delay;
  gint32 playout_buffer_len;
  guint32 jitter;
  GstClockTime median_delay;
  gchar main_file[255];

  if(!this->joiner || !this->log_flag) goto done;
  sprintf(main_file, "logs/sub_rcv_sum.csv");

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    guint32 rcvd_bytes;
    guint32 discarded_bytes;
    subflow = (Subflow *) val;
    if(!subflow->logfile){
      subflow->logfile = g_malloc(255);
      sprintf(subflow->logfile, "logs/sub_%d_rcv.csv", subflow->id);
    }

    discarded_bytes = subflow->discarded_bytes;

    mprtpr_path_get_RR_stats(subflow->path,
                             NULL,
                             NULL,
                             &jitter,
                             NULL,
                             NULL,
                             &rcvd_bytes);

    subflow->rcvd_bytes_sum -= subflow->rcvd_bytes[subflow->rcvd_bytes_index];
    if(++subflow->rcvd_bytes_index){ subflow->rcvd_bytes_index = 0; }
    subflow->rcvd_bytes[subflow->rcvd_bytes_index] = rcvd_bytes - subflow->rcvd_bytes[subflow->rcvd_bytes_index];
    subflow->rcvd_bytes_sum += subflow->rcvd_bytes[subflow->rcvd_bytes_index];


    mprtpr_path_get_XR7243_stats(subflow->path,
                                 NULL,
                                 &subflow->discarded_bytes);

    mprtpr_path_get_XROWD_stats(subflow->path,
                                &median_delay,
                                NULL,
                                NULL);

    discarded_bytes = subflow->discarded_bytes - discarded_bytes;

    mprtp_logger(subflow->logfile, "%u,%u,%lu,%u\n",
            subflow->rcvd_bytes_sum * 8,
            discarded_bytes * 8,
            median_delay,
            jitter);

  }

  stream_joiner_get_stats(this->joiner, &playout_delay, &playout_buffer_len);
  mprtp_logger(main_file,"%f,%d\n", playout_delay,playout_buffer_len);

done:
  return;
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

  _logging(this);

  if(!this->enabled) goto done;

  _path_ticker_main(this);
  _orp_main(this);
  _system_notifier_main(this);

done:
  next_scheduler_time = _now(this) + 100 * GST_MSECOND;
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
rcvctrler_set_additional_reports(RcvController * this,
                          gboolean rfc3611_reports,
                          gboolean rfc7097_reports,
                          gboolean rfc7243_reports
                          )
{
  THIS_WRITELOCK (this);
  this->rfc3611_enabled = rfc3611_reports;
  this->rfc7097_enabled = rfc7097_reports;
  this->rfc7243_enabled = rfc7243_reports;
  THIS_WRITEUNLOCK (this);
}


void rcvctrler_set_logging_flag(RcvController *this, gboolean log_flag)
{
  THIS_WRITELOCK (this);
  this->log_flag = log_flag;
  THIS_WRITEUNLOCK (this);
}

//------------------------- Incoming Report Processor -------------------


void
rcvctrler_receive_mprtcp (RcvController *this, GstBuffer * buf)
{
  Subflow *subflow;
  GstMPRTCPReportSummary *summary;

  THIS_WRITELOCK (this);

  summary = report_processor_process_mprtcp(this->report_processor, buf);

  subflow =
      (Subflow *) g_hash_table_lookup (this->subflows,
      GINT_TO_POINTER (summary->subflow_id));

  if (subflow == NULL) {
    GST_WARNING_OBJECT (this,
        "MPRTCP riport can not be binded any "
        "subflow with the given id: %d", summary->subflow_id);
    goto done;
  }

  if(summary->SR.processed){
    this->report_is_flowable = TRUE;
    mprtpr_path_add_delay(subflow->path, get_epoch_time_from_ntp_in_ns(NTP_NOW - summary->SR.ntptime));
    report_producer_set_ssrc(this->report_producer, summary->ssrc);
    subflow->LSR = summary->SR.ntptime;
  }

  g_free(summary);

done:
  THIS_WRITEUNLOCK (this);
}


//------------------------ Outgoing Report Producer -------------------------

static void
_orp_main(RcvController * this)
{
  ReportIntervalCalculator* ricalcer;
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  guint report_length = 0;
  GstBuffer *buffer;


  if (!this->report_is_flowable) {
      //goto done;
  }

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    subflow = (Subflow *) val;
    ricalcer = subflow->ricalcer;
    if(!ricalcer_do_report_now(ricalcer)){
      continue;
    }

    report_producer_begin(this->report_producer, subflow->id);
    _orp_add_rr(this, subflow);
    if(this->rfc7243_enabled){
      _orp_add_xr_rfc7243(this, subflow);
    }
    if(this->rfc7097_enabled){
      _orp_add_xr_rfc7097(this, subflow);
    }
    if(this->rfc3611_enabled){
      _orp_add_xr_rfc3611(this, subflow);
    }
    DISABLE_LINE _orp_add_xr_owd(this, subflow);
    _orp_add_xr_owd_rle(this, subflow);

    buffer = report_producer_end(this->report_producer, &report_length);

    mprtpr_path_set_chunks_reported(subflow->path);

    subflow->avg_rtcp_size += (report_length - subflow->avg_rtcp_size) / 4.;
    this->send_mprtcp_packet_func (this->send_mprtcp_packet_data, buffer);

    ricalcer_refresh_parameters(ricalcer,
                                MIN_MEDIA_RATE,
                                subflow->avg_rtcp_size);
  }

  DISABLE_LINE _uint16_diff(0,0);
}



void _orp_add_rr(RcvController * this, Subflow *subflow)
{
  guint8 fraction_lost;
  guint32 ext_hsn;
  guint32 lost;
  guint32 expected;
  guint32 total_received;
  guint32 total_lost;
  guint32 jitter;
  guint16 cycle_num;
  guint16 HSN;
  guint32 LSR;
  guint32 DLSR;

  mprtpr_path_get_RR_stats(subflow->path,
                             &HSN,
                             &cycle_num,
                             &jitter,
                             &total_received,
                             &total_lost,
                             NULL);

  expected      = _uint32_diff(subflow->HSN, HSN);
  lost          = _uint32_diff(subflow->total_lost, total_lost);

  fraction_lost = ((gdouble) lost / (gdouble) expected) * 256.;
  ext_hsn       = (((guint32) cycle_num) << 16) | ((guint32) HSN);

  subflow->HSN            = HSN;
  subflow->total_lost     = total_lost;
  subflow->total_received = total_received;

  LSR = (guint32) (subflow->LSR >> 16);

  if (subflow->LSR == 0) {
      DLSR = 0;
  } else {
      guint64 temp;
      temp = NTP_NOW - subflow->LSR;
      DLSR = (guint32)(temp>>16);
  }


  report_producer_add_rr(this->report_producer,
                         fraction_lost,
                         subflow->total_lost,
                         ext_hsn,
                         jitter,
                         LSR,
                         DLSR
                         );
}


void _orp_add_xr_rfc7243(RcvController * this, Subflow *subflow)
{
  guint32 discarded_bytes;
  mprtpr_path_get_XR7243_stats(subflow->path,
                             NULL,
                             &discarded_bytes);

  report_producer_add_xr_rfc7243(this->report_producer,
                                 discarded_bytes);

}

void _orp_add_xr_owd(RcvController * this, Subflow *subflow)
{
  GstClockTime median_delay, min_delay, max_delay;
  guint32      u32_median_delay, u32_min_delay, u32_max_delay;

  mprtpr_path_get_XROWD_stats(subflow->path,
                                 &median_delay,
                                 &min_delay,
                                 &max_delay);
  u32_median_delay = (guint32)(get_ntp_from_epoch_ns(median_delay)>>16);
  u32_min_delay    = (guint32)(get_ntp_from_epoch_ns(min_delay)>>16);
  u32_max_delay    = (guint32)(get_ntp_from_epoch_ns(max_delay)>>16);

  report_producer_add_xr_owd(this->report_producer,
                             u32_median_delay,
                             u32_min_delay,
                             u32_max_delay);
}

void _orp_add_xr_owd_rle(RcvController * this, Subflow *subflow)
{
  GstRTCPXR_Chunk *chunks;
  guint chunks_num;
  guint16 begin_seq, end_seq;

  chunks = mprtpr_path_get_owd_chunks(subflow->path, &chunks_num, &begin_seq, &end_seq);

  report_producer_add_xr_owd_rle(this->report_producer,
                                 3,
                                 begin_seq,
                                 end_seq,
                                 chunks,
                                 chunks_num);

  g_free(chunks);
}

void _orp_add_xr_rfc3611(RcvController * this, Subflow *subflow)
{
  GstRTCPXR_Chunk *chunks;
  guint chunks_num;
  guint16 begin_seq, end_seq;

  chunks = mprtpr_path_get_lost_chunks(subflow->path, &chunks_num, &begin_seq, &end_seq);

  report_producer_add_xr_rfc3611(this->report_producer,
                                 3,
                                 begin_seq,
                                 end_seq,
                                 chunks,
                                 chunks_num);

  g_free(chunks);
}

void _orp_add_xr_rfc7097(RcvController * this, Subflow *subflow)
{
  GstRTCPXR_Chunk *chunks;
  guint chunks_num;
  guint16 begin_seq, end_seq;

  chunks = mprtpr_path_get_lost_chunks(subflow->path, &chunks_num, &begin_seq, &end_seq);

  report_producer_add_xr_rfc7097(this->report_producer,
                                 3,
                                 begin_seq,
                                 end_seq,
                                 chunks,
                                 chunks_num);

  g_free(chunks);
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
  if(this->logfile){
    g_free(this->logfile);
  }
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
