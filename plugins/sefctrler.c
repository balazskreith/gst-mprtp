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
#include "streamtracker.h"

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define MAX_RIPORT_INTERVAL (5 * GST_SECOND)
#define REPORTTIMEOUT (3 * MAX_RIPORT_INTERVAL)
#define PATH_RTT_MAX_TRESHOLD (800 * GST_MSECOND)
#define PATH_RTT_MIN_TRESHOLD (600 * GST_MSECOND)
#define MAX_SUBFLOW_MOMENT_NUM 5
#define SUBFLOW_DEFAULT_GOODPUT 64000

GST_DEBUG_CATEGORY_STATIC (sefctrler_debug_category);
#define GST_CAT_DEFAULT sefctrler_debug_category

#define _irt0(this) (this->ir_moments+this->ir_moments_index)
#define _irt1(this) (this->ir_moments + (this->ir_moments_index == 0 ? MAX_SUBFLOW_MOMENT_NUM-1 : this->ir_moments_index-1))
#define _irt2(this) _irt(this, -2)
#define _irt3(this) _irt(this, -3)
#define _irt4(this) _irt(this, -4)
#define _ort0(this) (this->or_moments+this->or_moments_index)
#define _ort1(this) (this->or_moments+!this->or_moments_index)
#define _irt0_get_discarded_bytes(this) (_irt0(this)->early_discarded_bytes + _irt0(this)->late_discarded_bytes)

G_DEFINE_TYPE (SndEventBasedController, sefctrler, G_TYPE_OBJECT);

typedef struct _Subflow Subflow;
typedef struct _IRMoment IRMoment;
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


struct _IRMoment{
  GstClockTime        time;
  GstClockTime        RTT;
  guint32             jitter;
  guint32             cum_packet_lost;
  guint32             lost;
  guint64             delay_last;
  guint64             delay_40;
  guint64             delay_80;
  guint64             skew;
  guint64             min_skew;
  guint64             max_skew;
  guint32             early_discarded_bytes;
  guint32             late_discarded_bytes;
  guint32             early_discarded_bytes_sum;
  guint32             late_discarded_bytes_sum;
  guint16             HSSN;
  guint16             cycle_num;
  guint16             expected_packets;
  guint32             expected_payload_bytes;
  gdouble             lost_rate;
  gdouble             goodput;
  MPRTPSPathState     state;
  gint64              skew_diff;
  gboolean            checked;
};

