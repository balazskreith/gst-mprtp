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

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define MAX_RIPORT_INTERVAL (5 * GST_SECOND)
#define RIPORT_TIMEOUT (3 * MAX_RIPORT_INTERVAL)
#define PATH_RTT_MAX_TRESHOLD (800 * GST_MSECOND)
#define PATH_RTT_MIN_TRESHOLD (600 * GST_MSECOND)
GST_DEBUG_CATEGORY_STATIC (sefctrler_debug_category);
#define GST_CAT_DEFAULT sefctrler_debug_category
#define _ct0(this) (this->records+this->records_index)
#define _ct1(this) (this->records + (this->records_index == 0 ? this->records_max-1 : this->records_index-1))
#define _st0(this) (this->records+this->records_index)
#define _st1(this) (this->records + (this->records_index == 0 ? this->records_max-1 : this->records_index-1))
#define _st2(this) _st(this, -2)


G_DEFINE_TYPE (SndEventBasedController, sefctrler, G_TYPE_OBJECT);

typedef struct _Subflow Subflow;
typedef struct _SubflowRecord SubflowRecord;


typedef enum
{
  EVENT_FI,
  EVENT_DISTORTION,
  EVENT_SETTLED,
  EVENT_CONGESTION,
  EVENT_LATE,
  EVENT_LOSTS,
} Event;


struct _SubflowRecord
{
  GstClockTime moment;

  //Raw data - RR
  guint8 fraction_lost;
  guint32 lost_packets_num;
  guint32 jitter;
  guint32 LSR;
  guint32 DLSR;

  //Delta data
  guint32 sent_bytes;
  guint16 HSSN;
  guint32 early_discarded_bytes;
  guint32 late_discarded_bytes;

  //derived data
  gdouble goodput;
  gdouble lost_rate;
  GstClockTime RTT;
};

struct _Subflow
{
  MPRTPSPath *path;
  guint8 id;
  GstClock *sysclock;
  GstClockTime joined_time;

  //Records
  SubflowRecord *records;
  gint records_index;
  gint records_max;

  SubflowRecord saved_record;
  GstClockTime saved_time;
  //Monotonicly inreased data
  guint16 HSSN;
  guint16 cycle_num;
  guint32 late_discarded_bytes_sum;
  guint32 early_discarded_bytes_sum;
  guint32 sent_packet_num;
  guint32 sent_payload_bytes;

  //Report calculations
  guint32 received_receiver_reports_num;
  GstClockTime last_receiver_riport_time;
  gdouble avg_rtcp_size;
  gboolean first_report_calculated;
  GstClockTime normal_report_time;

};



struct _ControllerRecord
{
  struct
  {
    gdouble c, mc, nc;
  } goodput;
  struct
  {
    guint c, mc, nc;
  } subflows;
  gfloat max_nc_goodput;
};

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void sefctrler_finalize (GObject * object);
static void sefctrler_run (void *data);

static guint16 _uint16_diff (guint16 a, guint16 b);
static guint32 _uint32_diff (guint32 a, guint32 b);
static void sefctrler_receive_mprtcp (gpointer this, GstBuffer * buf);
static void _report_processing_selector (Subflow * this,
    GstMPRTCPSubflowBlock * block);
static void
_riport_processing_rrblock_processor (Subflow * this, GstRTCPRRBlock * rrb);
static void
_report_processing_xrblock_processor (Subflow * this, GstRTCPXR_RFC7243 * xrb);
GstBuffer *_get_mprtcp_sr_block (SndEventBasedController * this,
    Subflow * subflow, guint16 * buf_length);
Event _check_state (Subflow * this);
static void _setup_sr_riport (Subflow * this, GstRTCPSR * sr, guint32 ssrc);
static void _fire (SndEventBasedController * this, Subflow * subflow,
    Event event);
static void
_send_mprtcp_sr_block (SndEventBasedController * this, Subflow * subflow);
static void
_controller_record_add_subflow (SndEventBasedController * this,
    Subflow * subflow);

static gboolean _do_report_now (Subflow * this);
static void _recalc_report_time (Subflow * this);

static Event _check_report_timeout (Subflow * this);
static void _refresh_actual_record (Subflow * this);
static void sefctrler_riport_can_flow (gpointer this);
void _cstep (SndEventBasedController * this);

