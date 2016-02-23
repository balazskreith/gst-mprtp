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

#define DEFAULT_RAMP_UP_AGGRESSIVITY 0.
#define DEFAULT_DISCARD_AGGRESSIVITY .1
// Min OWD target. Default value: 0.1 -> 100ms
#define OWD_TARGET_LO 100 * GST_MSECOND
//Max OWD target. Default value: 0.4s -> 400ms
#define OWD_TARGET_HI 400 * GST_MSECOND
#define INIT_CWND 100000
// Max video rampup speed in bps/s (bits per second increase per second)
#define RAMP_UP_MAX_SPEED 200000.0f // bps/s
#define RAMP_UP_MIN_SPEED 2000.0f // bps/s
//CWND scale factor due to loss event. Default value: 0.6
#define BETA 0.6
// Target rate scale factor due to loss event. Default value: 0.8
#define BETA_R 0.8
//Interval between video bitrate adjustments. Default value: 0.2s ->200ms
#define RATE_ADJUST_INTERVAL 200 * GST_MSECOND /* ms */
//Min target_bitrate [bps]
//#define TARGET_BITRATE_MIN 500000
#define TARGET_BITRATE_MIN SUBFLOW_DEFAULT_SENDING_RATE / 2
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

typedef struct _Moment Moment;

typedef enum{
  EVENT_CONGESTION           = -2,
  EVENT_DISTORTION           = -1,
  EVENT_FI                   =  0,
  EVENT_SETTLED              =  1,
  EVENT_STEADY               =  2,
  EVENT_PROBE                =  3,
}Event;

typedef enum{
  STATE_OVERUSED       = -1,
  STATE_STABLE         =  0,
  STATE_MONITORED      =  1,
}State;

typedef enum{
  STAGE_REDUCE            = -3,
  STAGE_BOUNCE            = -2,
  STAGE_MITIGATE          = -1,
  STAGE_KEEP              =  0,
  STAGE_RAISE             =  1,
}Stage;

typedef enum{
  BITRATE_CHANGE          = 1,
  BITRATE_SLOW            = 2,
  BITRATE_FORCED          = 4,
}AimFlag;


struct _Moment{
  GstClockTime      time;
  //Explicit Congestion Notifier Influence Values (ECN)
  gboolean          lost;
  gboolean          discard;
  gboolean          recent_discard;
  gboolean          recent_lost;
  gboolean          path_is_congested;
  gboolean          path_is_lossy;
  gboolean          tr_is_mitigated;
  gint32            has_expected_lost;
//  gint32            incoming_bitrate;
  gint32            sender_bitrate;
  gint32            receiver_bitrate;
  gdouble           discard_rate;
  SubAnalyserResult analysation;

  //application
  Event             event;
  State             state;
  Stage             stage;
  gboolean          controlled;

};



//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

 void subratectrler_finalize (GObject * object);


// const gdouble ST_ = 1.1; //Stable treshold
// const gdouble OT_ = 2.;  //Overused treshold
// const gdouble DT_ = 1.5; //Down Treshold


//--------------------MEASUREMENTS-----------------------

#define _ramp_up_speed(this) (MIN(RAMP_UP_SPEED, this->target_bitrate))
#define _now(this) (gst_clock_get_time(this->sysclock))
#define _get_moment(this, n) ((Moment*)(this->moments + n * sizeof(Moment)))
#define _pmtn(index) (index == MOMENTS_LENGTH ? MOMENTS_LENGTH - 1 : index == 0 ? MOMENTS_LENGTH - 1 : index - 1)
#define _mt0(this) _get_moment(this, this->moments_index)
#define _mt1(this) _get_moment(this, _pmtn(this->moments_index))
#define _mt2(this) _get_moment(this, _pmtn(_pmtn(this->moments_index)))
#define _mt3(this) _get_moment(this, _pmtn(_pmtn(_pmtn(this->moments_index))))