struct _ORMoment{
  GstClockTime        time;
  guint32             sent_packets_num;
  guint32             sent_payload_bytes;
  gdouble             media_rate;

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
  IRMoment*                  ir_moments;
  gint                       ir_moments_index;
  guint32                    ir_moments_num;
  ORMoment                   or_moments[2];
  gint                       or_moments_index;
  guint32                    or_moments_num;
  StateFunc                  fire;
  EventFunc                  check;
  guint8                     rate_calcer_id;
//  GstClockTime               RTT;
//  guint                      consecutive_late_RTT;
  gdouble                    avg_rtcp_size;
//  gdouble                    control_signal;
//  guint32                    consecutive_keep;
//  guint32                    (*get_goodput)(Subflow*);
//  guint                      tr;
//  GstClockTime               restored;
//  GstClockTime               marked;
  gdouble                    actual_rate;

//  gdouble                    actual_goodput;
//  BinTree*                   goodputs;
//  guint64                    goodputs_values[16];
//  GstClockTime               goodputs_arrived[16];
//  guint8                     goodputs_read;
//  guint8                     goodputs_write;

//  guint8                     required_wait;
//  gboolean                   increasable;
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
sefctrler_pacing (gpointer controller_ptr, gboolean allowed);
static gboolean
sefctrler_is_pacing (gpointer controller_ptr);
static GstStructure* sefctrler_state (gpointer controller_ptr);

static void
sefctrler_riport_can_flow (gpointer this);

//---------------------------------- Ticker ----------------------------------

static void
sefctrler_ticker_run (void *data);

//----------------------------------------------------------------------------


//------------------------- Incoming Report Processor -------------------
static void
_irp_producer_main(SndEventBasedController * this);

static IRMoment*
_irt (Subflow * this, gint moment);

static void
_step_ir (Subflow * this);

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
_report_processing_xr_owd_block_processor (SndEventBasedController *this,
                                            Subflow * subflow,
                                            GstRTCPXR_OWD * xrb);

static gfloat
_get_subflow_goodput (
    Subflow * this);

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
static void
_system_notifier_main(SndEventBasedController * this);

//----------------------------------------------------------------------------

//----------------------------- Split Controller -----------------------------
static void
_split_controller_main(SndEventBasedController * this);
static void
_recalc_bids(SndEventBasedController * this);
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

//static void sefctrler_stat_run (void *data);

void
sefctrler_init (SndEventBasedController * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->subflows = g_hash_table_new_full (NULL, NULL,
      NULL, (GDestroyNotify) _ruin_subflow);
  this->subflow_num = 0;
  this->bids_recalc_requested = FALSE;
  this->bids_commit_requested = FALSE;
  this->ssrc = g_random_int ();
  this->report_is_flowable = FALSE;
  this->pacing = FALSE;
  this->RTT_max = 5 * GST_SECOND;
  this->last_recalc_time = gst_clock_get_time(this->sysclock);
  this->event = SPLITCTRLER_EVENT_FI;
  this->rate_distor = make_sndrate_distor();
  g_rw_lock_init (&this->rwmutex);
  g_rec_mutex_init (&this->thread_mutex);
  this->thread = gst_task_new (sefctrler_ticker_run, this, NULL);
  gst_task_set_lock (this->thread, &this->thread_mutex);
  gst_task_start (this->thread);
//
//  g_rec_mutex_init (&this->stat_thread_mutex);
//  this->stat_thread = gst_task_new (sefctrler_stat_run, this, NULL);
//  gst_task_set_lock (this->stat_thread, &this->stat_thread_mutex);
//  gst_task_start (this->stat_thread);

}
//
//void
//sefctrler_stat_run (void *data)
//{
//  SndEventBasedController *this;
//  GstClockID clock_id;
//  GHashTableIter iter;
//  gpointer key, val;
//  Subflow *subflow;
////  guint32 actual;
//  GstClockTime next_scheduler_time;
//  this = data;
//  THIS_WRITELOCK (this);
////  g_print("# subflow1, subflow 2\n");
//  g_hash_table_iter_init (&iter, this->subflows);
//  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
//    subflow = (Subflow *) val;
//    if(subflow) goto next;
//    next:
//    continue;
//  }
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
//

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
  stream_splitter_add_path (this->splitter, subflow_id, path, SUBFLOW_DEFAULT_GOODPUT);
  new_subflow->rate_calcer_id = sndrate_distor_request_id(this->rate_distor, path, SUBFLOW_DEFAULT_GOODPUT);
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
  sndrate_distor_remove_id(this->rate_distor, lookup_result->rate_calcer_id);
  stream_splitter_rem_path (this->splitter, subflow_id);
  g_hash_table_remove (this->subflows, GINT_TO_POINTER (subflow_id));
  if (--this->subflow_num < 0) {
    this->subflow_num = 0;
  }
exit:
  THIS_WRITEUNLOCK (this);
}

void
sefctrler_pacing (gpointer controller_ptr, gboolean allowed)
{
  SndEventBasedController *this;
  this = SEFCTRLER (controller_ptr);
  THIS_WRITELOCK (this);
  this->pacing = allowed;
  THIS_WRITEUNLOCK (this);
}

gboolean
sefctrler_is_pacing (gpointer controller_ptr)
{
  SndEventBasedController *this;
  gboolean result;
  this = SEFCTRLER (controller_ptr);
  THIS_READLOCK (this);
  result = this->pacing;
  THIS_READUNLOCK (this);
  return result;
}

