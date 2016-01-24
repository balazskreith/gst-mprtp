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
#include "sefctrler.h"
#include "streamsplitter.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>
#include <sys/timex.h>
#include "ricalcer.h"
#include "percentiletracker.h"
#include "subratectrler.h"

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define MAX_RIPORT_INTERVAL (5 * GST_SECOND)
#define REPORTTIMEOUT (3 * MAX_RIPORT_INTERVAL)
#define PATH_RTT_MAX_TRESHOLD (800 * GST_MSECOND)
#define PATH_RTT_MIN_TRESHOLD (600 * GST_MSECOND)
#define MAX_SUBFLOW_MOMENT_NUM 5
#define SUBFLOW_DEFAULT_SENDING_RATE 128000

GST_DEBUG_CATEGORY_STATIC (sefctrler_debug_category);
#define GST_CAT_DEFAULT sefctrler_debug_category

#define _irt0(this) (this->ir_moments+this->ir_moments_index)
#define _irt1(this) (this->ir_moments + (this->ir_moments_index == 0 ? MAX_SUBFLOW_MOMENT_NUM-1 : this->ir_moments_index-1))
#define _irt2(this) _irt(this, 2)
#define _irt3(this) _irt(this, 3)
#define _irt4(this) _irt(this, 4)
#define _ort0(this) (this->or_moments+this->or_moments_index)
#define _ort1(this) (this->or_moments+!this->or_moments_index)
#define _irt0_get_discarded_bytes(this) (_irt0(this)->early_discarded_bytes + _irt0(this)->late_discarded_bytes)

G_DEFINE_TYPE (SndEventBasedController, sefctrler, G_TYPE_OBJECT);


typedef struct _Subflow Subflow;
//typedef struct _RRMeasurement RRMeasurement;
typedef struct _ORMoment ORMoment;
typedef struct _SubflowState SubflowState;



//                        ^
//                        | Event
// .---.    .--------.-----------.
// |   |    | T | SystemNotifier |
// |   |    | i |----------------|
// | I |    | c |      ORP       |->Reports
// | R |-E->| k |----------------|
// | P |    | e |   SplitCtrler  |
// |   |    | r |                |
// '---'    '---'----------------'
//                        | Bids
//                        V


typedef enum
{
  EVENT_LATE             = -3,
  EVENT_LOST             = -2,
  EVENT_DISTORTION       = -1,
  EVENT_FI               =  0,
  EVENT_SETTLEMENT       =  1,
} Event;



struct _ORMoment{
  GstClockTime        time;
  guint32             sent_packets_num;
  guint32             sent_payload_bytes;
  gdouble             receiving_rate;

};


struct _SubflowState{
  gdouble            goodput;
};

typedef void (*StateFunc)(SndEventBasedController*,Subflow *,Event);
typedef Event (*EventFunc)(Subflow *);

struct _Subflow
{
  MPRTPSPath*                path;
  GstClockTime               joined_time;
  guint8                     id;
  GstClock*                  sysclock;
  ReportIntervalCalculator*  ricalcer;
  RRMeasurement*                  ir_moments;
  gint                       ir_moments_index;
  guint32                    ir_moments_num;
  ORMoment                   or_moments[2];
  gint                       or_moments_index;
  guint32                    or_moments_num;
  StateFunc                  fire;
  EventFunc                  check;
  gboolean                   ready;
//  guint8                     rate_calcer_id;
  guint8                     lost_history;
  guint8                     late_discarded_history;
  gdouble                    avg_rtcp_size;
  gdouble                    actual_rate;

  SubflowRateController*     rate_controller;
};

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------


static void
sefctrler_finalize (GObject * object);
static void
sefctrler_rem_path (gpointer controller_ptr, guint8 subflow_id);
static void
sefctrler_add_path (gpointer controller_ptr, guint8 subflow_id,MPRTPSPath * path);
static void
sefctrler_riport_can_flow (gpointer this);

//---------------------------------- Ticker ----------------------------------

static void
sefctrler_ticker_run (void *data);

//----------------------------------------------------------------------------


//------------------------- Incoming Report Processor -------------------
static void
_irp_producer_main(SndEventBasedController * this);

static RRMeasurement*
_irt (Subflow * this, gint moment);

static void
_step_ir (Subflow * this);

static void
_assemble_measurement(
    SndEventBasedController * this,
    Subflow *subflow);

static void sefctrler_receive_mprtcp (
    gpointer this,
    GstBuffer * buf);

static void _report_processing_selector (
    SndEventBasedController *this,
    Subflow * subflow,
    GstMPRTCPSubflowBlock * block);

static void
_report_processing_rrblock_processor (
    SndEventBasedController *this,
    Subflow * subflow,
    GstRTCPRRBlock * rrb);

static void
_report_processing_xr_7243_block_processor (
    SndEventBasedController *this,
    Subflow * subflow,
    GstRTCPXR_RFC7243 * xrb);

static void
_report_processing_xr_owd_block_processor (
    SndEventBasedController *this,
    Subflow * subflow,
    GstRTCPXR_OWD * xrb);
static void
_report_processing_xr_rfc7097_block_processor (
    SndEventBasedController *this,
    Subflow * subflow,
    GstRTCPXR_RFC7097 * xrb);

static void
_report_processing_xr_rfc3611_block_processor (
    SndEventBasedController *this,
    Subflow * subflow,
    GstRTCPXR_RFC3611 * xrb);

static void
_report_processing_xr_owd_rle_block_processor (
    SndEventBasedController *this,
    Subflow * subflow,
    GstRTCPXR_OWD_RLE * xrb);

static gfloat
_get_subflow_goodput (
    Subflow * this,
    gdouble* receiver_rate);

static Event
_subflow_check_p_state(
    Subflow *subflow);

static Event
_subflow_check_nc_state(
    Subflow *subflow);

static Event
_subflow_check_l_state(
    Subflow *subflow);

static Event
_subflow_check_c_state(
    Subflow *subflow);

static void
_subflow_fire_p_state(
    SndEventBasedController *this,
    Subflow *subflow,
    Event event);

static void
_subflow_fire_nc_state(
    SndEventBasedController *this,
    Subflow *subflow,
    Event event);

static void
_subflow_fire_l_state(
    SndEventBasedController *this,
    Subflow *subflow,
    Event event);

static void
_subflow_fire_c_state(
    SndEventBasedController *this,
    Subflow *subflow,
    Event event);

static void
_subflow_state_transit_to(
    Subflow *subflow,
    MPRTPSPathState target);

