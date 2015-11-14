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

#define _ct0(this) (this->splitctrler_moments + this->splitctrler_index)
#define _ct1(this) (this->splitctrler_moments + 1 - this->splitctrler_index)
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
  EVENT_LATE             = -6,
  EVENT_CONGESTION       = -5,
  EVENT_LOSSY            = -4,
  EVENT_INSUFFICIENT     = -3,
  EVENT_DISTORTION       = -1,
  EVENT_FI               =  0,
  EVENT_SETTLEMENT       =  1,
  EVENT_DEACTIVATE       =  16,
  EVENT_ACTIVATE         =  17,
} Event;


struct _SplitCtrlerMoment{
  GstClockTime        time;
  struct{
    guint num,nc,c,l,monitored;
  }subflows;
//  guint ditorted;
  gdouble goodput;
//  GstClockTime        max_RTT;

};

struct _IRMoment{
  GstClockTime        time;
  GstClockTime        RTT;
//  gdouble             goodput;
//  gdouble             goodput_variation;
  guint32             jitter;
  guint32             cum_packet_lost;
  guint32             lost;
  guint64             delay;
  guint64             min_delay;
  guint64             max_delay;
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

struct _Subflow
{
  MPRTPSPath*                path;
  GstClockTime               joined_time;
  guint8                     id;
  GstClock*                  sysclock;
  ReportIntervalCalculator*  ricalcer;
//  gboolean                   imprecise;
//  guint                      imprecise_counter;
  IRMoment*                  ir_moments;
  gint                       ir_moments_index;
  guint32                    ir_moments_num;
  ORMoment                   or_moments[2];
  gint                       or_moments_index;
  guint32                    or_moments_num;
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
_riport_processing_rrblock_processor (
    SndEventBasedController *this,
    Subflow * subflow,
    GstRTCPRRBlock * rrb);

static void
_report_processing_xr_7243_block_processor (
    SndEventBasedController *this,
    Subflow * subflow,
    GstRTCPXR_RFC7243 * xrb);

static void
_report_processing_xr_skew_block_processor (
    SndEventBasedController *this,
    Subflow * subflow,
    GstRTCPXR_Skew * xrb);

static void
_refresh_subflow_delays(
    SndEventBasedController *this,
    guint64 delay);

static void
_refresh_goodput (Subflow * this);

static void _subflow_fire (SndEventBasedController * this, Subflow * subflow,
    Event event);

guint32 _get_max_goodput(Subflow * subflow);
guint32 _get_min_goodput(Subflow * subflow);
//Actions
typedef void (*Action) (SndEventBasedController *, Subflow *);
//static void _action_recalc (SndEventBasedController * this, Subflow * subflow);
static void _perform_restore (SndEventBasedController * this, Subflow * subflow);
static void _perform_increase (SndEventBasedController * this, Subflow * subflow);
static void _perform_fall (SndEventBasedController * this, Subflow * subflow);;
static void _perform_keep (SndEventBasedController * this, Subflow * subflow);;
static void _perform_mitigate (SndEventBasedController * this, Subflow * subflow);
static void _perform_reduce (SndEventBasedController * this, Subflow * subflow);

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
static void
_step_c(SndEventBasedController *this);
static gboolean
_is_rate_stable(SndEventBasedController * this);
static gboolean
_is_goodput_stable(SndEventBasedController * this);
static void
_reset_c(SndEventBasedController *this);
static void
_refresh_c(SndEventBasedController *this, Subflow *subflow);
static SplitCtrlerEvent
_get_system_event(SndEventBasedController *this);
static Subflow*
_get_first_subflow(SndEventBasedController *this, MPRTPSPathState state);
static guint32
_get_lowest_cons_keep_from_nc_subflow(SndEventBasedController *this);
static Subflow*
_get_first_retored_subflow(SndEventBasedController *this);
static gboolean
_is_subflow_stable(Subflow*this);
//----------------------------------------------------------------------------

//------------------------- Utility functions --------------------------------
static Subflow *_subflow_ctor (void);
static void _subflow_dtor (Subflow * this);
static void _ruin_subflow (gpointer * subflow);
static Subflow *_make_subflow (guint8 id, MPRTPSPath * path);
static void reset_subflow (Subflow * this);
static void _add_goodput(Subflow* this, guint64 goodput);
static void _obsolate_goodput(Subflow *this);
static void _half_goodput(Subflow *this);
static guint16 _uint16_diff (guint16 a, guint16 b);
static guint32 _uint32_diff (guint32 a, guint32 b);
static gint _cmp_for_maxtree (guint64 x, guint64 y);
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
  this->splitctrler_index = 0;
  this->splitctrler_moments = g_malloc0(sizeof(SplitCtrlerMoment) * 2);
  this->changed_num = 0;
  this->pacing = FALSE;
  this->RTT_max = 5 * GST_SECOND;
  this->last_recalc_time = gst_clock_get_time(this->sysclock);
  this->subflow_delays_tree = make_bintree(_cmp_for_maxtree);
  this->subflow_delays_index = 0;
  this->required_fi_wait = gst_clock_get_time(this->sysclock) + 20 * GST_SECOND;
  this->event = SPLITCTRLER_EVENT_FI;
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
  _refresh_goodput (this);
  if(_irt0(this)->RTT) this->RTT = _irt0(this)->RTT;
  this->ir_moments_index = (this->ir_moments_index + 1) % MAX_SUBFLOW_MOMENT_NUM;
  memset ((gpointer) _irt0 (this), 0, sizeof (IRMoment));
//  _st0(this)->RTT                   = _irt1(this)->RTT;

