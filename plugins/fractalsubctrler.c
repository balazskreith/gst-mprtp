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
#include "fractalsubctrler.h"
#include "reportproc.h"

GST_DEBUG_CATEGORY_STATIC (fractalsubctrler_debug_category);
#define GST_CAT_DEFAULT fractalsubctrler_debug_category

G_DEFINE_TYPE (FRACTaLSubController, fractalsubctrler, G_TYPE_OBJECT);


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

//epsilon value for target or monitoring time approvement
#define APPROVEMENT_EPSILON 0.25

//mininmal pacing bitrate
#define MIN_PACING_BITRATE 50000

//determines the minimum monitoring interval
#define MIN_MONITORING_INTERVAL 2

//determines the maximum monitoring interval
#define MAX_MONITORING_INTERVAL 14

//determines the maximal treshold for fractional lost
#define MAX_FL_TRESHOLD 0.0

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
  gdouble             appr_interval_epsilon;
  gint32              min_monitoring_interval;
  gint32              max_monitoring_interval;
  gdouble             min_pacing_bitrate;

  gdouble             max_FL_treshold;

};



#define _priv(this) ((Private*)this->priv)
#define _stat(this) this->stat
#define _approvement(this) this->approvement
#define _subflow(this) (this->subflow)

#define _stage(this) _priv(this)->stage
#define _event(this) _priv(this)->event
#define _set_event(this, e) _event(this) = e
#define _set_pending_event(this, e) this->pending_event = e

#define _min_pacing_bitrate(this)     _priv(this)->min_pacing_bitrate
#define _appr_interval_eps(this)      _priv(this)->appr_interval_epsilon
#define _min_appr_int(this)           _priv(this)->min_approve_interval
#define _appr_min_fact(this)          _priv(this)->approve_min_factor
#define _appr_max_fact(this)          _priv(this)->approve_max_factor
#define _min_ramp_up(this)            _priv(this)->min_ramp_up_bitrate
#define _max_ramp_up(this)            _priv(this)->max_ramp_up_bitrate
#define _min_target(this)             _priv(this)->min_target_bitrate
#define _max_target(this)             _priv(this)->max_target_bitrate
#define _mon_min_int(this)            _priv(this)->min_monitoring_interval
#define _mon_max_int(this)            _priv(this)->max_monitoring_interval
#define _max_FL_th(this)              _priv(this)->max_FL_treshold
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

 void fractalsubctrler_finalize (GObject * object);


// const gdouble ST_ = 1.1; //Stable treshold
// const gdouble OT_ = 2.;  //Overused treshold
// const gdouble DT_ = 1.5; //Down Treshold


#define _now(this) (gst_clock_get_time(this->sysclock))

 static gboolean
 _rtp_sending_filter(
     FRACTaLSubController* this,
     SndPacket *packet);

 static void
 _on_rtp_sending(
     FRACTaLSubController* this,
     SndPacket *packet);

static void
_reduce_stage(
    FRACTaLSubController *this);

static void
_keep_stage(
    FRACTaLSubController *this);

static void
_probe_stage(
    FRACTaLSubController *this);

static void
_increase_stage(
    FRACTaLSubController *this);

static void
_switch_stage_to(
    FRACTaLSubController *this,
    Stage target,
    gboolean execute);

static void
_set_keeping_point(
    FRACTaLSubController *this,
    gint32 bitrate);

static void
_set_bottleneck_point(
    FRACTaLSubController *this,
    gint32 bitrate);

static void
_refresh_monitoring_approvement(
    FRACTaLSubController *this);

static void
_start_monitoring(
    FRACTaLSubController *this);

static void
_stop_monitoring(
    FRACTaLSubController *this);

static void
_refresh_increasing_approvement(
    FRACTaLSubController *this);

static void
_refresh_reducing_approvement(
    FRACTaLSubController *this);

static void
_start_increasement(
    FRACTaLSubController *this);

static guint
_get_approvement_interval(
    FRACTaLSubController* this);

static guint
_get_monitoring_interval(
    FRACTaLSubController* this);

static void
_change_sndsubflow_target_bitrate(
    FRACTaLSubController* this,
    gint32 new_target);

static void
_logging(
    FRACTaLSubController* this);

static void
_execute_stage(
    FRACTaLSubController *this);

static void
_fire(
    FRACTaLSubController *this,
    Event event);