static void
_subflow_fall_action(
    SndEventBasedController *this,
    Subflow *subflow);
//Actions
typedef void (*Action) (SndEventBasedController *, Subflow *);
//static void _action_recalc (SndEventBasedController * this, Subflow * subflow);

//----------------------------------------------------------------------------

//------------------------ Outgoing Report Producer -------------------------
static void
_orp_producer_main(SndEventBasedController * this);

static void
_send_mprtcp_sr_block (SndEventBasedController * this,
                       Subflow * subflow,
                       guint32 *sent_report_length);
static void
_step_or(Subflow *this);

static GstBuffer*
_get_mprtcp_sr_block (SndEventBasedController* this,
                      Subflow* subflow,
                      guint32* buf_length);
static void
_setup_sr_riport (Subflow * this,
                  GstRTCPSR * sr,
                  guint32 ssrc);

static gboolean
_check_report_timeout (Subflow * this);
//----------------------------------------------------------------------------

//----------------------------- System Notifier ------------------------------
//static void
//_system_notifier_main(SndEventBasedController * this);

static void
_system_notifier_utilization(gpointer sndrate_distor, gpointer data);
//----------------------------------------------------------------------------

//----------------------------- Split Controller -----------------------------
static void
_split_controller_main(SndEventBasedController * this);
static guint32
_recalc_bids(SndEventBasedController * this);
static gboolean
_subflows_are_ready(SndEventBasedController * this);
//----------------------------------------------------------------------------

//------------------------- Utility functions --------------------------------
static Subflow *_subflow_ctor (void);
static void _subflow_dtor (Subflow * this);
static void _ruin_subflow (gpointer * subflow);
static Subflow *_make_subflow (guint8 id, MPRTPSPath * path);
static void reset_subflow (Subflow * this);
static guint16 _uint16_diff (guint16 a, guint16 b);
static guint32 _uint32_diff (guint32 a, guint32 b);
//----------------------------------------------------------------------------




//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------


void
sefctrler_class_init (SndEventBasedControllerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = sefctrler_finalize;

  GST_DEBUG_CATEGORY_INIT (sefctrler_debug_category, "sefctrler", 0,
      "MpRTP Sending Event Based Flow Controller");

}

void
sefctrler_finalize (GObject * object)
{
  SndEventBasedController *this = SEFCTRLER (object);
  g_hash_table_destroy (this->subflows);
  gst_task_stop (this->thread);
  gst_task_join (this->thread);

  g_object_unref (this->sysclock);

}

static void sefctrler_stat_run (void *data);

void
sefctrler_init (SndEventBasedController * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->subflows = g_hash_table_new_full (NULL, NULL,
      NULL, (GDestroyNotify) _ruin_subflow);
  this->subflow_num = 0;
  this->bids_recalc_requested = FALSE;
  this->bids_commit_requested = FALSE;
  this->target_rate = 128000;
  this->ssrc = g_random_int ();
  this->report_is_flowable = FALSE;
  this->pacing = FALSE;
  this->RTT_max = 5 * GST_SECOND;
  this->last_recalc_time = gst_clock_get_time(this->sysclock);
//  this->event = SPLITCTRLER_EVENT_FI;
  this->rate_distor = make_sndrate_distor(_system_notifier_utilization, this);
  g_rw_lock_init (&this->rwmutex);
  g_rec_mutex_init (&this->thread_mutex);
  this->thread = gst_task_new (sefctrler_ticker_run, this, NULL);
  gst_task_set_lock (this->thread, &this->thread_mutex);
  gst_task_start (this->thread);

//  fprintf(this->stat_file, "f");
  g_rec_mutex_init (&this->stat_thread_mutex);
  this->stat_thread = gst_task_new (sefctrler_stat_run, this, NULL);
  gst_task_set_lock (this->stat_thread, &this->stat_thread_mutex);
  gst_task_start (this->stat_thread);

}

static gboolean file_init = FALSE;
static gchar file_names[10][255];
static FILE *files[10], *main_file = NULL;
void
sefctrler_stat_run (void *data)
{
  SndEventBasedController *this;
  GstClockID clock_id;
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  GstClockTime next_scheduler_time;
//  gdouble media_target = 0.;
  FILE *file = NULL;
  gint32 sender_bitrate;
  gint32 goodput;
  gint32 monitored_bits;
  gint32 target_bitrate;
  guint32 queued_bits;
  guint64 target_delay;
  guint64 ltt80th_delay;
  guint64 recent_delay;

  this = data;
  THIS_WRITELOCK (this);
  if(!file_init){
    gint i = 0;
    for(i=0; i<10; ++i) {
        files[i] = NULL;
        sprintf(file_names[i], "sub_%d_snd.csv", i);
    }
    file_init = TRUE;
  }

  if( !main_file) main_file=fopen("sub_snd_sum.csv", "w");
  else main_file=fopen("sub_snd_sum.csv", "a");

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) &key, (gpointer) & val)) {
    subflow = (Subflow *) val;
    if(!subflow) goto next;
    if( !files[subflow->id]) files[subflow->id]=fopen(file_names[subflow->id], "w");
    else files[subflow->id]=fopen(file_names[subflow->id], "a");

    file = files[subflow->id];
    ;

    subratectrler_extract_stats(subflow->rate_controller,
                                      &sender_bitrate,
                                      &goodput,
                                      &monitored_bits,
                                      &target_bitrate,
                                      &queued_bits,
                                      &target_delay,
                                      &ltt80th_delay,
                                      &recent_delay);

    fprintf(file, "%d,%d,%d,%d,%d,%lu,%lu,%lu\n",
            sender_bitrate,
            goodput,
            sender_bitrate + monitored_bits,
            target_bitrate,
            queued_bits,
            target_delay,
            ltt80th_delay,
            recent_delay);
//    fprintf(file, "%f,%f,",
//            media_target / 125.,
//            stream_splitter_get_media_rate(this->splitter) / 125.);
//    fprintf(file, "|\n");
    fclose(file);
//
  next:
    continue;
  }

  fprintf(main_file,"%f\n", 0.);

  fclose(main_file);

  THIS_WRITEUNLOCK(this);

  next_scheduler_time = gst_clock_get_time(this->sysclock) + GST_SECOND;
  clock_id = gst_clock_new_single_shot_id (this->sysclock, next_scheduler_time);

  if (gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED) {
    GST_WARNING_OBJECT (this, "The playout clock wait is interrupted");
  }
  gst_clock_id_unref (clock_id);
}


