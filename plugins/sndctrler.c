/* GStreamer Scheduling tree
 * Copyright (C) 2015 Balázs Kreith (contact: balazs.kreith@gmail.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
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
#include "sndctrler.h"
#include "streamsplitter.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>
#include <sys/timex.h>
#include "ricalcer.h"
#include "percentiletracker.h"
#include "subratectrler.h"
#include <stdlib.h>

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define SR_WINDOW_SIZE 10
#define MAX_RIPORT_INTERVAL (5 * GST_SECOND)
#define REPORTTIMEOUT (3 * MAX_RIPORT_INTERVAL)
#define PATH_RTT_MAX_TRESHOLD (800 * GST_MSECOND)
#define PATH_RTT_MIN_TRESHOLD (600 * GST_MSECOND)
#define MAX_SUBFLOW_MOMENT_NUM 8
#define MIN_MEDIA_RATE 50000


GST_DEBUG_CATEGORY_STATIC (sndctrler_debug_category);
#define GST_CAT_DEFAULT sndctrler_debug_category

#define _now(this) (gst_clock_get_time (this->sysclock))

G_DEFINE_TYPE (SndController, sndctrler, G_TYPE_OBJECT);


typedef struct _Subflow Subflow;

//IRP: Incoming Report Processor
//ORP: Outoing Report Processor
//____________________________________________
//                     ^
//                     | Event
// .---.    .--------.-----------.
// |   |    | T | SystemNotifier |
// |   |    | i |----------------|
// | I |    | c |      ORP       |->Reports
// | R |-E->| k |----------------|
// | P |    | e |   SplitCtrler  |
// |   |    | r |                |
// '---'    '---'----------------'
//                     | Sending targets
//                     V
//          .--------------------.
//          |   StreamSplitter   |
//          '--------------------'
//____________________________________________
//

typedef enum{
  NO_CONTROLLING            = 0,
  REPORT_WAITING            = 1,
  REPORT_ARRIVED            = 2,
  REPORT_ANALYSED           = 3,
}ProcessState;


struct _Subflow
{
  guint8                     id;
  MPRTPSPath*                path;
  GstClock*                  sysclock;
  ReportIntervalCalculator*  ricalcer;
  SubflowRateController*     rate_controller;
  GstMPRTCPReportSummary*    reports;
  PercentileTracker*         sr_window;
  guint32                    sending_bitrate;
  GstClockTime               joined_time;
  GstClockTime               last_report;
  guint8                     lost_history;
  gdouble                    avg_rtcp_size;
  ProcessState               process_state;
  guint32                    packet_count;
  guint32                    octet_count;
};

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
_logging (
    SndController *this);

static void
sefctrler_finalize (GObject * object);


//---------------------------------- Ticker ----------------------------------

static void
sndctrler_ticker_run (void *data);

//----------------------------------------------------------------------------

static void
_subflow_iterator(
    SndController * this,
    void(*process)(Subflow*,gpointer),
    gpointer data);

static void
_enable_controlling(
    Subflow *subflow,
    gpointer data);

static void
_disable_controlling(
    Subflow *subflow,
    gpointer data);

//------------------------- Incoming Report Processor -------------------
static void
_irp_processor_main(SndController * this);

static void
_check_subflow_losts(
    SndController *this,
    Subflow *subflow);

static void
_check_report_timeout(
    SndController *this,
    Subflow * subflow);

//Actions
typedef void (*Action) (SndController *, Subflow *);
//static void _action_recalc (SndEventBasedController * this, Subflow * subflow);

//----------------------------------------------------------------------------

//------------------------ Outgoing Report Producer -------------------------
static void
_orp_producer_main(SndController * this);

static void
_orp_add_sr (
    SndController * this,
    Subflow * subflow);

//----------------------------------------------------------------------------

//----------------------------- Split Controller -----------------------------
static void
_ratedistor_main(SndController * this);
//----------------------------------------------------------------------------

//------------------------- Utility functions --------------------------------
static Subflow *_subflow_ctor (void);
static void _subflow_dtor (Subflow * this);
static void _ruin_subflow (gpointer subflow);
static Subflow *_make_subflow (guint8 id, MPRTPSPath * path);
static void _reset_subflow (Subflow * this);
static void _sending_rate_pipe(gpointer data, PercentileTrackerPipeData* stats);

//----------------------------------------------------------------------------




//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------


void
sndctrler_class_init (SndControllerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = sefctrler_finalize;

  GST_DEBUG_CATEGORY_INIT (sndctrler_debug_category, "sndctrler", 0,
      "MpRTP Sending Controller");

}

void
sefctrler_finalize (GObject * object)
{
  SndController *this = SNDCTRLER (object);
  g_hash_table_destroy (this->subflows);
  gst_task_stop (this->thread);
  gst_task_join (this->thread);

  g_object_unref (this->sysclock);

}

void
sndctrler_init (SndController * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->subflows = g_hash_table_new_full (NULL, NULL,
      NULL, (GDestroyNotify) _ruin_subflow);
  this->subflow_num = 0;
  this->report_is_flowable = FALSE;
  this->rate_distor        = make_sndrate_distor();
  this->report_producer    = g_object_new(REPORTPRODUCER_TYPE, NULL);
  this->report_processor   = g_object_new(REPORTPROCESSOR_TYPE, NULL);
  this->thread             = gst_task_new (sndctrler_ticker_run, this, NULL);
  this->made               = _now(this);
  this->enabled            = FALSE;

  report_processor_set_logfile(this->report_processor, "logs/snd_reports.log");
  g_rw_lock_init (&this->rwmutex);
  g_rec_mutex_init (&this->thread_mutex);
  gst_task_set_lock (this->thread, &this->thread_mutex);
  gst_task_start (this->thread);

//  fprintf(this->stat_file, "f");

}

void _logging (SndController *this)
{
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  gint32 sender_bitrate = 0;
  gint32 media_target = 0;
  gint32 media_rate = 0;
  gint32 monitored_bitrate = 0;
  gint32 target_bitrate = 0;
  gint32 queue_bytes = 0;
  gdouble weight = 0.;
  gchar filename[255], main_file[255];

  sprintf(main_file, "logs/sub_snd_sum.csv");

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) &key, (gpointer) & val)) {
    subflow = (Subflow *) val;
    if(!subflow) goto next;
    sprintf(filename, "logs/sub_%d_snd.csv", subflow->id);
    if(this->enabled){
      sender_bitrate    = mprtps_path_get_sent_bytes_in1s(subflow->path) * 8;
      monitored_bitrate = subratectrler_get_monitoring_bitrate(subflow->rate_controller);
      target_bitrate    = stream_splitter_get_sending_target(this->splitter, subflow->id);
      weight            = stream_splitter_get_sending_weight(this->splitter, subflow->id);
    }
    mprtp_logger(filename,
                 "%d,%d,%d,%f\n",
                sender_bitrate / 1000,
                monitored_bitrate / 1000,
                target_bitrate / 1000,
                weight
                );

    mprtp_logger("logs/path_rates.csv", "%f,", weight);
    media_rate += sender_bitrate;
    media_target += target_bitrate;
  next:
    continue;
  }
  mprtp_logger("logs/path_rates.csv", "\n");
  {
    gint32 encoder_bitrate = 0;
    encoder_bitrate = packetssndqueue_get_encoder_bitrate(this->pacer);
    queue_bytes     = packetssndqueue_get_bytes_in_queue(this->pacer);
    mprtp_logger(main_file,"%d,%d,%d,%d\n",
                 media_rate / 1000,
                 media_target / 1000,
                 encoder_bitrate / 1000,
                 queue_bytes / 125
                 );
  }

}

void sndctrler_set_initial_disabling(SndController *this, GstClockTime time)
{
  sndrate_set_initial_disabling_time(this->rate_distor, time);
}

void
sndctrler_add_path (SndController *this, guint8 subflow_id, MPRTPSPath * path)
{
  Subflow *lookup_result,*new_subflow;
  THIS_WRITELOCK (this);
  lookup_result =
      (Subflow *) g_hash_table_lookup (this->subflows,
      GINT_TO_POINTER (subflow_id));
  if (lookup_result != NULL) {
    GST_WARNING_OBJECT (this, "The requested add operation can not be done "
        "due to duplicated subflow id (%d)", subflow_id);
    goto exit;
  }
  new_subflow = _make_subflow (subflow_id, path);
  g_hash_table_insert (this->subflows, GINT_TO_POINTER (subflow_id),
                       new_subflow);
  ++this->subflow_num;
  stream_splitter_add_path (this->splitter, subflow_id, path, SUBFLOW_DEFAULT_SENDING_RATE);
  sndrate_distor_add_controlled_subflow(this->rate_distor, new_subflow->id);
  if(this->enabled){
    _enable_controlling(new_subflow, this);
  }
  DISABLE_LINE _subflow_iterator(NULL, NULL, NULL);
exit:
  THIS_WRITEUNLOCK (this);
}

void
sndctrler_rem_path (SndController *this, guint8 subflow_id)
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

  if(this->enabled){
    _disable_controlling(lookup_result, this);
  }

  stream_splitter_rem_path (this->splitter, subflow_id);
  sndrate_distor_rem_controlled_subflow(this->rate_distor, subflow_id);
  g_hash_table_remove (this->subflows, GINT_TO_POINTER (subflow_id));
  if (--this->subflow_num < 0) {
    this->subflow_num = 0;
  }
exit:
  THIS_WRITEUNLOCK (this);
}



void
sndctrler_setup (SndController * this, StreamSplitter * splitter, PacketsSndQueue *pacer)
{
  THIS_WRITELOCK (this);
  this->splitter = splitter;
  this->pacer = pacer;
  sndrate_distor_setup(this->rate_distor, this->splitter, this->pacer);
  THIS_WRITEUNLOCK (this);
}


void
sndctrler_setup_callbacks(SndController *this,
                          gpointer mprtcp_send_data,
                          GstBufferReceiverFunc mprtcp_send_func,
                          gpointer utilization_signal_data,
                          GstSchedulerSignaling utilization_signal_func)
{
  THIS_WRITELOCK (this);
  this->send_mprtcp_packet_func = mprtcp_send_func;
  this->send_mprtcp_packet_data = mprtcp_send_data;
  this->utilization_signal_func = utilization_signal_func;
  this->utilization_signal_data = utilization_signal_data;
  THIS_WRITEUNLOCK (this);
}

void
sndctrler_setup_siganling(gpointer ptr,
                                void(*scheduler_signaling)(gpointer, gpointer),
                                gpointer scheduler)
{
  SndController * this = ptr;
  THIS_WRITELOCK (this);
  this->utilization_signal_func = scheduler_signaling;
  this->utilization_signal_data = scheduler;
  THIS_WRITEUNLOCK (this);
}


void
sndctrler_report_can_flow (SndController *this)
{
  GST_DEBUG_OBJECT (this, "RTCP riport can now flowable");
  THIS_WRITELOCK (this);
  this->report_is_flowable = TRUE;
  THIS_WRITEUNLOCK (this);
}

void sndctrler_enable_auto_rate_and_cc(SndController *this)
{
  GST_DEBUG_OBJECT (this, "Enable auto rate and flow controlling");
  THIS_WRITELOCK (this);
  this->enabled = TRUE;
  _subflow_iterator(this, _enable_controlling, this);
  THIS_WRITEUNLOCK (this);
}

void sndctrler_disable_auto_rate_and_cc(SndController *this)
{
  GST_DEBUG_OBJECT (this, "Disable auto rate and flow controlling");
  THIS_WRITELOCK (this);
  this->enabled = FALSE;
  _subflow_iterator(this, _disable_controlling, this);
  THIS_WRITEUNLOCK (this);
}

//---------------------------------- Ticker ----------------------------------
void
sndctrler_ticker_run (void *data)
{
  GstClockTime next_scheduler_time;
  SndController *this;
  GstClockID clock_id;

  this = SNDCTRLER (data);
  THIS_WRITELOCK (this);

  if(!this->enabled) goto done;

  _irp_processor_main(this);
  _ratedistor_main(this);
  _orp_producer_main(this);
  _logging(this);
//  _system_notifier_main(this);

done:
  next_scheduler_time = _now(this) + 100 * GST_MSECOND;
  ++this->ticknum;
  THIS_WRITEUNLOCK (this);
  clock_id = gst_clock_new_single_shot_id (this->sysclock, next_scheduler_time);

  if (gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED) {
    GST_WARNING_OBJECT (this, "The playout clock wait is interrupted");
  }
  gst_clock_id_unref (clock_id);

  //clockshot;
}
//----------------------------------------------------------------------------

void _subflow_iterator(
    SndController * this,
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

void _enable_controlling(Subflow *subflow, gpointer data)
{
  SndController *this = data;
  g_print("EZT EN HIVOGATOM AKARMIKOR IS?\n");
  subflow->rate_controller = make_subratectrler(this->rate_distor, subflow->path);
}

void _disable_controlling(Subflow *subflow, gpointer data)
{
//  SndController *this = data;
}

//------------------------- Incoming Report Processor -------------------

void
_irp_processor_main(SndController * this)
{
  GHashTableIter     iter;
  gpointer           key, val;
  Subflow*           subflow;
  guint32            sending_rate;
  SubflowMeasurement measurement;

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    subflow = (Subflow *) val;

    if(subflow->process_state == NO_CONTROLLING){
      continue;
    }
    sending_rate = mprtps_path_get_sent_bytes_in1s(subflow->path);
    percentiletracker_add(subflow->sr_window, sending_rate);

    if(subflow->process_state != REPORT_ARRIVED){
      continue;
    }
    memset(&measurement, 0, sizeof(SubflowMeasurement));
    measurement.reports = subflow->reports;
    measurement.sending_bitrate = subflow->sending_bitrate;

    subratectrler_measurement_update(subflow->rate_controller, &measurement);
    subflow->process_state = REPORT_WAITING;
  }
}

void _check_subflow_losts(SndController *this, Subflow *subflow)
{
  guint8                  path_is_non_lossy;
  guint                   losts = 0;
  gboolean                expected_lost;
  GstMPRTCPReportSummary *summary;

  summary = subflow->reports;
  if(packetssndqueue_expected_lost(this->pacer)){
    this->expected_lost_detected = _now(this);
  }

  expected_lost          = 0 < this->expected_lost_detected && _now(this) - this->expected_lost_detected < GST_SECOND * 10;
  subflow->lost_history +=  (0. < summary->RR.lost_rate && !expected_lost)?1:0;
  path_is_non_lossy      = mprtps_path_is_non_lossy(subflow->path);

  losts += subflow->lost_history & 1  ? 1 : 0;
  losts += subflow->lost_history & 2  ? 1 : 0;
  losts += subflow->lost_history & 4  ? 1 : 0;
  losts += subflow->lost_history & 8  ? 1 : 0;
  losts += subflow->lost_history & 16 ? 1 : 0;

  subflow->lost_history<<=1;

  if(path_is_non_lossy){
    if(3 < losts){
      mprtps_path_set_lossy(subflow->path);
    }
  }else if(!losts){
    mprtps_path_set_non_lossy(subflow->path);
  }
}

void
_check_report_timeout (SndController *this, Subflow * subflow)
{
  guint8                  path_is_active;

  path_is_active = mprtps_path_is_active(subflow->path);

  if(path_is_active) {
    GstClockTime touched = MAX(subflow->last_report, subflow->joined_time);
    if(touched < _now(this) - REPORTTIMEOUT){
      mprtps_path_set_passive(subflow->path);
    }
  }else if(_now(this) - REPORTTIMEOUT < subflow->last_report){
      mprtps_path_set_active(subflow->path);
  }
}

void
sndctrler_receive_mprtcp (SndController *this, GstBuffer * buf)
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
    mprtp_free(summary);
    goto done;
  }

  if(subflow->reports){
    mprtp_free(subflow->reports);
  }
  subflow->reports = summary;
  subflow->last_report = _now(this);
  subflow->process_state = REPORT_ARRIVED;
  _check_subflow_losts(this, subflow);
  DISABLE_LINE _check_report_timeout(this, subflow);
done:
  THIS_WRITEUNLOCK (this);
}


//---------------------------------------------------------------------------


//------------------------ Outgoing Report Producer -------------------------

void
_orp_producer_main(SndController * this)
{
  ReportIntervalCalculator* ricalcer;
  GHashTableIter iter;
  gpointer key, val;
  guint sent_report_length = 0;
  Subflow *subflow;
  GstBuffer *buf;

  if(!this->report_is_flowable){
    goto done;
  }

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    subflow = (Subflow *) val;
    ricalcer = subflow->ricalcer;
    if (!ricalcer_do_report_now(ricalcer)) {
      continue;
    }

    report_producer_begin(this->report_producer, subflow->id);
    _orp_add_sr(this, subflow);
    buf = report_producer_end(this->report_producer, &sent_report_length);
    this->send_mprtcp_packet_func (this->send_mprtcp_packet_data, buf);
    sent_report_length += 12 /* RTCP HEADER*/ + (28<<3) /*UDP+IP HEADER*/;

    subflow->avg_rtcp_size +=
        ((gfloat) sent_report_length - subflow->avg_rtcp_size) / 4.;

    ricalcer_refresh_parameters(ricalcer,
                                MIN_MEDIA_RATE,
                                subflow->avg_rtcp_size);
  }