//----------------------------------------------------------------
//----------------- subflow specific functions -------------------
//----------------------------------------------------------------
static Subflow *_subflow_ctor (void);
static void _subflow_dtor (Subflow * this);
static void _ruin_subflow (gpointer * subflow);
static Subflow *_make_subflow (guint8 id, MPRTPSPath * path);
static void reset_subflow (Subflow * this);

static SubflowRecord *_st (Subflow * this, gint moment);
void _sstep (Subflow * this);

//Actions
typedef void (*Action) (SndEventBasedController *, Subflow *);
static void _action_recalc (SndEventBasedController * this, Subflow * subflow);
static void _action_fall (SndEventBasedController * this, Subflow * subflow);;
static void _action_reload (SndEventBasedController * this, Subflow * subflow);;
static void _action_keep (SndEventBasedController * this, Subflow * subflow);;

static void _sefctrler_recalc (SndEventBasedController * this);
static gfloat _get_mitigation (SndEventBasedController * this,
    Subflow * subflow);
static gfloat _get_reduction (SndEventBasedController * this,
    Subflow * subflow);
static gfloat _get_compensation (SndEventBasedController * this,
    Subflow * subflow);


static void sefctrler_rem_path (gpointer controller_ptr, guint8 subflow_id);
static void sefctrler_add_path (gpointer controller_ptr, guint8 subflow_id,
    MPRTPSPath * path);
static void sefctrler_pacing (gpointer controller_ptr, gboolean allowed);
static gboolean sefctrler_is_pacing (gpointer controller_ptr);
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
  this->riport_is_flowable = FALSE;
  this->records_max = 5;
  this->records =
      (ControllerRecord *) g_malloc0 (sizeof (ControllerRecord) *
      this->records_max);
  this->changed_num = 0;
  this->pacing = FALSE;
  g_rw_lock_init (&this->rwmutex);
  g_rec_mutex_init (&this->thread_mutex);
  this->thread = gst_task_new (sefctrler_run, this, NULL);
  gst_task_set_lock (this->thread, &this->thread_mutex);
  gst_task_start (this->thread);
}

void
sefctrler_run (void *data)
{
  GstClockTime now, next_scheduler_time;
  SndEventBasedController *this;
  GHashTableIter iter;
  gpointer key, val;
  Event event;
  Subflow *subflow;
  GstClockID clock_id;
  gboolean calc_controller_record = FALSE;
  this = SEFCTRLER (data);
  THIS_WRITELOCK (this);
  if (this->new_report_arrived) {
    calc_controller_record = TRUE;
    _cstep (this);
    this->new_report_arrived = FALSE;
  }

  now = gst_clock_get_time (this->sysclock);
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    subflow = (Subflow *) val;

    if ((event = _check_report_timeout (subflow)) == EVENT_LATE) {
      _fire (this, subflow, event);
      continue;
    }

    if (calc_controller_record) {
      _controller_record_add_subflow (this, subflow);
    }

    if (this->riport_is_flowable && _do_report_now (subflow)) {
      _send_mprtcp_sr_block (this, subflow);
      _recalc_report_time (subflow);

    }
  }
  if (this->bids_recalc_requested) {
    this->bids_recalc_requested = FALSE;
    _sefctrler_recalc (this);
    this->bids_commit_requested = TRUE;
  }
  if (this->bids_commit_requested) {
    this->bids_commit_requested = FALSE;
    stream_splitter_commit_changes (this->splitter);
  }
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
_send_mprtcp_sr_block (SndEventBasedController * this, Subflow * subflow)
{
  GstBuffer *buf;
  guint16 report_length = 0;

  buf = _get_mprtcp_sr_block (this, subflow, &report_length);
  this->send_mprtcp_packet_func (this->send_mprtcp_packet_data, buf);

  report_length += 12 /*MPRTCP REPOR HEADER */  +
      (28 << 3) /*UDP Header overhead */ ;

  subflow->avg_rtcp_size +=
      ((gfloat) report_length - subflow->avg_rtcp_size) / 4.;
}


