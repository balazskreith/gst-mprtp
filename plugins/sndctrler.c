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
#include "subratectrler.h"
#include <stdlib.h>

#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)


GST_DEBUG_CATEGORY_STATIC (sndctrler_debug_category);
#define GST_CAT_DEFAULT sndctrler_debug_category

#define _now(this) (gst_clock_get_time (this->sysclock))

G_DEFINE_TYPE (SndController, sndctrler, G_TYPE_OBJECT);

typedef struct{
  guint8   subflow_id;
  gpointer udata;
  void   (*dispose)(gpointer udata);
  void   (*disable)(gpointer udata);
  void   (*enable)(gpointer udata);
  void   (*time_updater)(gpointer udata);
  void   (*report_updater)(gpointer udata, );
}CongestionController;

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
sndctrler_finalize (GObject * object);


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
_orp_producer_main(
    SndController * this);

static void
_orp_add_sr (
    SndController * this,
    SndSubflow * subflow);

//----------------------------------------------------------------------------
static MPRTPPluginSignalData*
_create_utilization_message(
    SndController* this,
    SndSubflow *subflow,
    GstMPRTCPReportSummary *summary);

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
make_sndctrler(RTPPackets *rtppackets, SndTracker *sndtracker, SndSubflows* subflows, GAsyncQueue *mprtcpq, GAsyncQueue *emitterq)
{
  SndController* this = (SndController*)g_object_new(SNDCTRLER_TYPE, NULL);
  this->mprtcpq    = g_async_queue_ref(mprtcpq);
  this->emitterq   = g_async_queue_ref(emitterq);
  this->subflows   = g_object_ref(subflows);
  this->sndtracker = g_object_ref(sndtracker);
  this->rtppackets = g_object_ref(rtppackets);

  return this;
}


typedef struct{
  guint8                    subflow_id;
  RTCPIntervalMode          new_mode;
}ChangeIntervalHelper;

