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
#include "fbrafbprod.h"
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

#define PROFILING(func) \
{  \
  GstClockTime start, elapsed; \
  start = _now(this); \
  func; \
  elapsed = GST_TIME_AS_MSECONDS(_now(this) - start); \
  if(0 < elapsed) g_print("elapsed time in ms: %lu\n", elapsed); \
} \

#define REGULAR_REPORT_PERIOD_TIME (5*GST_SECOND)

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

#define RATEWINDOW_LENGTH 100
typedef struct _RateWindow{
  guint32 items[RATEWINDOW_LENGTH];
  gint    index;
  guint32 rate_value;
}RateWindow;

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
  guint32                       total_discarded_bytes;
  guint32                       total_packets_discarded_or_lost;
  guint16                       HSSN;
  guint64                       last_SR_report_sent;
  guint64                       last_SR_report_rcvd;
  GstClockTime                  LRR;

  gchar                        *logfile;
  gchar                        *statfile;
  gboolean                      log_initialized;

  gboolean                      regular_report_enabled;
  gboolean                      fb_report_enabled;
  GstClockTime                  next_regular_report;
  guint                         controlling_mode;

  GstClockTime                  next_feedback;
  gpointer                      fbproducer;
  GstClockTime                (*feedback_interval_calcer)(gpointer data);
  void                        (*feedback_setup_feedback)(gpointer data, ReportProducer *reportprod);

  RateWindow                    received_bytes;
  guint32                       receiver_rate;

};

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
rcvctrler_finalize (GObject * object);

static void
refctrler_ticker (void *data);

//------------------------ Outgoing Report Producer -------------------------

static void
_orp_main(
    RcvController * this);

static void
_orp_add_rr(
    RcvController *this,
    Subflow *subflow);

//----------------------------- System Notifier ------------------------------
static void
_system_notifier_main(RcvController * this);

//----- feedback specific functions
static GstClockTime
_default_interval_calcer(gpointer data);

static void
_default_feedback_appender(gpointer data, ReportProducer *reportprod);


//------------------------- Utility functions --------------------------------
static Subflow*
_make_subflow (
    guint8 id,
    MpRTPRPath * path);

static void
_ruin_subflow (
    gpointer * subflow);

static void
_reset_subflow (
    Subflow * subflow);

static void
_subflow_iterator(
    RcvController * this,
    void(*process)(Subflow*,gpointer),
    gpointer data);

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

static void
_update_receiver_rate(
    Subflow* subflow);

static guint32
_update_ratewindow(
    RateWindow *window,
    guint32 value);

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
rcvctrler_setup (RcvController *this, StreamJoiner * joiner, FECDecoder* fecdecoder)
{
  THIS_WRITELOCK (this);
  this->joiner     = joiner;
  this->fecdecoder = fecdecoder;
  THIS_WRITEUNLOCK (this);
}

void rcvctrler_change_interval_type(RcvController * this, guint8 subflow_id, guint type)
{
  Subflow *subflow;
  ReportIntervalCalculator *ricalcer;
  GHashTableIter iter;
  gpointer key, val;

  THIS_WRITELOCK (this);

  DISABLE_LINE _subflow_iterator(this, NULL, NULL);

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    subflow = (Subflow *) val;
    ricalcer = subflow->ricalcer;
    if(subflow_id == 255 || subflow_id == 0 || subflow_id == subflow->id){
      switch(type){
        case 0:
          ricalcer_set_mode(ricalcer, RTCP_INTERVAL_REGULAR_INTERVAL_MODE);
        break;
        case 1:
          ricalcer_set_mode(ricalcer, RTCP_INTERVAL_EARLY_RTCP_MODE);
        break;
        case 2:
        default:
          ricalcer_set_mode(ricalcer, RTCP_INTERVAL_IMMEDIATE_FEEDBACK_MODE);
        break;
      }
    }
  }
  THIS_WRITEUNLOCK (this);
}


