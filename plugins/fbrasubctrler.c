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
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "fbrasubctrler.h"
#include "reportproc.h"

GST_DEBUG_CATEGORY_STATIC (fbrasubctrler_debug_category);
#define GST_CAT_DEFAULT fbrasubctrler_debug_category

G_DEFINE_TYPE (FBRASubController, fbrasubctrler, G_TYPE_OBJECT);


//determine the minimum interval in seconds must be stay in probe stage
//before the target considered to be accepted
#define MIN_APPROVE_INTERVAL 50 * GST_MSECOND

//determine the minimum multiplying factor for aprovements
//before the target considered to be accepted
#define APPROVE_MIN_FACTOR 1.0

//determine the maximum multiplying factor for aprovements
//before the target considered to be accepted
#define APPROVE_MAX_FACTOR 2.0

//determines the minimum ramp up bitrate
#define RAMP_UP_MIN_SPEED 50000

//determines the maximum ramp up bitrate
#define RAMP_UP_MAX_SPEED 250000

//Min target_bitrate [bps]
//#define TARGET_BITRATE_MIN 500000
#define TARGET_BITRATE_MIN 100000

//Max target_bitrate [bps] - 0 means infinity
#define TARGET_BITRATE_MAX 0

//approvement epsilon
#define APPROVEMENT_EPSILON 0.25

//interval epsilon
#define INTERVAL_EPSILON 0.25

//determines the minimum monitoring interval
#define MIN_MONITORING_INTERVAL 2

//determines the maximum monitoring interval
#define MAX_MONITORING_INTERVAL 14
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

  GstClockTime        min_approve_interval;
  gdouble             approve_min_factor;
  gdouble             approve_max_factor;
  gint32              min_ramp_up_bitrate;
  gint32              max_ramp_up_bitrate;
  gint32              min_target_bitrate;
  gint32              max_target_bitrate;
  gdouble             approvement_epsilon;
  gint32              min_monitoring_interval;
  gint32              max_monitoring_interval;
  gdouble             interval_epsilon;
};



#define _priv(this) ((Private*)this->priv)
#define _stat(this) this->stat
#define _subflow(this) (this->subflow)

#define _stage(this) _priv(this)->stage
#define _event(this) _priv(this)->event
#define _set_event(this, e) _event(this) = e
#define _set_pending_event(this, e) this->pending_event = e

#define _appr_eps(this)               _priv(this)->approvement_epsilon
#define _interval_eps(this)           _priv(this)->interval_epsilon
#define _min_appr_int(this)           _priv(this)->min_approve_interval
#define _appr_min_fact(this)          _priv(this)->approve_min_factor
#define _appr_max_fact(this)          _priv(this)->approve_max_factor
#define _min_ramp_up(this)            _priv(this)->min_ramp_up_bitrate
#define _max_ramp_up(this)            _priv(this)->max_ramp_up_bitrate
#define _min_target(this)             _priv(this)->min_target_bitrate
#define _max_target(this)             _priv(this)->max_target_bitrate
#define _mon_min_int(this)            _priv(this)->min_monitoring_interval
#define _mon_max_int(this)            _priv(this)->max_monitoring_interval
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

 void fbrasubctrler_finalize (GObject * object);


// const gdouble ST_ = 1.1; //Stable treshold
// const gdouble OT_ = 2.;  //Overused treshold
// const gdouble DT_ = 1.5; //Down Treshold


#define _now(this) (gst_clock_get_time(this->sysclock))

 static void
 _on_rtp_sending(
     FBRASubController* this,
     SndPacket *packet);

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

static void
_update_approvement(
    FBRASubController *this);

static gdouble
_off_target(
    FBRASubController *this,
    gint pow,
    gdouble eps);

static guint
_get_monitoring_interval(
    FBRASubController *this);

static guint
_get_approvement_interval(
    FBRASubController* this);

static void
_execute_stage(
    FBRASubController *this);

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

  sndtracker_rem_on_packet_sent(this->sndtracker, this->subflow->id,
      (ListenerFunc) _on_rtp_sending);

  mprtp_free(this->priv);
  g_object_unref(this->fbprocessor);
  g_free(this->stat);
  g_object_unref(this->sndtracker);
  g_object_unref(this->sysclock);
}