static void _subflow_change_interval_type(gpointer item, gpointer udata)
{
  SndSubflow* subflow = item;
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

void sndctrler_change_interval_type(SndController * this, guint8 subflow_id, guint type)
{
  ChangeIntervalHelper helper;
  helper.new_mode = type;
  helper.subflow_id = subflow_id;
  sndsubflows_iterate(this->subflows, _subflow_change_interval_type, &helper);
}



static gint _controller_by_subflow_id(gpointer item, gpointer udata)
{
  CongestionController *controller = item;
  SndSubflow *subflow = udata;
  return subflow->id == controller->subflow_id ? 0 : -1;
}

typedef struct{
  guint8                    subflow_id;
  CongestionControllingMode new_mode;
  SndController            *snd_controller;
  gboolean                 *enable_fec;
}ChangeControllingHelper;

static void _subflow_change_controlling_mode(gpointer item, gpointer udata)
{
  SndSubflow              *subflow = item;
  ChangeControllingHelper *helper = udata;
  GSList                  *controller_item;
  SndController           *this = helper->snd_controller;

  if(helper->subflow_id == 0 || helper->subflow_id == 255){
    goto change;
  }
  if(helper->subflow_id != subflow->id){
    goto done;
  }
change:
  controller_item = g_slist_find_custom(this->controllers, _controller_by_subflow_id, subflow);
  if(controller_item){
    CongestionController *controller = controller_item->data;
    if(subflow->congestion_controlling_mode == helper->new_mode){
      goto done;
    }
    _dispose_congestion_controller(this, controller);
  }

  subflow->congestion_controlling_mode = helper->new_mode;
  switch(helper->new_mode){
    case CONGESTION_CONTROLLING_MODE_FBRAPLUS:
      this->controllers = g_slist_prepend(this->controllers, _create_fbraplus(this, subflow));
      if(helper->enable_fec){
        *helper->enable_fec = TRUE;
      }
      break;
    case CONGESTION_CONTROLLING_MODE_NONE:
    default:
      break;
  };

done:
  return;
}

void sndctrler_change_controlling_mode(SndController * this,
                                       guint8 subflow_id,
                                       CongestionControllingMode controlling_mode,
                                       gboolean *enable_fec)
{
  ChangeControllingHelper helper;
  helper.new_mode = controlling_mode;
  helper.subflow_id = subflow_id;
  helper.snd_controller = this;
  helper.enable_fec = enable_fec;
  sndsubflows_iterate(this->subflows, _subflow_change_controlling_mode, &helper);
}


typedef struct{
  guint8                    subflow_id;
  GstClockTime              report_timeout;
}ChangeReportTimeoutHelper;

static void _subflow_change_report_timeout(gpointer item, gpointer udata)
{
  SndSubflow* subflow = item;
  ChangeReportTimeoutHelper* helper = udata;
  if(helper->subflow_id == 0 || helper->subflow_id == 255){
    goto change;
  }
  if(helper->subflow_id != subflow->id){
    goto remain;
  }
change:
  subflow->report_timeout = helper->report_timeout;
remain:
  return;
}

void sndctrler_setup_report_timeout(SndController * this, guint8 subflow_id, GstClockTime report_timeout)
{
  ChangeReportTimeoutHelper helper;
  helper.report_timeout = report_timeout;
  helper.subflow_id = subflow_id;
  sndsubflows_iterate(this->subflows, _subflow_change_report_timeout, &helper);
}


void
sndctrler_setup (SndController   *this,
                 StreamSplitter  *splitter,
                 SendingRateDistributor *sndratedistor,
                 FECEncoder      *fecencoder)
{


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

  //TODO: Here report timeout check stuff

  for(it = this->controllers; it; it = it->next){
    CongestionController* controller = it->data;
    controller->time_updater(controller->udata);
  }
}

void
sndctrler_receive_mprtcp (SndController *this, GstBuffer * buf)
{
  SndSubflow *subflow;
  GstMPRTCPReportSummary *summary;
  GSList* it;
  MPRTPPluginSignalData *msg;

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

  msg = _create_utilization_message(this, subflow, summary);
  g_async_queue_push(this->emitterq, msg);

done:
  return;
}


//---------------------------------------------------------------------------


//------------------------ Outgoing Report Producer -------------------------

static void _orp_producer_helper(SndSubflow *subflow, gpointer udata)
{
  SndController*            this;
  ReportIntervalCalculator* ricalcer;
  GstBuffer*                buf;
  guint                     report_length = 0;

  this     = udata;
  ricalcer = this->ricalcer;

  if(subflow->congestion_controlling_mode == CONGESTION_CONTROLLING_MODE_NONE){
    goto done;
  }

  if(!ricalcer_rtcp_regular_allowed(ricalcer, subflow)){
    goto done;
  }

  report_producer_begin(this->report_producer, subflow->id);
  _orp_add_sr(this, subflow);
  buf = report_producer_end(this->report_producer, &report_length);
  g_async_queue_push(this->mprtcpq, buf);

  //report_length += 12 /* RTCP HEADER*/ + (28<<3) /*UDP+IP HEADER*/;
  //ricalcer_update_avg_report_size(ricaler, report_length);

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
_orp_add_sr (SndController * this, SndSubflow * subflow)
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

MPRTPPluginSignalData* _create_utilization_message(SndController* this, SndSubflow *subflow, GstMPRTCPReportSummary *summary)
{
  MPRTPPluginSignalData* signaldata, *result;
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

  result = g_slice_new0(MPRTPPluginSignalData);
  memcpy(result, signaldata, sizeof(MPRTPPluginSignalData));
  return result;
}

//---------------------- Utility functions ----------------------------------


void _dispose_congestion_controller(SndController* this, CongestionController* controller)
{
  controller->disable(controller->udata);
  controller->dispose(controller->udata);
  g_slist_remove(this->controllers, controller);
  g_slice_free(CongestionController, controller);
}

CongestionController* _create_fbraplus(SndController* this, SndSubflow* subflow)
{
  CongestionController *result = g_slice_new0(CongestionController);
  result->subflow_id = subflow->id;
  result->udata = make_fbrasubctrler(this->rtppackets, this->sndtracker, subflow);
  return result;
}


#undef REPORTTIMEOUT