static void _change_controlling_mode(RcvController *this, Subflow *subflow, guint controlling_mode)
{
  subflow->regular_report_enabled   = FALSE;
  subflow->fb_report_enabled        = FALSE;

  if(subflow->controlling_mode != controlling_mode && !subflow->fbproducer){
    g_object_unref(subflow->fbproducer);
  }

  subflow->controlling_mode = controlling_mode;
  switch(controlling_mode){
    case 0:
      subflow->feedback_interval_calcer  = _default_interval_calcer;
      subflow->feedback_setup_feedback = _default_feedback_appender;
      GST_DEBUG_OBJECT(subflow, "subflow %d set to no controlling mode", subflow->id);
      break;
    case 1:
      subflow->feedback_interval_calcer = _default_interval_calcer;
      subflow->feedback_setup_feedback = _default_feedback_appender;
      subflow->regular_report_enabled = TRUE;
      GST_DEBUG_OBJECT(subflow, "subflow %d set to only report processing mode", subflow->id);
      break;
    case 2:
      subflow->fbproducer = make_fbrafbproducer(this->ssrc, subflow->id);
      subflow->feedback_interval_calcer = fbrafbproducer_get_interval;
      subflow->feedback_setup_feedback = fbrafbproducer_setup_feedback;
      mprtpr_path_set_packetstracker(subflow->path, fbrafbproducer_track, subflow->fbproducer);
      subflow->regular_report_enabled   = TRUE;
      subflow->fb_report_enabled        = TRUE;
      if(ricalcer_rtcp_fb_allowed(subflow->ricalcer) == FALSE){
        g_warning("RTCP Immediate feedback message is not allowed, although FBRA+ requires it. FBRA+ will send it anyway.");
      }
      break;
    default:
      g_warning("Unknown controlling mode requested for subflow %d", subflow->id);
      break;
  }
}

void rcvctrler_change_controlling_mode(RcvController * this, guint8 subflow_id, guint controlling_mode)
{
  Subflow *subflow;
  GHashTableIter iter;
  gpointer key, val;
  THIS_WRITELOCK (this);
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    subflow = (Subflow *) val;
    if(subflow_id == 255 || subflow_id == 0 || subflow_id == subflow->id){
      _change_controlling_mode(this, subflow, controlling_mode);
    }
  }
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

  mprtp_free(this->fec_early_repaired_bytes);
  mprtp_free(this->fec_total_repaired_bytes);
}

void
rcvctrler_init (RcvController * this)
{
  this->sysclock           = gst_system_clock_obtain ();
  this->subflows           = g_hash_table_new_full (NULL, NULL,NULL, (GDestroyNotify) _ruin_subflow);
  this->ssrc               = g_random_int ();
  this->report_is_flowable = FALSE;
  this->report_producer    = g_object_new(REPORTPRODUCER_TYPE, NULL);
  this->report_processor   = g_object_new(REPORTPROCESSOR_TYPE, NULL);

  this->fec_early_repaired_bytes         = mprtp_malloc(sizeof(RateWindow));
  this->fec_total_repaired_bytes         = mprtp_malloc(sizeof(RateWindow));
  this->made               = _now(this);

  report_processor_set_logfile(this->report_processor, "rcv_reports.log");
  report_producer_set_logfile(this->report_producer, "rcv_produced_reports.log");
  g_rw_lock_init (&this->rwmutex);
  g_rec_mutex_init (&this->thread_mutex);
  this->thread = gst_task_new (refctrler_ticker, this, NULL);
  gst_task_set_lock (this->thread, &this->thread_mutex);
  gst_task_start (this->thread);

}