void
fbrasubctrler_init (FBRASubController * this)
{
  this->priv = mprtp_malloc(sizeof(Private));
  this->sysclock = gst_system_clock_obtain();

  _priv(this)->approve_min_factor               = APPROVE_MIN_FACTOR;
  _priv(this)->approve_max_factor               = APPROVE_MAX_FACTOR;
  _priv(this)->min_approve_interval             = MIN_APPROVE_INTERVAL;
  _priv(this)->min_ramp_up_bitrate              = RAMP_UP_MIN_SPEED;
  _priv(this)->max_ramp_up_bitrate              = RAMP_UP_MAX_SPEED;
  _priv(this)->min_target_bitrate               = TARGET_BITRATE_MIN;
  _priv(this)->max_target_bitrate               = TARGET_BITRATE_MAX;

  _priv(this)->approvement_epsilon              = APPROVEMENT_EPSILON;
  _priv(this)->interval_epsilon                 = INTERVAL_EPSILON;

  _priv(this)->min_monitoring_interval          = MIN_MONITORING_INTERVAL;
  _priv(this)->max_monitoring_interval          = MAX_MONITORING_INTERVAL;
}

FBRASubController *make_fbrasubctrler(SndTracker *sndtracker, SndSubflow *subflow)
{
  FBRASubController *this   = g_object_new (FBRASUBCTRLER_TYPE, NULL);

  this->sndtracker          = g_object_ref(sndtracker);
  this->subflow             = subflow;
  this->made                = _now(this);
  this->stat                = g_malloc0(sizeof(FBRAPlusStat));
  this->fbprocessor         = make_fbrafbprocessor(sndtracker, subflow, this->stat);


  sndsubflow_set_state(subflow, SNDSUBFLOW_STATE_STABLE);
  _switch_stage_to(this, STAGE_KEEP, FALSE);

  sndtracker_add_on_packet_sent(this->sndtracker, this->subflow->id,
      (ListenerFunc) _on_rtp_sending, this);

  return this;
}

void fbrasubctrler_enable(FBRASubController *this)
{
  this->enabled    = TRUE;


}

void fbrasubctrler_disable(FBRASubController *this)
{
  _switch_stage_to(this, STAGE_KEEP, FALSE);
  this->enabled = FALSE;

}

void _on_rtp_sending(FBRASubController* this, SndPacket *packet)
{
  gdouble pacing_time;
  gdouble pacing_bitrate;
  gdouble srtt_in_s;
  if(!this->enabled){
    return;
  }
  srtt_in_s = _stat(this)->srtt * .000000001;
  pacing_bitrate = this->cwnd / srtt_in_s;
  pacing_time = (gdouble)packet->payload_size / pacing_bitrate;
  this->subflow->pacing_time = pacing_time * GST_SECOND;
}


void fbrasubctrler_time_update(FBRASubController *this)
{
  if(!this->enabled){
    goto done;
  }

  fbrafbprocessor_time_update(this->fbprocessor);
  _update_approvement(this);

done:
  return;
}

