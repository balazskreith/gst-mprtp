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
#include "gstmprtcpbuffer.h"
#include "streamsplitter.h"
#include "streamjoiner.h"
#include "mprtplogger.h"
#include "fractalfbprod.h"
#include "rcvctrler.h"
#include "ricalcer.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


#define MIN_MEDIA_RATE 50000

#define _now(this) (gst_clock_get_time (this->sysclock))

GST_DEBUG_CATEGORY_STATIC (rcvctrler_debug_category);
#define GST_CAT_DEFAULT rcvctrler_debug_category

G_DEFINE_TYPE (RcvController, rcvctrler, G_TYPE_OBJECT);

#define REGULAR_REPORT_PERIOD_TIME (5 * GST_SECOND)


typedef void (*CallFunc)(gpointer udata);
typedef gboolean (*TimeUpdaterFunc)(gpointer udata);
typedef void (*ReportTransitFunc)(gpointer udata, GstMPRTCPReportSummary* summary);
typedef struct{
  CongestionControllingType type;
  RcvSubflow*               subflow;
  gpointer                  udata;
  TimeUpdaterFunc           time_updater;
  ReportTransitFunc         report_updater;
}FeedbackProducer;


//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
rcvctrler_finalize (GObject * object);

//------------------------ Outgoing Report Producer -------------------------
static void
_receiver_report_updater(
    RcvController * this);

static void
_create_rr(
    RcvController *this,
    RcvSubflow *subflow);


//------------------------- Utility functions --------------------------------

static guint32
_uint32_diff (
    guint32 a,
    guint32 b);

static guint16
_uint16_diff (
    guint16 a,
    guint16 b);

static FeedbackProducer*
_create_fractal(
    RcvController* this,
    RcvSubflow* subflow);

static void
_on_subflow_detached(
    RcvController* this,
    RcvSubflow *subflow);

static void
_on_congestion_controlling_changed(
    RcvController* this,
    RcvSubflow *subflow);


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
rcvctrler_init (RcvController * this)
{
  this->sysclock           = gst_system_clock_obtain ();
  this->ssrc               = g_random_int ();
  this->report_is_flowable = FALSE;
  this->report_producer    = g_object_new(REPORTPRODUCER_TYPE, NULL);
  this->report_processor   = g_object_new(REPORTPROCESSOR_TYPE, NULL);
  this->made               = _now(this);

  this->ricalcer           = make_ricalcer(FALSE);

  report_processor_set_logfile(this->report_processor, "rcv_reports.log");
  report_producer_set_logfile(this->report_producer, "rcv_produced_reports.log");

}

void
rcvctrler_finalize (GObject * object)
{
  RcvController *this = RCVCTRLER (object);
  g_object_unref (this->sysclock);
  g_object_unref(this->report_producer);
  g_object_unref(this->report_processor);

  g_object_unref(this->subflows);
  g_object_unref(this->rcvtracker);
  g_object_unref(this->on_rtcp_ready);

}

RcvController*
make_rcvctrler(
    RcvTracker *rcvtracker,
    RcvSubflows* subflows,
    Notifier* on_rtcp_ready)
{
  RcvController* this = (RcvController*)g_object_new(RCVCTRLER_TYPE, NULL);

  this->sysclock      = gst_system_clock_obtain();
  this->on_rtcp_ready = g_object_ref(on_rtcp_ready);
  this->subflows      = g_object_ref(subflows);
  this->rcvtracker    = g_object_ref(rcvtracker);

  rcvsubflows_add_on_subflow_detached_cb(
      this->subflows, (ListenerFunc)_on_subflow_detached, this);

  rcvsubflows_add_on_congestion_controlling_type_changed_cb(
      this->subflows, (ListenerFunc)_on_congestion_controlling_changed, this);

  return this;
}


//------------------------- Incoming Report Processor -------------------


void
rcvctrler_time_update (RcvController *this)
{
  GstClockTime now = _now(this);

  if(now - 10 * GST_MSECOND < this->last_time_update){
    goto done;
  }

  _receiver_report_updater(this);

  this->last_time_update = now;

done:
  return;
}


void
rcvctrler_receive_mprtcp (RcvController *this, GstBuffer * buf)
{
  RcvSubflow *subflow;
  GstMPRTCPReportSummary *summary;

  summary = &this->reports_summary;
  memset(summary, 0, sizeof(GstMPRTCPReportSummary));

  report_processor_process_mprtcp(this->report_processor, buf, summary);

  subflow = rcvsubflows_get_subflow(this->subflows, summary->subflow_id);

  if(summary->SR.processed){
    this->report_is_flowable = TRUE;
    report_producer_set_sender_ssrc(this->report_producer, summary->ssrc);
    subflow->last_SR_report_sent = summary->SR.ntptime;
    subflow->last_SR_report_rcvd = NTP_NOW;
  }

}