#define _anres(this) _mt0(this)->analysation
#define _anres_t1(this) _mt1(this)->analysation
#define _anres_t2(this) _mt2(this)->analysation
#define _delays(this) _anres(this).delay_indicators
#define _bitrate(this) _anres(this).rate_indicators
#define _delays_t1(this) _anres_t1(this).delay_indicators
#define _bitrate_t1(this) _anres_t1(this).rate_indicators
//#define _reset_target_points(this) numstracker_reset(this->target_points)
#define _skip_frames_for(this, duration) mprtps_path_set_skip_duration(this->path, duration);
#define _qtrend(this) _anres(this).qtrend
#define _qtrend_th(this) 1.05
#define _qtrend_th2(this) 1.5
#define _deoff(this) _anres(this).delay_off
#define _rdiscard(this) (0 < _mt0(this)->recent_discard)
#define _discard(this) (this->discard_aggressivity < _anres(this).discards_rate)
#define _lost(this)  ((_mt0(this)->has_expected_lost ? FALSE : _mt0(this)->path_is_lossy ? FALSE : _mt0(this)->lost > 0))
#define _rlost(this) (!_mt0(this)->has_expected_lost && !_mt0(this)->path_is_lossy && _mt0(this)->recent_lost > 0)
#define _state_t1(this) _mt1(this)->state
#define _state(this) _mt0(this)->state
#define _stage(this) _mt0(this)->stage
#define _stage_t1(this) _mt1(this)->stage
#define _stage_t2(this) _mt2(this)->stage
#define _stage_t3(this) _mt3(this)->stage
#define _TR(this) this->target_bitrate
#define _SR(this) _anres(this).sending_rate_median
#define _RR(this) _mt0(this)->receiver_bitrate
#define _RR_t1(this) _mt1(this)->receiver_bitrate
#define _min_br(this) MIN(_SR(this), _TR(this))
#define _max_br(this) MAX(_SR(this), _TR(this))
#define _set_pending_event(this, e) this->pending_event = e
#define _set_event(this, e) _mt0(this)->event = e
#define _event(this) _mt0(this)->event
#define _event_t1(this) _mt1(this)->event


 static Moment*
_m_step(
    SubflowRateController *this);

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

#define _transit_state_to(this, target)   _mt0(this)->state = target;

static void
_fire(
    SubflowRateController *this,
    Event event);

#define _enable_pacing(this) _set_path_pacing(this, TRUE)
#define _disable_pacing(this) _set_path_pacing(this, FALSE)
#define _pacing_enabled(this) this->path_is_paced

static void
_set_path_pacing(
    SubflowRateController *this,
    gboolean enable_pacing);

static void
_disable_controlling(
    SubflowRateController *this);

#define MAX_MONITORING_INTERVAL 14
#define MIN_MONITORING_INTERVAL 5
#define MAX_MONITORING_RATE 200000
#define _disable_monitoring(this) _set_monitoring_interval(this, 0)

static void
_reset_monitoring(
    SubflowRateController *this);

static void
_setup_monitoring(
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

 static gboolean
 _is_near_to_bottleneck_point(
     SubflowRateController *this);

 static gdouble
 _get_bottleneck_influence(
     SubflowRateController *this);

static void
_add_bottleneck_point(
    SubflowRateController *this,
    gint32 rate);

static gboolean
 _open_cwnd(
     SubflowRateController *this);

static void
_append_to_log(
    SubflowRateController *this,
    const gchar * format,
    ...);

static void
_log_measurement_update_state(
    SubflowRateController *this);

static void
_log_abbrevations(
    SubflowRateController *this,
    FILE *file);


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
  g_object_unref(this->sysclock);
}

void
subratectrler_init (SubflowRateController * this)
{
  this->moments = g_malloc0(sizeof(Moment) * MOMENTS_LENGTH);
  this->sysclock = gst_system_clock_obtain();
  this->analyser = make_subanalyser(10, 10 * GST_SECOND);
  g_rw_lock_init (&this->rwmutex);
}


SubflowRateController *make_subratectrler(void)
{
  SubflowRateController *result;
  result = g_object_new (SUBRATECTRLER_TYPE, NULL);
  return result;
}

void subratectrler_enable_logging(SubflowRateController *this,
                                                    const gchar *filename)
{
  FILE *file;
  THIS_WRITELOCK(this);
  this->log_enabled = TRUE;
  this->logtick = 0;
  strcpy( this->log_filename, filename);
  file = fopen(this->log_filename, "w");
  _log_abbrevations(this, file);
  fclose(file);
  THIS_WRITEUNLOCK(this);
}

void subratectrler_disable_logging(SubflowRateController *this)
{
  THIS_WRITELOCK(this);
  this->log_enabled = FALSE;
  THIS_WRITEUNLOCK(this);
}