  _irt0(this)->early_discarded_bytes_sum = _irt1(this)->early_discarded_bytes_sum;
  _irt0(this)->late_discarded_bytes_sum  = _irt1(this)->late_discarded_bytes_sum;
  _irt0(this)->time                      = gst_clock_get_time(this->sysclock);
  _irt0(this)->state                     = _irt1(this)->state;

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
      _riport_processing_rrblock_processor (this,
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
        case GST_RTCP_XR_SKEW_BLOCK_TYPE_IDENTIFIER:
          _report_processing_xr_skew_block_processor (this,
                                                      subflow,
                                                      &block->xr_skew);
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
_riport_processing_rrblock_processor (SndEventBasedController *this,
                                      Subflow * subflow,
                                      GstRTCPRRBlock * rrb)
{
  guint64 LSR, DLSR;
//  GstClockTime now;
  guint32 LSR_read, DLSR_read, HSSN_read;
  guint8 fraction_lost;
  Event event;

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

  //--------------------------
  //evaluating
  //--------------------------
  event = EVENT_FI;
  if(subflow->ir_moments_num < 2){
    goto fire;
  }
  if(subflow->imprecise){
    goto done;
  }
//  g_print("S%d RTT: %lu\n", subflow->id, _irt0(subflow)->RTT);
  if(PATH_RTT_MAX_TRESHOLD < _irt0(subflow)->RTT && ++subflow->consecutive_late_RTT> 2){
    event = EVENT_LATE;
    goto fire;
  }
  subflow->consecutive_late_RTT = 0;
  switch(_irt0(subflow)->state){
    case MPRTPS_PATH_STATE_NON_CONGESTED:
      if(_irt2(subflow)->lost_rate > 0. &&
         _irt1(subflow)->lost_rate > 0. &&
         _irt0(subflow)->lost_rate > 0.)
      {
          event = EVENT_CONGESTION;
          goto fire;
      }else if(_irt0(subflow)->lost_rate > 0.){
        if((_irt4(subflow)->lost_rate > 0. ||
            _irt3(subflow)->lost_rate > 0. ||
            _irt2(subflow)->lost_rate > 0. ||
            _irt1(subflow)->lost_rate > 0.) &&
            !_irt2(subflow)->late_discarded_bytes &&
            !_irt1(subflow)->late_discarded_bytes &&
            !_irt0(subflow)->late_discarded_bytes){
            event = EVENT_LOSSY;
            goto fire;
        }
      }
      else
      {
          event = EVENT_FI;
          goto fire;
      }
    break;
    case MPRTPS_PATH_STATE_LOSSY:
      if(subflow->actual_rate < 0.05 && !mprtps_path_is_monitoring(subflow->path)){
          event = EVENT_INSUFFICIENT;
          goto fire;
      }
      if(_irt2(subflow)->lost_rate > 0. &&
         _irt1(subflow)->lost_rate > 0. &&
         _irt0(subflow)->lost_rate > 0.)
      {
          event = EVENT_CONGESTION;
          goto fire;
      }else if(_irt0(subflow)->lost_rate > 0.){
          event = EVENT_DISTORTION;
          goto fire;
      }else{
        if(_irt0(subflow)->lost_rate == 0. &&
           _irt1(subflow)->lost_rate == 0. &&
           _irt2(subflow)->lost_rate == 0. &&
           _irt3(subflow)->lost_rate == 0. &&
           _irt4(subflow)->lost_rate == 0.)
        {
            //monitoring off
            event = EVENT_SETTLEMENT;
            goto fire;
        }
      }
      break;
    case MPRTPS_PATH_STATE_CONGESTED:
      if(subflow->actual_rate < 0.05 && !mprtps_path_is_monitoring(subflow->path)){
          event = EVENT_INSUFFICIENT;
          goto fire;
      }
      if(_irt0(subflow)->lost_rate == 0. &&
         _irt1(subflow)->lost_rate == 0. &&
         _irt2(subflow)->lost_rate == 0. &&
         _irt3(subflow)->lost_rate == 0. &&
         _irt4(subflow)->lost_rate == 0.)
      {
          event = EVENT_SETTLEMENT;
          goto fire;
      }else if(_irt0(subflow)->lost_rate > 0.){
          event = EVENT_DISTORTION;
          goto fire;
      }
    break;
    default:
      break;
  }
  fire:
  _subflow_fire(this, subflow, event);
  done:
  return;
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
_report_processing_xr_skew_block_processor (SndEventBasedController *this,
                                            Subflow * subflow,
                                            GstRTCPXR_Skew * xrb)
{
  gboolean  skew_flag;
  guint32  characterization_value;
  guint16  min,max;
  GstClockTime now;

  now = gst_clock_get_time(this->sysclock);
  gst_rtcp_xr_skew_getdown (xrb,
                            NULL,        //interval_metric
                            &skew_flag,  //is it skew or delay?
                            NULL,        //ssrc of the stream
                            NULL,        //percentile of the characterization value
                            NULL,        //bytes as a base of the characterization
                            &characterization_value, //the characterization value itself
                            &min,        //minimum value relatively
                            &max         //maximum value relatively
                              );
//  gst_print_rtcp_xr_skew(xrb);

  if(!skew_flag){
    _irt0(subflow)->delay =     get_epoch_time_from_ntp_in_ns(characterization_value);
    _irt0(subflow)->min_delay = get_epoch_time_from_ntp_in_ns(characterization_value - (min<<16));
    _irt0(subflow)->max_delay = get_epoch_time_from_ntp_in_ns(characterization_value + (max<<16));
    goto evaluate_delay;
  }else{
    _irt0(subflow)->skew =     get_epoch_time_from_ntp_in_ns(characterization_value  & 0xFFFF0000);
    _irt0(subflow)->min_skew = get_epoch_time_from_ntp_in_ns((characterization_value & 0xFFFF0000) - (min<<16));
    _irt0(subflow)->max_skew = get_epoch_time_from_ntp_in_ns((characterization_value & 0xFFFF0000) + (max<<16));
    goto evaluate_skew;
  }

  //--------------------------
  //evaluating
  //--------------------------
evaluate_skew:
  if(subflow->ir_moments_num < 5){
    mprtps_path_set_marker(subflow->path, MPRTPS_PATH_MARKER_NEUTRAL);
    goto done;
  }
  if(!_is_subflow_stable(subflow)){
    _irt3(subflow)->skew_diff = 0;
    mprtps_path_set_marker(subflow->path, MPRTPS_PATH_MARKER_NEUTRAL);
    goto done;
  }
  if(!_irt3(subflow)->skew_diff ||
     !_irt2(subflow)->skew_diff ||
     !_irt1(subflow)->skew_diff)
    {
      mprtps_path_set_marker(subflow->path, MPRTPS_PATH_MARKER_NEUTRAL);
      goto done;
    }
  _irt0(subflow)->skew_diff = (gint64) _irt0(subflow)->skew - (gint64) _irt1(subflow)->skew;

  if(_irt3(subflow)->skew_diff < _irt2(subflow)->skew_diff &&
     _irt2(subflow)->skew_diff < _irt1(subflow)->skew_diff &&
     _irt1(subflow)->skew_diff < _irt0(subflow)->skew_diff)
  {
    mprtps_path_set_marker(subflow->path, MPRTPS_PATH_MARKER_OVERUSED);
    subflow->marked = now;
  }
  else if(_irt0(subflow)->skew_diff < _irt1(subflow)->skew_diff &&
          _irt1(subflow)->skew_diff < _irt2(subflow)->skew_diff &&
          _irt2(subflow)->skew_diff < _irt3(subflow)->skew_diff)
  {
    mprtps_path_set_marker(subflow->path, MPRTPS_PATH_MARKER_UNDERUSED);
    subflow->marked = now;
  }
  else if(subflow->marked < now - 15 * GST_SECOND)
  {
      mprtps_path_set_marker(subflow->path, MPRTPS_PATH_MARKER_NEUTRAL);
  }

  goto done;

evaluate_delay:
  {
    guint64 min_delay, max_delay, delay, treshold;
    guint32 jitter;

    delay = _irt0(subflow)->delay;
    //evaluate path
    {
      gdouble gamma1,gamma2;
      GstClockTime lt_delay, lt_min_dealy, lt_max_delay, prev_delay;
      mprtps_path_get_delays(subflow->path,
                             &lt_delay,
                             &prev_delay,
                             &lt_min_dealy,
                             &lt_max_delay);
      gamma1 = (gdouble)prev_delay / (gdouble)delay;
      gamma2 = (gdouble)lt_delay / (gdouble)delay;
      if(gamma1 < .75 || gamma2 < .75){
        _subflow_fire(this, subflow, EVENT_DISTORTION);
      }
    }
    mprtps_path_add_delay(subflow->path, delay);
    if(bintree_get_num(this->subflow_delays_tree) < 2){
      _refresh_subflow_delays(this, delay);
      goto done;
    }

    jitter = _irt0(subflow)->jitter>>1;
    min_delay = bintree_get_bottom_value(this->subflow_delays_tree);
    max_delay = bintree_get_top_value(this->subflow_delays_tree);
    treshold = (!subflow->imprecise)?DELAY_SKEW_ACTIVE_TRESHOLD:DELAY_SKEW_DEACTIVE_TRESHOLD;
    //minimum
    if(treshold < max_delay && delay + jitter < max_delay - treshold) goto imprecise;
    //maximum
    if(jitter < delay){
      if(min_delay + treshold < delay - jitter) goto imprecise;
    }
    else{
      if(min_delay + treshold < delay) goto imprecise;
    }
    subflow->imprecise_counter = 0;
    _refresh_subflow_delays(this, delay);
    if(subflow->imprecise) _subflow_fire(this, subflow, EVENT_ACTIVATE);
    goto done;
  imprecise:
    if(++subflow->imprecise_counter < 3){
      GST_WARNING("Imprecise delay");
      goto done;
    }
    if(!subflow->imprecise) _subflow_fire(this, subflow, EVENT_DEACTIVATE);
    subflow->imprecise_counter = 0;
  }
  goto done;
done:
  return;
}

void _refresh_subflow_delays(SndEventBasedController *this, guint64 delay)
{
  this->subflow_delays[this->subflow_delays_index] = delay;
  bintree_insert_value(this->subflow_delays_tree, this->subflow_delays[this->subflow_delays_index]);
  if(this->subflow_delays[++this->subflow_delays_index] > 0){
    bintree_delete_value(this->subflow_delays_tree, this->subflow_delays[this->subflow_delays_index]);
  }
}


void
_refresh_goodput (Subflow * this)
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
    this->actual_goodput = _irt0(this)->goodput = goodput;
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
    _obsolate_goodput(this);
    if(!mprtps_path_is_monitoring(this->path)){
      _add_goodput(this, goodput);

    }
//    g_print("Sub-%d-bid: %f =  (%f*%d + %f)/%d\n",
//                  this->id,
//                  _irt0 (this)->bid,
//                  _irt1 (this)->bid,
//                  ck,
//                  goodput,
//                  ck+1);
  }

  //sending_bid
  {

  }
}


void
_subflow_fire (SndEventBasedController * this, Subflow * subflow, Event event)
{
  MPRTPSPath *path;
  MPRTPSPathState path_state;
  GstClockTime now;
  Action action = NULL;
  path = subflow->path;
  action = _perform_keep;
  if(subflow->ir_moments_num < 2){
    goto done;
  }
//  g_print ("FIRE->%d-state:%d:%d\n", subflow->id, mprtps_path_get_state (path), event);

  if(!subflow->imprecise && event == EVENT_DEACTIVATE){
    subflow->imprecise = TRUE;
    mprtps_path_set_passive (path);
    action = _perform_fall;
    goto done;
  }else if(subflow->imprecise && event == EVENT_ACTIVATE){
    subflow->imprecise = FALSE;
    mprtps_path_set_active (path);
    action = _perform_restore;
    goto done;
  }
  if(subflow->imprecise){
    goto done;
  }


  path_state = mprtps_path_get_state (path);
  now = gst_clock_get_time(this->sysclock);

  //passive state
  if (path_state == MPRTPS_PATH_STATE_PASSIVE) {
    switch (event) {
      case EVENT_SETTLEMENT:
        mprtps_path_set_active (path);
        mprtps_path_set_trial_begin (path);
        subflow->tr = 1;
        action = _perform_restore;
        break;
      case EVENT_FI:
      default:
        break;
    }
    goto done;
  }
  //whichever state we are the LATE event means the same
  if (event == EVENT_LATE) {
    mprtps_path_set_passive (path);
    action = _perform_fall;
    goto done;
  }

  if (path_state == MPRTPS_PATH_STATE_NON_CONGESTED) {
    switch (event) {
      case EVENT_CONGESTION:
        this->congestion_is_ongoing = TRUE;
        this->congestion_happened = now;
        mprtps_path_set_congested (path);
        action = _perform_reduce;
        break;
      case EVENT_DISTORTION:
        this->distortion_happened = now;
        mprtps_path_set_trial_end (path);
        _half_goodput(subflow);
        action = _perform_mitigate;
        break;
      case EVENT_LOSSY:
        this->congestion_is_ongoing = TRUE;
        this->congestion_happened = now;
        mprtps_path_set_lossy (path);
        action = _perform_mitigate;
        subflow->required_wait = 3;
        break;
      case EVENT_FI:
      default:
        if(mprtps_path_is_in_trial(path))
        {
          this->restore_happened = now;
          action = _perform_restore;
        }
        else if(subflow->increasable)
        {
          action = _perform_increase;
          subflow->increasable = FALSE;
        }
        else
        {
          subflow->control_signal = 1.;
          action = _perform_keep;
        }
        break;
    }
    goto done;
  }
  //lossy path
  if (path_state == MPRTPS_PATH_STATE_LOSSY) {
    switch (event) {
      case EVENT_INSUFFICIENT:
        subflow->required_wait = 5;
        action = _perform_fall;
        mprtps_path_turn_monitoring_on(path);
        this->congestion_is_ongoing = FALSE;
        break;
      case EVENT_SETTLEMENT:
        if(subflow->required_wait > 0){
          --subflow->required_wait;
          action = _perform_keep;
          goto done;
        }
        mprtps_path_turn_monitoring_off(path);
        mprtps_path_set_non_lossy (path);
        mprtps_path_set_trial_begin (path);
        this->restore_happened = now;
        this->congestion_is_ongoing = FALSE;
        subflow->tr = 1;
        action = _perform_restore;
        break;
      case EVENT_CONGESTION:
        if(subflow->required_wait > 0){
          --subflow->required_wait;
          action = _perform_mitigate;
          goto done;
        }
        mprtps_path_set_non_lossy (path);
        mprtps_path_set_congested (path);
        action = _perform_reduce;
        subflow->required_wait = 3;
        break;
      case EVENT_DISTORTION:
        action = _perform_mitigate;
        break;
      case EVENT_FI:
      default:
        if(subflow->required_wait > 0) --subflow->required_wait;
        action = _perform_keep;
        break;
    }
    goto done;
  }
  //the path is congested
  switch (event) {
    case EVENT_INSUFFICIENT:
      action = _perform_fall;
      mprtps_path_turn_monitoring_on(path);
      this->congestion_is_ongoing = FALSE;
      subflow->required_wait = 5;
      break;
    case EVENT_SETTLEMENT:
      if(subflow->required_wait > 0){
        --subflow->required_wait;
        action = _perform_keep;
        goto done;
      }
      mprtps_path_turn_monitoring_off(path);
      mprtps_path_set_non_congested (path);
      mprtps_path_set_non_lossy (path);
      mprtps_path_set_trial_begin (path);
      this->restore_happened = now;
      this->congestion_is_ongoing = FALSE;
      subflow->tr = 1;
      action = _perform_restore;
      break;
    case EVENT_DISTORTION:
      action = _perform_reduce;
      break;
    case EVENT_FI:
    default:
      if(subflow->required_wait > 0) --subflow->required_wait;
      action = _perform_keep;
      break;
  }

done:
  if(action) action (this, subflow);
  _irt0(subflow)->state = mprtps_path_get_state (path);
  return;
}

guint32 _get_max_goodput(Subflow * subflow)
{
  if(!bintree_get_num(subflow->goodputs)){
    return SUBFLOW_DEFAULT_GOODPUT;
  }
  return bintree_get_top_value(subflow->goodputs);
}

guint32 _get_min_goodput(Subflow * subflow)
{
  if(!bintree_get_num(subflow->goodputs)){
    return SUBFLOW_DEFAULT_GOODPUT;
  }
  return bintree_get_bottom_value(subflow->goodputs);
}

void
_perform_keep (SndEventBasedController * this, Subflow * subflow)
{
  ++subflow->consecutive_keep;
  subflow->get_goodput = _get_max_goodput;
}


void
_perform_restore (SndEventBasedController * this, Subflow * subflow)
{
  subflow->consecutive_keep = 0;
  this->bids_recalc_requested = TRUE;
  subflow->control_signal = (gfloat)subflow->tr / (gfloat)(subflow->tr + 1);
  subflow->get_goodput = _get_max_goodput;
  if(++subflow->tr > 4){
    this->restored_happened =
          subflow->restored = gst_clock_get_time(this->sysclock);
    mprtps_path_set_trial_end(subflow->path);
  }
}

void
_perform_increase (SndEventBasedController * this, Subflow * subflow)
{
  subflow->consecutive_keep = 0;
  this->bids_recalc_requested = TRUE;
  subflow->get_goodput = _get_max_goodput;
  subflow->control_signal = 1.2;
}

void
_perform_mitigate (SndEventBasedController * this, Subflow * subflow)
{
  subflow->consecutive_keep = 0;
  subflow->control_signal *= .6;
  this->bids_recalc_requested = TRUE;
  subflow->get_goodput = _get_max_goodput;
  GST_DEBUG_OBJECT (this, "Mitigate action is performed with "
      "Event based controller on subflow %d", subflow->id);
}

void
_perform_reduce (SndEventBasedController * this, Subflow * subflow)
{
  subflow->consecutive_keep = 0;
  subflow->control_signal *= .3;
  subflow->get_goodput = _get_max_goodput;
  this->bids_recalc_requested = TRUE;
  GST_DEBUG_OBJECT (this, "Mitigate action is performed with "
      "Event based controller on subflow %d", subflow->id);
}

void
_perform_fall (SndEventBasedController * this, Subflow * subflow)
{
  subflow->consecutive_keep = 0;
  subflow->control_signal = 0.;
  subflow->get_goodput = _get_max_goodput;
  stream_splitter_rem_path (this->splitter, subflow->id);
  this->bids_commit_requested = TRUE;
  //save
  GST_DEBUG_OBJECT (this, "Fall action is performed with "
      "Event based controller on subflow %d", subflow->id);
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
//    g_print("S:%d->REPORT_TOO_LATE\n", this->id);
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
      _subflow_fire (this, subflow, EVENT_LATE);
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
  this->scheduler_signaling(this->scheduler, _ct1(this)->goodput * 8. / 1000.);
  goto done;
underused:
  this->scheduler_signaling(this->scheduler, 0);
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
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  GstClockTime now;
  gboolean all_report_arrived = TRUE;
  now = gst_clock_get_time(this->sysclock);
  //reset _cthis
  _reset_c(this);
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    subflow = (Subflow *) val;
    if(!mprtps_path_is_active(subflow->path)){
      continue;
    }
    all_report_arrived &= _ct0(this)->time < _irt0(subflow)->time;
    _refresh_c(this, subflow);
  }
  if(all_report_arrived || _ct0(this)->time < now - 15 * GST_SECOND)
    this->bids_recalc_requested = TRUE;
  if (!this->bids_recalc_requested) goto recalc_done;