void
refctrler_ticker (void *data)
{
  GstClockTime next_scheduler_time;
  RcvController *this;
  GstClockID clock_id;

  this = RCVCTRLER (data);
  THIS_WRITELOCK (this);

//  PROFILING(_orp_main(this));
  _orp_main(this);

  _system_notifier_main(this);

  next_scheduler_time = _now(this) + 10 * GST_MSECOND;
  THIS_WRITEUNLOCK (this);

  clock_id = gst_clock_new_single_shot_id (this->sysclock, next_scheduler_time);

  if (gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED) {
    GST_WARNING_OBJECT (this, "The playout clock wait is interrupted");
  }
  gst_clock_id_unref (clock_id);
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



//------------------------- Incoming Report Processor -------------------


void
rcvctrler_receive_mprtcp (RcvController *this, GstBuffer * buf)
{
  Subflow *subflow;
  GstMPRTCPReportSummary *summary;

  THIS_WRITELOCK (this);

  summary = &this->reports_summary;
  memset(summary, 0, sizeof(GstMPRTCPReportSummary));

  report_processor_process_mprtcp(this->report_processor, buf, summary);

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
//    mprtpr_path_add_delay(subflow->path, get_epoch_time_from_ntp_in_ns(NTP_NOW - summary->SR.ntptime));
    report_producer_set_ssrc(this->report_producer, summary->ssrc);
    subflow->last_SR_report_sent = summary->SR.ntptime;
    subflow->last_SR_report_rcvd = NTP_NOW;
  }

done:
  THIS_WRITEUNLOCK (this);
}


//------------------------ Outgoing Report Producer -------------------------


void
_orp_main(RcvController * this)
{
  ReportIntervalCalculator* ricalcer;
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  guint report_length = 0;
  GstBuffer *buffer;
  gchar interval_logfile[255];
  GstClockTime elapsed_x, elapsed_y, now;

  if(!this->report_is_flowable) {
    goto done;
  }
  now = _now(this);

  ++this->orp_tick;
  elapsed_x  = GST_TIME_AS_MSECONDS(_now(this) - this->made);
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    gboolean report_created = FALSE;

    subflow  = (Subflow *) val;
    ricalcer = subflow->ricalcer;
    if(!subflow->regular_report_enabled){
      continue;
    }

    _update_receiver_rate(subflow);

    if(subflow->next_regular_report <= now){
      gdouble interval;
      report_producer_begin(this->report_producer, subflow->id);
      _orp_add_rr(this, subflow);
      interval = ricalcer_get_next_regular_interval(subflow->ricalcer);
      subflow->next_regular_report = now;
      subflow->next_regular_report += interval * GST_SECOND;
      //logging the report timeout
      memset(interval_logfile, 0, 255);
      sprintf(interval_logfile, "sub_%d_rtcp_ints.csv", subflow->id);

      elapsed_y  = GST_TIME_AS_MSECONDS(_now(this) - subflow->LRR);
      subflow->LRR = _now(this);
      mprtp_logger(interval_logfile, "%lu,%f\n", elapsed_x/100, (gdouble)elapsed_y / 1000.);
      report_created = TRUE;
    }

    if(subflow->fb_report_enabled && subflow->next_feedback <= now){
      if(!report_created){
        report_producer_begin(this->report_producer, subflow->id);
      }
      subflow->feedback_setup_feedback(subflow->fbproducer, this->report_producer);
      subflow->next_feedback = now + subflow->feedback_interval_calcer(subflow->fbproducer);
      report_created = TRUE;
    }

    if(!report_created){
        continue;
    }

    buffer = report_producer_end(this->report_producer, &report_length);
    this->send_mprtcp_packet_func(this->send_mprtcp_packet_data, buffer);
    report_length += 12 /* RTCP HEADER*/ + (28<<3) /*UDP+IP HEADER*/;
    subflow->avg_rtcp_size += (report_length - subflow->avg_rtcp_size) / 4.;

    ricalcer_refresh_rate_parameters(ricalcer,
                              CONSTRAIN(MIN_MEDIA_RATE>>3  /*because we need bytes */,
                                        250000,
                                        subflow->receiver_rate),
                              subflow->avg_rtcp_size);

  }

  DISABLE_LINE _uint16_diff(0,0);