void subratectrler_set(SubflowRateController *this,
                         MPRTPSPath *path,
                         guint32 sending_target,
                         guint64 initial_disabling)
{
  THIS_WRITELOCK(this);
  this->path = g_object_ref(path);
  memset(this->moments, 0, sizeof(Moment) * MOMENTS_LENGTH);

  this->id = mprtps_path_get_id(this->path);
  this->setup_time = _now(this);
  this->monitoring_interval = 3;
  this->pacing_bitrate = INIT_CWND;
  this->target_bitrate = sending_target;
  this->min_rate = TARGET_BITRATE_MIN;
  this->max_rate = TARGET_BITRATE_MAX;
  this->min_target_point = MIN(TARGET_BITRATE_MIN, sending_target);
  this->max_target_point = MAX(TARGET_BITRATE_MAX, sending_target * 2);
  this->discard_aggressivity = DEFAULT_DISCARD_AGGRESSIVITY;
  this->ramp_up_aggressivity = DEFAULT_RAMP_UP_AGGRESSIVITY;

  //Todo: Using this fnc pointer in SndRateDistor for central rate distribution.
  this->change_target_bitrate_fnc = _change_target_bitrate;
  _switch_stage_to(this, STAGE_KEEP, FALSE);
  _transit_state_to(this, STATE_STABLE);
  subanalyser_reset(this->analyser);
  if(initial_disabling < 10  *GST_SECOND){
    this->disable_controlling = _now(this) + initial_disabling;
  }else{
    this->disable_controlling = _now(this) + initial_disabling;
  }
  THIS_WRITEUNLOCK(this);
}

void subratectrler_unset(SubflowRateController *this)
{
  THIS_WRITELOCK(this);
  mprtps_path_set_monitor_interval_and_duration(this->path, 0, 0);
  g_object_unref(this->path);
  this->path = NULL;
  THIS_WRITEUNLOCK(this);
}

void subratectrler_time_update(
                         SubflowRateController *this,
                         gint32 *target_bitrate,
                         gint32 *extra_bitrate,
                         UtilizationSubflowReport *rep,
                         gboolean *overused)
{
  if (_now(this) - RATE_ADJUST_INTERVAL < this->last_target_bitrate_adjust) {
    goto done;
  }

  subanalyser_time_update(this->analyser, this->path);
//  this->change_target_bitrate_fnc(this, this->target_bitrate);
//  _change_target_bitrate(this, _adjust_bitrate(this));
//  DISABLE_LINE _set_pacing_bitrate(this, this->target_bitrate * 1.5, TRUE);

//done:
  this->last_target_bitrate_adjust = _now(this);
done:
  if(rep){
    rep->lost_bytes = _mt0(this)->lost;
    rep->discarded_rate = _discard(this);
    rep->owd = 0;//Todo: Fix it
    rep->max_rate = this->max_rate;
    rep->min_rate = this->min_rate;
    rep->ramp_up_aggressivity = this->ramp_up_aggressivity;
    rep->discard_aggressivity = this->discard_aggressivity;
  }
  if(target_bitrate)
    *target_bitrate = this->target_bitrate;
  if(extra_bitrate)
    *extra_bitrate = this->monitored_bitrate;
  if(overused)
    *overused = _state(this) == STATE_OVERUSED;
  return;

}

void subratectrler_change_targets(
                         SubflowRateController *this,
                         gint32 min_rate,
                         gint32 max_rate,
                         gdouble ramp_up_aggressivity,
                         gdouble discard_aggressivity)
{
  THIS_WRITELOCK(this);
  this->min_rate = min_rate;
  this->max_rate = max_rate;
  this->ramp_up_aggressivity = ramp_up_aggressivity;
  this->discard_aggressivity = discard_aggressivity;
  if(this->target_bitrate < this->min_rate){
    this->target_bitrate = this->min_rate;
//    if(this->max_rate && this->max_target_point < this->target_bitrate){
//      this->max_target_point = this->max_rate;
//    }
  }

  THIS_WRITEUNLOCK(this);
}

