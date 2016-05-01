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
#include "packetsrcvtracker.h"
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

#define RATEWINDOW_LENGTH 10
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
  NumsTracker*                  reported_seqs;

  gdouble                       avg_rtcp_size;
  guint32                       total_lost;
  guint32                       total_received;
  guint32                       total_discarded_bytes;
  guint16                       HSSN;
  guint64                       last_SR_report_sent;
  guint64                       last_SR_report_rcvd;
  GstClockTime                  LRR;

  gchar                        *logfile;
  gchar                        *statfile;
  gboolean                      log_initialized;
  PacketsRcvTracker*            packetstracker;

  gboolean                      rfc3550_enabled;
  gboolean                      rfc7243_enabled;
  gboolean                      owd_enabled;
  guint                         fbra_marc_enabled;

  guint                         controlling_mode;

  RateWindow                    discarded_payload_bytes;
  RateWindow                    discarded_packets;
  RateWindow                    lost_packets;
  RateWindow                    received_packets;
  RateWindow                    received_bytes;
  RateWindow                    HSSNs;

  GstClockTime                  next_feedback;
  GstClockTime                (*feedback_interval_calcer)(Subflow*);
  void                        (*feedback_message_appender)(RcvController*,Subflow*);

  union{
    GstRTCPAFB_RMDIRecord   rmdi_records[RTCP_AFB_RMDI_RECORDS_NUM];
  }feedbacks;
};

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
rcvctrler_finalize (GObject * object);

static void
refctrler_ticker (void *data);

static void
_logging (
    gpointer data);

//------------------------ Outgoing Report Producer -------------------------

static void
_orp_main(
    RcvController * this);

static void
_orp_add_rr(
    RcvController *this,
    Subflow *subflow);

static void
_orp_add_xr_rfc7243(
    RcvController *this,
    Subflow *subflow);

static void
_orp_add_xr_rfc7097(
    RcvController * this,
    Subflow *subflow);

static void
_orp_add_xr_owd(
    RcvController *this,
    Subflow *subflow);

static void
_orp_add_xr_remb(
    RcvController * this,
    Subflow *subflow);

void
_orp_fbra_marc_feedback(
    RcvController * this,
    Subflow *subflow);


//----------------------------- System Notifier ------------------------------
static void
_system_notifier_main(RcvController * this);

//----- feedback specific functions
static GstClockTime
_default_interval_calcer(Subflow *subflow);

static void
_default_feedback_appender(RcvController *this,Subflow* subflow);

static GstClockTime
_fbra2_interval_calcer(Subflow *subflow);

static void
_fbra2_feedback_appender(RcvController *this, Subflow* subflow);

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

static guint32
_update_ratewindow(
    RateWindow *window,
    guint32 value);

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