#define _disable_monitoring(this) _start_monitoring(this, 0)


//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
fractalsubctrler_class_init (FRACTaLSubControllerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fractalsubctrler_finalize;

  GST_DEBUG_CATEGORY_INIT (fractalsubctrler_debug_category, "fractalsubctrler", 0,
      "FRACTAL+ Subflow Rate Controller");

}


void
fractalsubctrler_finalize (GObject * object)
{
  FRACTaLSubController *this;
  this = FRACTALSUBCTRLER(object);

  sndtracker_rem_on_packet_sent(this->sndtracker, (ListenerFunc) _on_rtp_sending);

  mprtp_free(this->priv);

  g_object_unref(this->fbprocessor);
  g_free(this->stat);
  g_free(this->approvement);
  g_object_unref(this->sndtracker);
  g_object_unref(this->sysclock);
}


void
fractalsubctrler_init (FRACTaLSubController * this)
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

  _priv(this)->appr_interval_epsilon            = APPROVEMENT_EPSILON;
  _priv(this)->min_pacing_bitrate               = MIN_PACING_BITRATE;

  _priv(this)->min_monitoring_interval          = MIN_MONITORING_INTERVAL;
  _priv(this)->max_monitoring_interval          = MAX_MONITORING_INTERVAL;
  _priv(this)->max_FL_treshold                  = MAX_FL_TRESHOLD;
}

FRACTaLSubController *make_fractalsubctrler(SndTracker *sndtracker, SndSubflow *subflow)
{
  FRACTaLSubController *this   = g_object_new (FRACTALSUBCTRLER_TYPE, NULL);

  this->sndtracker          = g_object_ref(sndtracker);
  this->subflow             = subflow;
  this->made                = _now(this);
  this->stat                = g_malloc0(sizeof(FRACTaLStat));
  this->approvement         = g_malloc0(sizeof(FRACTaLApprovement));
  this->fbprocessor         = make_fractalfbprocessor(sndtracker, subflow, this->stat, this->approvement);

  sndsubflow_set_state(subflow, SNDSUBFLOW_STATE_STABLE);
  _switch_stage_to(this, STAGE_KEEP, FALSE);

  sndtracker_add_on_packet_sent_with_filter(this->sndtracker,
      (ListenerFunc) _on_rtp_sending,
      (ListenerFilterFunc) _rtp_sending_filter,
      this);

  return this;
}

void fractalsubctrler_enable(FRACTaLSubController *this)
{
  this->enabled    = TRUE;


}

void fractalsubctrler_disable(FRACTaLSubController *this)
{
  _switch_stage_to(this, STAGE_KEEP, FALSE);
  this->enabled = FALSE;

}

gboolean _rtp_sending_filter(FRACTaLSubController* this, SndPacket *packet)
{
  return this->subflow->id == packet->subflow_id;
}

void _on_rtp_sending(FRACTaLSubController* this, SndPacket *packet)
{
  gdouble pacing_time = 0.;
  gdouble pacing_bitrate;
  gdouble srtt_in_s;

  ++this->sent_packets;
  if(!this->enabled || this->stat->measurements_num < 10){
    return;
  }

  srtt_in_s = _stat(this)->srtt * .000000001;
  pacing_bitrate = 0. < srtt_in_s ? this->cwnd / srtt_in_s : 50000.;
  pacing_time = (gdouble)packet->payload_size / pacing_bitrate;
  this->subflow->pacing_time = _now(this) + pacing_time * GST_SECOND;
//  g_print("pacing_time: %f/%f=%f\n", (gdouble)packet->payload_size, pacing_bitrate, pacing_time);

  if(0 < this->monitoring_interval && this->sent_packets % this->monitoring_interval == 0){
    sndsubflow_monitoring_request(this->subflow);
  }
}