  this->bids_recalc_requested = FALSE;
  this->bids_commit_requested = TRUE;
  _recalc_bids(this);
recalc_done:
  if (!this->bids_commit_requested) goto process_done;
  this->bids_commit_requested = FALSE;
  stream_splitter_commit_changes (this->splitter);
  this->rate_is_state = _is_rate_stable(this);
  if(0) _get_lowest_cons_keep_from_nc_subflow(this);
  if(0) _get_first_subflow(this, MPRTPS_PATH_STATE_NON_CONGESTED);
  this->event = _get_system_event(this);
  _step_c(this);
process_done:
  return;
}

void _recalc_bids(SndEventBasedController * this)
{
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  guint32 sb;
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    subflow = (Subflow *) val;
    if(!mprtps_path_is_active(subflow->path)){
      continue;
    }
    sb = subflow->control_signal * subflow->get_goodput(subflow);
//    g_print("Subflow %d sending bid %u =  %f * %u\n",
//            subflow->id,
//            sb,
//            subflow->control_signal,
//            _get_max_goodput(subflow));
    stream_splitter_setup_sending_bid(this->splitter,
                                      subflow->id,
                                      sb);
    //    g_print("%d,",_irt2(subflow)->goodput < _irt1(subflow)->goodput ? 1 : 0);
//        g_print("%f,",subflow->actual_goodput);
  }
}