void
sefctrler_add_path (gpointer ptr, guint8 subflow_id, MPRTPSPath * path)
{
  Subflow *lookup_result;
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
  g_hash_table_insert (this->subflows, GINT_TO_POINTER (subflow_id),
      _make_subflow (subflow_id, path));
  ++this->subflow_num;
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

void
sefctrler_set_callbacks (void (**riport_can_flow_indicator) (gpointer),
    void (**controller_add_path) (gpointer, guint8, MPRTPSPath *),
    void (**controller_rem_path) (gpointer, guint8),
    void (**controller_pacing) (gpointer, gboolean),
    gboolean (**controller_is_pacing)(gpointer))
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
sefctrler_receive_mprtcp (gpointer ptr, GstBuffer * buf)
{
  GstMPRTCPSubflowBlock *block;
  SndEventBasedController *this = SEFCTRLER (ptr);
  guint16 subflow_id;
  guint8 info_type;
  Subflow *subflow;
  GstMapInfo map = GST_MAP_INFO_INIT;
  Event event;
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

  _report_processing_selector (subflow, block);
  this->new_report_arrived = TRUE;
  event = _check_state (subflow);
//  g_print ("Subflow: %d Actual state: %d event: %d\n",
//      subflow->id, mprtps_path_get_state (subflow->path), event);
  if (event != EVENT_FI) {
    _fire (this, subflow, event);
  }

done:
  gst_buffer_unmap (buf, &map);
  THIS_WRITEUNLOCK (this);
}

void
sefctrler_riport_can_flow (gpointer ptr)
{
  SndEventBasedController *this;
  this = SEFCTRLER (ptr);
  GST_DEBUG_OBJECT (this, "RTCP riport can now flowable");
  THIS_WRITELOCK (this);
  this->riport_is_flowable = TRUE;
  THIS_WRITEUNLOCK (this);
}


//----------------------------------------------
// -------- Subflow related functions ----------
//----------------------------------------------
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
  result->records_max = 3;
  result->records =
      (SubflowRecord *) g_malloc0 (sizeof (struct _SubflowRecord) *
      result->records_max);
  result->records_index = 0;
  reset_subflow (result);
  return result;
}

void
reset_subflow (Subflow * this)
{
  gint i;
  for (i = 0; i < this->records_max; ++i) {
    memset (this->records, 0, sizeof (SubflowRecord) * this->records_max);
  }
}

SubflowRecord *
_st (Subflow * this, gint moment)
{
  gint index;
  index = this->records_index - (moment % this->records_max);
  if (index < 0)
    index = this->records_max - index;
  return this->records + index;
}

void
_sstep (Subflow * this)
{
  this->records_index = (this->records_index + 1) % this->records_max;
  memset ((gpointer) _st0 (this), 0, sizeof (SubflowRecord));
  ++this->received_receiver_reports_num;
}