void fractalsubctrler_time_update(FRACTaLSubController *this)
{
  gdouble sr_corr_ratio;
  gdouble rtpqdelay_factor;

  if(!this->enabled){
    goto done;
  }

  if(!this->backward_congestion && this->last_report < _now(this) - MAX(.5 * GST_SECOND, 3 * _stat(this)->srtt)){

    GST_WARNING_OBJECT(this, "Backward congestion on subflow %d", this->subflow->id);

    _stop_monitoring(this);
    _set_event(this, EVENT_DISTORTION);
    _switch_stage_to(this, STAGE_KEEP, TRUE);
    _change_sndsubflow_target_bitrate(this, this->keeping_point);
    this->backward_congestion = TRUE;
    goto done;
  }else if(this->backward_congestion){
    goto done;
  }

  DISABLE_LINE _logging(this);

  fractalfbprocessor_time_update(this->fbprocessor);

  if(_stat(this)->measurements_num < 10){
    this->bottleneck_point = this->target_bitrate = _stat(this)->sender_bitrate;
    goto done;
  }


  rtpqdelay_factor = CONSTRAIN(1., 2., (gdouble) _stat(this)->delay_in_rtpqueue / (gdouble) (20 * GST_MSECOND));

  switch(this->subflow->state){
    case SNDSUBFLOW_STATE_OVERUSED:
      {
        gint32 corrigation = MAX(0, _stat(this)->bytes_in_flight - _stat(this)->BiF_80th);
        _change_sndsubflow_target_bitrate(this, this->keeping_point * _stat(this)->owd_log_corr - corrigation);
        sr_corr_ratio    = CONSTRAIN(.5, 1.5, this->target_bitrate / _stat(this)->sender_bitrate);
        this->cwnd = this->awnd * sr_corr_ratio;
      }
      break;
    case SNDSUBFLOW_STATE_STABLE:
      {
        this->cwnd = this->awnd * rtpqdelay_factor;
//        _change_sndsubflow_target_bitrate(this, new_target);
        if(0. < _stat(this)->FL_50th && !this->monitoring_interval){
          this->monitoring_interval = _mon_min_int(this);
        }
      }
      break;
    case SNDSUBFLOW_STATE_UNDERUSED:
    {
      this->cwnd = this->awnd * (1. + rtpqdelay_factor);
    }
      break;
    default:
      break;
  }

  this->cwnd = MAX(_min_pacing_bitrate(this), this->cwnd);

done:
  return;
}

static void _stat_print(FRACTaLSubController *this)
{
  FRACTaLStat *stat = this->stat;
  g_print("BiF:%-3d %-3d->%-3.0f|FEC:%-4d|SR:%-4d|RR:%-4d|Tr:%-4d|Btl:%-4d|%d-%d-%d-%d|OWD:%-3lu+%-3lu (%-3lu) %1.1f-%-1.1f|FL:%-1.2f+%1.2f (%-1.2f)\n",
      stat->bytes_in_flight / 125,
      stat->newly_acked_bytes / 125,
      this->cwnd / 1000,

      stat->fec_bitrate / 1000,
//      stat->sender_bitrate / 1000,
      (gint32)(stat->sr_avg / 1000),
//      stat->receiver_bitrate / 1000,
      (gint32)(stat->rr_avg / 1000),
      this->target_bitrate / 1000,
      this->bottleneck_point / 1000,

      _priv(this)->stage,
      this->subflow->state,
      this->monitoring_approved,
      this->increasing_approved,

      GST_TIME_AS_MSECONDS(_stat(this)->owd_50th),
      GST_TIME_AS_MSECONDS(_stat(this)->owd_std),
      GST_TIME_AS_MSECONDS(_stat(this)->last_owd),
      GST_TIME_AS_MSECONDS(_now(this) - this->made) / 1000.,

      stat->owd_log_corr,
      stat->FL_50th,
      stat->FL_std,
      stat->FL_in_1s
      );

//  g_print("SR: %d | Stalled bytes:%d\n",
//      stat->sender_bitrate,
//      stat->stalled_bytes);
}

void fractalsubctrler_report_update(
                         FRACTaLSubController *this,
                         GstMPRTCPReportSummary *summary)
{
  GstClockTime max_approve_idle_th;
  if(!this->enabled){
    goto done;
  }

  this->backward_congestion = FALSE;
  this->last_report = _now(this);

  fractalfbprocessor_report_update(this->fbprocessor, summary);

  DISABLE_LINE _stat_print(this);

  this->approve_measurement  = FALSE;
  if(10 < _stat(this)->measurements_num){
    _execute_stage(this);
  }
  this->approve_measurement |= _subflow(this)->state != SNDSUBFLOW_STATE_OVERUSED;

  max_approve_idle_th = CONSTRAIN(3 * GST_SECOND, 10 * GST_SECOND, 10 * _stat(this)->srtt);
  if(this->last_approved < _now(this) - max_approve_idle_th){
    this->approve_measurement = TRUE;
  }
//  if(_subflow(this)->state != SNDSUBFLOW_STATE_OVERUSED){
//      fractalfbprocessor_approve_measurement(this->fbprocessor);
//      this->last_approved = _now(this);
//  }else if(this->last_approved < _now(this) - max_approve_idle_th){
//      //fractalfbprocessor_approve_measurement(this->fbprocessor);
//  }

  if(this->approve_measurement){
    fractalfbprocessor_approve_measurement(this->fbprocessor);
    this->last_approved = _now(this);
  }
done:
  return;
}