gboolean _is_rate_stable(SndEventBasedController * this)
{
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  gdouble actual_rate,diff;
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    subflow = (Subflow *) val;
    if(!mprtps_path_is_active(subflow->path)){
      actual_rate = 0.;
    }else{
      actual_rate = stream_splitter_get_sending_rate(this->splitter, subflow->id);
    }
    diff = subflow->actual_rate - actual_rate;
    if(diff < 0.) diff *= -1.;
    this->rate_diff = diff;
    subflow->actual_rate = actual_rate;
  }
//  g_print("Rate diff: %f\n", this->rate_diff);
  return this->rate_diff < 0.05 ? TRUE : FALSE;
}

gboolean _is_goodput_stable(SndEventBasedController * this)
{
  if(_ct0(this)->goodput == 0.) return FALSE;
  if(_ct0(this)->goodput < _ct1(this)->goodput * .95) return FALSE;
  if(_ct1(this)->goodput * 1.05 < _ct0(this)->goodput) return FALSE;
  return TRUE;
}

void _reset_c(SndEventBasedController *this)
{
  _ct0(this)->subflows.num = 0;
  _ct0(this)->subflows.nc = 0;
  _ct0(this)->subflows.c = 0;
  _ct0(this)->subflows.l = 0;
  _ct0(this)->subflows.monitored = 0;
  _ct0(this)->goodput = 0.;
}