void
_cstep (SndEventBasedController * this)
{
  this->records_index = (this->records_index + 1) % this->records_max;
  memset ((gpointer) _ct0 (this), 0, sizeof (ControllerRecord));
  ++this->changed_num;
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



void
_controller_record_add_subflow (SndEventBasedController * this,
    Subflow * subflow)
{
  MPRTPSPathState state;
  state = mprtps_path_get_state (subflow->path);
  switch (state) {
    case MPRTPS_PATH_STATE_NON_CONGESTED:
      _ct0 (this)->goodput.nc += _st0 (subflow)->goodput;
      ++_ct0 (this)->subflows.nc;
      break;
    case MPRTPS_PATH_STATE_MIDDLY_CONGESTED:
      _ct0 (this)->goodput.mc += _st0 (subflow)->goodput;
      ++_ct0 (this)->subflows.mc;
      break;
    case MPRTPS_PATH_STATE_CONGESTED:
      _ct0 (this)->goodput.c += _st0 (subflow)->goodput;
      ++_ct0 (this)->subflows.c;
      break;
    default:
      break;
  }
}


//------------------ Riport Processing and evaluation -------------------

void
_report_processing_selector (Subflow * this, GstMPRTCPSubflowBlock * block)
{
  guint8 pt;

  gst_rtcp_header_getdown (&block->block_header, NULL, NULL, NULL, &pt, NULL,
      NULL);

  if (pt == (guint8) GST_RTCP_TYPE_RR) {
    _sstep (this);
    _riport_processing_rrblock_processor (this, &block->receiver_riport.blocks);
    this->last_receiver_riport_time = gst_clock_get_time (this->sysclock);
  } else if (pt == (guint8) GST_RTCP_TYPE_XR) {
    _report_processing_xrblock_processor (this, &block->xr_rfc7243_riport);
  } else {
    GST_WARNING ("Sending flow control can not process report type %d.", pt);
  }
  _refresh_actual_record (this);
}

void
_riport_processing_rrblock_processor (Subflow * this, GstRTCPRRBlock * rrb)
{
  GstClockTime LSR, DLSR;
  GstClockTime now;
  guint32 LSR_read, DLSR_read, HSSN_read;
  guint16 HSSN;
  guint8 fraction_lost;
  //RiportProcessorRRProcessor *this_stage = _riport_processing_rrprocessor_;

  now = gst_clock_get_time (this->sysclock);
  //--------------------------
  //validating
  //--------------------------
  gst_rtcp_rrb_getdown (rrb, NULL, &fraction_lost, NULL, &HSSN_read, NULL,
      &LSR_read, &DLSR_read);

  HSSN = (guint16) (HSSN_read & 0x0000FFFF);
  _st0 (this)->HSSN = _uint16_diff (this->HSSN, HSSN);
  this->HSSN = HSSN;
  this->cycle_num = (guint16) ((HSSN_read & 0x0000FFFF) >> 16);
  LSR = (now & 0xFFFF000000000000ULL) | (((guint64) LSR_read) << 16);
  DLSR = (guint64) DLSR_read *GST_MSECOND;

  if (_st0 (this)->HSSN > 32767) {
    GST_WARNING_OBJECT (this, "Receiver report validation failed "
        "on subflow %d " "due to HSN difference inconsistency.", this->id);
    return;
  }

  if (this->received_receiver_reports_num && (LSR == 0 || DLSR == 0)) {
    return;
  }
  //--------------------------
  //processing
  //--------------------------
  if (LSR > 0) {
    guint64 diff = now - LSR;
    //g_print("Diff: %lu, DLSR: %lu\n", diff, DLSR);
    if (DLSR < diff) {
      _st0 (this)->RTT = diff - DLSR;
    }
  }
  _st0 (this)->fraction_lost = fraction_lost;

  _st0 (this)->lost_rate = ((gdouble) fraction_lost) / 256.;
//  if(fraction_lost > 0.) g_print("LOST REPORT ARRIVED\n");
//  g_print("%d", this->id);
//  gst_print_rtcp_rrb(rrb);
  //Debug print
//  g_print ("Receiver riport for subflow %d is processed\n"
//      "lost_rate: %f; "
//      "RTT (in ms): %lu\n",
//      this->id,
//      _st0 (this)->lost_rate, GST_TIME_AS_MSECONDS (_st0 (this)->RTT));

}


void
_report_processing_xrblock_processor (Subflow * this, GstRTCPXR_RFC7243 * xrb)
{
  guint8 interval_metric;
  guint32 discarded_bytes;
  gboolean early_bit;
//  g_print("DISCARDED REPORT ARRIVED\n");
  gst_rtcp_xr_rfc7243_getdown (xrb, &interval_metric,
      &early_bit, NULL, &discarded_bytes);

  if (interval_metric == RTCP_XR_RFC7243_I_FLAG_CUMULATIVE_DURATION) {
    if (early_bit) {
      _st0 (this)->early_discarded_bytes =
          discarded_bytes - this->early_discarded_bytes_sum;
      this->early_discarded_bytes_sum = discarded_bytes;
    } else {
      _st0 (this)->late_discarded_bytes =
          discarded_bytes - this->late_discarded_bytes_sum;
      this->late_discarded_bytes_sum = discarded_bytes;
    }
  } else if (interval_metric == RTCP_XR_RFC7243_I_FLAG_INTERVAL_DURATION) {
    if (early_bit) {
      _st0 (this)->early_discarded_bytes = discarded_bytes;
      this->early_discarded_bytes_sum += _st0 (this)->early_discarded_bytes;
    } else {
      _st0 (this)->late_discarded_bytes = discarded_bytes;
      this->late_discarded_bytes_sum += _st0 (this)->late_discarded_bytes;
    }
  } else if (interval_metric == RTCP_XR_RFC7243_I_FLAG_SAMPLED_METRIC) {

  }

}


void
_refresh_actual_record (Subflow * this)
{
  //goodput
  {
    GstClockTimeDiff interval;
    GstClockTime seconds, now;
    MPRTPSPath *path;
    gfloat payload_bytes_sum = 0.;
    guint32 discarded_bytes;
    gfloat goodput;
    guint32 octet_sum;

    now = gst_clock_get_time (this->sysclock);
    interval = GST_CLOCK_DIFF (this->last_receiver_riport_time, now);
    seconds = GST_TIME_AS_SECONDS ((GstClockTime) interval);
    if (this->last_receiver_riport_time == 0) {
      seconds = 0;
    }
    path = this->path;

    octet_sum = mprtps_path_get_sent_octet_sum_for (path, _st0 (this)->HSSN);
    payload_bytes_sum = (gfloat) (octet_sum << 3);

    discarded_bytes = _st0 (this)->early_discarded_bytes +
        _st0 (this)->late_discarded_bytes;
    if (seconds > 0) {
      goodput = (payload_bytes_sum *
          (1. - _st0 (this)->lost_rate) -
          (gfloat) discarded_bytes) / ((gfloat) seconds);

      //      g_print("%f = (%f * (1.-%f) - %f) / %f\n", result,
      //                payload_bytes_sum, this->lost_rate,
      //                (gfloat) discarded_bytes, (gfloat) seconds);

    } else {
      goodput = (payload_bytes_sum *
          (1. - _st0 (this)->lost_rate) - (gfloat) discarded_bytes);

      //      g_print("%f = (%f * (1.-%f) - %f)\n", result,
      //                payload_bytes_sum, this->lost_rate,
      //                (gfloat) discarded_bytes);

    }
    _st0 (this)->goodput = goodput;
    _st0 (this)->moment = now;
  }
}


//--------------------------------------------------------------------------
//--------------------------------- Actions --------------------------------
//--------------------------------------------------------------------------
void
_action_recalc (SndEventBasedController * this, Subflow * subflow)
{
  this->bids_recalc_requested = TRUE;
  GST_DEBUG_OBJECT (this, "Recalc action is performed with "
      "Event based controller on subflow %d", subflow->id);
}

void
_action_keep (SndEventBasedController * this, Subflow * subflow)
{
  GST_DEBUG_OBJECT (this, "Keep action is performed with "
      "Event based controller on subflow %d", subflow->id);
}

void
_action_reload (SndEventBasedController * this, Subflow * subflow)
{
  GstClockTime now;
  stream_splitter_add_path (this->splitter, subflow->id, subflow->path);
  this->bids_recalc_requested = TRUE;
  //load
  now = gst_clock_get_time (this->sysclock);
  if (now - GST_SECOND * 60 < subflow->saved_time) {
    _st0 (subflow)->goodput = subflow->saved_record.goodput;
  }
  GST_DEBUG_OBJECT (this, "Reload action is performed with "
      "Event based controller on subflow %d", subflow->id);
}

void
_action_fall (SndEventBasedController * this, Subflow * subflow)
{
  stream_splitter_rem_path (this->splitter, subflow->id);
  this->bids_commit_requested = TRUE;
  //save
  memcpy (&subflow->saved_record, _st0 (subflow), sizeof (SubflowRecord));
  subflow->saved_time = gst_clock_get_time (this->sysclock);
  GST_DEBUG_OBJECT (this, "Fall action is performed with "
      "Event based controller on subflow %d", subflow->id);
}

void
_sefctrler_recalc (SndEventBasedController * this)
{
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  MPRTPSPath *path;
  gboolean is_non_congested;
  gfloat max_bid = 1000.;
  gfloat allocated_bid = 0.;
  gfloat sending_bid;

  g_print ("RECALCULATION STARTED\n");

  if (_ct0 (this)->goodput.c > 0. || _ct0 (this)->goodput.mc > 0.) {
    g_hash_table_iter_init (&iter, this->subflows);
    while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
      subflow = (Subflow *) val;
      path = subflow->path;
      if (!mprtps_path_is_active (path) ||
          mprtps_path_get_state (path) == MPRTPS_PATH_STATE_NON_CONGESTED) {
        continue;
      }

      is_non_congested = mprtps_path_is_non_congested (path);
      if (is_non_congested) {
        //not congested but lossy
        sending_bid =
            MIN (_st0 (subflow)->goodput / _ct0 (this)->goodput.mc * max_bid,
            _st0 (subflow)->goodput * (1. - _get_reduction (this, subflow)));
      } else {
        //congested
        sending_bid =
            MIN (_st0 (subflow)->goodput / _ct0 (this)->goodput.c * max_bid,
            _st0 (subflow)->goodput * (1. - _get_mitigation (this, subflow)));
      }
      allocated_bid += sending_bid;
      stream_splitter_setup_sending_bid (this->splitter, subflow->id,
          (guint32) (sending_bid + .5));
    }
  }

  if (_ct0 (this)->goodput.nc > 0.) {
    g_hash_table_iter_init (&iter, this->subflows);
    while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
      subflow = (Subflow *) val;
      path = subflow->path;
      if (!mprtps_path_is_active (path) ||
          mprtps_path_get_state (path) != MPRTPS_PATH_STATE_NON_CONGESTED) {
        continue;
      }
      if (_ct0 (this)->max_nc_goodput < _st0 (subflow)->goodput) {
        _ct0 (this)->max_nc_goodput = _st0 (subflow)->goodput;
      }
      //not congested, not lossy
      sending_bid = _st0 (subflow)->goodput / _ct0 (this)->goodput.nc *
          (max_bid - allocated_bid) * _get_compensation (this, subflow);

      allocated_bid += sending_bid;
      stream_splitter_setup_sending_bid (this->splitter, subflow->id,
          (guint32) (sending_bid + .5));

      if (this->pacing) {
        guint32 bytes_per_ms;
        bytes_per_ms = _st0 (subflow)->goodput / 1000. * 1.2;
        mprtps_path_set_max_bytes_per_ms (subflow->path, bytes_per_ms);
      }

    }
  }
}