void
sefctrler_add_path (gpointer ptr, guint8 subflow_id, MPRTPSPath * path)
{
  Subflow *lookup_result,*new_subflow;
  SndEventBasedController *this;
  this = SEFCTRLER (ptr);
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
  new_subflow->rate_controller = sndrate_distor_add_controllable_path(this->rate_distor, path, SUBFLOW_DEFAULT_SENDING_RATE);

  {
    gchar filename[255];
    sprintf(filename, "subratectrler_%d.log", new_subflow->id);
    subratectrler_enable_logging(new_subflow->rate_controller, filename);
  }

  this->splitter2 = (StreamSplitter2 *) g_object_new (STREAM_SPLITTER2_TYPE, NULL);
  stream_splitter2_add_path(this->splitter2, 2, path, 64000);
//  stream_splitter2_add_path(this->splitter2, 3, path, 64000);

exit:
  THIS_WRITEUNLOCK (this);
}

void
sefctrler_rem_path (gpointer ptr, guint8 subflow_id)
{
  Subflow *lookup_result;
  SndEventBasedController *this;
  this = SEFCTRLER (ptr);
  THIS_WRITELOCK (this);
  lookup_result =
      (Subflow *) g_hash_table_lookup (this->subflows,
      GINT_TO_POINTER (subflow_id));
  if (lookup_result == NULL) {
    GST_WARNING_OBJECT (this, "The requested remove operation can not be done "
        "due to not existed subflow id (%d)", subflow_id);
    goto exit;
  }
  sndrate_distor_remove_id(this->rate_distor, lookup_result->id);
  stream_splitter_rem_path (this->splitter, subflow_id);
  g_hash_table_remove (this->subflows, GINT_TO_POINTER (subflow_id));
  if (--this->subflow_num < 0) {
    this->subflow_num = 0;
  }
exit:
  THIS_WRITEUNLOCK (this);
}



void
sefctrler_set_callbacks(void(**riport_can_flow_indicator)(gpointer),
                              void(**controller_add_path)(gpointer,guint8,MPRTPSPath*),
                              void(**controller_rem_path)(gpointer,guint8))
{
  if (riport_can_flow_indicator) {
    *riport_can_flow_indicator = sefctrler_riport_can_flow;
  }
  if (controller_add_path) {
    *controller_add_path = sefctrler_add_path;
  }
  if (controller_rem_path) {
    *controller_rem_path = sefctrler_rem_path;
  }
}

void
sefctrler_setup (SndEventBasedController * this, StreamSplitter * splitter)
{
  THIS_WRITELOCK (this);
  this->splitter = splitter;
  this->splitter2 = (StreamSplitter2 *) g_object_new (STREAM_SPLITTER2_TYPE, NULL);
  THIS_WRITEUNLOCK (this);
}

GstBufferReceiverFunc
sefctrler_setup_mprtcp_exchange (SndEventBasedController * this,
    gpointer data, GstBufferReceiverFunc func)
{
  GstBufferReceiverFunc result;
  THIS_WRITELOCK (this);
  this->send_mprtcp_packet_func = func;
  this->send_mprtcp_packet_data = data;
  result = sefctrler_receive_mprtcp;
  THIS_WRITEUNLOCK (this);
  return result;
}

void
sefctrler_setup_siganling(gpointer ptr,
                                void(*scheduler_signaling)(gpointer, gpointer),
                                gpointer scheduler)
{
  SndEventBasedController * this = ptr;
  THIS_WRITELOCK (this);
  this->utilization_signal_request = scheduler_signaling;
  this->utilization_signal_data = scheduler;
  THIS_WRITEUNLOCK (this);
}

void
sefctrler_riport_can_flow (gpointer ptr)
{
  SndEventBasedController *this;
  this = SEFCTRLER (ptr);
  GST_DEBUG_OBJECT (this, "RTCP riport can now flowable");
  THIS_WRITELOCK (this);
  this->report_is_flowable = TRUE;
  THIS_WRITEUNLOCK (this);
}

//---------------------------------- Ticker ----------------------------------
void
sefctrler_ticker_run (void *data)
{
  GstClockTime now, next_scheduler_time;
  SndEventBasedController *this;
  GstClockID clock_id;

  this = SEFCTRLER (data);
  THIS_WRITELOCK (this);
  now = gst_clock_get_time (this->sysclock);
  _irp_producer_main(this);
  _split_controller_main(this);
  _orp_producer_main(this);
//  _system_notifier_main(this);
//done:
  next_scheduler_time = now + 100 * GST_MSECOND;
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


//------------------------- Incoming Report Processor -------------------

void
_irp_producer_main(SndEventBasedController * this)
{
  GHashTableIter iter;
  gpointer       key, val;
  Subflow*       subflow;
  GstClockTime   now;
  Event          event;
  MPRTPSPath*    slowest_path = NULL;
  GstClockTime   delay, slowest_delay = 0;

  now = gst_clock_get_time(this->sysclock);
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    subflow = (Subflow *) val;
    if(_irt0(subflow)->checked) goto checked;
    if(now - 120 * GST_MSECOND < _irt0(subflow)->time) goto not_checked;

    _assemble_measurement(this, subflow);
    _irt0(subflow)->goodput = _get_subflow_goodput(subflow, &_irt0(subflow)->receiver_rate);
    if(0) g_print("%p", _irt(subflow, 0));

    _irt0(subflow)->sending_weight = stream_splitter_get_sending_rate(this->splitter, subflow->id);
    subratectrler_measurement_update(subflow->rate_controller, _irt0(subflow));

    delay = mprtps_path_get_delay(subflow->path);
    if(slowest_delay < delay){
      slowest_path = subflow->path;
    }

    event = subflow->check(subflow);
    subflow->fire(this, subflow, event);
    _irt0(subflow)->checked = TRUE;
    subflow->ready = TRUE;
  checked:
    continue;

  not_checked:
    continue;
  }

  if(slowest_path) {
    mprtps_path_set_slow(slowest_path);
  }
  return;

}

RRMeasurement *
_irt (Subflow * this, gint moment)
{
  gint index;
  RRMeasurement *result;
//  index = this->ir_moments_index - (moment % MAX_SUBFLOW_MOMENT_NUM);
//  if (index < 0)
//    index = MAX_SUBFLOW_MOMENT_NUM - index;
//  return this->ir_moments + index;
//  index = this->ir_moments_index;
again:
  if(moment <= 0){
    result = this->ir_moments + this->ir_moments_index;
    goto done;
  }
  if(index == 0) index = MAX_SUBFLOW_MOMENT_NUM - 1;
  else --index;
  --moment;
  goto again;
done:
  return result;
}