void _refresh_c(SndEventBasedController *this, Subflow *subflow)
{
  MPRTPSPathState path_state;
  path_state = mprtps_path_get_state(subflow->path);
  if(path_state == MPRTPS_PATH_STATE_NON_CONGESTED){
      ++_ct0(this)->subflows.nc;
  }else if(path_state == MPRTPS_PATH_STATE_LOSSY){
      ++_ct0(this)->subflows.l;
  }else if(path_state == MPRTPS_PATH_STATE_CONGESTED){
      ++_ct0(this)->subflows.c;
  }
  if(mprtps_path_is_monitoring(subflow->path)){
    ++_ct0(this)->subflows.monitored;
  }
  ++_ct0(this)->subflows.num;
  _ct0(this)->goodput += subflow->actual_goodput;
}

SplitCtrlerEvent _get_system_event(SndEventBasedController *this)
{
  GstClockTime now;
  now = gst_clock_get_time(this->sysclock);

  if(!_ct0(this)->subflows.num) {
 //      g_print("SYSTEM STATE CHECK: NO SUBFLOW\n");
       goto fi;
   }
   if(!_ct0(this)->subflows.nc) {
 //      g_print("SYSTEM STATE CHECK: NO NC SUBFLOW\n");
       goto overused;
   }
   if(now < this->required_fi_wait){
     goto fi;
   }
   if(this->congestion_is_ongoing){
       goto fi;
   }
   if(now - 15 * GST_SECOND < this->distortion_happened){
     goto fi;
   }
   if(now - 15 * GST_SECOND < this->congestion_happened){
     if(this->congestion_happened < this->distortion_happened)
     {
       goto overused;
     }
     goto fi;
   }
   if(this->restored_happened < this->restore_happened){
     goto fi;
   }

   if(0 < this->restore_happened && this->restore_happened < 15 * GST_SECOND){
     Subflow *subflow;
     subflow = _get_first_retored_subflow(this);
     if(!subflow) goto fi;
     if(mprtps_path_get_marker(subflow->path) == MPRTPS_PATH_MARKER_UNDERUSED){
       subflow->increasable = TRUE;
       this->required_fi_wait = now + 10 * GST_SECOND;
     }
   }

   if(_is_goodput_stable(this) && _is_rate_stable(this)){
     goto underused;
   }

fi:
   return SPLITCTRLER_EVENT_FI;
underused:
  if(now < this->required_underused_wait) goto fi;
  this->required_underused_wait = now + 20 * GST_SECOND;
  return SPLITCTRLER_EVENT_UNDERUSED;
overused:
  if(now < this->required_overused_wait) goto fi;
  this->required_overused_wait = now + 20 * GST_SECOND;
  return SPLITCTRLER_EVENT_OVERUSED;

}

