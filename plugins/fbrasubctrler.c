/* GStreamer
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
#include "fbrasubctrler.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

//#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
//#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
//#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
//#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define THIS_READLOCK(this)
#define THIS_READUNLOCK(this)
#define THIS_WRITELOCK(this)
#define THIS_WRITEUNLOCK(this)

GST_DEBUG_CATEGORY_STATIC (fbrasubctrler_debug_category);
#define GST_CAT_DEFAULT fbrasubctrler_debug_category

G_DEFINE_TYPE (FBRASubController, fbrasubctrler, G_TYPE_OBJECT);


////determines the treshold for utilization, in which below the path considered to be congested
//#define DISCARD_CONGESTION_MAX_TRESHOLD 0.1
//
////determines the treshold for utilization, in which below the path considered to be distorted
//#define DISCARD_DISTORTION_MAX_TRESHOLD 0.0
//
////determines a treshold for trend calculations, in which above the KEEP stage not let it to PROBE
//#define OWD_CORR_DISTORTION_TRESHOLD 1.05
//
////determines a treshold for trend calculations, in which above the path considered to be congested
//#define OWD_CORR_CONGESTION_TRESHOLD 1.2

////determines a treshold for trend calculations, in which above the KEEP stage not let it to PROBE
//#define REACTIVE_CONGESTION_DETECTION_ALLOWED TRUE

typedef struct _Private Private;

typedef enum{
  EVENT_CONGESTION           = -2,
  EVENT_DISTORTION           = -1,
  EVENT_FI                   =  0,
  EVENT_SETTLED              =  1,
  EVENT_READY                =  2,
}Event;

typedef enum{
  STAGE_REDUCE             = -1,
  STAGE_KEEP               =  0,
  STAGE_PROBE              =  1,
  STAGE_INCREASE           =  2,
}Stage;


struct _Private{
  GstClockTime        time;
  Event               event;
  Stage               stage;
  gdouble             avg_rtp_payload;

};



#define _priv(this) ((Private*)this->priv)
#define _fbstat(this) this->fbstat
#define _throttler(this) _priv(this)->throttler

#define _btlp(this) this->bottleneck_point
#define _mon_int(this) this->monitoring_interval
#define _mon_br(this) this->monitored_bitrate
#define _qdelay_processed(this) _fbstat(this).owd_processed
#define _rdiscards(this) _fbstat(this).recent_discards

#define _state_t1(this) this->state_t1
#define _stage(this) _priv(this)->stage
#define _stage_t1(this) _priv(this)->stage_t1
#define _event(this) _priv(this)->event
#define _set_event(this, e) _event(this) = e
#define _set_pending_event(this, e) this->pending_event = e

#define _stability_th(this) _priv(this)->stability_treshold

#define _SR(this) (_fbstat(this).sent_bytes_in_1s * 8)
#define _GP(this) (_fbstat(this).goodput_bytes * 8)
#define _GP_t1(this) (_priv(this)->goodput_bitrate_t1)
#define _min_br(this) MIN(_SR(this), _TR(this))
#define _max_br(this) MAX(_SR(this), _TR(this))
#define _owd_corr(this) _fbstat(this).owd_corr

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

 void fbrasubctrler_finalize (GObject * object);


// const gdouble ST_ = 1.1; //Stable treshold
// const gdouble OT_ = 2.;  //Overused treshold
// const gdouble DT_ = 1.5; //Down Treshold


#define _now(this) (gst_clock_get_time(this->sysclock))

static void
_reduce_stage(
    FBRASubController *this);

static void
_keep_stage(
    FBRASubController *this);

static void
_probe_stage(
    FBRASubController *this);

static void
_increase_stage(
    FBRASubController *this);

static void
_switch_stage_to(
    FBRASubController *this,
    Stage target,
    gboolean execute);

#define _transit_state_to(this, target) \
   this->state_t1 = this->state;        \
   this->state = target;                \


static void
_fire(
    FBRASubController *this,
    Event event);

#define _disable_monitoring(this) _set_monitoring_interval(this, 0)


//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
fbrasubctrler_class_init (FBRASubControllerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fbrasubctrler_finalize;

  GST_DEBUG_CATEGORY_INIT (fbrasubctrler_debug_category, "fbrasubctrler", 0,
      "FBRA+ Subflow Rate Controller");

}


void
fbrasubctrler_finalize (GObject * object)
{
  FBRASubController *this;
  this = FBRASUBCTRLER(object);
  mprtp_free(this->priv);
  g_object_unref(this->fbprocessor);
  g_object_unref(this->sysclock);
}


void
fbrasubctrler_init (FBRASubController * this)
{
  this->priv = mprtp_malloc(sizeof(Private));
  this->sysclock = gst_system_clock_obtain();
}

GstClockTime last_pacing;
gboolean fbrasubctrler_path_approver(gpointer data, RTPPacket *packet)
{
  FBRASubController *this = data;
  guint payload_size = packet->payload_size;
  GstClockTime now = _now(this);

  {
//    gdouble cwnd = _fbstat(this).BiF.median * .6;
    gdouble rtt_in_s = (gdouble)_RTT(this) / (gdouble) GST_SECOND;
    gdouble pacing_bitrate = MAX(200000., (this->cwnd * 8.) / rtt_in_s);
    gdouble pacing_time = (gdouble) ((payload_size + 24) * 8) / (gdouble)pacing_bitrate;
    g_print("rtp_packet: %d (%hu) | rtt_in_s: %f, pacing_bitrate: %f, pacing_time: %f | dt: %-1.4f\n",
    		(payload_size + 24) * 8,
			packet->abs_seq,
			rtt_in_s,
			pacing_bitrate,
			pacing_time,
			(gdouble) GST_TIME_AS_USECONDS(now - last_pacing));
    last_pacing = now;
    _priv(this)->pacing_approve_time = now + (pacing_time * (gdouble)GST_SECOND);
    goto approve;
  }

approve:
  this->last_rtp_size = payload_size;
  fbratargetctrler_update_rtpavg(this->targetctrler, this->last_rtp_size);
  return TRUE;
disapprove:
  return FALSE;
//  g_print("BiF: %d->%d (%1.3f - %1.3f)\n",
//          _fbstat(this).sent_bytes_in_1s * 8,
//          _fbstat(this).bytes_in_flight * 8,
//          (gdouble)_fbstat(this).bytes_in_flight / (gdouble)_fbstat(this).sent_bytes_in_1s,
//          (gdouble)_fbstat(this).sent_bytes_in_1s * (gdouble)(_fbstat(this).owd_stt * .000000001) * 8);
  return TRUE;

}


FBRASubController *make_fbrasubctrler(SndSubflow *subflow, SndTracker *sndtracker)
{
  FBRASubController *this;
  this                      = g_object_new (FBRASUBCTRLER_TYPE, NULL);
  this->sndtracker          = g_object_unref(sndtracker);
  this->subflow             = subflow;
  this->made                = _now(this);


  subflow->state = SNDSUBFLOW_STATE_STABLE;
  _switch_stage_to(this, STAGE_KEEP, FALSE);

  return result;
}

void fbrasubctrler_enable(FBRASubController *this)
{
  GstClockTime now = _now(this);
  this->enabled             = TRUE;

}

void fbrasubctrler_disable(FBRASubController *this)
{
  _switch_stage_to(this, STAGE_KEEP, FALSE);
  this->measurements_num = 0;
  this->enabled = FALSE;
}


static void _execute_stage(FBRASubController *this)
{
  if(this->measurements_num < 5){
    goto done;
  }

  if(_now(this) - _priv(this)->adjust_th < this->last_executed){
    goto done;
  }

  if(this->pending_event != EVENT_FI){
    _fire(this, this->pending_event);
    this->pending_event = EVENT_FI;
  }
  //Execute stage
  this->stage_fnc(this);
  _fire(this, _event(this));
  _priv(this)->event = EVENT_FI;
  _priv(this)->controlled = TRUE;
  this->last_executed = _now(this);

done:
  return;
}

gboolean fbrasubctrler_time_update(FBRASubController *this)
{
  GstClockTime fbinterval_th;

  //TODO: update sender_rate

  if(!this->enabled || _now(this) < this->disable_end || this->measurements_num < 5){
    goto done;
  }

  this->monitored_bitrate = mprtps_path_get_monitored_bitrate(this->path, &this->monitored_packets);
  fbinterval_th = 150 * GST_MSECOND;

  if(!_bcongestion(this) && this->last_fb_arrived < _now(this) - fbinterval_th){
    fbratargetctrler_break(this->targetctrler);
    _switch_stage_to(this, STAGE_KEEP, FALSE);
    g_print("backward congestion fbinterval: %lu\n", fbinterval_th);
    _bcongestion(this) = TRUE;
    goto done;
  }

done:
  return FALSE;
}

GstClockTime start = 0;
GstClockTime previous = 0;
void fbrasubctrler_report_update(
                         FBRASubController *this,
                         GstMPRTCPReportSummary *summary)
{
  GstClockTime max_approve_idle_th;

  if(!this->enabled){
    goto done;
  }

  _execute_stage(this);

  max_approve_idle_th = CONSTRAIN(100 * GST_MSECOND, 500 * GST_MSECOND, 2 * _RTT(this));

  if(_state(this) != SNDSUBFLOW_STATE_OVERUSED){
      fbrafbprocessor_approve_measurement(this->fbprocessor);
      this->last_approved = _now(this);
  }else if(this->last_approved < _now(this) - max_approve_idle_th){
      fbrafbprocessor_approve_measurement(this->fbprocessor);
  }

done:
  return;
}

static gboolean _distortion(FBRASubController *this)
{
  GstClockTime owd_th;

//  if(300 * GST_MSECOND < _fbstat(this).owd_ltt80){
//	  return FALSE;
//  }
  owd_th = _fbstat(this).owd_ltt80 + CONSTRAIN(30 * GST_MSECOND,
		  	  	  	  	  	  	  	  	  	   150 * GST_MSECOND,
											   _fbstat(this).owd_th_dist_in_ms * GST_MSECOND);
  if(owd_th < _fbstat(this).owd_stt){
    return TRUE;
  }
//  This approach based on density of the distortion of the path
//  if(.2 < _fbstat(this).overused_avg){
//    return TRUE;
//  }

  return FALSE;
}

static gboolean _congestion(FBRASubController *this)
{
//  gdouble FD_th;

  {
    GstClockTime owd_th;
    owd_th = _fbstat(this).owd_ltt80 + CONSTRAIN(100 * GST_MSECOND,
    											 300 * GST_MSECOND,
												 _fbstat(this).owd_th_cng_in_ms * GST_MSECOND);
    if(owd_th < _fbstat(this).owd_stt){
      return TRUE;
    }
  }

//  FD_th = CONSTRAIN(.02, .1, _fbstat(this).FD_avg * 2);
//  if(FD_th < _FD(this)){
//    return TRUE;
//  }
  return FALSE;
}

void
_reduce_stage(
    FBRASubController *this)
{
  if(_congestion(this)){
    fbratargetctrler_break(this->targetctrler);
  }else if(!fbratargetctrler_get_approvement(this->targetctrler)){
    goto done;
  }

  _set_event(this, EVENT_SETTLED);
  _switch_stage_to(this, STAGE_KEEP, FALSE);
done:
  fbratargetctrler_refresh_target(this->targetctrler);
  return;
}

void
_keep_stage(
    FBRASubController *this)
{
  GstClockTime now = _now(this);

  if(_congestion(this)){
    //fbratargetctrler_break(this->targetctrler);
    _set_event(this, EVENT_CONGESTION);
    //_switch_stage_to(this, STAGE_REDUCE, FALSE);
    goto done;
  }

  if(_distortion(this)){
    //fbratargetctrler_halt(this->targetctrler);
    _set_event(this, EVENT_DISTORTION);
    this->last_distorted = now;
    goto done;
  }else if(_state(this) != SNDSUBFLOW_STATE_STABLE){
    _set_event(this, EVENT_SETTLED);
    goto done;
  }

  if(!fbratargetctrler_get_approvement(this->targetctrler)){
    goto done;
  }else if(now - _RTT(this) < this->last_distorted){
    goto done;
  }

  //fbratargetctrler_probe(this->targetctrler);
  //_switch_stage_to(this, STAGE_PROBE, FALSE);
done:
  return;
}

void
_probe_stage(
    FBRASubController *this)
{
  if(_congestion(this)){
    fbratargetctrler_break(this->targetctrler);
    _set_event(this, EVENT_CONGESTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    goto done;
  }

  if(_distortion(this)){
    fbratargetctrler_revert(this->targetctrler);
    _set_event(this, EVENT_DISTORTION);
    _switch_stage_to(this, STAGE_KEEP, FALSE);
    this->last_distorted = _now(this);
    goto done;
  }

  if(!fbratargetctrler_get_approvement(this->targetctrler)){
    goto done;
  }

  fbratargetctrler_accelerate(this->targetctrler);
  fbratargetctrler_probe(this->targetctrler);
  _switch_stage_to(this, STAGE_INCREASE, FALSE);
  _set_event(this, EVENT_READY);
done:
  fbratargetctrler_refresh_target(this->targetctrler);
  return;
}

void
_increase_stage(
    FBRASubController *this)
{
  if(_congestion(this)){
    fbratargetctrler_break(this->targetctrler);
    _set_event(this, EVENT_CONGESTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    goto done;
  }

  if(_distortion(this)){
    fbratargetctrler_revert(this->targetctrler);
    _set_event(this, EVENT_DISTORTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    goto done;
  }

//  if(this->increasement_started < _now(this) - CONSTRAIN(50 * GST_MSECOND, 3 * GST_SECOND, 3 * _RTT(this))){
  if(this->increasement_started < _now(this) - MAX(300 * GST_MSECOND, 5 * _RTT(this))){
    fbratargetctrler_revert(this->targetctrler);
    _switch_stage_to(this, STAGE_PROBE, FALSE);
    goto done;
  }

  if(!fbratargetctrler_get_approvement(this->targetctrler)){
    goto done;
  }

  _switch_stage_to(this, STAGE_PROBE, FALSE);
done:
  fbratargetctrler_refresh_target(this->targetctrler);
  return;
}

void
_fire(
    FBRASubController *this,
    Event event)
{
  switch(_state(this)){
    case SNDSUBFLOW_STATE_OVERUSED:
      switch(event){
        case EVENT_CONGESTION:
        break;
        case EVENT_SETTLED:
          sndsubflow_set_state(this->subflow, SNDSUBFLOW_STATE_STABLE);
        break;
        case EVENT_FI:
        default:
        break;
      }
    break;
    case SNDSUBFLOW_STATE_STABLE:
      switch(event){
        case EVENT_CONGESTION:
          sndsubflow_set_state(this->subflow, SNDSUBFLOW_STATE_OVERUSED);
        break;
        case EVENT_DISTORTION:
          sndsubflow_set_state(this->subflow, SNDSUBFLOW_STATE_OVERUSED);
          break;
        case EVENT_READY:
          sndsubflow_set_state(this->subflow, SNDSUBFLOW_STATE_UNDERUSED);
          break;
        case EVENT_SETTLED:
        case EVENT_FI:
        default:
        break;
      }
    break;
    case SNDSUBFLOW_STATE_UNDERUSED:
      switch(event){
        case EVENT_CONGESTION:
          sndsubflow_set_state(this->subflow, SNDSUBFLOW_STATE_OVERUSED);
        break;
        case EVENT_DISTORTION:
          sndsubflow_set_state(this->subflow, SNDSUBFLOW_STATE_OVERUSED);
        break;
        case EVENT_SETTLED:
          sndsubflow_set_state(this->subflow, SNDSUBFLOW_STATE_STABLE);
        break;
        case EVENT_FI:
        default:
        break;
      }
    break;
    default:
      g_warning("The desired state not exists.");
      break;
  }
}


void _switch_stage_to(
    FBRASubController *this,
    Stage target,
    gboolean execute)
{
  switch(target){
     case STAGE_KEEP:
       this->stage_fnc = _keep_stage;
     break;
     case STAGE_REDUCE:
       this->congestion_detected = _now(this);
       this->stage_fnc = _reduce_stage;
     break;
     case STAGE_INCREASE:
    this->increasement_started = _now(this);
       this->stage_fnc = _increase_stage;
     break;
     case STAGE_PROBE:
       this->stage_fnc = _probe_stage;
     break;
   }
  _priv(this)->stage         = target;
  if(execute){
      this->stage_fnc(this);
  }
}



