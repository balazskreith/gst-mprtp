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
#include "refctrler.h"
#include "streamsplitter.h"
#include "gstmprtcpbuffer.h"
#include "mprtprpath.h"
#include "streamjoiner.h"
#include <math.h>
#include <string.h>

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

GST_DEBUG_CATEGORY_STATIC (refctrler_debug_category);
#define GST_CAT_DEFAULT refctrler_debug_category

G_DEFINE_TYPE (RcvEventBasedController, refctrler, G_TYPE_OBJECT);

#define NORMAL_RIPORT_PERIOD_TIME (5*GST_SECOND)

typedef struct _Subflow Subflow;

struct _Subflow
{
  MpRTPRPath *path;
  guint8 id;
  GstClock *sysclock;
  GstClockTime joined_time;
  GstClockTime normal_report_time;
  GstClockTime actual_report_interval;
  GstClockTime last_report_interval;
  GstClockTime last_rr_report_sent_time;
  gboolean first_report_calculated;
  gdouble media_rate;
  gdouble avg_rtcp_size;
  gboolean allow_early;
  gboolean faster_reporting_started_time;
  guint packet_limit_to_riport;
  gboolean urgent_riport_is_requested;
//  guint64 path_skew;
  GstClockTime LSR;
  guint16 HSN;

  guint16 last_total_lost_packet_num;
  guint16 actual_total_lost_packet_num;
  guint32 last_total_late_discarded_bytes;
  guint32 actual_total_late_discarded_bytes;
  guint32 last_total_received_packet_num;
  guint32 actual_total_received_packet_num;
  guint32 last_total_bytes_received;
  guint32 actual_total_bytes_received;
  guint32 last_total_payload_bytes;
  guint32 actual_total_payload_bytes;
};

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void refctrler_finalize (GObject * object);
static void refctrler_run (void *data);
static GstBuffer *_get_mprtcp_xr_block (RcvEventBasedController * this,
    Subflow * subflow, guint16 * block_length);
static GstBuffer *_get_mprtcp_rr_block (RcvEventBasedController * this,
    Subflow * subflow, guint16 * block_length);

void _setup_xr_rfc2743_late_discarded_riport (Subflow * this,
    GstRTCPXR_RFC7243 * xr, guint32 ssrc);
void _setup_rr_riport (Subflow * this, GstRTCPRR * rr, guint32 ssrc);
static guint16 _uint16_diff (guint16 a, guint16 b);
static void refctrler_receive_mprtcp (gpointer subflow, GstBuffer * buf);
static void _report_processing_selector (Subflow * this,
    GstMPRTCPSubflowBlock * block);
static void _report_processing_srblock_processor (Subflow * subflow,
    GstRTCPSRBlock * srb);
void _recalc_report_time (Subflow * this);
gboolean _do_report_now (Subflow * subflow);

static guint32 _uint32_diff (guint32 a, guint32 b);
static void refctrler_rem_path (gpointer controller_ptr, guint8 subflow_id);
static void refctrler_add_path (gpointer controller_ptr, guint8 subflow_id,
    MpRTPRPath * path);

static void refctrler_riport_can_flow (gpointer subflow);
//subflow functions
static Subflow *make_subflow (guint8 id, MpRTPRPath * path);
static void ruin_subflow (gpointer * subflow);
static void reset_subflow (Subflow * subflow);
static Subflow *subflow_ctor (void);
static void subflow_dtor (Subflow * this);
//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------


void
refctrler_class_init (RcvEventBasedControllerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = refctrler_finalize;

  GST_DEBUG_CATEGORY_INIT (refctrler_debug_category, "refctrler", 0,
      "MpRTP Receiving Event Flow Reporter");

}

void
refctrler_finalize (GObject * object)
{
  RcvEventBasedController *this = REFCTRLER (object);
  g_hash_table_destroy (this->subflows);
  gst_task_stop (this->thread);
  gst_task_join (this->thread);

  g_object_unref (this->sysclock);
}

void
refctrler_init (RcvEventBasedController * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->subflows = g_hash_table_new_full (NULL, NULL,
      NULL, (GDestroyNotify) ruin_subflow);
  this->ssrc = g_random_int ();
  this->riport_is_flowable = FALSE;

  g_rw_lock_init (&this->rwmutex);
  g_rec_mutex_init (&this->thread_mutex);
  this->thread = gst_task_new (refctrler_run, this, NULL);
  gst_task_set_lock (this->thread, &this->thread_mutex);
  gst_task_start (this->thread);

}

