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
#include <stdlib.h>

#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)


GST_DEBUG_CATEGORY_STATIC (sndctrler_debug_category);
#define GST_CAT_DEFAULT sndctrler_debug_category

#define _now(this) (gst_clock_get_time (this->sysclock))

G_DEFINE_TYPE (SndController, sndctrler, G_TYPE_OBJECT);

typedef void (*CallFunc)(gpointer udata);
typedef gboolean (*TimeUpdaterFunc)(gpointer udata);
typedef void (*TransitFunc)(gpointer udata, GstMPRTCPReportSummary* summary);
typedef struct{
  CongestionControllingType type;
  guint8                    subflow_id;
  gpointer                  udata;
  CallFunc                  dispose;
  CallFunc                  disable;
  CallFunc                  enable;
  TimeUpdaterFunc           time_updater;
  TransitFunc               report_updater;
}CongestionController;

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
sndctrler_finalize (GObject * object);

static void
_on_subflow_active_changed(
    SndController* this,
    SndSubflow *subflow);

static void
_on_subflow_detached(
    SndController* this,
    SndSubflow *subflow);

static void
_on_congestion_controlling_changed(
    SndController* this,
    SndSubflow *subflow);

static void
_dispose_congestion_controller(
    SndController* this,
    CongestionController* controller);

static CongestionController*
_create_fbraplus(
    SndController* this,
    SndSubflow* subflow);

//----------------------------------------------------------------------------

//------------------------ Outgoing Report Producer -------------------------
static void
_sender_report_updater(
    SndController * this);

static void
_create_sr (
    SndController * this,
    SndSubflow * subflow);

//----------------------------------------------------------------------------
static MPRTPPluginSignalData*
_update_subflow_report_utilization(
    SndController* this,
    SndSubflow *subflow,
    GstMPRTCPReportSummary *summary);

static MPRTPPluginSignalData*
_update_subflow_target_utilization(
    SndController* this,
    SndSubflow *subflow);


//----------------------------------------------------------------------------




//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------


void
sndctrler_class_init (SndControllerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = sndctrler_finalize;

  GST_DEBUG_CATEGORY_INIT (sndctrler_debug_category, "sndctrler", 0,
      "MPRTP Sending Controller");

}

void
sndctrler_finalize (GObject * object)
{
  SndController *this = SNDCTRLER (object);

  g_object_unref (this->sysclock);
  g_async_queue_unref(this->mprtcpq);
  g_async_queue_unref(this->emitterq);
  g_object_unref(this->subflows);
  g_object_unref(this->sndtracker);
  g_object_unref(this->rtppackets);

  g_slice_free(MPRTPPluginSignalData, this->mprtp_signal_data);
}

void
sndctrler_init (SndController * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->report_is_flowable = FALSE;
  this->report_producer    = g_object_new(REPORTPRODUCER_TYPE, NULL);
  this->report_processor   = g_object_new(REPORTPROCESSOR_TYPE, NULL);
  this->made               = _now(this);
  this->controllers        = NULL;
  this->ricalcer           = make_ricalcer(TRUE);
  this->mprtp_signal_data  = g_slice_new0(MPRTPPluginSignalData);

  report_processor_set_logfile(this->report_processor, "snd_reports.log");
  report_producer_set_logfile(this->report_producer, "snd_produced_reports.log");

}

SndController*
make_sndctrler(RTPPackets *rtppackets,
    SndTracker *sndtracker,
    SndSubflows* subflows,
    GAsyncQueue *mprtcpq,
    GAsyncQueue *emitterq)
{
  SndController* this = (SndController*)g_object_new(SNDCTRLER_TYPE, NULL);

  this->mprtcpq    = g_async_queue_ref(mprtcpq);
  this->emitterq   = g_async_queue_ref(emitterq);
  this->subflows   = g_object_ref(subflows);
  this->sndtracker = g_object_ref(sndtracker);
  this->rtppackets = g_object_ref(rtppackets);

  sndsubflows_add_on_subflow_detached_cb(
      this->subflows, (NotifierFunc)_on_subflow_detached, this);

  sndsubflows_add_on_congestion_controlling_type_changed_cb(
      this->subflows, (NotifierFunc)_on_congestion_controlling_changed, this);

  sndsubflows_add_on_path_active_changed_cb(
      this->subflows, (NotifierFunc)_on_subflow_active_changed, this);
  return this;
}