void
_step_ir (Subflow * this)
{
  this->ir_moments_index = (this->ir_moments_index + 1) % MAX_SUBFLOW_MOMENT_NUM;
  memset ((gpointer) _irt0 (this), 0, sizeof (RRMeasurement));
//  _st0(this)->RTT                   = _irt1(this)->RTT;

  _irt0(this)->early_discarded_bytes_sum = _irt1(this)->early_discarded_bytes_sum;
  _irt0(this)->late_discarded_bytes_sum  = _irt1(this)->late_discarded_bytes_sum;
  _irt0(this)->time                      = gst_clock_get_time(this->sysclock);
  _irt0(this)->state                     = _irt1(this)->state;
  _irt0(this)->checked                   = FALSE;
  _irt0(this)->sent_payload_bytes_sum    = mprtps_path_get_total_sent_payload_bytes(this->path);
  this->late_discarded_history<<=1;
  this->lost_history<<=1;
  ++this->ir_moments_num;
}

void _assemble_measurement(SndEventBasedController * this, Subflow *subflow)
{
  guint chunks_num, chunk_index;

  if(_irt0(subflow)->rfc7243_arrived) goto total_discarded_done;
  if(!_irt0(subflow)->rfc7097_arrived) goto total_discarded_done;

  chunks_num = _irt0(subflow)->rle_discards.length;
  for(chunk_index = 0; chunk_index < chunks_num; ++chunk_index)
  {
     _irt0(subflow)->late_discarded_bytes +=
     _irt0(subflow)->rle_discards.values[chunk_index];
  }
  _irt0(subflow)->late_discarded_bytes_sum +=
     _irt0(subflow)->late_discarded_bytes;

total_discarded_done:
  if(!_irt0(subflow)->rfc7097_arrived) goto recent_discarded_done;

  chunk_index = _irt0(subflow)->rle_discards.length - 1;
  _irt0(subflow)->recent_discard = _irt0(subflow)->rle_discards.values[chunk_index];

recent_discarded_done:
  if(!_irt0(subflow)->owd_rle_arrived) goto recent_delay_done;

  chunk_index = _irt0(subflow)->rle_delays.length - 1;
  _irt0(subflow)->recent_delay = _irt0(subflow)->rle_delays.values[chunk_index];

recent_delay_done:
  if(!_irt0(subflow)->rfc3611_arrived) goto recent_losts_done;

  chunk_index = _irt0(subflow)->rle_losts.length - 1;
  _irt0(subflow)->recent_lost = _irt0(subflow)->rle_losts.values[chunk_index];
  chunks_num = _irt0(subflow)->rle_losts.length;
  for(chunk_index = 0; chunk_index < chunks_num; ++chunk_index)
  {
    _irt0(subflow)->rfc3611_cum_lost +=
    _irt0(subflow)->rle_losts.values[chunk_index];
  }

recent_losts_done:
  _irt0(subflow)->expected_lost = mprtps_path_has_expected_lost(subflow->path);

  //Determine if path is lossy
  {
    guint losts = 0;
    losts += _irt0(subflow)->lost && !_irt0(subflow)->expected_lost? 1 : 0;
    losts += _irt1(subflow)->lost && !_irt1(subflow)->expected_lost ? 1 : 0;
    losts += _irt2(subflow)->lost && !_irt2(subflow)->expected_lost ? 1 : 0;
    losts += _irt3(subflow)->lost && !_irt3(subflow)->expected_lost ? 1 : 0;
    losts += _irt4(subflow)->lost && !_irt4(subflow)->expected_lost ? 1 : 0;

    if(losts == 0){
      mprtps_path_set_non_lossy(subflow->path);
    }else if(losts > 2){
      mprtps_path_set_lossy(subflow->path);
    }
  }
  return;
}

void
sefctrler_receive_mprtcp (gpointer ptr, GstBuffer * buf)
{
  GstMPRTCPSubflowBlock *block;
  SndEventBasedController *this = SEFCTRLER (ptr);
  guint16 subflow_id;
  guint8 report_type;
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
  gst_rtcp_header_getdown (&block->block_header,
                           NULL, NULL, NULL, &report_type, NULL, NULL);
  if(report_type == GST_RTCP_TYPE_RR){
    _step_ir (subflow);
  }
  _report_processing_selector (this, subflow, block);

done:
  gst_buffer_unmap (buf, &map);
  THIS_WRITEUNLOCK (this);
}


void
_report_processing_selector (SndEventBasedController *this,
                             Subflow * subflow,
                             GstMPRTCPSubflowBlock * block)
{
  guint8 pt;

  gst_rtcp_header_getdown (&block->block_header, NULL, NULL, NULL, &pt, NULL,
      NULL);
  switch(pt){
    case GST_RTCP_TYPE_RR:
      _report_processing_rrblock_processor (this,
                                            subflow,
                                            &block->receiver_riport.blocks);
    break;
    case GST_RTCP_TYPE_XR:
    {
      guint8 xr_block_type;
      gst_rtcp_xr_block_getdown((GstRTCPXR*) &block->xr_header,
                                &xr_block_type, NULL,  NULL);
      switch(xr_block_type){
        case GST_RTCP_XR_RFC3611_BLOCK_TYPE_IDENTIFIER:
          _report_processing_xr_rfc3611_block_processor (this,
                                                      subflow,
                                                      &block->xr_rfc3611_report);
        break;
        case GST_RTCP_XR_RFC7243_BLOCK_TYPE_IDENTIFIER:
          _report_processing_xr_7243_block_processor (this,
                                                      subflow,
                                                      &block->xr_rfc7243_riport);
        break;
        case GST_RTCP_XR_OWD_BLOCK_TYPE_IDENTIFIER:
          _report_processing_xr_owd_block_processor(this,
                                                    subflow,
                                                    &block->xr_owd);
        break;
        case GST_RTCP_XR_RFC7097_BLOCK_TYPE_IDENTIFIER:
          _report_processing_xr_rfc7097_block_processor(this,
                                                        subflow,
                                                        &block->xr_rfc7097_report);
          break;
        case GST_RTCP_XR_OWD_RLE_BLOCK_TYPE_IDENTIFIER:
          _report_processing_xr_owd_rle_block_processor(this,
                                                        subflow,
                                                        &block->xr_owd_rle_report);
          break;
        default:
          GST_WARNING_OBJECT(this, "Unrecognized RTCP XR REPORT");
        break;
      }
    }
    break;
    default:
      GST_WARNING_OBJECT(this, "Unrecognized MPRTCP Report block");
    break;
  }
}