gfloat
_get_mitigation (SndEventBasedController * this, Subflow * subflow)
{
  gfloat result;
  GST_DEBUG ("get mitigation value for subflow %d at controller %p",
      subflow->id, this);
  result = .5;
  return result;
}

gfloat
_get_reduction (SndEventBasedController * this, Subflow * subflow)
{
  gfloat result;
  GST_DEBUG ("get reduction value for subflow %d at controller %p", subflow->id,
      this);
  result = .2;
  return result;
}

gfloat
_get_compensation (SndEventBasedController * this, Subflow * subflow)
{
  gfloat result = 1.;
  GST_DEBUG ("get compensation value for subflow %d at controller %p",
      subflow->id, this);
  if (!mprtps_path_is_in_trial (subflow->path))
    goto done;
  if (_ct0 (this)->subflows.nc > 1 &&
      (_ct0 (this)->subflows.mc + _ct0 (this)->subflows.c) > 0 &&
      _ct1 (this)->max_nc_goodput <= _st0 (subflow)->goodput) {
    mprtps_path_set_trial_end (subflow->path);
    goto done;
  }
  if (_ct0 (this)->subflows.nc == 1 &&
      (_ct0 (this)->subflows.mc + _ct0 (this)->subflows.c) > 0) {
    //aggressive increasement
    result += .3;
  } else {
    //cautious increasement
    result += .05;
  }
done:
  return result;
}

