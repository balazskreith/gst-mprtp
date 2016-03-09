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
#include "subratectrler.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>
#include "bintree.h"
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

GST_DEBUG_CATEGORY_STATIC (subratectrler_debug_category);
#define GST_CAT_DEFAULT subratectrler_debug_category

G_DEFINE_TYPE (SubflowRateController, subratectrler, G_TYPE_OBJECT);

#define MOMENTS_LENGTH 8
#define KEEP_MAX 20
#define KEEP_MIN 5

#define DEFAULT_RAMP_UP_AGGRESSIVITY 0.
#define DEFAULT_DISCARD_AGGRESSIVITY .1
// Max video rampup speed in bps/s (bits per second increase per second)
#define RAMP_UP_MAX_SPEED 200000.0f // bps/s
#define RAMP_UP_MIN_SPEED 2000.0f // bps/s
//CWND scale factor due to loss event. Default value: 0.6
#define BETA 0.6
// Target rate scale factor due to loss event. Default value: 0.8
#define BETA_R 0.8
//Min target_bitrate [bps]
//#define TARGET_BITRATE_MIN 500000
#define TARGET_BITRATE_MIN (SUBFLOW_DEFAULT_SENDING_RATE>>1)
//Max target_bitrate [bps]
#define TARGET_BITRATE_MAX 0
//Timespan [s] from lowest to highest bitrate. Default value: 10s->10000ms
#define RAMP_UP_TIME 10000
//Guard factor against early congestion onset.
//A higher value gives less jitter possibly at the
//expense of a lower video bitrate. Default value: 0.0..0.95
#define PRE_CONGESTION_GUARD .0
//Guard factor against RTP queue buildup. Default value: 0.0..2.0
#define TX_QUEUE_SIZE_FACTOR 1.0

typedef struct _Private Private;

typedef enum{
  EVENT_CONGESTION           = -2,
  EVENT_DISTORTION           = -1,
  EVENT_FI                   =  0,
  EVENT_SETTLED              =  1,
  EVENT_STEADY               =  2,
  EVENT_PROBE                =  3,
}Event;

typedef enum{
  STAGE_REDUCE            = -3,
  STAGE_BOUNCE            = -2,
  STAGE_MITIGATE          = -1,
  STAGE_KEEP              =  0,
  STAGE_RAISE             =  1,
}Stage;

#define SR_TR_ARRAY_LENGTH 3

struct _Private{
  GstClockTime        time;
  gboolean            lost;
  gboolean            discard;
  gboolean            recent_discard;
  gboolean            recent_lost;
  gboolean            path_is_congested;
  gboolean            path_is_lossy;
  gint32              sender_bitrate;
  gboolean            mitigated;
  Event               event;
  Stage               stage;
  gboolean            controlled;
  SubflowMeasurement* measurement;
  gboolean            tr_correlated;

  gint32              sending_bitrate_sum;
  gint32              target_bitrate_sum;
  gint32              sending_bitrates[SR_TR_ARRAY_LENGTH];
  gint32              target_bitrates[SR_TR_ARRAY_LENGTH];
  gint                sr_tr_index;
};


#define _priv(this) ((Private*)this->priv)
#define _state(this) this->state
#define _stage(this) _priv(this)->stage
#define _event(this) _priv(this)->event
#define _set_event(this, e) _event(this) = e
#define _set_pending_event(this, e) this->pending_event = e
#define _measurement(this) _priv(this)->measurement
#define _anres(this) _measurement(this)->netq_analysation
#define _TR(this) this->target_bitrate
#define _SR(this) (_priv(this)->sending_bitrate_sum / 3)
#define _min_br(this) MIN(_SR(this), _TR(this))
#define _max_br(this) MAX(_SR(this), _TR(this))

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

 void subratectrler_finalize (GObject * object);


// const gdouble ST_ = 1.1; //Stable treshold
// const gdouble OT_ = 2.;  //Overused treshold
// const gdouble DT_ = 1.5; //Down Treshold