done:
  return;
}

void
_orp_add_sr (SndController * this, Subflow * subflow)
{
  guint64 ntptime;
  guint32 rtptime;
  guint32 packet_count;
  guint32 octet_count;

  ntptime = NTP_NOW;

  rtptime = 0;

  packet_count  = mprtps_path_get_total_sent_packets_num(subflow->path);
  octet_count = mprtps_path_get_total_sent_payload_bytes(subflow->path)>>3;

  report_producer_add_sr(this->report_producer,
                         ntptime,
                         rtptime,
                         packet_count - subflow->packet_count,
                         octet_count - subflow->octet_count);

  subflow->packet_count = packet_count;
  subflow->octet_count = octet_count;

}

//---------------------------------------------------------------------------


//----------------------------- Split Controller -----------------------------


static void _log_utilization(MPRTPPluginUtilization *u, GstClockTime seconds)
{
  gint i;
  SubflowUtilization *su;
  mprtp_logger("logs/sndctler.log",
    "############################# MPRTPPluginUtilization #############################\n"
    "# seconds since sndctrler made:      %-10lu                                  #\n"
    "# target_rate:  %-10d| encoder rate: %-10d                             #\n"
    "# max_rate:     %-10d| min_rate:     %-10d                             #\n"
    "# max_mtakover: %-10d| max_stakover: %-10d                             #\n",

    seconds,
    u->report.target_rate, 0,
    u->control.max_rate, u->control.min_rate,
    u->control.max_mtakover,
    u->control.max_stakover
    );

  for(i = 0; i < MPRTP_PLUGIN_MAX_SUBFLOW_NUM; ++i){
    su = &u->subflows[i];
    if(!su->controlled) continue;
    mprtp_logger("logs/sndctler.log",
      "#+++++++++++++++++++++++  Subflow: %d Controll part ++++++++++++++++++++++++++++++#\n"
      "# min_rate:     %-10d| max_rate:     %-10d                             #\n"
      "#+++++++++++++++++++++++  Subflow: %d Report part ++++++++++++++++++++++++++++++++#\n"
      "# disc_rate:    %-10d| lost_bytes:   %-10d| owd:          %-10lu   #\n"
      "# snd_rate:     %-10d| state:        %-10d| target_rate:  %-10d   #\n",

      i,

      su->control.min_rate, su->control.max_rate,

      i,

      su->report.discarded_bytes, su->report.lost_bytes, su->report.owd,
      su->report.sending_rate, su->report.state, su->report.target_rate

      );
  }


}