done:
  return;
}



void _orp_add_rr(RcvController * this, Subflow *subflow)
{
  guint8 fraction_lost;
  guint32 ext_hsn;
  guint32 received;
  guint32 lost;
  guint32 expected;
  guint32 total_received;
  guint32 jitter;
  guint16 cycle_num;
  guint16 HSSN;
  guint32 LSR;
  guint32 DLSR;

  mprtpr_path_get_regular_stats(subflow->path,
                             &HSSN,
                             &cycle_num,
                             &jitter,
                             &total_received);

  expected      = _uint32_diff(subflow->HSSN, HSSN);
  received      = total_received - subflow->total_received;
  lost          = received < expected ? expected - received : 0;

  fraction_lost = (expected == 0 || lost <= 0) ? 0 : (lost << 8) / expected;
  ext_hsn       = (((guint32) cycle_num) << 16) | ((guint32) HSSN);

//  g_print("expected: %u received: %u lost: %u fraction_lost: %d cycle num: %d\n",
//          expected, received, lost, fraction_lost, cycle_num);

  subflow->HSSN           = HSSN;
  subflow->total_lost    += lost;
  subflow->total_received = total_received;

  LSR = (guint32) (subflow->last_SR_report_sent >> 16);

  if (subflow->last_SR_report_sent == 0) {
      DLSR = 0;
  } else {
      guint64 temp;
      temp = NTP_NOW - subflow->last_SR_report_rcvd;
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

  ricalcer_refresh_packets_rate(subflow->ricalcer, received, lost, lost);

}



//----------------------------- System Notifier ------------------------------
void
_system_notifier_main(RcvController * this)
{

}

//----------------------------- Logging -----------------------------


//-------------------------Controller feedbacks and intervals ---------------
GstClockTime _default_interval_calcer(gpointer data)
{
  return 100 * GST_MSECOND;
}

void _default_feedback_appender(gpointer data, ReportProducer *reportprod)
{

}


//------------------------- Utility functions --------------------------------

Subflow *
_make_subflow (guint8 id, MpRTPRPath * path)
{
  Subflow *result                   = _subflow_ctor ();
  result->sysclock                  = gst_system_clock_obtain ();
  result->path                      = g_object_ref (path);;
  result->id                        = id;
  result->joined_time               = gst_clock_get_time (result->sysclock);
  result->ricalcer                  = make_ricalcer(FALSE);
  result->LRR                       = _now(result);
  result->feedback_interval_calcer  = _default_interval_calcer;
  result->feedback_setup_feedback = _default_feedback_appender;
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

void _subflow_iterator(
    RcvController * this,
    void(*process)(Subflow*,gpointer),
    gpointer data)
{
  GHashTableIter iter;
  gpointer       key, val;
  Subflow*       subflow;

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    subflow = (Subflow *) val;
    process(subflow, data);
  }
}

Subflow *
_subflow_ctor (void)
{
  Subflow *result;
  result = mprtp_malloc (sizeof (Subflow));
  return result;
}

void
_subflow_dtor (Subflow * this)
{
  g_return_if_fail (this);
  mprtp_free (this);
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

void _update_receiver_rate(Subflow* subflow)
{
  guint32 total_packets, total_payloads;
  mprtpr_path_get_total_receivements(subflow->path, &total_packets, &total_payloads);
  subflow->receiver_rate = _update_ratewindow(&subflow->received_bytes,
                                              (total_payloads + 48 * 8 * total_packets));
}

guint32 _update_ratewindow(RateWindow *window, guint32 value)
{
  window->items[window->index] = value;
  window->index = (window->index + 1) % RATEWINDOW_LENGTH;
  window->rate_value = value - window->items[window->index];
  return window->rate_value;
}


#undef MAX_RIPORT_INTERVAL
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