//--------------------MEASUREMENTS-----------------------

#define _now(this) (gst_clock_get_time(this->sysclock))

static void
_reduce_stage(
    SubflowRateController *this);

static void
_bounce_stage(
    SubflowRateController *this);

static void
_keep_stage(
    SubflowRateController *this);

static void
_mitigate_stage(
    SubflowRateController *this);

static void
_raise_stage(
    SubflowRateController *this);

static void
_switch_stage_to(
    SubflowRateController *this,
    Stage target,
    gboolean execute);

#define _transit_state_to(this, target)   this->state = target;

static void
_fire(
    SubflowRateController *this,
    Event event);

static void
_disable_controlling(
    SubflowRateController *this);

#define MAX_MONITORING_INTERVAL 14
#define MIN_PROBE_MONITORING_INTERVAL 5
#define MIN_BOUNCE_MONITORING_INTERVAL 2
#define MAX_MONITORING_RATE 200000
#define _disable_monitoring(this) _set_monitoring_interval(this, 0)

static void
_reset_monitoring(
    SubflowRateController *this);

static void
_setup_bounce_monitoring(
    SubflowRateController *this);

static void
_setup_raise_monitoring(
    SubflowRateController *this);

static void
_set_monitoring_interval(
    SubflowRateController *this,
    guint interval);

static guint
_calculate_monitoring_interval(
    SubflowRateController *this,
    guint32 desired_bitrate);

static void
_change_target_bitrate(SubflowRateController *this, gint32 new_target);

 static gdouble
 _get_bottleneck_influence(
     SubflowRateController *this);

static void
_add_bottleneck_point(
    SubflowRateController *this,
    gint32 rate);

static void
_logging(
    SubflowRateController *this);


//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
subratectrler_class_init (SubflowRateControllerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = subratectrler_finalize;

  GST_DEBUG_CATEGORY_INIT (subratectrler_debug_category, "subratectrler", 0,
      "MpRTP Manual Sending Controller");

}

void
subratectrler_finalize (GObject * object)
{
  SubflowRateController *this;
  this = SUBRATECTRLER(object);
  mprtp_free(this->priv);
  g_object_unref(this->analyser);
  g_object_unref(this->sysclock);
  g_object_unref(this->path);
  g_object_unref(this->rate_controller);
}

void
subratectrler_init (SubflowRateController * this)
{
  this->priv = mprtp_malloc(sizeof(Private));
  this->sysclock = gst_system_clock_obtain();
  g_rw_lock_init (&this->rwmutex);

  //Initial values
  this->min_rate         = this->min_target_point = TARGET_BITRATE_MIN;
  this->max_rate         = this->max_target_point = TARGET_BITRATE_MAX;
  this->keep             = KEEP_MIN;
  this->target_bitrate   = SUBFLOW_DEFAULT_SENDING_RATE;

}


SubflowRateController *make_subratectrler(SendingRateDistributor* rate_controlller, MPRTPSPath *path)
{
  SubflowRateController *result;
  result                      = g_object_new (SUBRATECTRLER_TYPE, NULL);
  result->path                = g_object_ref(path);
  result->rate_controller     = g_object_ref(rate_controlller);
  result->id                  = mprtps_path_get_id(result->path);
  result->setup_time          = _now(result);
  result->monitoring_interval = 3;
  result->analyser            = make_netqueue_analyser(result->id);
  _switch_stage_to(result, STAGE_KEEP, FALSE);
  _transit_state_to(result, SUBFLOW_STATE_STABLE);

  return result;
}