void
sndctrler_report_can_flow (SndController *this)
{
  this->report_is_flowable = TRUE;
}

void
sndctrler_time_update (SndController *this)
{
  GSList *it;
  GstClockTime now = _now(this);
  gboolean emit_signal = FALSE;

  if(now - 20 * GST_MSECOND < this->last_time_update){
    goto done;
  }
  this->last_time_update = now;

  for(it = this->controllers; it; it = it->next){
    CongestionController* controller = it->data;
    emit_signal |= controller->time_updater(controller->udata);
  }
  emit_signal |= this->last_emit < now - 200 * GST_MSECOND;

  if(emit_signal){
    MPRTPPluginSignalData *msg;
    msg = g_slice_new0(MPRTPPluginSignalData);

    _update_subflow_target_utilization(this);
    memcpy(msg, this->mprtp_signal_data, sizeof(MPRTPPluginSignalData));
    g_async_queue_push(this->emitterq, msg);

    this->last_emit = now;
  }

  _sender_report_updater(this);
done:
  return;
}

void
sndctrler_receive_mprtcp (SndController *this, GstBuffer * buf)
{
  SndSubflow *subflow;
  GstMPRTCPReportSummary *summary;
  GSList* it;
  GstClockTime now = _now(this);

  summary = &this->reports_summary;
  memset(summary, 0, sizeof(GstMPRTCPReportSummary));

  report_processor_process_mprtcp(this->report_processor, buf, summary);
  subflow = sndsubflows_get_subflow(this->subflows, summary->subflow_id);

  if(!subflow){
    g_warning("Report arrived referring to subflow not exists");
    goto done;
  }

  for(it = this->controllers; it; it = it->next){
    CongestionController* controller = it->data;
    if(controller->subflow_id != summary->subflow_id){
      continue;
    }
    controller->report_updater(controller->udata, summary);
    break;
  }

done:
  return;
}


//---------------------------------------------------------------------------


//------------------------ Outgoing Report Producer -------------------------

static void _sender_report_updater_helper(SndSubflow *subflow, gpointer udata)
{
  SndController*            this;
  ReportIntervalCalculator* ricalcer;
  GstBuffer*                buf;
  guint                     report_length = 0;

  this     = udata;
  ricalcer = this->ricalcer;

  if(subflow->congestion_controlling_type == CONGESTION_CONTROLLING_MODE_NONE){
    goto done;
  }

  if(!ricalcer_rtcp_regular_allowed(ricalcer, subflow)){
    goto done;
  }

  report_producer_begin(this->report_producer, subflow->id);
  _create_sr(this, subflow);
  buf = report_producer_end(this->report_producer, &report_length);
  g_async_queue_push(this->mprtcpq, buf);

  //report_length += 12 /* RTCP HEADER*/ + (28<<3) /*UDP+IP HEADER*/;
  //ricalcer_update_avg_report_size(ricaler, report_length);

done:
  return;
}

void
_sender_report_updater(SndController * this)
{

  if(!this->report_is_flowable){
    goto done;
  }

  sndsubflows_iterate(this->subflows, (GFunc) _sender_report_updater_helper, this);

done:
  return;
}

void
_create_sr (SndController * this, SndSubflow * subflow)
{
  guint64 ntptime;
  guint32 rtptime;
  guint32 packet_count;
  guint32 octet_count;
  SndTrackerStat* stat;

  ntptime = NTP_NOW;

  rtptime = 0;
  stat = sndtracker_get_subflow_stat(this->sndtracker, subflow->id);

  packet_count  = stat->total_sent_packets;
  octet_count = (stat->total_sent_bytes)>>3;

  report_producer_add_sr(this->report_producer,
                         ntptime,
                         rtptime,
                         packet_count - subflow->sent_packet_count,
                         octet_count - subflow->sent_octet_count);

  subflow->sent_packet_count = packet_count;
  subflow->sent_octet_count = octet_count;

}

//---------------------------------------------------------------------------