void
refctrler_run (void *data)
{
  GstClockTime now, next_scheduler_time;
  RcvEventBasedController *this;
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  GstClockID clock_id;
  MpRTPRPath *path;
//  guint64 max_path_skew = 0;

  this = REFCTRLER (data);
  THIS_WRITELOCK (this);
  now = gst_clock_get_time (this->sysclock);

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    subflow = (Subflow *) val;
    path = subflow->path;

    subflow->actual_total_late_discarded_bytes =
        mprtpr_path_get_total_late_discarded_bytes_num (path);
    subflow->actual_total_lost_packet_num =
        mprtpr_path_get_total_packet_losts_num (path);
    subflow->actual_total_received_packet_num =
        mprtpr_path_get_total_received_packets_num (path);
    subflow->actual_total_bytes_received =
        mprtpr_path_get_total_bytes_received(path);
    subflow->actual_total_payload_bytes =
        mprtpr_path_get_total_payload_bytes(path);



    if (this->riport_is_flowable && _do_report_now (subflow)) {
      guint16 report_length = 0;
      guint16 block_length = 0;
      GstBuffer *block;

      block = _get_mprtcp_rr_block (this, subflow, &block_length);
      report_length += block_length;
      if (subflow->actual_total_late_discarded_bytes !=
          subflow->last_total_late_discarded_bytes) {
        GstBuffer *xr;
        xr = _get_mprtcp_xr_block (this, subflow, &block_length);
        block = gst_buffer_append (block, xr);
        report_length += block_length;
      }
      report_length += 12 /*MPRTCP REPOR HEADER */  +
          (28 << 3) /*UDP Header overhead */ ;

      this->send_mprtcp_packet_func (this->send_mprtcp_packet_data, block);

      subflow->avg_rtcp_size +=
          ((gfloat) report_length - subflow->avg_rtcp_size) / 4.;

      subflow->last_total_late_discarded_bytes =
          subflow->actual_total_late_discarded_bytes;
      subflow->last_total_lost_packet_num =
          subflow->actual_total_lost_packet_num;
      subflow->last_total_received_packet_num =
          subflow->actual_total_received_packet_num;
      subflow->last_total_bytes_received =
          subflow->actual_total_bytes_received;
      subflow->last_total_payload_bytes =
          subflow->actual_total_payload_bytes;
      subflow->last_rr_report_sent_time = now;

      if(subflow->last_report_interval){
        mprtpr_path_removes_obsolate_packets(
            subflow->path,
            subflow->last_report_interval + subflow->actual_report_interval
            );
      }
      _recalc_report_time (subflow);
    }
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
refctrler_add_path (gpointer controller_ptr, guint8 subflow_id,
    MpRTPRPath * path)
{
  RcvEventBasedController *this;
  Subflow *lookup_result;
  this = REFCTRLER (controller_ptr);
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
      make_subflow (subflow_id, path));
exit:
  THIS_WRITEUNLOCK (this);
}