GstStructure*
sefctrler_state (gpointer controller_ptr)
{
  SndEventBasedController *this;
  GstStructure *result = NULL;
//  GHashTableIter iter;
//  gpointer key, val;
//  Subflow *subflow;
//  gint index = 0;
//  GValue g_value = { 0 };
//  gchar *field_name;

  this = SEFCTRLER (controller_ptr);
  THIS_WRITELOCK (this);
//
//    result = gst_structure_new ("SchedulerStateReport",
//        "length", G_TYPE_UINT, this->subflows_num, NULL);
//    g_value_init (&g_value, G_TYPE_UINT);
//    g_hash_table_iter_init (&iter, this->subflows);
//    while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
//      subflow = (Subflow *) val;
//
//      field_name = g_strdup_printf ("subflow-%d-id", index);
//      g_value_set_uint (&g_value, mprtps_path_get_id (subflow->id));
//      gst_structure_set_value (result, field_name, &g_value);
//      g_free (field_name);
//
//      field_name = g_strdup_printf ("subflow-%d-fractional_lost", index);
//      g_value_set_uint (&g_value, _st0(subflow)->fraction_lost);
//      gst_structure_set_value (result, field_name, &g_value);
//      g_free (field_name);
//
//      field_name = g_strdup_printf ("subflow-%d-sent_payload_bytes", index);
//      g_value_set_uint (&g_value,
//          mprtps_path_get_total_sent_payload_bytes (path));
//      gst_structure_set_value (result, field_name, &g_value);
//      g_free (field_name);
//
//      ++index;
//    }
//  _ct0(this)->is_it_new = FALSE;

  THIS_WRITEUNLOCK (this);
  return result;
}




void
sefctrler_set_callbacks(void(**riport_can_flow_indicator)(gpointer),
                              void(**controller_add_path)(gpointer,guint8,MPRTPSPath*),
                              void(**controller_rem_path)(gpointer,guint8),
                              void(**controller_pacing)(gpointer, gboolean),
                              gboolean (**controller_is_pacing)(gpointer),
                              GstStructure* (**controller_state)(gpointer))
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
  if (controller_pacing) {
    *controller_pacing = sefctrler_pacing;
  }
  if (controller_is_pacing) {
    *controller_is_pacing = sefctrler_is_pacing;
  }
  if(controller_state){
    *controller_state = sefctrler_state;
  }

}

void
sefctrler_setup (SndEventBasedController * this, StreamSplitter * splitter)
{
  THIS_WRITELOCK (this);
  this->splitter = splitter;
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
                                void(*scheduler_signaling)(gpointer, guint64),
                                gpointer scheduler)
{
  SndEventBasedController * this = ptr;
  THIS_WRITELOCK (this);
  this->scheduler_signaling = scheduler_signaling;
  this->scheduler = scheduler;
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
//----------------------------------------------------------------------------


//------------------------- Incoming Report Processor -------------------

void
_irp_producer_main(SndEventBasedController * this)
{
  GHashTableIter iter;
  gpointer       key, val;
  Subflow*       subflow;
  gboolean       all_subflow_checked = TRUE;
  GstClockTime   now;
  Event          event;
  gfloat         goodput;
  gdouble        variance;

  now = gst_clock_get_time(this->sysclock);

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    subflow = (Subflow *) val;
    if(_irt0(subflow)->checked) goto checked;
    if(now - 120 * GST_MSECOND < _irt0(subflow)->time) goto not_checked;
    goodput = _get_subflow_goodput(subflow);
//    variance = (gdouble)streamtracker_get_stats(this->delays, NULL, NULL, NULL) / (gdouble)_irt0(subflow)->jitter;
    variance = .1;
    sndrate_distor_measurement_update(this->rate_distor,
                                      subflow->rate_calcer_id,
                                      goodput,
                                      variance);
    event = subflow->check(subflow);
    subflow->fire(this, subflow, event);
    _irt0(subflow)->checked = TRUE;
  checked:
    all_subflow_checked&=TRUE;
    continue;

  not_checked:
    all_subflow_checked&=FALSE;
    continue;
  }

  if(!this->all_subflow_are_checked && all_subflow_checked){
    this->all_subflow_are_checked_time = now;
  }
  this->all_subflow_are_checked = all_subflow_checked;

}

IRMoment *
_irt (Subflow * this, gint moment)
{
  gint index;
  index = this->ir_moments_index - (moment % MAX_SUBFLOW_MOMENT_NUM);
  if (index < 0)
    index = MAX_SUBFLOW_MOMENT_NUM - index;
  return this->ir_moments + index;
}