void
_fire (SndEventBasedController * this, Subflow * subflow, Event event)
{
  MPRTPSPath *path;
  MPRTPSPathState path_state;
  Action action;

  path = subflow->path;
  path_state = mprtps_path_get_state (path);

  action = _action_keep;
  g_print ("FIRE->%d:%d\n", subflow->id, event);
  //passive state
  if (path_state == MPRTPS_PATH_STATE_PASSIVE) {
    switch (event) {
      case EVENT_SETTLED:
        mprtps_path_set_active (path);
        action = _action_reload;
        break;
      case EVENT_FI:
      default:
        break;
    }
    goto lets_rock;
  }
  //whichever state we are the LATE event means the same
  if (event == EVENT_LATE) {
    mprtps_path_set_passive (path);
    action = _action_fall;
    goto lets_rock;
  }

  if (path_state == MPRTPS_PATH_STATE_NON_CONGESTED) {
    switch (event) {
      case EVENT_CONGESTION:
        mprtps_path_set_congested (path);
        action = _action_recalc;
        break;
      case EVENT_DISTORTION:
        mprtps_path_set_trial_end (path);
        break;
      case EVENT_LOSTS:
        mprtps_path_set_lossy (path);
        action = _action_recalc;
        break;
      case EVENT_FI:
      default:
        if (mprtps_path_is_in_trial (path))
          action = _action_recalc;

        break;
    }
    goto lets_rock;
  }
  //lossy path
  if (path_state == MPRTPS_PATH_STATE_MIDDLY_CONGESTED) {
    switch (event) {
      case EVENT_SETTLED:
        mprtps_path_set_non_lossy (path);
        action = _action_reload;
        break;
      case EVENT_CONGESTION:
        mprtps_path_set_non_lossy (path);
        mprtps_path_set_congested (path);
        action = _action_recalc;
        break;
      case EVENT_FI:
      default:
        break;
    }
    goto lets_rock;
  }
  //the path is congested
  switch (event) {
    case EVENT_SETTLED:
      mprtps_path_set_non_congested (path);
      mprtps_path_set_trial_begin (path);
      action = _action_reload;
      break;
    case EVENT_FI:
    default:
      break;
  }

lets_rock:
  action (this, subflow);
  return;
}