void
_report_processing_rrblock_processor (SndEventBasedController *this,
                                      Subflow * subflow,
                                      GstRTCPRRBlock * rrb)
{
  guint64 LSR, DLSR;
//  GstClockTime now;
  guint32 LSR_read, DLSR_read, HSSN_read;
  guint8 fraction_lost;

  //--------------------------
  //validating
  //--------------------------
  gst_rtcp_rrb_getdown (rrb, NULL, &fraction_lost, &_irt0(subflow)->cum_packet_lost,
                        &HSSN_read, &_irt0(subflow)->jitter,
                        &LSR_read, &DLSR_read);
  _irt0(subflow)->lost = _uint32_diff(_irt1(subflow)->cum_packet_lost,
                                      _irt0(subflow)->cum_packet_lost);
  _irt0(subflow)->HSSN = (guint16) (HSSN_read & 0x0000FFFF);
  _irt0(subflow)->cycle_num = (guint16) (HSSN_read>>16);
  _irt0(subflow)->expected_packets = _uint16_diff(_irt1(subflow)->HSSN,
                                                  _irt0(subflow)->HSSN);
  _irt0(subflow)->PiT = _uint16_diff(_irt0(subflow)->HSSN,
                                     mprtps_path_get_HSN(subflow->path));
  LSR = (guint64) LSR_read;
  DLSR = (guint64) DLSR_read;

  if (_irt0(subflow)->expected_packets > 32767) {
    GST_WARNING_OBJECT (subflow, "Receiver report validation failed "
        "on subflow %d " "due to HSN difference inconsistency.", subflow->id);
    return;
  }
  _irt0(subflow)->expected_payload_bytes =
      mprtps_path_get_sent_octet_sum_for (subflow->path, _irt0 (subflow)->expected_packets)<<3;

  mprtps_path_get_bytes_in_flight(subflow->path, &_irt0(subflow)->bytes_in_flight_acked, NULL);
  _irt0(subflow)->bytes_in_queue = mprtps_path_get_bytes_in_queue(subflow->path);
  if (subflow->ir_moments_num > 1 && (LSR == 0 || DLSR == 0)) {
    return;
  }
  //--------------------------
  //processing
  //--------------------------
  if (LSR > 0 && DLSR > 0) {
    guint64 diff;
    diff = ((guint32)(NTP_NOW>>16)) - LSR - DLSR;
   _irt0 (subflow)->RTT = get_epoch_time_from_ntp_in_ns(diff<<16);
  }

  _irt0 (subflow)->lost_rate = ((gdouble) fraction_lost) / 256.;
  subflow->lost_history +=  (_irt0 (subflow)->lost_rate>0.)?1:0;
//  gst_print_rtcp_rrb(rrb);
//  g_print("%d: \n", subflow->id, _irt0(subflow)->lost);

}


void
_report_processing_xr_7243_block_processor (SndEventBasedController *this,
                                            Subflow * subflow,
                                            GstRTCPXR_RFC7243 * xrb)
{
  guint8 interval_metric;
  guint32 discarded_bytes;
  gboolean early_bit;
//  g_print("DISCARDED REPORT ARRIVED\n");
  gst_rtcp_xr_rfc7243_getdown (xrb, &interval_metric,
      &early_bit, NULL, &discarded_bytes);
  _irt0 (subflow)->rfc7243_arrived = TRUE;
  switch(interval_metric)
  {
    case RTCP_XR_RFC7243_I_FLAG_CUMULATIVE_DURATION:
      if (early_bit) {
        _irt0 (subflow)->early_discarded_bytes = discarded_bytes - _irt1 (subflow)->early_discarded_bytes_sum;
        _irt0 (subflow)->early_discarded_bytes_sum = discarded_bytes;
      } else {
        _irt0 (subflow)->late_discarded_bytes = discarded_bytes - _irt1 (subflow)->late_discarded_bytes_sum;
        _irt1 (subflow)->late_discarded_bytes_sum = discarded_bytes;
      }
    break;
    case RTCP_XR_RFC7243_I_FLAG_INTERVAL_DURATION:
      if (early_bit) {
        _irt0 (subflow)->early_discarded_bytes = discarded_bytes;
        _irt0 (subflow)->early_discarded_bytes_sum += discarded_bytes;
      } else {
          _irt0 (subflow)->late_discarded_bytes = discarded_bytes;
          _irt0 (subflow)->late_discarded_bytes_sum += discarded_bytes;
      }
    break;
    case RTCP_XR_RFC7243_I_FLAG_SAMPLED_METRIC:
    default:
    break;
  }
//  gst_print_rtcp_xr_7243(xrb);
  subflow->late_discarded_history+=(_irt0 (subflow)->late_discarded_bytes>0)?1:0;
}

void
_report_processing_xr_owd_block_processor (SndEventBasedController *this,
                                            Subflow * subflow,
                                            GstRTCPXR_OWD * xrb)
{
  guint32 median_delay,min_delay,max_delay;

  gst_rtcp_xr_owd_getdown(xrb,
                           NULL,
                           NULL,
                           &median_delay,
                           &min_delay,
                           &max_delay);

  _irt0(subflow)->recent_delay = median_delay;
  _irt0(subflow)->recent_delay<<=16;
  _irt0(subflow)->recent_delay = get_epoch_time_from_ntp_in_ns(_irt0(subflow)->recent_delay);
  _irt0(subflow)->min_delay = min_delay;
  _irt0(subflow)->min_delay<<=16;
  _irt0(subflow)->min_delay = get_epoch_time_from_ntp_in_ns(_irt0(subflow)->min_delay);
  _irt0(subflow)->max_delay = max_delay;
  _irt0(subflow)->max_delay<<=16;
  _irt0(subflow)->max_delay = get_epoch_time_from_ntp_in_ns(_irt0(subflow)->max_delay);

//  gst_print_rtcp_xr_owd(xrb);
  //--------------------------
  //evaluating
  //--------------------------
}



void
_report_processing_xr_rfc7097_block_processor (SndEventBasedController *this,
                                            Subflow * subflow,
                                            GstRTCPXR_RFC7097 * xrb)
{
  guint chunks_num, chunk_index;
  guint16 running_length;
  GstRTCPXR_Chunk *chunk;

  _irt0 (subflow)->rfc7097_arrived = TRUE;
  chunks_num = gst_rtcp_xr_rfc7097_get_chunks_num(xrb);
  for(chunk_index = 0;
      chunk_index < chunks_num;
      ++chunk_index)
  {
      chunk = gst_rtcp_xr_rfc7097_get_chunk(xrb, chunk_index);

      //Terminate chunk
      if(*((guint16*)chunk) == 0) break;
      gst_rtcp_xr_chunk_getdown(chunk, NULL, NULL, &running_length);
      _irt0(subflow)->rle_discards.values[chunk_index] = running_length;
      ++_irt0(subflow)->rle_discards.length;
  }
}