static void _update_tr_corr(SubflowRateController *this,
                            SubflowMeasurement *measurement)
{
  gdouble tr_corr;
  _priv(this)->sending_bitrates[_priv(this)->sr_tr_index] = measurement->sending_bitrate;
  _priv(this)->target_bitrates[_priv(this)->sr_tr_index]  = this->target_bitrate;

  _priv(this)->sending_bitrate_sum += _priv(this)->sending_bitrates[_priv(this)->sr_tr_index];
  _priv(this)->target_bitrate_sum  += _priv(this)->target_bitrates[_priv(this)->sr_tr_index];

  if(++_priv(this)->sr_tr_index == SR_TR_ARRAY_LENGTH){
    _priv(this)->sr_tr_index = 0;
  }

  _priv(this)->sending_bitrate_sum -= _priv(this)->sending_bitrates[_priv(this)->sr_tr_index];
  _priv(this)->target_bitrate_sum  -= _priv(this)->target_bitrates[_priv(this)->sr_tr_index];

  tr_corr =  _priv(this)->sending_bitrate_sum / _priv(this)->target_bitrate_sum;

  _priv(this)->tr_correlated = .95 < tr_corr && tr_corr < 1.05;


}

void subratectrler_measurement_update(
                         SubflowRateController *this,
                         SubflowMeasurement *measurement)
{

  struct _SubflowUtilizationReport *report = &this->utilization.report;

  netqueue_analyser_do(this->analyser, measurement->reports, &measurement->netq_analysation);

  _priv(this)->measurement   = measurement;

  _update_tr_corr(this, measurement);

  if(0 < this->disable_controlling && this->disable_controlling < _now(this)){
      this->disable_controlling = 0;
  }
  if(5 < this->measurements_num && this->disable_controlling == 0LU){
    if(this->pending_event != EVENT_FI){
      _fire(this, this->pending_event);
      this->pending_event = EVENT_FI;
    }
    //Execute stage
    this->stage_fnc(this);
    _fire(this, _event(this));
    _priv(this)->controlled = TRUE;
  }else{
    _priv(this)->controlled = FALSE;
  }

  _logging(this);
  ++this->measurements_num;

  report->discarded_bytes = 0; //Fixme
  report->lost_bytes = 0;//Fixme
  report->sending_rate = _SR(this);
  report->target_rate = _TR(this);
  report->state = _state(this);
  report->rtt  = _measurement(this)->reports->RR.RTT;
  _priv(this)->measurement = NULL;

  sndrate_setup_report(this->rate_controller, this->id, report);
  return;
}

void subratectrler_setup_controls(
                         SubflowRateController *this, struct _SubflowUtilizationControl* src)
{
  struct _SubflowUtilizationControl *ctrl = &this->utilization.control;
  memcpy(ctrl, src, sizeof(struct _SubflowUtilizationControl));
  this->min_rate = ctrl->min_rate;
  this->max_rate = ctrl->max_rate;
  if(this->target_bitrate < this->min_rate) this->target_bitrate = this->min_rate;
}

void
_reduce_stage(
    SubflowRateController *this)
{
  if(!this->reduced || 3 < this->in_congestion){
    _change_target_bitrate(this, _min_br(this) * .6);
    this->reduced = TRUE;
    this->in_congestion = 0;
    goto done;
  }
  if(_anres(this).congested){
    ++this->in_congestion;
    goto done;
  }
  this->in_congestion = 0;
  _switch_stage_to(this, STAGE_BOUNCE, FALSE);
done:
  return;
}

void
_bounce_stage(
    SubflowRateController *this)
{
  if(_anres(this).congested){
    this->bottleneck_point = _TR(this) * .9;
    _switch_stage_to(this, STAGE_REDUCE, TRUE);
    _set_event(this, EVENT_CONGESTION);
    goto exit;
  }

  _change_target_bitrate(this, this->bottleneck_point);
  _switch_stage_to(this, STAGE_KEEP, FALSE);
  _set_pending_event(this, EVENT_SETTLED);
  this->keep = KEEP_MIN;
exit:
  return;
}