void _update_subflow_report_utilization(SndController* this, SndSubflow *subflow, GstMPRTCPReportSummary *summary)
{
  MPRTPPluginSignalData* signaldata;
  MPRTPSubflowUtilizationSignalData *subflowdata;

  signaldata  = this->mprtp_signal_data;
  subflowdata = &signaldata->subflow[subflow->id];
  if(summary->RR.processed){
    subflowdata->HSSN = summary->RR.HSSN;
    subflowdata->RTT = summary->RR.RTT;
    subflowdata->cum_packet_lost = summary->RR.cum_packet_lost;
    subflowdata->cycle_num = summary->RR.cycle_num;
    subflowdata->lost_rate = summary->RR.lost_rate;
    subflowdata->jitter = summary->RR.jitter;
  }

  if(summary->XR.OWD.processed){
    subflowdata->owd_max = summary->XR.OWD.max_delay;
    subflowdata->owd_min = summary->XR.OWD.min_delay;
    subflowdata->owd_median = summary->XR.OWD.median_delay;
  }
}

static void _update_sndsubflow(gpointer item, gpointer udata)
{
  SndSubflow *subflow = item;
  MPRTPPluginSignalData *signaldata = udata;
  MPRTPSubflowUtilizationSignalData *subflowdata;

  subflowdata = &signaldata->subflow[subflow->id];
  subflowdata->target_bitrate = subflow->target_bitrate;
}

void _update_subflow_target_utilization(SndController* this)
{
  MPRTPPluginSignalData* signaldata;

  signaldata  = this->mprtp_signal_data;
  sndsubflows_iterate(this->subflows, _update_sndsubflow, signaldata);
  signaldata->target_media_rate = sndsubflows_get_total_target(this->subflows);
}


//---------------------- Event handlers ----------------------------------

static gint _controller_by_subflow_id(gpointer item, gpointer udata)
{
  CongestionController *controller = item;
  SndSubflow *subflow = udata;
  return subflow->id == controller->subflow_id ? 0 : -1;
}

void _on_subflow_active_changed(SndController* this, SndSubflow *subflow)
{
  GSList* controller_item;
  CongestionController *controller;
  controller_item = g_slist_find_custom(this->controllers, _controller_by_subflow_id, subflow);
  if(!controller_item){
    return;
  }
  controller = controller_item->data;
  if(subflow->active){
    controller->enable(controller->udata);
  }else{
    controller->disable(controller->udata);
  }
}

void _on_subflow_detached(SndController* this, SndSubflow *subflow)
{
  GSList* controller_item;
  CongestionController *controller;
  controller_item = g_slist_find_custom(this->controllers, _controller_by_subflow_id, subflow);
  if(!controller_item){
    return;
  }
  controller = controller_item->data;
  _dispose_congestion_controller(this, controller);
}

void _on_congestion_controlling_changed(SndController* this, SndSubflow *subflow)
{
  GSList* controller_item;

  controller_item = g_slist_find_custom(this->controllers, _controller_by_subflow_id, subflow);
  if(controller_item){
    CongestionController *controller = controller_item->data;
    if(subflow->congestion_controlling_type == controller->type){
      goto done;
    }
    _dispose_congestion_controller(this, controller);
  }

  switch(subflow->congestion_controlling_type){
    case CONGESTION_CONTROLLING_MODE_FBRAPLUS:
      this->controllers = g_slist_prepend(this->controllers, _create_fbraplus(this, subflow));
      break;
    case CONGESTION_CONTROLLING_MODE_NONE:
    default:
      break;
  };
done:
  return;
}

//---------------------- Utility functions ----------------------------------


void _dispose_congestion_controller(SndController* this, CongestionController* controller)
{
  controller->disable(controller->udata);
  controller->dispose(controller->udata);
  this->controllers = g_slist_remove(this->controllers, controller);
  g_slice_free(CongestionController, controller);
}

CongestionController* _create_fbraplus(SndController* this, SndSubflow* subflow)
{
  CongestionController *result = g_slice_new0(CongestionController);
  result->type           = CONGESTION_CONTROLLING_MODE_FBRAPLUS;
  result->subflow_id     = subflow->id;
  result->udata          = make_fbrasubctrler(this->rtppackets, this->sndtracker, subflow);
  result->disable        = (CallFunc) fbrasubctrler_disable;
  result->dispose        = (CallFunc) g_object_unref;
  result->enable         = (CallFunc) fbrasubctrler_enable;
  result->time_updater   = (TimeUpdaterFunc) fbrasubctrler_time_update;
  result->report_updater = (TransitFunc) fbrasubctrler_report_update;
  return result;
}