void subratectrler_measurement_update(
                         SubflowRateController *this,
                         RRMeasurement * measurement)
{
  if(measurement->goodput <= 0.) goto done;
  _m_step(this);

  _mt0(this)->time                = measurement->time;
//  _mt0(this)->lost                = measurement->lost;
  _mt0(this)->lost                = measurement->rfc3611_cum_lost;
  _mt0(this)->recent_lost         = measurement->recent_lost;
  _mt0(this)->recent_discard      = measurement->recent_discarded_bytes;
  _mt0(this)->path_is_lossy       = !mprtps_path_is_non_lossy(this->path);
  _mt0(this)->has_expected_lost   = _mt1(this)->has_expected_lost;
  _mt0(this)->state               = _mt1(this)->state;
  _mt0(this)->stage               = _mt1(this)->stage;
  _mt0(this)->discard_rate        = measurement->goodput / measurement->receiver_rate;
  _mt0(this)->receiver_bitrate    = measurement->receiver_rate * 8;
  _mt0(this)->event               = EVENT_FI;

  if(measurement->expected_lost)
    _mt0(this)->has_expected_lost   = 3;
  else if(_mt0(this)->has_expected_lost > 0)
    --_mt0(this)->has_expected_lost;

  subanalyser_measurement_analyse(this->analyser,
                                  measurement,
                                  _TR(this),
                                  &_mt0(this)->analysation);

  _mt0(this)->sender_bitrate      = _anres(this).sending_rate_median;

  if(0 < this->disable_controlling && this->disable_controlling < _now(this)){
      this->disable_controlling = 0;
  }
  if(5 < this->moments_num && this->disable_controlling == 0LU){
    if(this->pending_event != EVENT_FI){
      _fire(this, this->pending_event);
      this->pending_event = EVENT_FI;
    }
    //Execute stage
    this->stage_fnc(this);
    _fire(this, _event(this));
    _mt0(this)->controlled = TRUE;
  }

  _log_measurement_update_state(this);

  if(_state(this) != STATE_OVERUSED && _state_t1(this) == STATE_OVERUSED){
    _mt0(this)->has_expected_lost   = 3;
  }
//  if(_mt0(this)->state != STATE_OVERUSED || this->last_reference_added < _now(this) - 10 * GST_SECOND){
//    subanalyser_measurement_add_to_reference(this->analyser, measurement);
//    this->last_reference_added = _now(this);
//  }
done:
  return;
}

Moment* _m_step(SubflowRateController *this)
{
  if(++this->moments_index == MOMENTS_LENGTH){
    this->moments_index = 0;
  }
  ++this->moments_num;
  memset(_mt0(this), 0, sizeof(Moment));
  return _mt0(this);
}


void
_reduce_stage(
    SubflowRateController *this)
{
  //Todo: put in order
  this->max_target_point = _RR(this) * .9;
  _add_bottleneck_point(this, this->max_target_point);
  this->target_bitrate  = _min_br(this);
  this->target_bitrate *= _bitrate(this).tr_correlated ?  .6 : .8;
  _switch_stage_to(this, STAGE_BOUNCE, FALSE);
}

void
_bounce_stage(
    SubflowRateController *this)
{
  _open_cwnd(this);
  //Todo: put in order
  this->target_bitrate = this->max_target_point;
  _switch_stage_to(this, STAGE_KEEP, FALSE);
  _set_pending_event(this, EVENT_SETTLED);
  return;
}

void
_keep_stage(
    SubflowRateController *this)
{
  _open_cwnd(this);
  if(_delays(this).distortion || _bitrate(this).distortion){
    _switch_stage_to(this, STAGE_MITIGATE, TRUE);
    _set_event(this, EVENT_DISTORTION);
    goto done;
  }

  if(!_bitrate(this).tr_correlated){
    goto done;
  }

  this->max_target_point = MAX(_TR(this), this->max_target_point);
  this->min_target_point = _TR(this) *  .95;

  if(_now(this) - 2 * GST_SECOND < this->congestion_detected){
    goto done;
  }

  _switch_stage_to(this, STAGE_RAISE, FALSE);
  _set_event(this, EVENT_PROBE);

done:
  return;
}