Event
_check_state (Subflow * this)
{
  Event result = EVENT_FI;
  GstClockTime sent_passive;
  GstClockTime sent_congested;
  GstClockTime sent_middly_congested;
  GstClockTime now;
  gboolean consequtive_lost;
  gboolean any_discards, alost, adiscard;
  gboolean consequtive_non_lost;
  MPRTPSPathState path_state;

  alost = _st0 (this)->lost_packets_num;
  adiscard = _st0 (this)->late_discarded_bytes
      || _st0 (this)->early_discarded_bytes;

  consequtive_lost = alost &&
      _st1 (this)->lost_packets_num && _st2 (this)->lost_packets_num;

  consequtive_non_lost = !alost &&
      !_st1 (this)->lost_packets_num && !_st2 (this)->lost_packets_num;

  any_discards = adiscard ||
      _st1 (this)->late_discarded_bytes ||
      _st1 (this)->early_discarded_bytes ||
      _st2 (this)->late_discarded_bytes || _st2 (this)->early_discarded_bytes;


  path_state = mprtps_path_get_state (this->path);
  now = gst_clock_get_time (this->sysclock);
  switch (path_state) {
    case MPRTPS_PATH_STATE_NON_CONGESTED:
      if (PATH_RTT_MAX_TRESHOLD < _st0 (this)->RTT) {
        result = EVENT_LATE;
        goto done;
      }

      if (consequtive_lost && any_discards) {
        result = EVENT_CONGESTION;
        goto done;
      }

      if (consequtive_lost) {
        result = EVENT_LOSTS;
        goto done;
      }

      if (alost || adiscard) {
        result = EVENT_DISTORTION;
        goto done;
      }
      break;
    case MPRTPS_PATH_STATE_MIDDLY_CONGESTED:
      if (PATH_RTT_MAX_TRESHOLD < _st0 (this)->RTT) {
        result = EVENT_LATE;
        goto done;
      }

      sent_middly_congested =
          mprtps_path_get_time_sent_to_middly_congested (this->path);

      if (consequtive_non_lost && sent_middly_congested < now - 20 * GST_SECOND) {
        result = EVENT_SETTLED;
        goto done;
      }
      if (any_discards && sent_middly_congested < now - 10 * GST_SECOND) {
        result = EVENT_CONGESTION;
        goto done;
      }
      break;
    case MPRTPS_PATH_STATE_CONGESTED:
      if (PATH_RTT_MAX_TRESHOLD < _st0 (this)->RTT) {
        result = EVENT_LATE;
        goto done;
      }

      sent_congested = mprtps_path_get_time_sent_to_congested (this->path);
      if (consequtive_non_lost && sent_congested < now - 20 * GST_SECOND) {
        result = EVENT_SETTLED;
        goto done;
      }
      break;
    case MPRTPS_PATH_STATE_PASSIVE:
      sent_passive = mprtps_path_get_time_sent_to_passive (this->path);
      if (_st0 (this)->RTT < PATH_RTT_MIN_TRESHOLD &&
          sent_passive < now - GST_SECOND * 20) {
        result = EVENT_SETTLED;
        goto done;
      }
      break;
    default:
      break;
  }
done:
  return result;
}