void
_report_processing_xr_rfc3611_block_processor (SndEventBasedController *this,
                                            Subflow * subflow,
                                            GstRTCPXR_RFC3611 * xrb)
{
  guint chunks_num, chunk_index;
  guint16 running_length;
  GstRTCPXR_Chunk *chunk;

  _irt0 (subflow)->rfc3611_arrived = TRUE;
  chunks_num = gst_rtcp_xr_rfc3611_get_chunks_num(xrb);
  for(chunk_index = 0;
      chunk_index < chunks_num;
      ++chunk_index)
  {
      chunk = gst_rtcp_xr_rfc3611_get_chunk(xrb, chunk_index);

      //Terminate chunk
      if(*((guint16*)chunk) == 0) break;
      gst_rtcp_xr_chunk_getdown(chunk, NULL, NULL, &running_length);
      _irt0(subflow)->rle_losts.values[chunk_index] = running_length;
      ++_irt0(subflow)->rle_losts.length;
  }
}



void
_report_processing_xr_owd_rle_block_processor (SndEventBasedController *this,
                                            Subflow * subflow,
                                            GstRTCPXR_OWD_RLE * xrb)
{
  guint chunks_num, chunk_index;
  guint16 running_length;
  GstRTCPXR_Chunk *chunk;

  _irt0 (subflow)->owd_rle_arrived = TRUE;
//gst_print_rtcp_xr_owd_rle(xrb);
  chunks_num = gst_rtcp_xr_owd_rle_get_chunks_num(xrb);
  for(chunk_index = 0;
      chunk_index < chunks_num;
      ++chunk_index)
  {
      chunk = gst_rtcp_xr_owd_rle_get_chunk(xrb, chunk_index);

      //Terminate chunk
      if(*((guint16*)chunk) == 0) break;
      gst_rtcp_xr_chunk_getdown(chunk, NULL, NULL, &running_length);
      _irt0(subflow)->rle_delays.values[chunk_index] = (GstClockTime)running_length * GST_MSECOND;
      if(_irt0(subflow)->rle_delays.values[chunk_index] == 0){
        if(_irt0(subflow)->rle_delays.length == 0)
          g_warning("OWD delay at first index should not be 0");
        else break;
      }
      ++_irt0(subflow)->rle_delays.length;
  }
}



gfloat
_get_subflow_goodput (Subflow * this, gdouble* receiver_rate)
{
  //goodput
  {
    GstClockTimeDiff interval;
    GstClockTime seconds;
    gdouble secondsd;
    gfloat expected_payload_bytes = 0.;
    guint32 discarded_bytes;
    gfloat goodput;
    gdouble sent_payload_bytes;

    if (_irt1(this)->time == 0) {
        interval = GST_CLOCK_DIFF (this->joined_time, _irt0(this)->time);
    }else{
        interval = GST_CLOCK_DIFF (_irt1(this)->time, _irt0(this)->time);
    }
    seconds = GST_TIME_AS_SECONDS ((GstClockTime) interval);

    expected_payload_bytes = (gfloat) _irt0(this)->expected_payload_bytes;
    sent_payload_bytes = _irt0(this)->sent_payload_bytes_sum - _irt1(this)->sent_payload_bytes_sum;

    discarded_bytes = _irt0_get_discarded_bytes(this);
    if (seconds > 0) {
      secondsd = (gdouble) GST_TIME_AS_MSECONDS ((GstClockTime) interval) / 1000.;
      if(receiver_rate)
        *receiver_rate = (expected_payload_bytes * (1. - _irt0 (this)->lost_rate)) / (secondsd);
      goodput = (expected_payload_bytes *
          (1. - _irt0 (this)->lost_rate) -
          (gfloat) discarded_bytes) / (secondsd);
      _irt0(this)->sender_rate = sent_payload_bytes / secondsd;

    } else {
        //g_print("S%d PB: %f ->%lu\n", this->id, payload_bytes_sum, GST_TIME_AS_MSECONDS(_irt0(this)->time - _irt1(this)->time));
      if(receiver_rate)
        *receiver_rate = (expected_payload_bytes * (1. - _irt0 (this)->lost_rate));
      goodput = (expected_payload_bytes *
          (1. - _irt0 (this)->lost_rate) - (gfloat) discarded_bytes);
      _irt0(this)->sender_rate = sent_payload_bytes;
    }
    _irt0(this)->goodput = goodput;
    return goodput;
  }
}

Event _subflow_check_p_state(Subflow *subflow)
{
  return EVENT_FI;
}

Event _subflow_check_nc_state(Subflow *subflow)
{
  return EVENT_FI;
}

Event _subflow_check_l_state(Subflow *subflow)
{
  return EVENT_FI;
}

Event _subflow_check_c_state(Subflow *subflow)
{
  return EVENT_FI;
}

void _subflow_fire_p_state(SndEventBasedController *this,Subflow *subflow,Event event)
{
  MPRTPSPath *path = subflow->path;
  MPRTPSPathState state;
  subflow->fire = _subflow_fire_p_state;
  if(event != EVENT_SETTLEMENT) goto done;

  mprtps_path_set_active(path);
  state = mprtps_path_get_state(path);
  subflow->rate_controller = sndrate_distor_add_controllable_path(
      this->rate_distor,
      path,
      SUBFLOW_DEFAULT_SENDING_RATE);

  _subflow_state_transit_to(subflow, state);
done:
  return;
}

void _subflow_fire_nc_state(SndEventBasedController *this,Subflow *subflow,Event event)
{
//  MPRTPSPath *path = subflow->path;
  subflow->fire = _subflow_fire_nc_state;
  switch (event) {
   case EVENT_LATE:
     _subflow_fall_action(this, subflow);
     _subflow_state_transit_to(subflow, MPRTPS_PATH_STATE_PASSIVE);
   break;
   case EVENT_DISTORTION:
     _subflow_state_transit_to(subflow, MPRTPS_PATH_STATE_CONGESTED);
     break;
   case EVENT_LOST:
     _subflow_state_transit_to(subflow, MPRTPS_PATH_STATE_LOSSY);
     break;
   case EVENT_FI:
   default:
     break;
 }
}
void _subflow_fire_l_state(SndEventBasedController *this,Subflow *subflow,Event event)
{
//  MPRTPSPath *path = subflow->path;
  subflow->fire = _subflow_fire_nc_state;
  switch (event) {
    case EVENT_LATE:
      _subflow_fall_action(this, subflow);
      _subflow_state_transit_to(subflow, MPRTPS_PATH_STATE_PASSIVE);
    break;
    case EVENT_SETTLEMENT:
      _subflow_state_transit_to(subflow, MPRTPS_PATH_STATE_NON_CONGESTED);
      break;
    case EVENT_DISTORTION:
      _subflow_state_transit_to(subflow, MPRTPS_PATH_STATE_CONGESTED);
      break;
    case EVENT_FI:
    default:
      break;
  }
}


