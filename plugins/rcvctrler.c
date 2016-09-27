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

typedef void (*CallFunc)(gpointer udata);
typedef gboolean (*TimeUpdaterFunc)(gpointer udata);
typedef void (*ReportTransitFunc)(gpointer udata, GstMPRTCPReportSummary* summary);
typedef struct{
  guint8                subflow_id;
  gpointer              udata;
  CallFunc              dispose;
  CallFunc              disable;
  CallFunc              enable;
  TimeUpdaterFunc       time_updater;
  ReportTransitFunc     report_updater;
  void (*report_callback)(gpointer udata, )
}FeedbackProducer;


//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
rcvctrler_finalize (GObject * object);

//------------------------ Outgoing Report Producer -------------------------

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
  g_object_unref(this->rtppackets);
  g_async_queue_unref(this->mprtcpq);

}

RcvController*
make_rcvctrler(RTPPackets *rtppackets,
    RcvTracker *rcvtracker,
    RcvSubflows* subflows,
    GAsyncQueue *mprtcpq,
    GAsyncQueue *emitterq)
{
  RcvController* this = (RcvController*)g_object_new(SNDCTRLER_TYPE, NULL);
  this->mprtcpq    = g_async_queue_ref(mprtcpq);
  this->subflows   = g_object_ref(subflows);
  this->rcvtracker = g_object_ref(rcvtracker);
  this->rtppackets = g_object_ref(rtppackets);

  return this;
}

typedef struct{
  guint8                    subflow_id;
  RTCPIntervalType          new_mode;
}ChangeIntervalHelper;

static void _subflow_change_interval_type(gpointer item, gpointer udata)
{
  RcvSubflow* subflow = item;
  ChangeIntervalHelper* helper = udata;
  if(helper->subflow_id == 0 || helper->subflow_id == 255){
    goto change;
  }
  if(helper->subflow_id != subflow->id){
    goto remain;
  }
change:
  subflow->rtcp_interval_mode = helper->new_mode;
remain:
  return;
}

void rcvctrler_change_interval_type(RcvController * this, guint8 subflow_id, guint type)
{
  ChangeIntervalHelper helper;
  helper.new_mode = type;
  helper.subflow_id = subflow_id;
  rcvsubflows_iterate(this->subflows, _subflow_change_interval_type, &helper);
}



static gint _fbproducer_by_subflow_id(gpointer item, gpointer udata)
{
  FeedbackProducer *fbproducer = item;
  RcvSubflow *subflow = udata;
  return subflow->id == fbproducer->subflow_id ? 0 : -1;
}

typedef struct{
  guint8                    subflow_id;
  CongestionControllingType new_mode;
  RcvController            *rcv_fbproducer;
}ChangeControllingHelper;

static void _subflow_change_controlling_mode(gpointer item, gpointer udata)
{
  RcvSubflow              *subflow = item;
  ChangeControllingHelper *helper = udata;
  GSList                  *fbproducer_item;
  RcvController           *this = helper->rcv_fbproducer;

  if(helper->subflow_id == 0 || helper->subflow_id == 255){
    goto change;
  }
  if(helper->subflow_id != subflow->id){
    goto done;
  }
change:
  fbproducer_item = g_slist_find_custom(this->fbproducers, _fbproducer_by_subflow_id, subflow);
  if(fbproducer_item){
    FeedbackProducer *fbproducer = fbproducer_item->data;
    if(subflow->congestion_controlling_mode == helper->new_mode){
      goto done;
    }
    _dispose_congestion_fbproducer(this, fbproducer);
  }

  subflow->congestion_controlling_mode = helper->new_mode;
  switch(helper->new_mode){
    case CONGESTION_CONTROLLING_MODE_FBRAPLUS:
      this->fbproducers = g_slist_prepend(this->fbproducers, _create_fbraplus(this, subflow));
      break;
    case CONGESTION_CONTROLLING_MODE_NONE:
    default:
      break;
  };

done:
  return;
}

void rcvctrler_change_controlling_mode(RcvController * this,
                                       guint8 subflow_id,
                                       CongestionControllingType controlling_mode,
                                       gboolean *enable_fec)
{
  ChangeControllingHelper helper;
  helper.new_mode = controlling_mode;
  helper.subflow_id = subflow_id;
  helper.rcv_fbproducer = this;
  rcvsubflows_iterate(this->subflows, _subflow_change_controlling_mode, &helper);
}



//------------------------- Incoming Report Processor -------------------


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
  producer->disable(producer->udata);
  producer->dispose(producer->udata);
  this->fbproducers = g_slist_remove(this->fbproducers, producer);
  g_slice_free(FeedbackProducer, producer);
}

FeedbackProducer* _create_fbraplus(SndController* this, SndSubflow* subflow)
{
  CongestionController *result = g_slice_new0(CongestionController);
  result->subflow_id     = subflow->id;
  result->udata          = make_fbrasubctrler(this->rtppackets, this->rcvtracker, subflow);
  result->disable        = (CallFunc) ;
  result->dispose        = (CallFunc) g_object_unref;
  result->enable         = (CallFunc) ;
  result->time_updater   = (TimeUpdaterFunc) ;
  result->report_updater = (ReportTransitFunc) ;
  return result;
}


#undef MAX_RIPORT_INTERVAL
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
