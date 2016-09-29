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

#define REGULAR_REPORT_PERIOD_TIME (5*GST_SECOND)


typedef void (*CallFunc)(gpointer udata);
typedef gboolean (*TimeUpdaterFunc)(gpointer udata);
typedef void (*ReportTransitFunc)(gpointer udata, GstMPRTCPReportSummary* summary);
typedef struct{
  RcvSubflow*           subflow;
  gpointer              udata;
  CallFunc              dispose;
  TimeUpdaterFunc       time_updater;
  ReportTransitFunc     report_updater;
  void                (*report_callback)(gpointer udata, );
}FeedbackProducer;


//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
rcvctrler_finalize (GObject * object);

//------------------------ Outgoing Report Producer -------------------------
static void
_receiver_report_updater(
    SndController * this);

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

static void
_dispose_fbproducer(
    RcvController* this,
    FeedbackProducer* producer);

static FeedbackProducer*
_create_fbraplus(
    RcvController* this,
    RcvSubflow* subflow);


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

  report_processor_set_logfile(this->report_processor, "rcv_reports.log");
  report_producer_set_logfile(this->report_producer, "rcv_produced_reports.log");

}

void
rcvctrler_finalize (GObject * object)
{
  RcvController *this = RCVCTRLER (object);
  g_hash_table_destroy (this->subflows);
  g_object_unref (this->sysclock);
  g_object_unref(this->report_producer);
  g_object_unref(this->report_processor);

  g_object_unref(this->subflows);
  g_object_unref(this->rcvtracker);
  g_async_queue_unref(this->mprtcpq);

}

RcvController*
make_rcvctrler(
    RcvTracker *rcvtracker,
    RcvSubflows* subflows,
    GAsyncQueue *mprtcpq)
{
  RcvController* this = (RcvController*)g_object_new(SNDCTRLER_TYPE, NULL);
  this->mprtcpq    = g_async_queue_ref(mprtcpq);
  this->subflows   = g_object_ref(subflows);
  this->rcvtracker = g_object_ref(rcvtracker);

  return this;
}


//------------------------- Incoming Report Processor -------------------


void
rcvctrler_time_update (RcvController *this)
{
  GSList *it;
  GstClockTime now = _now(this);

  if(now - 20 * GST_MSECOND < this->last_time_update){
    goto done;
  }
  this->last_time_update = now;

  for(it = this->fbproducers; it; it = it->next){
    FeedbackProducer* fbproducer = it->data;
    fbproducer->time_updater(fbproducer->udata);
  }

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

  subflow = sndsubflows_get_subflow(this->subflows, summary->subflow_id);

  if(summary->SR.processed){
    this->report_is_flowable = TRUE;
    report_producer_set_ssrc(this->report_producer, summary->ssrc);
    subflow->last_SR_report_sent = summary->SR.ntptime;
    subflow->last_SR_report_rcvd = NTP_NOW;
  }

done:
  return;
}


//------------------------ Outgoing Report Producer -------------------------


static void _receiver_report_updater_helper(RcvSubflow *subflow, gpointer udata)
{
  RcvController*            this;
  ReportIntervalCalculator* ricalcer;
  GstBuffer*                buf;
  guint                     report_length = 0;

  this     = udata;
  ricalcer = this->ricalcer;

  if(subflow->congestion_controlling_type == CONGESTION_CONTROLLING_TYPE_NONE){
    goto done;
  }

  if(!ricalcer_rtcp_regular_allowed(ricalcer, subflow)){
    goto done;
  }

  report_producer_begin(this->report_producer, subflow->id);
  _create_rr(this, subflow);
  buf = report_producer_end(this->report_producer, &report_length);
  g_async_queue_push(this->mprtcpq, buf);

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


//------------------------- Utility functions --------------------------------

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




void _dispose_fbproducer(RcvController* this, FeedbackProducer* producer)
{
  producer->dispose(producer->udata);
  this->fbproducers = g_slist_remove(this->fbproducers, producer);
  g_slice_free(FeedbackProducer, producer);
}

FeedbackProducer* _create_fbraplus(SndController* this, RcvSubflow* subflow)
{
  FeedbackProducer *result = g_slice_new0(FeedbackProducer);
  result->subflow        = subflow;
  result->udata          = make_fbrafbproducer()
  result->dispose        = (CallFunc) g_object_unref;
  result->time_updater   = (TimeUpdaterFunc) ;
  result->report_updater = (ReportTransitFunc);
  return result;
}


#undef MAX_RIPORT_INTERVAL
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