void
_step_ir (Subflow * this)
{
  this->ir_moments_index = (this->ir_moments_index + 1) % MAX_SUBFLOW_MOMENT_NUM;
  memset ((gpointer) _irt0 (this), 0, sizeof (IRMoment));
//  _st0(this)->RTT                   = _irt1(this)->RTT;

  _irt0(this)->early_discarded_bytes_sum = _irt1(this)->early_discarded_bytes_sum;
  _irt0(this)->late_discarded_bytes_sum  = _irt1(this)->late_discarded_bytes_sum;
  _irt0(this)->time                      = gst_clock_get_time(this->sysclock);
  _irt0(this)->state                     = _irt1(this)->state;
  _irt0(this)->checked                   = FALSE;
  ++this->ir_moments_num;
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


  LSR = (guint64) LSR_read;
  DLSR = (guint64) DLSR_read;

  if (_irt0(subflow)->expected_packets > 32767) {
    GST_WARNING_OBJECT (subflow, "Receiver report validation failed "
        "on subflow %d " "due to HSN difference inconsistency.", subflow->id);
    return;
  }
  _irt0(subflow)->expected_payload_bytes =
      mprtps_path_get_sent_octet_sum_for (subflow->path, _irt0 (subflow)->expected_packets)<<3;
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

//  g_print("%d: %u:%f\n", subflow->id, _irt0(subflow)->lost, _irt0(subflow)->lost_rate);

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
}

void
_report_processing_xr_owd_block_processor (SndEventBasedController *this,
                                            Subflow * subflow,
                                            GstRTCPXR_OWD * xrb)
{
  guint32 last_delay,min_delay,max_delay;

  gst_rtcp_xr_owd_getdown(xrb,
                           NULL,
                           NULL,
                           NULL,
                           NULL,
                           &last_delay,
                           &min_delay,
                           &max_delay);


  _irt0(subflow)->delay_last = get_epoch_time_from_ntp_in_ns(last_delay<<16);
  _irt0(subflow)->delay_40 =   get_epoch_time_from_ntp_in_ns(min_delay<<16);
  _irt0(subflow)->delay_80 =   get_epoch_time_from_ntp_in_ns(max_delay<<16);
  //--------------------------
  //evaluating
  //--------------------------


}