void _step_c(SndEventBasedController *this)
{
  this->splitctrler_index = 1 - this->splitctrler_index;
  memset ((gpointer) _ct0 (this), 0, sizeof (SplitCtrlerMoment));
  _ct0(this)->time = gst_clock_get_time(this->sysclock);
}

Subflow* _get_first_subflow(SndEventBasedController *this, MPRTPSPathState state)
{
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    subflow = (Subflow *) val;
    if(mprtps_path_get_state(subflow->path) == state)
      goto done;
  }
  subflow = NULL;
done:
  return subflow;
}


guint32 _get_lowest_cons_keep_from_nc_subflow(SndEventBasedController *this)
{
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  guint32 result = 1<<31;
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    subflow = (Subflow *) val;
    if(mprtps_path_get_state(subflow->path) != MPRTPS_PATH_STATE_NON_CONGESTED)
      continue;
    result = MIN(result, subflow->consecutive_keep);
  }
  if(result == (1<<31)) result = 0;
  return result;
}



Subflow* _get_first_retored_subflow(SndEventBasedController *this)
{
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
  {
    subflow = (Subflow *) val;
    if(mprtps_path_get_state(subflow->path) != MPRTPS_PATH_STATE_NON_CONGESTED)
      continue;
    if(gst_clock_get_time(this->sysclock) - 15 * GST_SECOND < subflow->restored){
      goto done;
    }
  }
  subflow = NULL;
done:
  return subflow;
}