static void _subratectrler_notify(Subflow *this, gpointer data)
{
  MPRTPPluginUtilization* ur = data;
  SubflowUtilization *sur = &ur->subflows[this->id];
  struct _SubflowUtilizationControl *ctrl = &sur->control;
  subratectrler_setup_controls(this->rate_controller, ctrl);
}

void
_ratedistor_main(SndController * this)
{
  MPRTPPluginUtilization* ur;
  ur = sndrate_distor_time_update(this->rate_distor);
  if(!ur) goto done;

  this->utilization_signal_func(this->utilization_signal_data, ur);
  _subflow_iterator(this, _subratectrler_notify, ur);
  _log_utilization(ur, GST_TIME_AS_SECONDS(_now(this) - this->made));
done:
  return;
}

//---------------------- Utility functions ----------------------------------
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

void
_ruin_subflow (gpointer subflow)
{
  Subflow *this;
  g_return_if_fail (subflow);
  this = (Subflow *) subflow;
  g_object_unref (this->sysclock);
  g_object_unref (this->path);
  _subflow_dtor (this);
}

Subflow *
_make_subflow (guint8 id, MPRTPSPath * path)
{
  Subflow *result;

  result                  = _subflow_ctor ();
  result->sysclock        = gst_system_clock_obtain ();
  result->path            = g_object_ref (path);
  result->id              = id;
  result->joined_time     = gst_clock_get_time (result->sysclock);
  result->ricalcer        = make_ricalcer(TRUE);
  result->sr_window       = make_percentiletracker(20, 50);

  percentiletracker_set_stats_pipe(result->sr_window, _sending_rate_pipe, result);
  percentiletracker_set_treshold(result->sr_window, 2 * GST_SECOND);
  _reset_subflow (result);
  return result;
}

void
_reset_subflow (Subflow * this)
{

}


void _sending_rate_pipe(gpointer data, PercentileTrackerPipeData* stats)
{
  Subflow *this = data;
  this->sending_bitrate = stats->percentile << 3; //<<3 equal to *8
}

#undef REPORTTIMEOUT
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
