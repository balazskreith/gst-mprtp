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

#define MIN_MEDIA_RATE 50000

//It determines how many packets can be followed in the path
//over the last 2 seconds.
#define DEFAULT_PATH_STAT_PACKETS_LENGTH 5000

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

#define RATEWINDOW_LENGTH 10
typedef struct _RateWindow{
  guint32 items[RATEWINDOW_LENGTH];
  gint    index;
  guint32 rate_value;
}RateWindow;

struct _Subflow
{
  guint8                     id;
  MPRTPSPath*                path;
  ReportIntervalCalculator*  ricalcer;
  SubflowRateController*     rate_controller;

  GstClockTime               joined_time;
  GstClockTime               last_SR_report_sent;
  GstClockTime               report_timeout;
  GstClockTime               next_regular_report;
  guint8                     lost_history;
  gboolean                   lost;
  gdouble                    avg_rtcp_size;
  guint32                    packet_count;
  guint32                    octet_count;

  guint                      controlling_mode;
  RateWindow                 fec_byterate;
  RateWindow                 fec_packets;

  gboolean                   emit_signal_request;

};

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
_fec_rate_refresher(
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

//------------------------- Incoming Report Processor -------------------

static void
_check_report_timeout(
    Subflow * subflow,
    gpointer data);

static void
_time_update (
    Subflow * subflow,
    gpointer data);

static void
_signal_update (
    Subflow *subflow,
    gpointer data);

//Actions
typedef void (*Action) (SndController *, Subflow *);
//static void _action_recalc (SndEventBasedController * this, Subflow * subflow);

//----------------------------------------------------------------------------

//------------------------ Outgoing Report Producer -------------------------
static void
_orp_producer_main(
    SndController * this);

static void
_orp_add_sr (
    SndController * this,
    Subflow * subflow);

//----------------------------------------------------------------------------

static void
_emit_signal(
    SndController* this);
//----------------------------------------------------------------------------
static void
_process_regular_reports(
    SndController* this,
    Subflow *subflow,
    GstMPRTCPReportSummary *summary);

//------------------------- Utility functions --------------------------------
static Subflow *_subflow_ctor (void);
static void _subflow_dtor (Subflow * this);
static void _ruin_subflow (gpointer subflow);
static Subflow *_make_subflow (SndController * this, guint8 id, MPRTPSPath * path);
static void _reset_subflow (Subflow * this);
static guint32 _update_ratewindow(RateWindow *window, guint32 value);
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

  mprtp_free(this->mprtp_signal_data);
}

void
sndctrler_init (SndController * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->subflows = g_hash_table_new_full (NULL, NULL,
      NULL, (GDestroyNotify) _ruin_subflow);
  this->subflow_num = 0;
  this->report_is_flowable = FALSE;
  this->report_producer    = g_object_new(REPORTPRODUCER_TYPE, NULL);
  this->report_processor   = g_object_new(REPORTPROCESSOR_TYPE, NULL);
  this->thread             = gst_task_new (sndctrler_ticker_run, this, NULL);
  this->made               = _now(this);
  this->mprtp_signal_data  = mprtp_malloc(sizeof(MPRTPPluginSignalData));

  report_processor_set_logfile(this->report_processor, "snd_reports.log");
  report_producer_set_logfile(this->report_producer, "snd_produced_reports.log");
  g_rw_lock_init (&this->rwmutex);
  g_rec_mutex_init (&this->thread_mutex);
  gst_task_set_lock (this->thread, &this->thread_mutex);
  gst_task_start (this->thread);


}


void sndctrler_change_interval_type(SndController * this, guint8 subflow_id, guint type)
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
  if(this->controlling_mode == controlling_mode){
    return;
  }
  this->controlling_mode = controlling_mode;

  switch(this->controlling_mode){
    case 0:
      subratectrler_change(this->rate_controller, SUBRATECTRLER_NO_CTRL);
      break;
    case 1:
      subratectrler_change(this->rate_controller, SUBRATECTRLER_NO_CTRL);
      break;
    case 2:
      subratectrler_change(this->rate_controller, SUBRATECTRLER_FBRA_MARC);
      break;
    default:
      g_warning("Unknown controlling mode requested for subflow %d", this->id);
      break;
  }
}

void sndctrler_change_controlling_mode(SndController * this,
                                       guint8 subflow_id,
                                       guint controlling_mode,
                                       gboolean *enable_fec)
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
    if(enable_fec && subflow->controlling_mode == 2){
      *enable_fec = TRUE;
    }
  }
  THIS_WRITEUNLOCK (this);
}

void sndctrler_setup_report_timeout(SndController * this, guint8 subflow_id, GstClockTime report_timeout)
{
  Subflow *subflow;
  GHashTableIter iter;
  gpointer key, val;

  THIS_WRITELOCK (this);
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    subflow = (Subflow *) val;
    if(subflow_id == 255 || subflow_id == 0 || subflow_id == subflow->id){
      subflow->report_timeout = report_timeout;
    }
  }
  THIS_WRITEUNLOCK (this);
}