void _subflow_fire_c_state(SndEventBasedController *this,Subflow *subflow,Event event)
{
//  MPRTPSPath *path = subflow->path;
  subflow->fire = _subflow_fire_nc_state;
  switch (event) {
    case EVENT_LATE:
      _subflow_fall_action(this, subflow);
      _subflow_state_transit_to(subflow, MPRTPS_PATH_STATE_PASSIVE);
    break;
    case EVENT_LOST:
    case EVENT_DISTORTION:
      break;
    case EVENT_SETTLEMENT:
    case EVENT_FI:
    default:
      _subflow_state_transit_to(subflow, MPRTPS_PATH_STATE_NON_CONGESTED);
      break;
  }
}

void _subflow_state_transit_to(Subflow *subflow, MPRTPSPathState target)
{
  switch(mprtps_path_get_state(subflow->path)){
      case MPRTPS_PATH_STATE_CONGESTED:
        subflow->fire = _subflow_fire_c_state;
        subflow->check = _subflow_check_c_state;
        break;
      case MPRTPS_PATH_STATE_LOSSY:
        subflow->fire = _subflow_fire_l_state;
        subflow->check = _subflow_check_l_state;
        break;
      case MPRTPS_PATH_STATE_PASSIVE:
        subflow->fire = _subflow_fire_p_state;
        subflow->check = _subflow_check_p_state;
        break;
      default:
      case MPRTPS_PATH_STATE_NON_CONGESTED:
        subflow->fire = _subflow_fire_nc_state;
        subflow->check = _subflow_check_nc_state;
        break;
    }
}

void _subflow_fall_action(SndEventBasedController *this, Subflow *subflow)
{
  mprtps_path_set_passive(subflow->path);
  stream_splitter_rem_path(this->splitter, subflow->id);
  sndrate_distor_remove_id(this->rate_distor, subflow->id);
}

gboolean
_check_report_timeout (Subflow * this)
{
  MPRTPSPath *path;
  GstClockTime now;
  MPRTPSPathState path_state;

  path = this->path;
  path_state = mprtps_path_get_state (path);
  now = gst_clock_get_time (this->sysclock);
  if (path_state == MPRTPS_PATH_STATE_PASSIVE) {
    goto done;
  }
  if (!this->ir_moments_num) {
    if (this->joined_time < now - REPORTTIMEOUT) {
//      g_print("S:%d->FIRST REPORT WAS NOT ARRIVED\n", this->id);
      return TRUE;
    }
    goto done;
  }
  if (_irt1(this)->time < now - REPORTTIMEOUT) {
//    g_print("S:%d->REPORT_TOO_LATE %lu < %lu\n", this->id, _irt1(this)->time, now - REPORTTIMEOUT);
    return TRUE;
  }
done:
  return FALSE;
}

//---------------------------------------------------------------------------


//------------------------ Outgoing Report Producer -------------------------

void
_orp_producer_main(SndEventBasedController * this)
{
  ReportIntervalCalculator* ricalcer;
  GHashTableIter iter;
  gpointer key, val;
  guint32 sent_report_length = 0;
  Subflow *subflow;

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    subflow = (Subflow *) val;
    ricalcer = subflow->ricalcer;
    if (!this->report_is_flowable || !ricalcer_do_report_now(ricalcer)) {
      continue;
    }

    if (_check_report_timeout (subflow)) {
      subflow->fire (this, subflow, EVENT_LATE);
    }
    _send_mprtcp_sr_block (this, subflow, &sent_report_length);

    subflow->avg_rtcp_size +=
        ((gfloat) sent_report_length - subflow->avg_rtcp_size) / 4.;

    ricalcer_refresh_parameters(ricalcer,
                                _ort0(subflow)->receiving_rate,
                                subflow->avg_rtcp_size);
  }
  return;
}

void
_send_mprtcp_sr_block (SndEventBasedController * this, Subflow * subflow, guint32 *sent_report_length)
{
  GstBuffer *buf;
  _step_or(subflow);
  buf = _get_mprtcp_sr_block (this,
                              subflow,
                              sent_report_length);
  this->send_mprtcp_packet_func (this->send_mprtcp_packet_data, buf);
  if(sent_report_length) *sent_report_length += 12 /* RTCP HEADER*/ + (28<<3) /*UDP+IP HEADER*/;
}

void _step_or(Subflow *this)
{
  this->or_moments_index = 1 - this->or_moments_index;
  memset ((gpointer) _ort0 (this), 0, sizeof (ORMoment));
  ++this->or_moments_num;
  _ort0(this)->sent_packets_num =
        mprtps_path_get_total_sent_packets_num(this->path);
  _ort0(this)->sent_payload_bytes =
      mprtps_path_get_total_sent_payload_bytes(this->path);
  _ort1(this)->time = gst_clock_get_time(this->sysclock);
  _ort0(this)->receiving_rate = 64000.;
}

GstBuffer *
_get_mprtcp_sr_block (SndEventBasedController * this,
    Subflow * subflow, guint32 * buf_length)
{
  GstMPRTCPSubflowBlock block;
  guint8 block_length;
  guint16 length;
  gpointer dataptr;
  GstRTCPSR *sr;
  GstBuffer *result;

  gst_mprtcp_block_init (&block);
  sr = gst_mprtcp_riport_block_add_sr (&block);
  _setup_sr_riport (subflow, sr, this->ssrc);
  gst_rtcp_header_getdown (&sr->header, NULL, NULL, NULL, NULL, &length, NULL);
  block_length = (guint8) length + 1;
  gst_mprtcp_block_setup (&block.info, MPRTCP_BLOCK_TYPE_RIPORT,
      block_length, (guint16) subflow->id);
  length = (block_length + 1) << 2;
  dataptr = g_malloc0 (length);
  memcpy (dataptr, &block, length);
  result = gst_buffer_new_wrapped (dataptr, length);
  if (buf_length) {
    *buf_length += (guint32)length;
  }
  return result;
}