static gboolean _distortion(FRACTaLSubController *this)
{
  GstClockTime owd_th = _stat(this)->owd_50th + CONSTRAIN(50 * GST_MSECOND, 250 * GST_MSECOND, _stat(this)->owd_std * 4);
//  gdouble      FL_th  = MIN(_stat(this)->FL_50th, .1);
  gdouble      FL_th  = MIN(_max_FL_th(this), _stat(this)->FL_50th + _stat(this)->FL_std * 4);

//  if(owd_th < _stat(this)->last_owd){
//    g_print("OWD distorted\n");
//  }

//  if(FL_th < _stat(this)->FL_in_1s){
//    g_print("FL distorted\n");
//  }

  return owd_th < _stat(this)->last_owd || FL_th < _stat(this)->FL_in_1s;
}

static void _undershoot(FRACTaLSubController *this, gint32 turning_point)
{
  gint32 keeping_point;
  keeping_point = CONSTRAIN(turning_point * .5, turning_point * .9, turning_point * pow(_stat(this)->owd_log_corr, 2));
  _set_keeping_point(this, keeping_point);
  this->reducing_approved   = FALSE;
  this->reducing_sr_reached = 0;
}

void
_reduce_stage(
    FRACTaLSubController *this)
{
  this->awnd = _stat(this)->BiF_min * 8;

  if(this->target_bitrate < this->keeping_point * .5){
    //TODO: TCP flow compensation and recursive congestion event consideration
    _undershoot(this, this->target_bitrate);
    _set_event(this, EVENT_CONGESTION);
    goto done;
  }

  _refresh_reducing_approvement(this);
  if(!this->reducing_approved){
    if(this->last_distorted < _now(this) - GST_SECOND){
      _set_keeping_point(this, _stat(this)->receiver_bitrate * .9);
    }else{
      gdouble off = (gdouble)(_now(this) - this->last_distorted) / (gdouble) GST_SECOND;
      _set_keeping_point(this, _stat(this)->receiver_bitrate * .9 * off + this->target_bitrate * (1.-off));
    }
    goto done;
  }

  _set_bottleneck_point(this, this->keeping_point);
  _change_sndsubflow_target_bitrate(this, this->bottleneck_point * .8);
  _switch_stage_to(this, STAGE_KEEP, FALSE);
done:
  return;

}

void
_keep_stage(
    FRACTaLSubController *this)
{

  this->awnd = _stat(this)->BiF_80th * 8 * 1.2;

  if(_distortion(this)){
    if(_subflow(this)->state != SNDSUBFLOW_STATE_STABLE){
      _undershoot(this, _stat(this)->sr_avg);
      _switch_stage_to(this, STAGE_REDUCE, FALSE);
      goto done;
    }
    _set_event(this, EVENT_DISTORTION);
    goto done;
  }else if(_subflow(this)->state != SNDSUBFLOW_STATE_STABLE){
    _set_event(this, EVENT_SETTLED);
    goto done;
  }

  if(_now(this) - MIN(2 * _stat(this)->srtt, GST_SECOND) < this->last_settled){
    gint32 new_target;
    new_target = this->target_bitrate * CONSTRAIN(.99, 1.01, _stat(this)->owd_log_corr);
    _change_sndsubflow_target_bitrate(this, new_target);
    goto done;
  }

  _set_keeping_point(this, this->target_bitrate);
  _start_monitoring(this);
  _switch_stage_to(this, STAGE_PROBE, FALSE);
done:
  return;
}