void
_mitigate_stage(
    SubflowRateController *this)
{
  //Fixme: do this
//  if(_delays(this).congestion || _lost(this)){
//    _switch_stage_to(this, STAGE_REDUCE, TRUE);
//    _set_event(this, EVENT_CONGESTION);
//    goto done;
//  }

  this->congestion_detected = _now(this);
  if(!this->bottleneck_point){
    this->bottleneck_point = _min_br(this);
    _change_target_bitrate(this, this->bottleneck_point * .9);
    goto done;
  }

  if(this->bottleneck_point * 1.1 < _min_br(this)){
    this->bottleneck_point = _min_br(this);
  }else if(_SR(this) * 1.05 < this->bottleneck_point){
    this->bottleneck_point = _SR(this);
  }
  _change_target_bitrate(this, MAX(_min_br(this) * (1.-_qtrend(this)) , this->bottleneck_point * .9));

done:
//  _set_path_pacing(this, TRUE);
  _set_pending_event(this, EVENT_SETTLED);
  _switch_stage_to(this, STAGE_KEEP, FALSE);
  return;
}


void
_raise_stage(
    SubflowRateController *this)
{
  if(0. < _qtrend(this) || _bitrate(this).distortion){
    _switch_stage_to(this, STAGE_MITIGATE, TRUE);
    _set_event(this, EVENT_DISTORTION);
    goto done;
  }
  if(!this->desired_bitrate){
    this->desired_bitrate  = _min_br(this) + this->monitored_bitrate;
    this->desired_increasing_rate = 1. / (gdouble) this->monitoring_interval;
  }

  _disable_monitoring(this);
  this->max_target_point = _min_br(this);
  this->min_target_point = _min_br(this) *.9;
  if(this->target_bitrate < this->desired_bitrate){
    this->target_bitrate  = CONSTRAIN(_min_br(this) + RAMP_UP_MIN_SPEED,
                                      this->desired_bitrate,
                                      _min_br(this) * MAX(1.05, 1. + this->desired_increasing_rate - _get_bottleneck_influence(this)));
    goto done;
  }
  _switch_stage_to(this, STAGE_KEEP, FALSE);
  _set_pending_event(this, EVENT_STEADY);
done:
  return;
}