void
_setup_sr_riport (Subflow * this, GstRTCPSR * sr, guint32 ssrc)
{
  guint64 ntptime;
  guint32 rtptime;
  guint32 packet_count_diff, payload_bytes;

  gst_rtcp_header_change (&sr->header, NULL, NULL, NULL, NULL, NULL, &ssrc);
//  ntptime = gst_clock_get_time (this->sysclock);
  ntptime = NTP_NOW;

  rtptime = (guint32) (gst_rtcp_ntp_to_unix (ntptime) >> 32);   //rtptime

  packet_count_diff = _uint32_diff (_ort1(this)->sent_packets_num,
      _ort0(this)->sent_packets_num);

  payload_bytes =
      _uint32_diff (_ort1(this)->sent_payload_bytes,
                    _ort0(this)->sent_payload_bytes );

  gst_rtcp_srb_setup (&sr->sender_block, ntptime, rtptime,
      packet_count_diff, payload_bytes >> 3);

  if(_ort1(this)->time > 0){
    GstClockTime interval;
    interval = GST_TIME_AS_SECONDS(_ort1(this)->time - _ort0(this)->time);
    if(interval < 1) interval = 1;
    _ort0(this)->receiving_rate = payload_bytes / interval;
  }

//  g_print("Created NTP time for subflow %d is %lu\n", this->id, ntptime);
}


//---------------------------------------------------------------------------

//----------------------------- System Notifier ------------------------------
//
//void
//_system_notifier_main(SndEventBasedController * this)
//{
//
//  if(this->event == SPLITCTRLER_EVENT_FI)  goto done;
//  if(this->event == SPLITCTRLER_EVENT_UNDERUSED) goto underused;
//  //overused
//
////  g_print("-------->SYSTEM IS OVERUSED<--------\n");
//  goto done;
//underused:
////  g_print("-------->SYSTEM IS UNDERUSED<--------\n");
//done:
//  this->event = SPLITCTRLER_EVENT_FI;
//  return;
//}

static void
_system_notifier_utilization(gpointer controller, gpointer data)
{
  SndEventBasedController *this = controller;
  this->utilization_signal_request(this->utilization_signal_data, data);
}

//---------------------------------------------------------------------------


//----------------------------- Split Controller -----------------------------

void
_split_controller_main(SndEventBasedController * this)
{
  GstClockTime now;
  now = gst_clock_get_time(this->sysclock);

  if(this->ticknum % 2 == 1)
    sndrate_distor_time_update(this->rate_distor);

  if(_subflows_are_ready(this) ||
     this->last_recalc_time < now - 15 * GST_SECOND)
  {
      this->bids_recalc_requested = TRUE;
  }
//  goto recalc_done;
  if (!this->bids_recalc_requested) goto recalc_done;
  this->bids_recalc_requested = FALSE;
  this->bids_commit_requested = TRUE;
  this->target_rate = _recalc_bids(this);
recalc_done:
  if (!this->bids_commit_requested) goto process_done;
  this->bids_commit_requested = FALSE;

  stream_splitter2_setup_sending_rate(this->splitter2, 2, g_random_int_range(1,1000));
//  stream_splitter2_setup_sending_rate(this->splitter2, 3, g_random_int_range(1,1000));
  stream_splitter2_commit_changes(this->splitter2);

  stream_splitter_commit_changes (this->splitter, 0 * this->target_rate, 0 * GST_MSECOND);

process_done:
  return;
}


typedef struct _Utilization{
  gint     delta_sr;
  gboolean accepted;
  gdouble  changing_rate;
}Utilization;

guint32 _recalc_bids(SndEventBasedController * this)
{
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  guint32 sending_bitrate;
  guint32 target_byterate = 0;
  GstClockTime now;

  now = gst_clock_get_time(this->sysclock);
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    subflow = (Subflow *) val;
    if(!mprtps_path_is_active(subflow->path)){
      continue;
    }

    sending_bitrate = subratectrler_get_target_bitrate(subflow->rate_controller);
    target_byterate += sending_bitrate>>3;
//    g_print("Subflow %d sending rate %u\n",
//            subflow->id,
//            sending_rate);
    stream_splitter_setup_sending_bid(this->splitter,
                                      subflow->id,
                                      sending_bitrate);
    subflow->ready = FALSE;
  }

  this->last_recalc_time = now;
  return target_byterate;
}


gboolean _subflows_are_ready(SndEventBasedController * this)
{
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  gboolean result = TRUE;

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    subflow = (Subflow *) val;
    if(!mprtps_path_is_active(subflow->path)){
      continue;
    }
    result&=subflow->ready;
  }

  return result;
}


//----------------------------------------------------------------------------


//---------------------- Utility functions ----------------------------------
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

Subflow *
_make_subflow (guint8 id, MPRTPSPath * path)
{
  Subflow *result = _subflow_ctor ();
  g_object_ref (path);
  result->sysclock = gst_system_clock_obtain ();
  result->path = path;
  result->id = id;
  result->joined_time = gst_clock_get_time (result->sysclock);
  _subflow_state_transit_to(result, MPRTPS_PATH_STATE_NON_CONGESTED);
  result->ir_moments =
      (RRMeasurement *) g_malloc0 (sizeof (RRMeasurement) * MAX_SUBFLOW_MOMENT_NUM);
  result->ir_moments_index = 0;
  result->ricalcer = make_ricalcer(TRUE);
  reset_subflow (result);
  _irt0(result)->time = result->joined_time;
  return result;
}

void
reset_subflow (Subflow * this)
{
  gint i;
  for (i = 0; i < MAX_SUBFLOW_MOMENT_NUM; ++i) {
    memset (this->ir_moments, 0, sizeof (RRMeasurement) * MAX_SUBFLOW_MOMENT_NUM);
  }
}

guint16
_uint16_diff (guint16 start, guint16 end)
{
  if (start <= end) {
    return end - start;
  }
  return ~((guint16) (start - end));
}


guint32
_uint32_diff (guint32 start, guint32 end)
{
  if (start <= end) {
    return end - start;
  }
  return ~((guint32) (start - end));
}



#undef _ct0
#undef _ct1
#undef _irt0
#undef _irt1
#undef _irt2
#undef _irt3
#undef _irt4
#undef DEBUG_MODE_ON
#undef MAX_RIPORT_INTERVAL
#undef REPORTTIMEOUT
#undef PATH_RTT_MAX_TRESHOLD
#undef PATH_RTT_MIN_TRESHOLD
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