gboolean _is_subflow_stable(Subflow*this)
{
  if(mprtps_path_get_state(this->path) != MPRTPS_PATH_STATE_NON_CONGESTED)
    return FALSE;
  if(this->consecutive_keep < 3) return FALSE;
  return TRUE;
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
  mprtps_path_set_marker(result->path, MPRTPS_PATH_MARKER_NEUTRAL);
  result->id = id;
  result->joined_time = gst_clock_get_time (result->sysclock);

  result->ir_moments =
      (IRMoment *) g_malloc0 (sizeof (IRMoment) * MAX_SUBFLOW_MOMENT_NUM);
  result->ir_moments_index = 0;
  result->control_signal = 1.;
  result->get_goodput = _get_max_goodput;
  result->ricalcer = make_ricalcer(TRUE);
  result->goodputs = make_bintree(_cmp_for_maxtree);
  memset(result->goodputs_arrived, 0, sizeof(GstClockTime)*16);
  memset(result->goodputs_values, 0, sizeof(guint64)*16);
  reset_subflow (result);
  return result;
}

void
reset_subflow (Subflow * this)
{
  gint i;
  for (i = 0; i < MAX_SUBFLOW_MOMENT_NUM; ++i) {
    memset (this->ir_moments, 0, sizeof (IRMoment) * MAX_SUBFLOW_MOMENT_NUM);
  }
  bintree_reset(this->goodputs);
  this->required_wait = 0;
}