gfloat
_get_subflow_goodput (Subflow * this)
{
  //goodput
  {
    GstClockTimeDiff interval;
    GstClockTime seconds;
    gfloat payload_bytes_sum = 0.;
    guint32 discarded_bytes;
    gfloat goodput;
    if (_irt1(this)->time == 0) {
        interval = GST_CLOCK_DIFF (this->joined_time, _irt0(this)->time);
    }else{
        interval = GST_CLOCK_DIFF (_irt1(this)->time, _irt0(this)->time);
    }
    seconds = GST_TIME_AS_SECONDS ((GstClockTime) interval);

    payload_bytes_sum = (gfloat) _irt0(this)->expected_payload_bytes;

    discarded_bytes = _irt0_get_discarded_bytes(this);
    if (seconds > 0) {
      goodput = (payload_bytes_sum *
          (1. - _irt0 (this)->lost_rate) -
          (gfloat) discarded_bytes) / ((gfloat) seconds);

    } else {
      goodput = (payload_bytes_sum *
          (1. - _irt0 (this)->lost_rate) - (gfloat) discarded_bytes);
    }
    _irt0(this)->goodput = goodput;
    return goodput;
//    this->actual_goodput = _irt0(this)->goodput = goodput;
//    g_print("Sub %d-GP: %f = (%f * (1.-%f) - %f), jitter: %u\n",
//                  this->id, goodput,
//                  payload_bytes_sum, _irt0 (this)->lost_rate,
//                  (gfloat) discarded_bytes,
//                  _irt0(this)->jitter);
//    {
//      gdouble diff;
//      _irt0(this)->goodput = goodput;
//      diff = _irt1(this)->goodput - _irt0(this)->goodput;
//      if(diff < 0.) diff*=-1.;
//      _irt0(this)->goodput_variation = _irt1(this)->goodput_variation + (diff - _irt1(this)->goodput_variation) / 16.;
//    }
//    _obsolate_goodput(this);
//    if(!mprtps_path_is_monitoring(this->path)){
//      _add_goodput(this, goodput);
//
//    }
//    g_print("Sub-%d-bid: %f =  (%f*%d + %f)/%d\n",
//                  this->id,
//                  _irt0 (this)->bid,
//                  _irt1 (this)->bid,
//                  ck,
//                  goodput,
//                  ck+1);
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
  subflow->rate_calcer_id = sndrate_distor_request_id(
      this->rate_distor,
      path,
      SUBFLOW_DEFAULT_GOODPUT);

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
//     sndrate_distor_undershoot(this->rate_distor, subflow->rate_calcer_id);
     _subflow_state_transit_to(subflow, MPRTPS_PATH_STATE_CONGESTED);
     break;
   case EVENT_LOST:
//     sndrate_distor_undershoot(this->rate_distor, subflow->rate_calcer_id);
     _subflow_state_transit_to(subflow, MPRTPS_PATH_STATE_LOSSY);
     break;
   case EVENT_FI:
   default:
     sndrate_distor_keep(this->rate_distor, subflow->rate_calcer_id);
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
//      sndrate_distor_bounce_back(this->rate_distor, subflow->rate_calcer_id);
      _subflow_state_transit_to(subflow, MPRTPS_PATH_STATE_NON_CONGESTED);
      break;
    case EVENT_DISTORTION:
//      sndrate_distor_undershoot(this->rate_distor, subflow->rate_calcer_id);
      _subflow_state_transit_to(subflow, MPRTPS_PATH_STATE_CONGESTED);
      break;
    case EVENT_FI:
    default:
      sndrate_distor_keep(this->rate_distor, subflow->rate_calcer_id);
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
//      sndrate_distor_undershoot(this->rate_distor, subflow->rate_calcer_id);
      break;
    case EVENT_SETTLEMENT:
    case EVENT_FI:
    default:
//      sndrate_distor_bounce_back(this->rate_distor, subflow->rate_calcer_id);
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
  sndrate_distor_remove_id(this->rate_distor, subflow->rate_calcer_id);
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

    ricalcer_do_next_report_time(ricalcer);
    ricalcer_refresh_parameters(ricalcer,
                                _ort0(subflow)->media_rate,
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
  _ort0(this)->media_rate = 64000.;
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
    _ort0(this)->media_rate = payload_bytes / interval;
  }

//  g_print("Created NTP time for subflow %d is %lu\n", this->id, ntptime);
}


//---------------------------------------------------------------------------

//----------------------------- System Notifier ------------------------------

void
_system_notifier_main(SndEventBasedController * this)
{

  if(this->event == SPLITCTRLER_EVENT_FI)  goto done;
  if(this->event == SPLITCTRLER_EVENT_UNDERUSED) goto underused;
  //overused

//  g_print("-------->SYSTEM IS OVERUSED<--------\n");
  goto done;
underused:
//  g_print("-------->SYSTEM IS UNDERUSED<--------\n");
done:
  this->event = SPLITCTRLER_EVENT_FI;
  return;
}

//---------------------------------------------------------------------------


//----------------------------- Split Controller -----------------------------

void
_split_controller_main(SndEventBasedController * this)
{
  GstClockTime now;
  now = gst_clock_get_time(this->sysclock);

  if(this->last_recalc_time < this->all_subflow_are_checked_time ||
     this->last_recalc_time < now - 15 * GST_SECOND)
  {
    this->bids_recalc_requested = TRUE;
  }
  if (!this->bids_recalc_requested) goto recalc_done;
  this->bids_recalc_requested = FALSE;
  this->bids_commit_requested = TRUE;
  _recalc_bids(this);
recalc_done:
  if (!this->bids_commit_requested) goto process_done;
  this->bids_commit_requested = FALSE;
  stream_splitter_commit_changes (this->splitter);

process_done:
  return;
}

void _recalc_bids(SndEventBasedController * this)
{
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  guint32 sb;
  GstClockTime now;
//  guint32 media_rate;

  now = gst_clock_get_time(this->sysclock);
//  media_rate = stream_splitter_get_media_rate(this->splitter);
//  sndrate_distor_time_update(this->rate_distor, media_rate);

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    subflow = (Subflow *) val;
    if(!mprtps_path_is_active(subflow->path)){
      continue;
    }
    if(0) g_print("%lu", _irt(subflow, 2)->time);
    sb = sndrate_distor_get_rate(this->rate_distor, subflow->rate_calcer_id);
//    g_print("Subflow %d sending bid %u =  %f * %u\n",
//            subflow->id,
//            sb,
//            subflow->control_signal,
//            _get_max_goodput(subflow));
    stream_splitter_setup_sending_bid(this->splitter,
                                      subflow->id,
                                      sb);
  }
  this->last_recalc_time = now;
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
      (IRMoment *) g_malloc0 (sizeof (IRMoment) * MAX_SUBFLOW_MOMENT_NUM);
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
    memset (this->ir_moments, 0, sizeof (IRMoment) * MAX_SUBFLOW_MOMENT_NUM);
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