//------------------------ Outgoing Report Producer -------------------------


static void _receiver_report_updater_helper(RcvSubflow *subflow, gpointer udata)
{
  RcvController*            this;
  GstBuffer*                buf;
  guint                     report_length = 0;

  this = udata;

  if(subflow->congestion_controlling_type == CONGESTION_CONTROLLING_TYPE_NONE){
    goto done;
  }

  if(ricalcer_rtcp_regular_allowed_rcvsubflow(this->ricalcer, subflow)){
    report_producer_begin(this->report_producer, subflow->id);
    _create_rr(this, subflow);
  }

  rcvsubflow_notify_rtcp_fb_cbs(subflow, this->report_producer);

  buf = report_producer_end(this->report_producer, &report_length);

  if(buf){
    notifier_do(this->on_rtcp_ready, buf);
  }

  //report_length += 12 /* RTCP HEADER*/ + (28<<3) /*UDP+IP HEADER*/;
  //ricalcer_update_avg_report_size(ricaler, report_length);

done:
  return;
}

void
_receiver_report_updater(RcvController * this)
{

  if(!this->report_is_flowable){
    goto done;
  }

  rcvsubflows_iterate(this->subflows, (GFunc) _receiver_report_updater_helper, this);

done:
  return;
}

void _create_rr(RcvController * this, RcvSubflow *subflow)
{
  guint8  fraction_lost;
  guint32 ext_hsn;
  guint32 received;
  guint32 lost;
  guint32 expected;
  guint16 cycle_num;
  guint32 LSR;
  guint32 DLSR;
  RcvTrackerSubflowStat* stat;

  stat = rcvtracker_get_subflow_stat(this->rcvtracker, subflow->id);

  expected      = _uint32_diff(subflow->highest_seq, stat->highest_seq);
  received      = stat->total_received_packets - subflow->total_received_packets;
  lost          = received < expected ? expected - received : 0;
  cycle_num     = stat->cycle_num;

  fraction_lost = (expected == 0 || lost <= 0) ? 0 : (lost << 8) / expected;
  ext_hsn       = (((guint32) cycle_num) << 16) | ((guint32) stat->highest_seq);

//  g_print("expected: %u received: %u lost: %u fraction_lost: %d cycle num: %d\n",
//          expected, received, lost, fraction_lost, cycle_num);

  subflow->highest_seq             = stat->highest_seq;
  subflow->total_lost_packets     += lost;
  subflow->total_received_packets  = stat->total_received_packets;

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
                         subflow->total_lost_packets,
                         ext_hsn,
                         stat->jitter,
                         LSR,
                         DLSR
                         );

}


//------------------------- Utility functions --------------------------------

guint32
_uint32_diff (guint32 start, guint32 end)
{

  DISABLE_LINE _uint16_diff(start, end);

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



FeedbackProducer* _create_fractal(RcvController* this, RcvSubflow* subflow)
{
  FeedbackProducer *result = g_slice_new0(FeedbackProducer);
  result->type           = CONGESTION_CONTROLLING_TYPE_FRACTAL;
  result->subflow        = subflow;
  result->udata          = make_fractalfbproducer(subflow, this->rcvtracker);
  return result;
}


static gint _producer_by_subflow_id(gconstpointer item, gconstpointer udata)
{
  const FeedbackProducer *producer = item;
  const RcvSubflow *subflow = udata;
  return subflow->id == producer->subflow->id ? 0 : -1;
}

static void _dispose_feedback_producer(RcvController* this, FeedbackProducer* producer)
{
  this->fbproducers = g_slist_remove(this->fbproducers, producer);
  g_slice_free(FeedbackProducer, producer);
}

void _on_subflow_detached(RcvController* this, RcvSubflow *subflow)
{
  GSList* producer_item;
  FeedbackProducer *producer;
  producer_item = g_slist_find_custom(this->fbproducers, subflow, _producer_by_subflow_id);
  if(!producer_item){
    return;
  }
  producer = producer_item->data;
  _dispose_feedback_producer(this, producer);
}

void _on_congestion_controlling_changed(RcvController* this, RcvSubflow *subflow)
{
  GSList* producer_item;

  producer_item = g_slist_find_custom(this->fbproducers, subflow, _producer_by_subflow_id);
  if(producer_item){
    FeedbackProducer *producer = producer_item->data;
    if(subflow->congestion_controlling_type == producer->type){
      goto done;
    }
    _dispose_feedback_producer(this, producer);
  }

  switch(subflow->congestion_controlling_type){
    case CONGESTION_CONTROLLING_TYPE_FRACTAL:
      this->fbproducers = g_slist_prepend(this->fbproducers, _create_fractal(this, subflow));
      break;
    case CONGESTION_CONTROLLING_TYPE_NONE:
    default:
      break;
  };
done:
  return;
}

#undef MAX_RIPORT_INTERVAL
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