void _add_goodput(Subflow* this, guint64 goodput)
{
  if(!goodput) return;
  this->goodputs_write = (this->goodputs_write + 1) & 15;
  if(this->goodputs_write == this->goodputs_read){
    guint64 value;
    value = this->goodputs_values[this->goodputs_read];
    this->goodputs_read = (this->goodputs_read + 1) & 15;
    bintree_delete_value(this->goodputs, value);
  }
  this->goodputs_arrived[this->goodputs_write] = gst_clock_get_time(this->sysclock);
  this->goodputs_values[this->goodputs_write] = goodput;
  bintree_insert_value(this->goodputs, goodput);
}

void _obsolate_goodput(Subflow *this)
{
  GstClockTime now;
  guint64 value;
  now =  gst_clock_get_time(this->sysclock);
again:
  if(now - 60 * GST_SECOND < this->goodputs_arrived[this->goodputs_read]) return;
  if(this->goodputs_read == this->goodputs_write) return;
  value = this->goodputs_values[this->goodputs_read];
  bintree_delete_value(this->goodputs, value);
  this->goodputs_read = (this->goodputs_read + 1) & 15;
  goto again;
}


void _half_goodput(Subflow *this)
{
  guint64 value;
  guint counter;
  counter = bintree_get_num(this->goodputs);
  counter>>=1;
again:
  if(!counter) return;
  if(this->goodputs_read == this->goodputs_write) return;
  value = this->goodputs_values[this->goodputs_read];
  bintree_delete_value(this->goodputs, value);
  this->goodputs_read = (this->goodputs_read + 1) & 15;
  --counter;
  goto again;
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

gint
_cmp_for_maxtree (guint64 x, guint64 y)
{
  return x == y ? 0 : x < y ? -1 : 1;
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