void
_keep_stage(
    SubflowRateController *this)
{
  gint32 target_rate = _TR(this);

//  if(_anres(this).distorted){
//    _switch_stage_to(this, STAGE_MITIGATE, TRUE);
//    _set_event(this, EVENT_DISTORTION);
//    goto exit;
//  }

  if(_anres(this).pierced){
    this->keep = MIN(this->keep * 2, KEEP_MAX);
    goto done;
  }

  if(_anres(this).stability < this->keep || !_priv(this)->tr_correlated){
    goto done;
  }

  if(0 < this->bottleneck_point && target_rate < this->bottleneck_point){
    target_rate = this->bottleneck_point;
    netqueue_analyser_reset_stability(this->analyser);
    goto done;
  }


//  _switch_stage_to(this, STAGE_RAISE, FALSE);
//  _set_event(this, EVENT_PROBE);
done:
  _change_target_bitrate(this, target_rate);
//exit:
  return;
}

void
_mitigate_stage(
    SubflowRateController *this)
{
  gint32 target_rate = _TR(this);
  this->congestion_detected = _now(this);
  //  if(_anres(this).congested || _lost(this)){
  if(_anres(this).congested){
    _switch_stage_to(this, STAGE_REDUCE, TRUE);
    _set_event(this, EVENT_CONGESTION);
    goto exit;
  }

  if(_priv(this)->mitigated){
    goto exit;
  }
  this->bottleneck_point = _max_br(this) * .9;
  target_rate = this->bottleneck_point * .9;

  _change_target_bitrate(this, target_rate);
  _set_pending_event(this, EVENT_SETTLED);
  _switch_stage_to(this, STAGE_KEEP, FALSE);
  this->keep = MAX(KEEP_MAX / 2, this->keep);
  _priv(this)->mitigated = TRUE;
exit:
  return;
}


void
_raise_stage(
    SubflowRateController *this)
{
  gint32 target_rate = _TR(this);

  if(_anres(this).distorted){
    _switch_stage_to(this, STAGE_MITIGATE, TRUE);
    _set_event(this, EVENT_DISTORTION);
    goto exit;
  }

  if(_anres(this).pierced){
    target_rate = this->bottleneck_point;
    this->keep = MAX(KEEP_MIN * 2, this->keep);
    _switch_stage_to(this, STAGE_KEEP, FALSE);
    _set_pending_event(this, EVENT_STEADY);
    _disable_monitoring(this);
    goto exit;
  }

  if(_anres(this).stability < this->keep || !_priv(this)->tr_correlated){
    goto done;
  }

  this->bottleneck_point = MAX(target_rate * .9, this->bottleneck_point);
  if(this->target_bitrate < this->desired_bitrate){
    target_rate  = target_rate + CONSTRAIN(RAMP_UP_MIN_SPEED,
                                           RAMP_UP_MAX_SPEED,
                                           this->desired_bitrate - this->target_bitrate);
    netqueue_analyser_reset_stability(this->analyser);
    goto done;
  }

  this->keep = MAX(this->keep / 2, KEEP_MIN);
  _switch_stage_to(this, STAGE_KEEP, FALSE);
  _set_pending_event(this, EVENT_STEADY);
  _disable_monitoring(this);
done:
  _change_target_bitrate(this, target_rate);
exit:
  return;
}