static void _fec_rate_refresh_per_subflow(Subflow *subflow, gpointer data)
{
  SndController *this = data;
  FECEncoder *fecencoder = this->fecencoder;
  guint32 fec_payload_bytes;
  guint32 fec_packets_num;
  guint32 fec_byterate;
  guint32 fec_packetsrate;

  fecencoder_get_stats(fecencoder, subflow->id, &fec_packets_num, &fec_payload_bytes);
  fec_byterate    = _update_ratewindow(&subflow->fec_byterate, fec_payload_bytes);
  fec_packetsrate = _update_ratewindow(&subflow->fec_packets, fec_packets_num);
  mprtps_path_set_monitored_bitrate(subflow->path, fec_byterate * 8, fec_packetsrate);

  this->fec_sum_bitrate     += fec_byterate * 8;
  this->fec_sum_packetsrate += fec_packetsrate;

  mprtp_logger("fecrates.csv",
                 "%u,",
                 (fec_byterate + 28 * 8 /*rtp+UDP fixed header*/ * fec_packetsrate) / 1000
    );
}

void _fec_rate_refresher(SndController *this)
{
  this->fec_sum_packetsrate = 0;
  this->fec_sum_bitrate = 0;
  _subflow_iterator(this, _fec_rate_refresh_per_subflow, this);

  mprtp_logger("fecrates.csv",
                   "%u\n",
                   (this->fec_sum_bitrate + 28 * 8 /*rtp+UDP fixed header*/ *  this->fec_sum_packetsrate) / 1000
      );
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
  new_subflow = _make_subflow (this, subflow_id, path);
  g_hash_table_insert (this->subflows, GINT_TO_POINTER (subflow_id),
                       new_subflow);
  ++this->subflow_num;
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
  g_hash_table_remove (this->subflows, GINT_TO_POINTER (subflow_id));
  if (--this->subflow_num < 0) {
    this->subflow_num = 0;
  }
exit:
  THIS_WRITEUNLOCK (this);
}