void
refctrler_rem_path (gpointer controller_ptr, guint8 subflow_id)
{
  RcvEventBasedController *this;
  Subflow *lookup_result;
  this = REFCTRLER (controller_ptr);
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
refctrler_set_callbacks (void (**riport_can_flow_indicator) (gpointer),
    void (**controller_add_path) (gpointer, guint8, MpRTPRPath *),
    void (**controller_rem_path) (gpointer, guint8))
{
  if (riport_can_flow_indicator) {
    *riport_can_flow_indicator = refctrler_riport_can_flow;
  }
  if (controller_add_path) {
    *controller_add_path = refctrler_add_path;
  }
  if (controller_rem_path) {
    *controller_rem_path = refctrler_rem_path;
  }
}



GstBufferReceiverFunc
refctrler_setup_mprtcp_exchange (RcvEventBasedController * this,
    gpointer data, GstBufferReceiverFunc func)
{
  GstBufferReceiverFunc result;
  THIS_WRITELOCK (this);
  this->send_mprtcp_packet_func = func;
  this->send_mprtcp_packet_data = data;
  result = refctrler_receive_mprtcp;
  THIS_WRITEUNLOCK (this);
  return result;
}

void
refctrler_receive_mprtcp (gpointer ptr, GstBuffer * buf)
{
  GstMPRTCPSubflowBlock *block;
  RcvEventBasedController *this = REFCTRLER (ptr);
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
  _report_processing_selector (subflow, block);

done:
  gst_buffer_unmap (buf, &map);
  THIS_WRITEUNLOCK (this);
}

void
refctrler_riport_can_flow (gpointer ptr)
{
  RcvEventBasedController *this;
  this = REFCTRLER (ptr);
  GST_DEBUG_OBJECT (this, "RTCP riport can now flowable");
  THIS_WRITELOCK (this);
  this->riport_is_flowable = TRUE;
  THIS_WRITEUNLOCK (this);
}


//----------------------------------------------
// -------- Subflow related functions ----------
//----------------------------------------------
Subflow *
subflow_ctor (void)
{
  Subflow *result;
  result = g_malloc0 (sizeof (Subflow));
  return result;
}

void
subflow_dtor (Subflow * this)
{
  g_return_if_fail (this);
  g_free (this);
}

void
ruin_subflow (gpointer * subflow)
{
  Subflow *this;
  g_return_if_fail (subflow);
  this = (Subflow *) subflow;
  g_object_unref (this->sysclock);
  g_object_unref (this->path);
  subflow_dtor (this);
}

Subflow *
make_subflow (guint8 id, MpRTPRPath * path)
{
  Subflow *result = subflow_ctor ();
  g_object_ref (path);
  result->sysclock = gst_system_clock_obtain ();
  result->path = path;
  result->id = id;
  result->joined_time = gst_clock_get_time (result->sysclock);
  reset_subflow (result);
  return result;
}

void
reset_subflow (Subflow * this)
{
  this->normal_report_time = 0;
  this->first_report_calculated = FALSE;
  this->media_rate = 64000.;
  this->avg_rtcp_size = 1024.;
  this->allow_early = TRUE;
  this->faster_reporting_started_time = 0;
  this->packet_limit_to_riport = 10;
  this->urgent_riport_is_requested = FALSE;
  this->LSR = 0;
  this->HSN = 0;
}

void
refctrler_setup (gpointer ptr, StreamJoiner * joiner)
{
  RcvEventBasedController *this;
  this = REFCTRLER (ptr);
  THIS_WRITELOCK (this);
  this->joiner = joiner;
  stream_joiner_path_obsolation(this->joiner, FALSE);
  THIS_WRITEUNLOCK (this);
}


guint16
_uint16_diff (guint16 a, guint16 b)
{
  if (a <= b) {
    return b - a;
  }
  return ~((guint16) (a - b));
}

GstBuffer *
_get_mprtcp_rr_block (RcvEventBasedController * this, Subflow * subflow,
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
  _setup_rr_riport (subflow, rr, this->ssrc);
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
  //gst_print_rtcp_rr(rr);
  return buf;
}


GstBuffer *
_get_mprtcp_xr_block (RcvEventBasedController * this, Subflow * subflow,
    guint16 * buf_length)
{
  GstMPRTCPSubflowBlock block;
  GstRTCPXR_RFC7243 *xr;
  gpointer dataptr;
  guint16 length;
  guint8 block_length;
  GstBuffer *buf;

  gst_mprtcp_block_init (&block);
  xr = gst_mprtcp_riport_block_add_xr_rfc2743 (&block);
  _setup_xr_rfc2743_late_discarded_riport (subflow, xr, this->ssrc);
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



void
_setup_xr_rfc2743_late_discarded_riport (Subflow * this,
    GstRTCPXR_RFC7243 * xr, guint32 ssrc)
{
  guint8 flag = RTCP_XR_RFC7243_I_FLAG_INTERVAL_DURATION;
  gboolean early_bit = FALSE;
  guint32 late_discarded_bytes;

  gst_rtcp_header_change (&xr->header, NULL, NULL, NULL, NULL, NULL, &ssrc);
  late_discarded_bytes =
      _uint32_diff (this->last_total_late_discarded_bytes,
      this->actual_total_late_discarded_bytes);
  gst_rtcp_xr_rfc7243_change (xr, &flag, &early_bit, NULL,
      &late_discarded_bytes);
  g_print("DISCARDED REPORT SETTED UP\n");
}

void
_setup_rr_riport (Subflow * this, GstRTCPRR * rr, guint32 ssrc)
{
  GstClockTime now;
  guint8 fraction_lost;
  guint32 ext_hsn, LSR, DLSR;
  guint16 expected;
  MpRTPRPath *path;
  guint16 HSN;
  guint16 cycle_num;
  guint32 jitter;
  guint16 diff_lost_packet_num;
  gdouble received_bytes, interval;

  gst_rtcp_header_change (&rr->header, NULL, NULL, NULL, NULL, NULL, &ssrc);

  now = gst_clock_get_time (this->sysclock);
  path = this->path;

  cycle_num = mprtpr_path_get_cycle_num (path);
  jitter = mprtpr_path_get_jitter (path);

  HSN = mprtpr_path_get_highest_sequence_number (path);
  expected = _uint16_diff (this->HSN, HSN);
  this->HSN = HSN;
  diff_lost_packet_num =
      _uint16_diff (this->last_total_lost_packet_num,
      this->actual_total_lost_packet_num);
  fraction_lost =
      (256. * (gfloat) diff_lost_packet_num) / ((gfloat) (expected));
  if(diff_lost_packet_num) g_print("LOST REPORT ASSEMBLED\n");
  ext_hsn = (((guint32) cycle_num) << 16) | ((guint32) HSN);

  LSR = (guint32) (this->LSR >> 16);

  if (this->LSR == 0 || now < this->LSR) {
    DLSR = 0;
  } else {
    DLSR = (guint32) GST_TIME_AS_MSECONDS ((GstClockTime)
        GST_CLOCK_DIFF (this->LSR, now));
  }
  gst_rtcp_rr_add_rrb (rr, 0,
      fraction_lost, this->actual_total_lost_packet_num, ext_hsn, jitter, LSR,
      DLSR);

  received_bytes = (gdouble) (this->actual_total_payload_bytes
      - this->last_total_payload_bytes);
  interval =
      (gdouble) GST_TIME_AS_SECONDS (now - this->last_rr_report_sent_time);
  if (interval < 1.) {
    interval = 1.;
  }
  this->media_rate = received_bytes / interval;
  //reset
//
//    g_print("this->media_rate = %f / %f = %f\n",
//                   received_bytes,
//                   interval, this->media_rate);
}



gboolean
_do_report_now (Subflow * this)
{
  gboolean result;
  GstClockTime now;

  now = gst_clock_get_time (this->sysclock);
  if (!this->first_report_calculated) {
    this->urgent_riport_is_requested = TRUE;
    this->first_report_calculated = TRUE;
    result = FALSE;
    goto done;
  }
  if (this->urgent_riport_is_requested && this->allow_early) {
    this->allow_early = FALSE;
    result = TRUE;
    goto done;
  }

  if (this->normal_report_time <= now) {
    guint32 received;
    received = _uint32_diff (this->last_total_received_packet_num,
        this->actual_total_received_packet_num);
    if (received < this->packet_limit_to_riport) {
      result = now - 7 * GST_SECOND < this->last_rr_report_sent_time;
      goto done;
    }
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
      this->media_rate > 0. ? this->media_rate : 64000.,        //rtcp_bw
      0,                        //we_sent
      this->avg_rtcp_size,      //avg_rtcp_size
      0);                       //initial
  if (this->urgent_riport_is_requested) {
    this->faster_reporting_started_time = now;
    this->urgent_riport_is_requested = FALSE;
    if (4. < interval) {
      interval = 4. * (g_random_double () + .5);
    }
    goto done;
  }
  this->allow_early = TRUE;
  if (this->faster_reporting_started_time < now - 15 * GST_SECOND) {
    interval /= 2.;
  } else if (now - 20 * GST_SECOND < this->faster_reporting_started_time) {
    interval *= 1.5;
  }

done:
  if (interval < 1.) {
    interval = 1. + g_random_double ();
  } else if (7.5 < interval) {
    interval = 5. * (g_random_double () + .5);
  }
  //g_print("Next interval for subflow %d: %f\n", this->id, interval);
  this->last_report_interval = this->actual_report_interval;
  this->actual_report_interval = (GstClockTime)interval * GST_SECOND;
  this->normal_report_time = now + this->actual_report_interval;

  return;
}



//------------------ Riport Processing and evaluation -------------------

void
_report_processing_selector (Subflow * this, GstMPRTCPSubflowBlock * block)
{
  guint8 pt;

  gst_rtcp_header_getdown (&block->block_header, NULL, NULL, NULL, &pt, NULL,
      NULL);

  if (pt == (guint8) GST_RTCP_TYPE_SR) {
    _report_processing_srblock_processor (this,
        &block->sender_riport.sender_block);
  } else {
    GST_WARNING ("Event Based Flow receive controller "
        "can only process MPRTCP SR riports. "
        "The received riport payload type is: %d", pt);
  }
}


guint32
_uint32_diff (guint32 start, guint32 end)
{
  if (start <= end) {
    return end - start;
  }
  return ~((guint32) (start - end));
}

void
_report_processing_srblock_processor (Subflow * this, GstRTCPSRBlock * srb)
{
  GST_DEBUG ("RTCP SR riport arrived for subflow %p->%p", this, srb);
  this->LSR = gst_clock_get_time (this->sysclock);
}

#undef MAX_RIPORT_INTERVAL
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