static void _change_controlling_mode(Subflow *this, guint controlling_mode)
{
  if(this->controlling_mode == 0 && controlling_mode != 0){
    this->packetstracker = mprtpr_path_ref_packetstracker(this->path);
  }else if(this->controlling_mode != 0 && controlling_mode == 0){
    this->packetstracker = mprtpr_path_unref_packetstracker(this->path);
  }

  this->rfc3550_enabled   = FALSE;
  this->rfc7243_enabled   = FALSE;
  this->owd_enabled       = FALSE;
  this->fbra_marc_enabled = FALSE;

  this->controlling_mode = controlling_mode;

  switch(controlling_mode){
    case 0:
      this->feedback_interval_calcer  = _default_interval_calcer;
      this->feedback_message_appender = _default_feedback_appender;
      GST_DEBUG_OBJECT(this, "subflow %d set to no controlling mode", this->id);
      break;
    case 1:
      this->feedback_interval_calcer = _default_interval_calcer;
      this->feedback_message_appender = _default_feedback_appender;
      this->rfc3550_enabled = TRUE;
      GST_DEBUG_OBJECT(this, "subflow %d set to only report processing mode", this->id);
      break;
    case 2:
      this->feedback_interval_calcer = _fbra2_interval_calcer;
      this->feedback_message_appender = _fbra2_feedback_appender;
      this->rfc3550_enabled   = TRUE;
      this->fbra_marc_enabled = TRUE;
      break;
    default:
      g_warning("Unknown controlling mode requested for subflow %d", this->id);
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
      _change_controlling_mode(subflow, controlling_mode);
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

  mprtp_logger_add_logging_fnc(_logging, this, 1, &this->rwmutex);
}

void
refctrler_ticker (void *data)
{
  GstClockTime next_scheduler_time;
  RcvController *this;
  GstClockID clock_id;

  this = RCVCTRLER (data);
  THIS_WRITELOCK (this);

  PROFILING(_orp_main(this));
//  _orp_main(this);

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
    mprtpr_path_add_delay(subflow->path, get_epoch_time_from_ntp_in_ns(NTP_NOW - summary->SR.ntptime));
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
  GstClockTime elapsed_x, elapsed_y;
  gboolean created = FALSE;

  if(!this->report_is_flowable) {
    goto done;
  }

  ++this->orp_tick;
  elapsed_x  = GST_TIME_AS_MSECONDS(_now(this) - this->made);
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    gboolean send_regular, send_fb;
    subflow  = (Subflow *) val;
    ricalcer = subflow->ricalcer;
    if(!subflow->rfc3550_enabled){
      continue;
    }
    if(mprtpr_path_is_urgent_request(subflow->path)){
      ricalcer_urgent_report_request(subflow->ricalcer);
    }

    send_regular = ricalcer_rtcp_regular_allowed(subflow->ricalcer);
    if(!send_regular && _now(this) < subflow->next_feedback){
      continue;
    }

    send_fb = ricalcer_rtcp_fb_allowed(subflow->ricalcer) && subflow->next_feedback < _now(this);


    if(!send_regular && !send_fb){
      continue;
    }

    if(send_regular){

      created = TRUE;
      report_producer_begin(this->report_producer, subflow->id);

      _orp_add_rr(this, subflow);
      if(subflow->owd_enabled){
        _orp_add_xr_owd(this, subflow);
      }
      if(subflow->rfc7243_enabled){
        _orp_add_xr_rfc7243(this, subflow);
      }

      //logging the report timeout
      memset(interval_logfile, 0, 255);
      sprintf(interval_logfile, "sub_%d_rtcp_ints.csv", subflow->id);

      elapsed_y  = GST_TIME_AS_MSECONDS(_now(this) - subflow->LRR);
      subflow->LRR = _now(this);
      mprtp_logger(interval_logfile, "%lu,%f\n", elapsed_x/100, (gdouble)elapsed_y / 1000.);
    }

    if(send_fb && 1 < subflow->controlling_mode){
      if(!created){
        created = TRUE;
        report_producer_begin(this->report_producer, subflow->id);
      }
      subflow->feedback_message_appender(this, subflow);
      subflow->next_feedback = _now(this) + subflow->feedback_interval_calcer(subflow);
    }

    if(created){
      buffer = report_producer_end(this->report_producer, &report_length);
      this->send_mprtcp_packet_func(this->send_mprtcp_packet_data, buffer);
    }
    report_length += 12 /* RTCP HEADER*/ + (28<<3) /*UDP+IP HEADER*/;
    subflow->avg_rtcp_size += (report_length - subflow->avg_rtcp_size) / 4.;

    ricalcer_refresh_parameters(ricalcer,
                                CONSTRAIN(MIN_MEDIA_RATE>>3  /*because we need bytes */, 25000, subflow->received_bytes.rate_value),
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

}


void _orp_add_xr_rfc7243(RcvController * this, Subflow *subflow)
{
  PacketsRcvTrackerStat trackerstat;
  packetsrcvtracker_get_stat(subflow->packetstracker, &trackerstat);
  if(trackerstat.total_payload_discarded == subflow->total_discarded_bytes){
    goto done;
  }

  report_producer_add_xr_discarded_bytes(this->report_producer,
                                         RTCP_XR_RFC7243_I_FLAG_CUMULATIVE_DURATION,
                                         FALSE,
                                         trackerstat.total_payload_discarded);

done:
  return;
}

void _orp_add_xr_rfc7097(RcvController * this, Subflow *subflow)
{
  GstRTCPXRChunk chunks[100];
  guint16 begin_seq = 0, end_seq = 0;
  guint chunks_len = 0;
  memset(&chunks, 0, sizeof(GstRTCPXRChunk) * 100);
  packetsrcvtracker_set_bitvectors(subflow->packetstracker, &begin_seq, &end_seq, chunks, &chunks_len);
  report_producer_add_xr_discarded_rle(this->report_producer,
                                       FALSE,
                                       0,
                                       begin_seq,
                                       end_seq,
                                       chunks,
                                       chunks_len
                                       );

  numstracker_add(subflow->reported_seqs, end_seq);
  return;
}

void _orp_add_xr_owd(RcvController * this, Subflow *subflow)
{
  GstClockTime median_delay, min_delay, max_delay;
  guint32      u32_median_delay, u32_min_delay, u32_max_delay;

  mprtpr_path_get_owd_stats(subflow->path,
                                 &median_delay,
                                 &min_delay,
                                 &max_delay);
  u32_median_delay = (guint32)(get_ntp_from_epoch_ns(median_delay)>>16);
  u32_min_delay    = (guint32)(get_ntp_from_epoch_ns(min_delay)>>16);
  u32_max_delay    = (guint32)(get_ntp_from_epoch_ns(max_delay)>>16);

  report_producer_add_xr_owd(this->report_producer,
                             RTCP_XR_RFC7243_I_FLAG_CUMULATIVE_DURATION,
                             u32_median_delay,
                             u32_min_delay,
                             u32_max_delay);
}

void _orp_add_xr_remb(RcvController * this, Subflow *subflow)
{
  gfloat estimation;
  guint16 hssn;

  estimation = packetsrcvtracker_get_remb(subflow->packetstracker, &hssn);

  report_producer_add_afb_remb(this->report_producer,
                               this->ssrc,
                               1,
                               estimation,
                               this->ssrc,
                               hssn);
}



void _orp_fbra_marc_feedback(RcvController * this, Subflow *subflow)
{
  GstClockTime median_delay;
  guint32 owd_sampling;
  GstRTCPAFB_RMDIRecord *records;
  gint i;
  records = &subflow->feedbacks.rmdi_records[0];

  //shift
  for(i=RTCP_AFB_RMDI_RECORDS_NUM-1; 0 < i; --i){
    records[i].HSSN              = records[i-1].HSSN;
    records[i].disc_packets_num  = records[i-1].disc_packets_num;
    records[i].owd_sample        = records[i-1].owd_sample;
  }


  mprtpr_path_get_owd_stats(subflow->path, &median_delay, NULL, NULL);
  owd_sampling = get_ntp_from_epoch_ns(median_delay)>>16;

  records[i].HSSN              = mprtpr_path_get_HSSN(subflow->path);
  //Todo: fix this
  records[i].disc_packets_num  = 0; //mprtpr_path_get_total_discarded_or_lost_packets(subflow->path);
  records[i].owd_sample        = owd_sampling;

  report_producer_add_afb_rmdi(this->report_producer, this->ssrc, records);

}



//----------------------------- System Notifier ------------------------------
void
_system_notifier_main(RcvController * this)
{

}

//----------------------------- Logging -----------------------------


static void _logging_helper(Subflow *subflow, gpointer data)
{
  PacketsRcvTrackerStat trackerstat;
  GstClockTime median_delay;
  gdouble fraction_lost;
  guint32 goodput_bytes;
  guint32 goodput_packets;
  guint32 u32_HSSN;

  if(!subflow->logfile){
    subflow->logfile = g_malloc0(255);
    sprintf(subflow->logfile, "sub_%d_rcv.csv", subflow->id);
    subflow->statfile = g_malloc0(255);
    sprintf(subflow->statfile, "sub_%d_stat.csv", subflow->id);
  }
  if(!subflow->log_initialized){
    subflow->packetstracker = mprtpr_path_ref_packetstracker(subflow->path);
  }

  packetsrcvtracker_get_stat(subflow->packetstracker, &trackerstat);

  u32_HSSN = (((guint32) trackerstat.cycle_num)<<16) | ((guint32) trackerstat.highest_seq);

  _update_ratewindow(&subflow->discarded_payload_bytes, trackerstat.total_payload_discarded);
  _update_ratewindow(&subflow->discarded_packets, trackerstat.total_packets_discarded);
  _update_ratewindow(&subflow->received_packets, trackerstat.total_packets_received);
  _update_ratewindow(&subflow->received_bytes, trackerstat.total_payload_received);
  _update_ratewindow(&subflow->lost_packets, trackerstat.total_packets_lost);
  _update_ratewindow(&subflow->HSSNs, u32_HSSN);

  mprtpr_path_get_owd_stats(subflow->path, &median_delay, NULL, NULL);

  fraction_lost = (gdouble)subflow->HSSNs.rate_value / (gdouble)subflow->received_packets.rate_value;
  goodput_bytes = subflow->received_bytes.rate_value - subflow->discarded_payload_bytes.rate_value;
  goodput_packets = subflow->received_packets.rate_value - subflow->received_packets.rate_value;


  mprtp_logger(subflow->logfile, "%u,%u,%lu,%u\n",
               (subflow->received_bytes.rate_value  * 8 + 48 * 8 * subflow->received_packets.rate_value )/1000, //KBit
               (subflow->discarded_payload_bytes.rate_value  * 8 + 48 * 8 * subflow->discarded_packets.rate_value)/1000, //KBit
          GST_TIME_AS_USECONDS(median_delay),
          0);


  mprtp_logger(subflow->statfile,
               "%u,%f,%u,%f\n",
               goodput_bytes * 8 + (goodput_packets * 48 /*bytes overhead */ * 8),
               fraction_lost,
               subflow->lost_packets.rate_value,
               (subflow->discarded_payload_bytes.rate_value * 8 + 48 * 8 * subflow->discarded_packets.rate_value )/1000);

}

void
_logging (gpointer data)
{
  RcvController *this;
  gdouble fecdecoder_early_ratio;
  guint32 fec_early_repaired_bytes;
  guint32 fec_total_repaired_bytes;

  this = data;

  if(!this->joiner) goto done;

  _subflow_iterator(this, _logging_helper, NULL);

  fecdecoder_get_stat(this->fecdecoder,
                            &fec_early_repaired_bytes,
                            &fec_total_repaired_bytes,
                            &this->FFRE);

    _update_ratewindow(this->fec_early_repaired_bytes, fec_early_repaired_bytes);
    _update_ratewindow(this->fec_total_repaired_bytes, fec_total_repaired_bytes);

  fecdecoder_early_ratio     = !((RateWindow*) this->fec_total_repaired_bytes)->rate_value ? 0. : (gdouble) ((RateWindow*) this->fec_early_repaired_bytes)->rate_value / (gdouble) ((RateWindow*) this->fec_total_repaired_bytes)->rate_value;
  mprtp_logger("fecdec_stat.csv",
               "%u,%u,%f,%f\n",
               ((RateWindow*) this->fec_early_repaired_bytes)->rate_value,
               ((RateWindow*) this->fec_total_repaired_bytes)->rate_value,
               fecdecoder_early_ratio,
               this->FFRE
               );

done:
  return;
}

//-------------------------Controller feedbacks and intervals ---------------
GstClockTime _default_interval_calcer(Subflow *subflow)
{
  return 100 * GST_MSECOND;
}

void _default_feedback_appender(RcvController *this,Subflow* subflow)
{
  DISABLE_LINE      _orp_fbra_marc_feedback(this, subflow);
  DISABLE_LINE      _orp_add_xr_rfc7243(this, subflow);
}

GstClockTime _fbra2_interval_calcer(Subflow *subflow)
{
  return (1.0/MIN(50,MAX(10,(subflow->received_bytes.rate_value * 8)/20000))) * GST_SECOND;
}

void _fbra2_feedback_appender(RcvController *this, Subflow* subflow)
{
  _orp_add_xr_owd(this, subflow);
  _orp_add_xr_rfc7097(this, subflow);
  _orp_add_xr_remb(this, subflow);
}

//------------------------- Utility functions --------------------------------

static void _removed_reported_seq(gpointer data, gint64 removed_HSSN)
{
  Subflow *subflow;
  subflow = data;
  if(subflow->packetstracker){
    packetsrcvtracker_update_reported_sn(subflow->packetstracker, removed_HSSN);
  }
}

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
  result->reported_seqs             = make_numstracker(20, GST_SECOND);
  result->feedback_interval_calcer  = _default_interval_calcer;
  result->feedback_message_appender = _default_feedback_appender;
  numstracker_add_rem_pipe(result->reported_seqs, _removed_reported_seq, result);

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
    mprtp_free(this->logfile);
    this->packetstracker = mprtpr_path_unref_packetstracker(this->path);
  }
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