void
_fire(
    SubflowRateController *this,
    Event event)
{
  switch(_state(this)){
    case SUBFLOW_STATE_OVERUSED:
      switch(event){
        case EVENT_CONGESTION:
          this->reduced = FALSE;
          mprtps_path_set_congested(this->path);
          _disable_controlling(this);
          _disable_monitoring(this);
        break;
        case EVENT_SETTLED:
          mprtps_path_set_non_congested(this->path);
          _disable_monitoring(this);
          _reset_monitoring(this);
          _transit_state_to(this, SUBFLOW_STATE_STABLE);
        break;
        case EVENT_PROBE:
          _setup_bounce_monitoring(this);
        break;
        case EVENT_FI:
        default:
        break;
      }
    break;
    case SUBFLOW_STATE_STABLE:
      switch(event){
        case EVENT_CONGESTION:
          this->reduced = FALSE;
          mprtps_path_set_congested(this->path);
          _transit_state_to(this, SUBFLOW_STATE_OVERUSED);
          _disable_controlling(this);
        break;
        case EVENT_DISTORTION:
          _transit_state_to(this, SUBFLOW_STATE_OVERUSED);
          break;
        case EVENT_PROBE:
          _transit_state_to(this, SUBFLOW_STATE_MONITORED);
          _setup_raise_monitoring(this);
        break;
        case EVENT_FI:
        default:
        break;
      }
    break;
    case SUBFLOW_STATE_MONITORED:
      switch(event){
        case EVENT_CONGESTION:
          this->reduced = FALSE;
          mprtps_path_set_congested(this->path);
          _transit_state_to(this, SUBFLOW_STATE_OVERUSED);
          _disable_controlling(this);
          _disable_monitoring(this);
        break;
        case EVENT_DISTORTION:
          _transit_state_to(this, SUBFLOW_STATE_OVERUSED);
          _disable_monitoring(this);
        break;
        case EVENT_STEADY:
          netqueue_analyser_reset_stability(this->analyser);
          _transit_state_to(this, SUBFLOW_STATE_STABLE);
          _disable_monitoring(this);
        break;
        case EVENT_PROBE:
          _setup_raise_monitoring(this);
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
    SubflowRateController *this,
    Stage target,
    gboolean execute)
{
  switch(target){
     case STAGE_KEEP:
       this->stage_fnc = _keep_stage;
     break;
     case STAGE_REDUCE:
       this->stage_fnc = _reduce_stage;
     break;
     case STAGE_BOUNCE:
       this->stage_fnc = _bounce_stage;
     break;
     case STAGE_MITIGATE:
       this->stage_fnc = _mitigate_stage;
     break;
     case STAGE_RAISE:
       this->stage_fnc = _raise_stage;
     break;
   }
  _priv(this)->stage = target;
  if(execute){
      this->stage_fnc(this);
  }
}

gint32 subratectrler_get_target_bitrate(SubflowRateController *this)
{
  return this->target_bitrate;
}

gint32 subratectrler_get_monitoring_bitrate(SubflowRateController *this)
{
  return this->monitored_bitrate;
}

void _disable_controlling(SubflowRateController *this)
{
  this->disable_controlling = _now(this) +  2 * GST_SECOND;
}

void _reset_monitoring(SubflowRateController *this)
{
  this->monitoring_interval = 0;
  this->monitoring_started = 0;
  this->monitored_bitrate = 0;
}

void _setup_bounce_monitoring(SubflowRateController *this)
{
  guint interval;
  gdouble plus_rate = 0, scl = 0;
  guint max_interval, min_interval;

  max_interval = MAX_MONITORING_INTERVAL;
  min_interval = MIN_BOUNCE_MONITORING_INTERVAL;

  scl =  (gdouble) this->min_rate;
  scl /= (gdouble)(_TR(this) - this->min_rate * .5);
  scl *= scl;
  scl = MIN(1., MAX(1./14., scl));
  plus_rate = _TR(this) * scl;
  plus_rate = MIN(this->bottleneck_point * .9, plus_rate);

  interval = _calculate_monitoring_interval(this, plus_rate);
  this->monitoring_interval = CONSTRAIN(min_interval, max_interval, interval);
  _set_monitoring_interval(this, this->monitoring_interval);
}

void _setup_raise_monitoring(SubflowRateController *this)
{
  if(this->bottleneck_point < _TR(this) * .9 || _TR(this) * 1.1 < this->bottleneck_point){
    this->monitoring_interval = 5;
  }else{
    this->monitoring_interval = 10;
  }
  _set_monitoring_interval(this, this->monitoring_interval);
  this->desired_bitrate  = _TR(this) + this->monitored_bitrate;
}

void _set_monitoring_interval(SubflowRateController *this, guint interval)
{
  this->monitoring_started = _now(this);
  if(interval > 0)
    this->monitored_bitrate = (gdouble)_TR(this) / (gdouble)interval;
  else
    this->monitored_bitrate = 0;
  return;
}

guint _calculate_monitoring_interval(SubflowRateController *this, guint32 desired_bitrate)
{
  gdouble actual, target, rate;
  guint monitoring_interval = 0;
  if(desired_bitrate <= 0){
     goto exit;
   }
  actual = _TR(this);
  target = actual + (gdouble) desired_bitrate;
  rate = target / actual;

  if(rate > 1.51) monitoring_interval = 2;
  else if(rate > 1.331) monitoring_interval = 3;
  else if(rate > 1.251) monitoring_interval = 4;
  else if(rate > 1.21) monitoring_interval = 5;
  else if(rate > 1.161) monitoring_interval = 6;
  else if(rate > 1.141) monitoring_interval = 7;
  else if(rate > 1.121) monitoring_interval = 8;
  else if(rate > 1.111) monitoring_interval = 9;
  else if(rate > 1.101) monitoring_interval = 10;
  else if(rate > 1.091) monitoring_interval = 11;
  else if(rate > 1.081) monitoring_interval = 12;
  else if(rate > 1.071) monitoring_interval = 13;
  else  monitoring_interval = 14;

exit:
  return monitoring_interval;

}

void _change_target_bitrate(SubflowRateController *this, gint32 new_target)
{
  if(0 < this->min_rate){
    this->target_bitrate = MAX(this->min_rate, new_target);
  }
  if(0 < this->max_rate){
    this->target_bitrate = MIN(this->max_rate, new_target);
  }
  g_print("TARGET RATE: %d\n", new_target);
  DISABLE_LINE _get_bottleneck_influence(this);
  DISABLE_LINE _add_bottleneck_point(this, 0);
}

gdouble _get_bottleneck_influence(SubflowRateController *this)
{
  gdouble result;
  gdouble b_point = this->bottleneck_point;
  gdouble br, br1, br2;
  if(!this->bottleneck_point) return 0.;
  if(_max_br(this) * 1.1 < this->bottleneck_point) return 0.;
  if(  this->target_bitrate < _min_br(this) * .9 ) return 0.;
  br1 = abs(this->bottleneck_point - _TR(this));
  br2 = abs(this->bottleneck_point - _SR(this));
  br = br1 < br2 ? _TR(this) : _SR(this);
  result = br / (2. * (b_point - br));
  result *= result;
//  result  *= 0. < result ? 2. : -.5;
  return CONSTRAIN(.01, 1., result);
}

void
_add_bottleneck_point(
    SubflowRateController *this, gint32 rate)
{
  this->congestion_detected = _now(this);
  this->bottleneck_point = rate;
}

void _logging(SubflowRateController *this)
{
  gchar filename[255];
  sprintf(filename, "logs/subratectrler_%d.log", this->id);
  mprtp_logger(filename,
               "############ S%d | State: %-2d | Disable time %lu | Ctrled: %d #################\n"
               "SR:         %-10d| TR:      %-10d| botlnck: %-10d|\n"
               "abs_max:    %-10d| abs_min: %-10d| max_tbr: %-10d| min_tbr: %-10d|\n"
               "stage:      %-10d| event:   %-10d| pevent:  %-10d| keep:    %-10d\n"
               "mon_br:     %-10d| mon_int: %-10d| tr_corr: %-10d|\n"
               "############################ Seconds since setup: %lu ##########################################\n",

               this->id, _state(this),
               this->disable_controlling > 0 ? GST_TIME_AS_MSECONDS(this->disable_controlling - _now(this)) : 0,
               _priv(this)->controlled,

               _priv(this)->sending_bitrate_sum / 3,
               _priv(this)->target_bitrate_sum / 3,
               this->bottleneck_point,

               this->max_rate,
               this->min_rate,
               this->max_target_point,
               this->min_target_point,

               _priv(this)->stage,
               _priv(this)->event,
               this->pending_event,
               this->keep,

               this->monitored_bitrate,
               this->monitoring_interval,
               _priv(this)->tr_correlated,

              GST_TIME_AS_SECONDS(_now(this) - this->setup_time)

  );
}