void
sndctrler_setup (SndController   *this,
                 StreamSplitter  *splitter,
                 SendingRateDistributor *sndratedistor,
                 FECEncoder      *fecencoder)
{
  THIS_WRITELOCK (this);
  this->fecencoder = fecencoder;
  this->sndratedistor = sndratedistor;
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

//---------------------------------- Ticker ----------------------------------
void
sndctrler_ticker_run (void *data)
{
  GstClockTime next_scheduler_time;
  SndController *this;
  GstClockID clock_id;

  this = SNDCTRLER (data);
  THIS_WRITELOCK (this);
  _orp_producer_main(this);
  _fec_rate_refresher(this);
  _subflow_iterator(this, _check_report_timeout, this);
  _subflow_iterator(this, _time_update, this);
  if(this->sndratedistor){
    this->target_bitrate_t1 = this->target_bitrate;
    this->target_bitrate = sndrate_distor_refresh(this->sndratedistor);
  }
  _emit_signal(this);
//  _system_notifier_main(this);

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

void
_check_report_timeout (Subflow * subflow, gpointer data)
{

  SndController*          this;
  gboolean                path_is_active;

  this = data;

  if(!subflow->report_timeout){
    return;
  }

  path_is_active = mprtps_path_is_active(subflow->path);

  if(path_is_active) {
    GstClockTime touched = MAX(subflow->last_SR_report_sent, subflow->joined_time);
    if(touched < _now(this) - subflow->report_timeout){
      mprtps_path_set_passive(subflow->path);
    }
  }else if(_now(this) - subflow->report_timeout < subflow->last_SR_report_sent){
      mprtps_path_set_active(subflow->path);
  }
}


void
_time_update (Subflow * subflow, gpointer data)
{
  SndController *this = data;
  MPRTPSubflowUtilizationSignalData *subsignal;
  if(subflow->controlling_mode < 2){
    return;
  }
  subsignal = &this->mprtp_signal_data->subflow[subflow->id];
  subratectrler_time_update(subflow->rate_controller);
  subratectrler_signal_request(subflow->rate_controller, &subsignal->ratectrler);
  subsignal->path_state     = mprtps_path_get_state(subflow->path);
  subsignal->target_bitrate = mprtps_path_get_target_bitrate(subflow->path);
}


void
_signal_update (Subflow * subflow, gpointer data)
{
  SndController *this = data;
  MPRTPSubflowUtilizationSignalData *subsignal;
  if(subflow->controlling_mode < 2){
    return;
  }
  subsignal = &this->mprtp_signal_data->subflow[subflow->id];
  subratectrler_signal_update(subflow->rate_controller, &subsignal->ratectrler);
}

void
sndctrler_receive_mprtcp (SndController *this, GstBuffer * buf)
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

  if(!subflow->controlling_mode){
    goto done;
  }

  _process_regular_reports(this, subflow, summary);
  subratectrler_report_update(subflow->rate_controller, summary);

  subflow->last_SR_report_sent = _now(this);

done:
  THIS_WRITEUNLOCK (this);
}


//---------------------------------------------------------------------------


//------------------------ Outgoing Report Producer -------------------------

static void _orp_producer_helper(Subflow *subflow, gpointer data)
{
  SndController*            this;
  ReportIntervalCalculator* ricalcer;
  GstBuffer*                buf;
  guint                     report_length = 0;
  GstClockTime              now;

  this     = data;
  ricalcer = subflow->ricalcer;
  now      = _now(this);

  if(!subflow->controlling_mode){
    goto done;
  }

  if(now < subflow->next_regular_report){
    goto done;
  }

  subflow->next_regular_report = now;
  subflow->next_regular_report += ricalcer_get_next_regular_interval(subflow->ricalcer) * GST_SECOND;

  report_producer_begin(this->report_producer, subflow->id);
  _orp_add_sr(this, subflow);
  buf = report_producer_end(this->report_producer, &report_length);
  this->send_mprtcp_packet_func (this->send_mprtcp_packet_data, buf);
  report_length += 12 /* RTCP HEADER*/ + (28<<3) /*UDP+IP HEADER*/;

  subflow->avg_rtcp_size +=
      ((gfloat) report_length - subflow->avg_rtcp_size) / 4.;

  ricalcer_refresh_rate_parameters(ricalcer,
                              CONSTRAIN(MIN_MEDIA_RATE, 100000, mprtps_path_get_target_bitrate(subflow->path))>>3 /*because we need the bytes */,
                              subflow->avg_rtcp_size);

done:
  return;
}

void
_orp_producer_main(SndController * this)
{

  if(!this->report_is_flowable){
    goto done;
  }

  _subflow_iterator(this, _orp_producer_helper, this);

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

static void _emit_signal_helper(Subflow *subflow, gpointer data)
{
  gboolean *emit_signal = data;
  *emit_signal |= subflow->emit_signal_request;
  subflow->emit_signal_request = FALSE;
}

void _emit_signal(SndController* this)
{
  gboolean emit_signal = FALSE;
  _subflow_iterator(this, _emit_signal_helper, &emit_signal);

  emit_signal |= this->target_bitrate_t1 * 1.05 < this->target_bitrate;
  emit_signal |= this->target_bitrate < this->target_bitrate_t1 * .95;

  if(!emit_signal){
    goto done;
  }

  this->mprtp_signal_data->target_media_rate = this->target_bitrate;
  this->utilization_signal_func(this->utilization_signal_data, this->mprtp_signal_data);
  _subflow_iterator(this, _signal_update, this);
done:
  return;
}
//---------------------------------------------------------------------------

void _process_regular_reports(SndController* this, Subflow *subflow, GstMPRTCPReportSummary *summary)
{
  MPRTPPluginSignalData* signaldata;
  MPRTPSubflowUtilizationSignalData *subflowdata;

  signaldata  = this->mprtp_signal_data;
  subflowdata = &signaldata->subflow[subflow->id];
  if(summary->RR.processed){
    MPRTPSubflowReceiverReport *report;
    report = &subflowdata->receiver_report;
    report->HSSN = summary->RR.HSSN;
    report->RTT = summary->RR.RTT;
    report->cum_packet_lost = summary->RR.cum_packet_lost;
    report->cycle_num = summary->RR.cycle_num;
    report->lost_rate = summary->RR.lost_rate;
    report->jitter = summary->RR.jitter;
    subflow->emit_signal_request = TRUE;
  }

  if(summary->XR.OWD.processed){
    MPRTPSubflowExtendedReport *report;
    report = &subflowdata->extended_report;
    report->owd_max = summary->XR.OWD.max_delay;
    report->owd_min = summary->XR.OWD.min_delay;
    report->owd_median = summary->XR.OWD.median_delay;
    subflow->emit_signal_request = TRUE;
  }


  if(summary->XR.DiscardedBytes.processed){
    MPRTPSubflowExtendedReport *report;
    report = &subflowdata->extended_report;
    report->total_discarded_bytes = summary->XR.DiscardedBytes.discarded_bytes;
    subflow->emit_signal_request = TRUE;
  }

  //Todo: FBRA marc signal emit request here

  //check weather the path is lossy
  if(0. < summary->RR.lost_rate){
    mprtps_path_set_lossy(subflow->path);
  }else if(!mprtps_path_is_non_lossy(subflow->path)){
    mprtps_path_set_non_lossy(subflow->path);
  }

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
  g_object_unref (this->path);
  g_object_unref(this->rate_controller);
  _subflow_dtor (this);
}

Subflow *
_make_subflow (SndController * this, guint8 id, MPRTPSPath * path)
{
  Subflow *result;

  result                  = _subflow_ctor ();
  result->path            = g_object_ref (path);
  result->id              = id;
  result->joined_time     = _now(this);
  result->ricalcer        = make_ricalcer(TRUE);
  result->rate_controller = make_subratectrler(path);
  _reset_subflow (result);
  return result;
}

void
_reset_subflow (Subflow * this)
{

}

guint32 _update_ratewindow(RateWindow *window, guint32 value)
{
  window->items[window->index] = value;
  window->index = (window->index + 1) % RATEWINDOW_LENGTH;
  window->rate_value = value - window->items[window->index];
  return window->rate_value;
}


#undef REPORTTIMEOUT
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