void
_fire(
    SubflowRateController *this,
    Event event)
{
  switch(_state(this)){
    case STATE_OVERUSED:
      switch(event){
        case EVENT_CONGESTION:
          mprtps_path_set_congested(this->path);
          _switch_stage_to(this, STAGE_REDUCE, TRUE);
          _transit_state_to(this, STATE_OVERUSED);
          _disable_controlling(this);
        break;
        case EVENT_SETTLED:
          mprtps_path_set_non_congested(this->path);
          _reset_monitoring(this);
          _transit_state_to(this, STATE_STABLE);
        break;
        case EVENT_FI:
        default:
        break;
      }
    break;
    case STATE_STABLE:
      switch(event){
        case EVENT_CONGESTION:
          mprtps_path_set_congested(this->path);
          _transit_state_to(this, STATE_OVERUSED);
          _disable_controlling(this);
        break;
        case EVENT_DISTORTION:
          _transit_state_to(this, STATE_OVERUSED);
          break;
        case EVENT_PROBE:
          this->desired_bitrate = 0;
          _transit_state_to(this, STATE_MONITORED);
          _setup_monitoring(this);
        break;
        case EVENT_FI:
        default:
        break;
      }
    break;
    case STATE_MONITORED:
      switch(event){
        case EVENT_CONGESTION:
          mprtps_path_set_congested(this->path);
          _transit_state_to(this, STATE_OVERUSED);
          _disable_controlling(this);
          _disable_monitoring(this);
        break;
        case EVENT_DISTORTION:
          _transit_state_to(this, STATE_OVERUSED);
          _disable_monitoring(this);
        break;
        case EVENT_STEADY:
          _transit_state_to(this, STATE_STABLE);
          _disable_monitoring(this);
        break;
        case EVENT_PROBE:
          _setup_monitoring(this);
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
  _mt0(this)->stage = target;
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

void _set_path_pacing(SubflowRateController *this, gboolean enable_pacing)
{
  mprtps_path_set_pacing_bitrate(this->path, this->pacing_bitrate = _TR(this), 400 * GST_MSECOND);
  mprtps_path_set_pacing(this->path, this->path_is_paced = enable_pacing);

}

void _disable_controlling(SubflowRateController *this)
{
  GstClockTime interval;
  interval = _mt0(this)->time - _mt1(this)->time;
  this->disable_controlling = _now(this) +  MIN(2 * GST_SECOND, 2 * interval);
}

void _reset_monitoring(SubflowRateController *this)
{
  this->monitoring_interval = 0;
  this->monitoring_started = 0;
  this->monitored_bitrate = 0;
}

void _setup_monitoring(SubflowRateController *this)
{
  guint interval;
  gdouble plus_rate = 0, scl = 0;
  guint max_interval, min_interval;

  max_interval = MAX_MONITORING_INTERVAL;
  min_interval = MIN_MONITORING_INTERVAL;

  //try to brake the bottleneck
  if(_max_br(this) < this->bottleneck_point && _is_near_to_bottleneck_point(this)){
      plus_rate = this->bottleneck_point - _min_br(this);
  }else{
    scl =  (gdouble) this->min_rate;
    scl /= (gdouble)(_TR(this) - this->min_rate * .5);
    scl *= scl;
    scl = MIN(1., MAX(1./14., scl));
  //  plus_rate = _TR(this) * scl * (1.-_get_bottleneck_influence(this));
    plus_rate = _TR(this) * scl;
    plus_rate = CONSTRAIN(RAMP_UP_MIN_SPEED, RAMP_UP_MAX_SPEED, plus_rate);
  }
     //cc point

  //considering congestion point


  _append_to_log (this,
    "####################### S%d Monitoring interval ################################\n"
    "last congestion point: %-10d| detected %lu ms ago|\n"
    "scl: %-5.3f|  plus: %-10.3f|\n"
    "######################################################################################\n",
    this->id,
    this->bottleneck_point,
    GST_TIME_AS_MSECONDS(_now(this) - this->congestion_detected),

    scl, plus_rate

    );
  plus_rate = MIN(plus_rate, MAX_MONITORING_RATE);
  interval = _calculate_monitoring_interval(this, plus_rate);
  if(_is_near_to_bottleneck_point(this)){
     interval = MAX(12, interval);
  }else if(interval < this->monitoring_interval){
    interval = this->monitoring_interval;
  }

  this->monitoring_interval = CONSTRAIN(min_interval, max_interval, interval);
  _set_monitoring_interval(this, this->monitoring_interval);
}

void _set_monitoring_interval(SubflowRateController *this, guint interval)
{
  this->monitoring_started = _now(this);
  if(interval > 0)
    this->monitored_bitrate = (gdouble)_TR(this) / (gdouble)interval;
  else
    this->monitored_bitrate = 0;
  mprtps_path_set_monitor_interval_and_duration(this->path, interval, 0 * GST_MSECOND);
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

  if(rate > 1.5) monitoring_interval = 2;
  else if(rate > 1.33) monitoring_interval = 3;
  else if(rate > 1.25) monitoring_interval = 4;
  else if(rate > 1.2) monitoring_interval = 5;
  else if(rate > 1.16) monitoring_interval = 6;
  else if(rate > 1.14) monitoring_interval = 7;
  else if(rate > 1.12) monitoring_interval = 8;
  else if(rate > 1.11) monitoring_interval = 9;
  else if(rate > 1.10) monitoring_interval = 10;
  else if(rate > 1.09) monitoring_interval = 11;
  else if(rate > 1.08) monitoring_interval = 12;
  else if(rate > 1.07) monitoring_interval = 13;
  else  monitoring_interval = 14;

exit:
  return monitoring_interval;

}

void _change_target_bitrate(SubflowRateController *this, gint32 new_target)
{
//  gint32 max_rate, min_rate;
//  max_rate = !this->max_rate ?  this->max_target_point : MIN(this->max_target_point, this->max_rate);
//  min_rate = !this->min_rate ? this->min_target_point  : MAX(this->min_target_point, this->min_rate);
  //  this->target_bitrate = CONSTRAIN(min_rate, max_rate, new_target);
  this->target_bitrate = CONSTRAIN(this->min_rate, this->max_rate, new_target);
}

gboolean _is_near_to_bottleneck_point(SubflowRateController *this)
{
  gint32 cc_point;
  cc_point = this->bottleneck_point;
  if(!cc_point) return FALSE;
  if( _min_br(this) * .9 < cc_point && cc_point < _max_br(this) * 1.1) return TRUE;
  return FALSE;
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
  result = br / (10. * (b_point - br));
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

gboolean _open_cwnd(SubflowRateController *this)
{
  mprtps_path_clear_queue(this->path);
  _disable_pacing(this);
  return TRUE;
}

void _append_to_log(SubflowRateController *this, const gchar * format, ...)
{
  FILE *file;
  va_list args;
  if(!this->log_enabled) return;
  file = fopen(this->log_filename, "a");
  va_start (args, format);
  vfprintf (file, format, args);
  va_end (args);
  fclose(file);
}

void _log_measurement_update_state(SubflowRateController *this)
{
  FILE *file;
  if(!this->log_enabled) return;
  file = fopen(this->log_filename, "a");
  if(++this->logtick % 60 == 0)
    _log_abbrevations(this, file);
  fprintf(file,
//  g_print (

          "############ S%d | State: %-2d | Disable time %lu | Ctrled: %d #################\n"
          "rlost:      %-10d| rdiscard:%-10d| lost:    %-10d| discard: %-10d|\n"
          "pth_cong:   %-10d| pth_lssy:%-10d| pacing:  %-10d| trend_th:%-10d|\n"
          "target_br:  %-10d| min_tbr: %-10d| max_tbr: %-10d| trend:   %-10.6f\n"
          "stage:      %-10d| near2cc: %-10d| exp_lst: %-10d| pevent:  %-10d|\n"
          "mon_br:     %-10d| mon_int: %-10d| pace_br: %-10d| lc_rate: %-10d|\n"
          "RR:         %-10d| SR:      %-10d| disc_rat:%-10.3f| ci:      %-10.3f|\n"
          "l:          %-10d| rl:      %-10d| d:       %-10d| rd:      %-10d\n"
          "abs_max:    %-10d| abs_min: %-10d| event:      %-7d| off_add: %-10.3f\n"
          "Delay--------------> distortion: %-10d| congestion: %-7d|\n"
          "Rates--------------> distorted:  %-7d| rr_corr: %-7d|\n"
          "Rates--------------> tr_corr:    %-10d|\n"
          "############################ Seconds since setup: %lu ##########################################\n",
          this->id, _state(this),
          this->disable_controlling > 0 ? GST_TIME_AS_MSECONDS(this->disable_controlling - _now(this)) : 0,
          _mt0(this)->controlled,

          _rlost(this),_rdiscard(this),_lost(this),_discard(this),

          !mprtps_path_is_non_congested(this->path),
          !mprtps_path_is_non_lossy(this->path),
          this->path_is_paced,
          _qtrend_th(this) < _qtrend(this),

          this->target_bitrate, this->min_target_point,
          this->max_target_point, _qtrend(this),

          _stage(this),
          _is_near_to_bottleneck_point(this),
          _mt0(this)->has_expected_lost, this->pending_event,

          this->monitored_bitrate, this->monitoring_interval,
          this->pacing_bitrate, this->bottleneck_point,

          _RR(this), _mt0(this)->sender_bitrate,
          _mt0(this)->discard_rate, _get_bottleneck_influence(this),

          _mt0(this)->lost, _mt0(this)->recent_lost,
          _mt0(this)->discard,_mt0(this)->recent_discard,

          this->max_rate, this->min_rate, _event(this), _anres(this).off,

          _delays(this).distortion,_delays(this).congestion,

          _bitrate(this).distortion,
          _bitrate(this).rr_correlated,_bitrate(this).tr_correlated,

         GST_TIME_AS_SECONDS(_now(this) - this->setup_time)

         );
  subanalyser_append_logfile(this->analyser, file);
  fclose(file);
}

void _log_abbrevations(SubflowRateController *this, FILE *file)
{
  fprintf(file,
    "############ Subflow %d abbrevations ##############################################################\n"
    "#  State:      The actual state (Overused (-1), Stable (0), Monitored (1))                        #\n"
    "#  Ctrled:     Indicate weather the state is controlled or not                                    #\n"
    "#  rlost:      recent losts                                                                       #\n"
    "#  rdiscard:   recent discards                                                                    #\n"
    "#  lost:       any losts                                                                          #\n"
    "#  pth_lssy:   Indicate weather the path is lossy                                                 #\n"
    "#  pth_cong:   Indicate weather the path is congested                                             #\n"
    "#  pth_slow:   Indicate weather the path is slow                                                  #\n"
    "###################################################################################################\n",
    this->id
  );
}