void fbrasubctrler_report_update(
                         FBRASubController *this,
                         GstMPRTCPReportSummary *summary)
{
  GstClockTime max_approve_idle_th;

  if(!this->enabled){
    goto done;
  }

  fbrafbprocessor_report_update(this->fbprocessor, summary);
  _execute_stage(this);

  max_approve_idle_th = CONSTRAIN(100 * GST_MSECOND, 500 * GST_MSECOND, 2 * _stat(this)->srtt);

  if(_subflow(this)->state != SNDSUBFLOW_STATE_OVERUSED){
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
  //consider fix tresholds
  GstClockTime owd_th = _stat(this)->owd_80th +
      CONSTRAIN(30 * GST_MSECOND, 150 * GST_MSECOND, _stat(this)->owd_in_ms_std * 4);

  gint32 BiF_th = _stat(this)->BiF_80th + MAX(5000, _stat(this)->BiF_80th * .2);

  return owd_th < _stat(this)->last_owd || BiF_th < _stat(this)->bytes_in_flight;
}

static gboolean _congestion(FBRASubController *this)
{
  if(this->last_distorted < this->last_settled + CONSTRAIN(100 * GST_MSECOND, GST_SECOND, 1.5 * _stat(this)->srtt)){
    return FALSE;
  }
  return TRUE;
}

void
_reduce_stage(
    FBRASubController *this)
{

  this->cwnd = MAX(10000, _stat(this)->BiF_80th) * 8;

  if(_distortion(this)){
    //keep undershooting?
    goto done;
  }

  if(!this->target_approvement){
    goto done;
  }

  _set_event(this, EVENT_SETTLED);
  _switch_stage_to(this, STAGE_KEEP, FALSE);
done:
  return;
}

void
_keep_stage(
    FBRASubController *this)
{

  this->cwnd = MAX(10000, _stat(this)->BiF_max * 1.5) * 8;

  if(_congestion(this)){
    _set_event(this, EVENT_CONGESTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    goto done;
  }

  if(_distortion(this)){
    _set_event(this, EVENT_DISTORTION);
    goto done;
  }else if(_subflow(this)->state != SNDSUBFLOW_STATE_STABLE){
    _set_event(this, EVENT_SETTLED);
    goto done;
  }

  if(!this->target_approvement){
    goto done;
  }else if(_now(this) - _stat(this)->srtt < this->last_distorted){
    goto done;
  }

  _switch_stage_to(this, STAGE_PROBE, FALSE);
done:
  return;
}

void
_probe_stage(
    FBRASubController *this)
{

  this->cwnd = MAX(10000, _stat(this)->BiF_max * 1.5) * 8;

  if(_distortion(this)){
    _set_event(this, EVENT_DISTORTION);
    _switch_stage_to(this, STAGE_KEEP, FALSE);
    goto done;
  }

  if(!this->target_approvement){
    goto done;
  }

  _switch_stage_to(this, STAGE_INCREASE, FALSE);
  _set_event(this, EVENT_READY);
done:
  return;
}

void
_increase_stage(
    FBRASubController *this)
{

  this->cwnd = MAX(10000, _stat(this)->BiF_max * 2) * 8  + this->delta_target;

  if(_distortion(this)){
    _set_event(this, EVENT_DISTORTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    goto done;
  }

  if(this->increasement_started < _now(this) - MAX(300 * GST_MSECOND, 5 * _stat(this)->srtt)){
    _switch_stage_to(this, STAGE_PROBE, FALSE);
    goto done;
  }

  if(!this->target_approvement){
    goto done;
  }

  _switch_stage_to(this, STAGE_PROBE, FALSE);
done:
  return;
}

void _execute_stage(FBRASubController *this)
{
  if(this->pending_event != EVENT_FI){
    _fire(this, this->pending_event);
    this->pending_event = EVENT_FI;
  }

  //Execute stage
  this->stage_fnc(this);
  _fire(this, _event(this));
  _priv(this)->event = EVENT_FI;
  this->last_executed = _now(this);
  ++this->rcved_fb_since_changed;

  return;
}


void
_fire(
    FBRASubController *this,
    Event event)
{

  switch(_subflow(this)->state){
    case SNDSUBFLOW_STATE_OVERUSED:
      switch(event){
        case EVENT_CONGESTION:
        break;
        case EVENT_SETTLED:
          this->last_settled = _now(this);
          sndsubflow_set_state(this->subflow, SNDSUBFLOW_STATE_STABLE);

          //bounce back target
        break;
        case EVENT_FI:
        default:
        break;
      }
    break;
    case SNDSUBFLOW_STATE_STABLE:
      switch(event){
        case EVENT_CONGESTION:
          //undershoot target
          this->last_distorted = _now(this);
          sndsubflow_set_state(this->subflow, SNDSUBFLOW_STATE_OVERUSED);
        break;
        case EVENT_DISTORTION:
          //corrigate target

          this->last_distorted = _now(this);
          sndsubflow_set_state(this->subflow, SNDSUBFLOW_STATE_OVERUSED);
          break;
        case EVENT_READY:
          sndsubflow_set_state(this->subflow, SNDSUBFLOW_STATE_UNDERUSED);
          break;
        case EVENT_SETTLED:
          this->last_settled = _now(this);
          break;
        case EVENT_FI:
        default:
        break;
      }
    break;
    case SNDSUBFLOW_STATE_UNDERUSED:
      switch(event){
        case EVENT_CONGESTION:
        case EVENT_DISTORTION:
          //revert and corrigate
          this->last_distorted = _now(this);
          sndsubflow_set_state(this->subflow, SNDSUBFLOW_STATE_OVERUSED);
        break;
        case EVENT_SETTLED:
          this->last_settled = _now(this);
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
  this->rcved_fb_since_changed = 0;
}


void _update_approvement(FBRASubController *this)
{
  //stalled bytes!
  gint32 min_desired_margin = MIN(this->desired_bitrate * 0.9, this->desired_bitrate - 100000);
//  gint32 max_desired_margin = MAX(this->desired_bitrate * 1.1, this->desired_bitrate + 100000);
  gint32 boundary;
  gint32 approving_interval;

  SndSubflowState state = sndsubflow_get_state(this->subflow);

  if(state == SNDSUBFLOW_STATE_OVERUSED && _stat(this)->receiver_bitrate < min_desired_margin){
    this->bottleneck_point = _stat(this)->receiver_bitrate;
    this->desired_bitrate = this->bottleneck_point * .6;
    goto done;
  }

  if(this->target_changed < this->target_reached){
    goto done;
  }

  if(this->rcved_fb_since_changed < 1){
    goto done;
  }

  boundary = CONSTRAIN(10000, 50000, this->desired_bitrate * _priv(this)->approvement_epsilon);

  if(_stat(this)->receiver_bitrate < this->desired_bitrate - boundary){
    goto done;
  }

  if(boundary + this->desired_bitrate < _stat(this)->receiver_bitrate){
    goto done;
  }

  if(_stat(this)->sender_bitrate < this->desired_bitrate - boundary){
    goto done;
  }

  if(boundary + this->desired_bitrate < _stat(this)->sender_bitrate){
    goto done;
  }

  boundary = MAX(this->delta_target - 20000, this->delta_target * (1.-_priv(this)->approvement_epsilon));
  if(0 < this->delta_target && _stat(this)->receiver_bitrate < this->stable_bitrate + boundary){
    goto done;
  }

  if(this->target_reached < this->target_changed){
    this->target_reached = _now(this);
    goto done;
  }

  DISABLE_LINE approving_interval = _get_monitoring_interval(this);
  approving_interval = _get_approvement_interval(this);
  if(_now(this) < this->target_reached + approving_interval){
    goto done;
  }
  this->target_approvement = TRUE;

done:
  return;
}


gdouble _off_target(FBRASubController *this, gint pow, gdouble eps)
{
  gint32 refpoint;
  gdouble result;
  gint i;
  refpoint = MAX(_min_target(this), this->bottleneck_point);
  if(this->desired_bitrate <= refpoint){
    return 0.;
  }
  result = this->desired_bitrate - refpoint;
  result /= this->desired_bitrate * eps;

  for(i=1; i<pow; ++i) result*=result;

  result = CONSTRAIN(0.,1., result);

  return result;
}


guint _get_monitoring_interval(FBRASubController *this)
{
  guint interval;
  gdouble off;
  gdouble epsilon;
  //  off = _off_target(this, 1, _mon_eps(this));

  {
    gdouble refpoint;
    refpoint = MAX(_min_target(this), this->bottleneck_point);
    epsilon = MIN(.25, (gdouble) _max_ramp_up(this) / refpoint);
  }

//  off = _off_target(this, 2, _mon_eps(this));
  off = _off_target(this, 2, epsilon);

  interval = off * _mon_min_int(this) + (1.-off) * _mon_max_int(this);

  while(this->desired_bitrate / interval < _min_ramp_up(this) && _mon_min_int(this) < interval){
    --interval;
  }
  while(_max_ramp_up(this) < this->desired_bitrate / interval && interval < _mon_max_int(this)){
    ++interval;
  }

  return interval;
}

guint _get_approvement_interval(FBRASubController* this)
{
  gdouble off;
  gdouble interval;

  off = _off_target(this, 2, _interval_eps(this));
  interval = off * _appr_min_fact(this) + (1.-off) * _appr_max_fact(this);
  return CONSTRAIN(.1 * GST_SECOND,  .6 * GST_SECOND, interval * _stat(this)->srtt);
}