void
_probe_stage(
    FRACTaLSubController *this)
{
  this->awnd = _stat(this)->BiF_80th * 8 * 1.2;

  if(_distortion(this)){
    _stop_monitoring(this);
    _undershoot(this, this->keeping_point + _stat(this)->fec_bitrate);
    _set_event(this, EVENT_DISTORTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    goto done;
  }

  _refresh_monitoring_approvement(this);
  if(!this->monitoring_approved){
    goto done;
  }else if(_stat(this)->rr_avg < _stat(this)->sr_avg * .9){
    goto done;
  }

  _start_increasement(this);
  _switch_stage_to(this, STAGE_INCREASE, FALSE);
  _set_event(this, EVENT_READY);
done:
  return;
}

void
_increase_stage(
    FRACTaLSubController *this)
{

  this->awnd = _stat(this)->BiF_max * 8 * 1.5;

  if(_distortion(this)){
    _stop_monitoring(this);
    _set_event(this, EVENT_DISTORTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    _undershoot(this, this->keeping_point + _stat(this)->fec_bitrate);
    goto done;
  }

  _refresh_increasing_approvement(this);
  if(!this->increasing_approved){
    goto done;
  }

  _start_monitoring(this);
  _set_event(this, EVENT_SETTLED);
  _switch_stage_to(this, STAGE_PROBE, FALSE);
  _set_keeping_point(this, this->target_bitrate);
done:
  return;
}

void _execute_stage(FRACTaLSubController *this)
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
    FRACTaLSubController *this,
    Event event)
{

  switch(_subflow(this)->state){
    case SNDSUBFLOW_STATE_OVERUSED:
      switch(event){
        case EVENT_CONGESTION:
          this->distorted_BiF       = _stat(this)->bytes_in_flight;
          this->last_distorted      = _now(this);
          this->congestion_detected = _now(this);
        break;
        case EVENT_SETTLED:
          this->last_settled        = _now(this);
          this->congestion_detected = 0;
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
        case EVENT_DISTORTION:
          this->distorted_BiF   = _stat(this)->bytes_in_flight;
          this->last_distorted  = _now(this);
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
          this->distorted_BiF  = _stat(this)->bytes_in_flight;
          this->last_distorted = _now(this);
          sndsubflow_set_state(this->subflow, SNDSUBFLOW_STATE_OVERUSED);
        break;
        case EVENT_SETTLED:
          this->last_settled = _now(this);
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
    FRACTaLSubController *this,
    Stage target,
    gboolean execute)
{
  switch(target){
    case STAGE_REDUCE:
      this->stage_fnc = _reduce_stage;
    break;
    case STAGE_KEEP:
       this->stage_fnc = _keep_stage;
     break;
     case STAGE_PROBE:
       this->stage_fnc = _probe_stage;
     break;
     case STAGE_INCREASE:
       this->stage_fnc = _increase_stage;
     break;
   }

  _priv(this)->stage   = target;

  if(execute){
      this->stage_fnc(this);
  }
  this->rcved_fb_since_changed = 0;
}


void _set_keeping_point(FRACTaLSubController *this, gint32 bitrate)
{
  this->keeping_point = bitrate;
}

void _set_bottleneck_point(FRACTaLSubController *this, gint32 bitrate)
{
  this->bottleneck_point = bitrate;
}

void _refresh_monitoring_approvement(FRACTaLSubController *this)
{
  GstClockTime interval;
  gint32  boundary;
  gdouble ratio = 0.8 / (gdouble)this->monitoring_interval;
  if(this->monitoring_approved){
    return;
  }
  boundary = MAX(_stat(this)->sender_bitrate * ratio, _min_ramp_up(this) * .8);
  if(_stat(this)->fec_bitrate < boundary){
    return;
  }
  if(!this->monitoring_approvement_started){
    this->monitoring_approvement_started = _now(this);
  }
  interval = _get_approvement_interval(this);
//  g_print("Monitoring approvement interval: %lu (srtt: %f)\n", interval, _stat(this)->srtt);
  if(_now(this) - interval < this->monitoring_approvement_started){
    return;
  }

  this->monitoring_approved = TRUE;
}

void _start_monitoring(FRACTaLSubController *this)
{
  this->monitoring_interval = _get_monitoring_interval(this);
  this->monitoring_approvement_started  = 0;
  this->monitoring_started  = _now(this);
  this->monitoring_approved = FALSE;
//  g_print("Monitoring monitoring interval: %d\n", this->monitoring_interval);
}

void _stop_monitoring(FRACTaLSubController *this)
{
  this->monitoring_interval = 0;
  this->monitoring_approvement_started  = 0;
  this->monitoring_approved = FALSE;
}

void _refresh_increasing_approvement(FRACTaLSubController *this)
{
  GstClockTime interval;
  gint32 boundary;

  if(this->increasing_approved){
    return;
  }

  boundary = CONSTRAIN(_min_ramp_up(this), _max_ramp_up(this), this->increasement) * .5;
  if(_stat(this)->sr_avg < this->target_bitrate - boundary){
    return;
  }
  if(!this->increasing_sr_reached){
    this->increasing_sr_reached = _now(this);
    return;
  }

  interval = _get_approvement_interval(this);
  if(_now(this) - interval < this->increasing_sr_reached){
    return;
  }

  this->increasing_approved = TRUE;
}


void _refresh_reducing_approvement(FRACTaLSubController *this)
{
  GstClockTime interval;
  gint32 boundary;

  if(this->reducing_approved){
    return;
  }

  boundary = MAX(30000, this->target_bitrate * .1);
  if(this->target_bitrate + boundary < _stat(this)->sr_avg){
    return;
  }
  if(!this->reducing_sr_reached){
    this->reducing_sr_reached = _now(this);
    return;
  }


  interval = _stat(this)->srtt;
  if(_now(this) - interval < this->reducing_sr_reached){
    return;
  }

  this->reducing_approved = TRUE;
}

void _start_increasement(FRACTaLSubController *this)
{
  this->increasement             = CONSTRAIN(_min_ramp_up(this), _max_ramp_up(this), _stat(this)->fec_bitrate);
  this->increasing_rr_reached    = 0;
  this->increasing_sr_reached    = 0;
  this->increasing_approved      = FALSE;

  _change_sndsubflow_target_bitrate(this, this->target_bitrate + this->increasement);
}


static gdouble _off_target(FRACTaLSubController *this, gint pow, gdouble eps)
{
  gint32 refpoint;
  gdouble result;
  gint i;
  refpoint = MAX(_min_target(this), this->bottleneck_point);
  if(this->target_bitrate <= refpoint){
    return 0.;
  }
  result = this->target_bitrate - refpoint;
  result /= this->target_bitrate * eps;

  for(i=1; i<pow; ++i) result*=result;

  result = CONSTRAIN(0.,1., result);

  return result;
}


guint _get_approvement_interval(FRACTaLSubController* this)
{
  gdouble off;
  gdouble interval;

  off = _off_target(this, 2, _appr_interval_eps(this));
  interval = off * _appr_min_fact(this) + (1.-off) * _appr_max_fact(this);
  return CONSTRAIN(.1 * GST_SECOND,  .6 * GST_SECOND, interval * _stat(this)->srtt);
}

guint _get_monitoring_interval(FRACTaLSubController* this)
{
  guint interval;
  gdouble off;
  gdouble epsilon;

  {
    gdouble refpoint;
    refpoint = MAX(_min_target(this), this->keeping_point);
    epsilon = MIN(.25, (gdouble) _max_ramp_up(this) / refpoint);
  }

  off = _off_target(this, 2, epsilon);

  interval = off * _mon_min_int(this) + (1.-off) * _mon_max_int(this);

  while(this->target_bitrate / interval < _min_ramp_up(this) && _mon_min_int(this) < interval){
    --interval;
  }
  while(_max_ramp_up(this) < this->target_bitrate / interval && interval < _mon_max_int(this)){
    ++interval;
  }

  return interval;
}

void _change_sndsubflow_target_bitrate(FRACTaLSubController* this, gint32 new_target)
{
//  g_print("new target: %d, min target: %d\n", new_target, _min_target(this));
  if(0 < _max_target(this)){
    this->target_bitrate = CONSTRAIN(_min_target(this), _max_target(this), new_target);
  }else{
    this->target_bitrate = MAX(_min_target(this), new_target);
  }

  sndsubflow_set_target_rate(this->subflow, this->target_bitrate);

}

void _logging(FRACTaLSubController* this)
{
  const GstClockTime sampling_time = 100 * GST_MSECOND;
  if(!g_file_test("triggered_stat", G_FILE_TEST_EXISTS)){
    return;
  }
again:
  if(_now(this) - sampling_time < this->last_log){
    return;
  }

  mprtp_logger("temp/targets.csv", "%d\n", this->target_bitrate);

  if(!this->last_log){
    this->last_log = _now(this);
  }else{
    this->last_log += sampling_time;
  }
  goto again;
}