Event
_check_report_timeout (Subflow * this)
{
  Event result = EVENT_FI;
  MPRTPSPath *path;
  GstClockTime now;
  MPRTPSPathState path_state;

  path = this->path;
  path_state = mprtps_path_get_state (path);
  now = gst_clock_get_time (this->sysclock);
  if (path_state == MPRTPS_PATH_STATE_PASSIVE) {
    goto done;
  }
  if (!this->received_receiver_reports_num) {
    if (this->joined_time < now - RIPORT_TIMEOUT) {
      result = EVENT_LATE;
    }
    goto done;
  }
  if (this->last_receiver_riport_time < now - RIPORT_TIMEOUT) {
    result = EVENT_LATE;
  }
done:
  return result;
}



gboolean
_do_report_now (Subflow * this)
{
  gboolean result;
  GstClockTime now;
  gdouble interval;

  now = gst_clock_get_time (this->sysclock);
  if (!this->first_report_calculated) {
    interval = rtcp_interval (1,        //senders
        2,                      //members
        _st0 (this)->goodput > 0. ? _st0 (this)->goodput : 64000.,      //rtcp_bw
        1,                      //we_sent
        this->avg_rtcp_size,    //avg_rtcp_size
        1);                     //initial

    this->first_report_calculated = TRUE;
    result = FALSE;
    this->normal_report_time = now + (GstClockTime) interval *GST_SECOND;
    goto done;
  }

  if (this->normal_report_time <= now) {
    result = TRUE;
    goto done;
  }
  result = FALSE;
done:
  return result;
}

void
_recalc_report_time (Subflow * this)
{
  gdouble interval;
  GstClockTime now;
  now = gst_clock_get_time (this->sysclock);

  interval = rtcp_interval (1,  //senders
      2,                        //members
      _st0 (this)->goodput > 0. ? _st0 (this)->goodput : 64000.,        //rtcp_bw
      1,                        //we_sent
      this->avg_rtcp_size,      //avg_rtcp_size
      0);                       //initial

//  g_print("interval: %f, %f, %f\n", this->goodput, this->avg_rtcp_size, interval);

  this->normal_report_time = now + (GstClockTime) interval *GST_SECOND;
  return;
}


GstBuffer *
_get_mprtcp_sr_block (SndEventBasedController * this,
    Subflow * subflow, guint16 * buf_length)
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
    *buf_length = length;
  }
  return result;
}


void
_setup_sr_riport (Subflow * this, GstRTCPSR * sr, guint32 ssrc)
{
  guint64 ntptime;
  guint32 rtptime;
  guint32 last_sent_packets_num;
  guint32 last_sent_payload_bytes;
  guint32 packet_count_diff, payload_bytes;
  MPRTPSPath *path;

  gst_rtcp_header_change (&sr->header, NULL, NULL, NULL, NULL, NULL, &ssrc);
  ntptime = gst_clock_get_time (this->sysclock);

  rtptime = (guint32) (gst_rtcp_ntp_to_unix (ntptime) >> 32);   //rtptime
  path = this->path;

  last_sent_packets_num = this->sent_packet_num;
  this->sent_packet_num = mprtps_path_get_total_sent_packets_num (path);
  packet_count_diff = _uint32_diff (last_sent_packets_num,
      this->sent_packet_num);

  last_sent_payload_bytes = this->sent_payload_bytes;
  this->sent_payload_bytes = mprtps_path_get_total_sent_payload_bytes (path);
  payload_bytes =
      _uint32_diff (last_sent_payload_bytes, this->sent_payload_bytes);

  gst_rtcp_srb_setup (&sr->sender_block, ntptime, rtptime,
      packet_count_diff, payload_bytes >> 3);
}

#undef _ct0
#undef _ct1
#undef _st0
#undef _st1
#undef _st2
#undef DEBUG_MODE_ON
#undef MAX_RIPORT_INTERVAL
#undef RIPORT_TIMEOUT
#undef PATH_RTT_MAX_TRESHOLD
#undef PATH_RTT_MIN_TRESHOLD
#undef MAX_RIPORT_INTERVAL
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
