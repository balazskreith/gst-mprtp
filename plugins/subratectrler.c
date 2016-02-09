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


// Min OWD target. Default value: 0.1 -> 100ms
#define OWD_TARGET_LO 100 * GST_MSECOND
//Max OWD target. Default value: 0.4s -> 400ms
#define OWD_TARGET_HI 400 * GST_MSECOND
#define INIT_CWND 100000
// Max video rampup speed in bps/s (bits per second increase per second)
#define RAMP_UP_MAX_SPEED 200000.0f // bps/s
//CWND scale factor due to loss event. Default value: 0.6
#define BETA 0.6
// Target rate scale factor due to loss event. Default value: 0.8
#define BETA_R 0.8
//Interval between video bitrate adjustments. Default value: 0.2s ->200ms
#define RATE_ADJUST_INTERVAL 200 * GST_MSECOND /* ms */
//Min target_bitrate [bps]
//#define TARGET_BITRATE_MIN 500000
#define TARGET_BITRATE_MIN SUBFLOW_DEFAULT_SENDING_RATE
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
  Congestion           = -2,
  Distortion           = -1,
  Fi                   =  0,
  Settlement           =  1,
  Steadiness           =  2,
  Increasement         =  3
}Event;

typedef enum{
  STATE_OVERUSED       = -1,
  STATE_STABLE         =  0,
  STATE_MONITORED      =  1,
}State;

typedef enum{
  STAGE_RESTRICT          = -3,
  STAGE_RELEASE           = -2,
  STAGE_MITIGATE          = -1,
  STAGE_CHECK             =  0,
  STAGE_PROBE             =  1,
  STAGE_RAISE             =  2,
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
  gboolean          path_is_slow;
  gboolean          tr_is_mitigated;
  gint32            has_expected_lost;
  gint32            incoming_bitrate;
  gint32            sender_bitrate;
  gdouble           discard_rate;
  SubAnalyserResult analysation;

  //application

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
//#define _reset_target_points(this) numstracker_reset(this->target_points)
#define _skip_frames_for(this, duration) mprtps_path_set_skip_duration(this->path, duration);
#define _DeCorrH(this) _anres(this).DeCorrH
#define _DeCorrL(this) _anres(this).DeCorrL
#define _DeCorrT(this) _anres(this).DeCorrT
#define _RateCorr(this) _anres(this).RateCorr
#define _TRateCorr(this) _anres(this).TRateCorr
#define _BiFCorr(this) _anres(this).BiFCorr
#define _DeCorrT_t1(this) _anres_t1(this).DeCorrT
#define _DiCoeffT(this) (0. < _DeCorrT_t1(this) ? _DeCorrT(this) / _DeCorrT_t1(this) : 0.)
#define _RRxBiF(this) _anres(this).RRxBiF

#define _rdiscard(this) (0 < _mt0(this)->recent_discard)
#define _discard_rate(this) _mt0(this)->discard_rate
#define _discrate(this) _discard_rate(this)
//#define _lost(this)  (!_mt0(this)->has_expected_lost && !_mt0(this)->path_is_lossy && _mt0(this)->lost > 0)
#define _lost(this)  (_mt0(this)->has_expected_lost ? FALSE : _mt0(this)->path_is_lossy ? FALSE : _mt0(this)->lost > 0)
#define _rlost(this) (!_mt0(this)->has_expected_lost && !_mt0(this)->path_is_lossy && _mt0(this)->recent_lost > 0)
//#define _discard(this) (!_mt0(this)->path_is_slow && _discard_rate(this) > .2)
#define _discard(this) (1. < _discard_rate(this))
#define _state_t1(this) _mt1(this)->state
#define _state(this) _mt0(this)->state
#define _stage(this) _mt0(this)->stage
#define _TR(this) this->target_bitrate
#define _IR(this) _mt0(this)->incoming_bitrate
#define _mitigated(this) _mt0(this)->tr_is_mitigated
#define _mitigated_t1(this) _mt1(this)->tr_is_mitigated
#define _mitigated_t2(this) _mt2(this)->tr_is_mitigated

 static gdouble AGGRESSIVITY_TRESHOLD = .4;


 static Moment*
_m_step(
    SubflowRateController *this);

static void
_undershoot(
    SubflowRateController *this,
    gboolean disable_controlling);

static void
_restrict_stage(
    SubflowRateController *this);

static void
_release_stage(
    SubflowRateController *this);

static void
_check_stage(
    SubflowRateController *this);

static void
_mitigate_stage(
    SubflowRateController *this);

static void
_raise_stage(
    SubflowRateController *this);

static void
_probe_stage(
    SubflowRateController *this);

static void
_switch_stage_to(
    SubflowRateController *this,
    Stage target,
    gboolean execute);

static void
_overused_state(
    SubflowRateController *this);

static void
_stable_state(
    SubflowRateController *this);

static void
_monitored_state(
    SubflowRateController *this);

static void _set_bitrate_flags(
    SubflowRateController *this,
    guint flags);

static void
_transit_state_to(
    SubflowRateController *this,
    State target);

static void
_set_pacing_bitrate(
    SubflowRateController *this,
    guint32 target_bitrate,
    gboolean enable_pacing);

#define _enable_pacing(this) _set_path_pacing(this, TRUE)
#define _disable_pacing(this) _set_path_pacing(this, FALSE)
#define _pacing_enabled(this) this->path_is_paced

static void
_set_path_pacing(
    SubflowRateController *this,
    gboolean enable_pacing);

static gdouble
_adjust_bitrate(
    SubflowRateController *this);

static void
_disable_controlling(
    SubflowRateController *this);

#define MAX_MONITORING_INTERVAL 14
#define MIN_MONITORING_INTERVAL 2
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
 _is_near_to_congestion_point(
     SubflowRateController *this);

 static gdouble
 _get_congestion_influence(
     SubflowRateController *this);

static void
_add_congestion_point(
    SubflowRateController *this,
    gint32 rate);

static gboolean
 _open_cwnd(
     SubflowRateController *this);

static void
_ir_stat_pipe(
    gpointer data,
    NumsTrackerStatData *stat);

static void
_tr_stat_pipe(
    gpointer data,
    NumsTrackerStatData *stat);

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
  this->IR_window = make_numstracker(32, 5 * GST_SECOND);
  numstracker_add_plugin(this->IR_window, (NumsTrackerPlugin*)make_numstracker_stat_plugin(_ir_stat_pipe, this));
  this->TR_window = make_numstracker(32, 5 * GST_SECOND);
  numstracker_add_plugin(this->TR_window, (NumsTrackerPlugin*)make_numstracker_stat_plugin(_tr_stat_pipe, this));
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
  this->target_bitrate = sending_target * 8;
  this->min_target_point = this->target_bitrate * .9;
  this->max_target_point = this->target_bitrate * 1.1;
  this->min_rate = TARGET_BITRATE_MIN;
  this->max_rate = TARGET_BITRATE_MAX;
  this->min_target_point = MIN(TARGET_BITRATE_MIN, sending_target * 8);
  this->max_target_point = MAX(TARGET_BITRATE_MAX, sending_target * 8);

  _transit_state_to(this, STATE_STABLE);
  _set_bitrate_flags(this, BITRATE_CHANGE);
  subanalyser_reset(this->analyser);
  if(initial_disabling < 10  *GST_SECOND){
    this->last_reference_added = 0;
    this->disable_controlling = _now(this) + initial_disabling;
  }else{
    this->last_reference_added = 0;
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
  gint64 ir;

  if (_now(this) - RATE_ADJUST_INTERVAL < this->last_target_bitrate_adjust) {
    goto done;
  }

  mprtps_path_get_sent_bytes_in1s(this->path, &ir);
  numstracker_add(this->IR_window, ir * 8);
  numstracker_add(this->TR_window, this->target_bitrate);
  this->target_fraction = this->ir_sum / this->tr_sum;

  _change_target_bitrate(this, _adjust_bitrate(this));
  if(this->bitrate_flags & BITRATE_FORCED){
    _set_pacing_bitrate(this, this->target_bitrate * 1.5, TRUE);
  }

//done:
  this->last_target_bitrate_adjust = _now(this);
done:
  if(rep){
    rep->lost_bytes = _mt0(this)->lost;
    rep->discarded_rate = _discard_rate(this) * 100.;
    rep->owd = 0;//Todo: Fix it
    rep->max_rate = this->max_rate;
    rep->min_rate = this->min_rate;
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
                         gint32 max_rate)
{
  THIS_WRITELOCK(this);
  this->min_rate = min_rate;
  this->max_rate = max_rate;
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
  _mt0(this)->path_is_slow        = !mprtps_path_is_not_slow(this->path);
  _mt0(this)->has_expected_lost   = _mt1(this)->has_expected_lost;
  _mt0(this)->state               = _mt1(this)->state;
  _mt0(this)->stage               = _mt1(this)->stage;
  _mt0(this)->incoming_bitrate    = measurement->incoming_rate * 8;
//  _mt0(this)->discard_rate        = 1. - measurement->goodput / measurement->receiver_rate;
  _mt0(this)->sender_bitrate      = measurement->sender_rate * 8;
  if(measurement->expected_lost)
    _mt0(this)->has_expected_lost   = 3;
  else if(_mt0(this)->has_expected_lost > 0)
    --_mt0(this)->has_expected_lost;

  subanalyser_measurement_analyse(this->analyser,
                                  measurement,
                                  _TR(this),
                                  &_mt0(this)->analysation);

  _mt0(this)->discard_rate        = _anres(this).DiscRate;

  if(0 < this->disable_controlling && this->disable_controlling < _now(this)){
      this->disable_controlling = 0;
  }
  if(this->disable_controlling == 0LU){
    this->state_controller(this);
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

void _undershoot(SubflowRateController *this, gboolean disable_controlling)
{
  gint32 undershoot_target;
  if(_RateCorr(this) < .75)
    undershoot_target = _TR(this) * .6;
  else
    undershoot_target = _TR(this) * .8;

  _append_to_log (this,
          "############################### S%d Undershooting #######################################\n"
          "u_target: %-10d|\n"
          "######################################################################################\n",
          this->id,

          undershoot_target
          );

//  _skip_frames_for(this, 250 * GST_MSECOND);

  _add_congestion_point(this, _TR(this));
  this->target_bitrate =
      this->desired_bitrate =
      this->min_target_point = undershoot_target;
  this->max_target_point = _TR(this);
  _set_bitrate_flags(this, BITRATE_FORCED);

  if(disable_controlling){
    _disable_controlling(this);
  }

  return;
}

void
_restrict_stage(
    SubflowRateController *this)
{
  //check weather we reached the inflection point
  if((1.2 < _DiCoeffT(this) && 1. < _DeCorrT(this))){
    //pacing is ineffective
    _undershoot(this, TRUE);
    goto done;
  }

  _open_cwnd(this);
  this->desired_bitrate = this->max_target_point;
  _set_bitrate_flags(this, BITRATE_CHANGE | BITRATE_SLOW);
  _switch_stage_to(this, STAGE_RELEASE, FALSE);

done:
  return;
}

void
_release_stage(
    SubflowRateController *this)
{
  if(1. < _DiCoeffT(this)){
    _switch_stage_to(this, STAGE_RESTRICT, TRUE);
    goto done;
  }

  if(2. < _DeCorrT(this) || _discard(this)){
    goto done;
  }
  this->desired_bitrate = this->max_target_point;
  _set_bitrate_flags(this, BITRATE_CHANGE | BITRATE_SLOW);
  this->settled = TRUE;
done:
  return;
}

void
_check_stage(
    SubflowRateController *this)
{

  //Check weather the delay fluctuation is over the target
  if(.5 < _DeCorrT(this) || .1 < _discrate(this)){
    if(1. < _DeCorrT(this) || 1. < _DiCoeffT(this) || AGGRESSIVITY_TRESHOLD < _discrate(this)){
      _add_congestion_point(this, _TR(this));
      this->max_target_point = _TR(this) * 1.05;
      _switch_stage_to(this, STAGE_MITIGATE, TRUE);
    }
    goto done;
  }

  this->desired_bitrate = this->max_target_point;
  if(1. < _BiFCorr(this)){
    _set_bitrate_flags(this, BITRATE_CHANGE | BITRATE_SLOW);
  }else{
    _set_bitrate_flags(this, BITRATE_CHANGE);
  }

  //check weather 90% of the sent bytes are received
  if(_RateCorr(this) < .9){
    goto done;
  }

  //Check weather the sent bitrate reached the target bitrate
  //Check weather we reached the maximum bitrate point
  if(_TRateCorr(this) < .9 || this->target_bitrate < this->max_target_point * .95){
  //if(_TRateCorr(this) < .9){
    goto done;
  }

  //In that case we found a target point
  this->min_target_point = _TR(this) *  .95;
  this->max_target_point = _TR(this) * 1.05;
  this->steady = TRUE;

done:
  return;
}

void
_mitigate_stage(
    SubflowRateController *this)
{
  //determine target points
  if( .95 < _TRateCorr(this) && this->target_bitrate * .95 < this->min_target_point){
    this->min_target_point*=.95;
  }

  if(1.5 < _DeCorrT(this) && 1.5 < _DeCorrT_t1(this)){
//    this->distorted = TRUE;
  }

  this->target_bitrate =
      this->desired_bitrate =
          MAX(this->target_bitrate * .95, this->min_target_point);
  _set_bitrate_flags(this, BITRATE_CHANGE);
  _switch_stage_to(this, STAGE_CHECK, FALSE);
}

void
_probe_stage(
    SubflowRateController *this)
{
  //Check weather the delay fluctuation is over the target
//  if(.75 < _DeCorrT(this) && 1. < _DiCoeffT(this)){
  if((.75 < _DeCorrT(this) && 1. < _DiCoeffT(this)) || AGGRESSIVITY_TRESHOLD < _discrate(this)){
    _add_congestion_point(this, _TR(this));
    this->min_target_point *= .95;
    _switch_stage_to(this, STAGE_MITIGATE, TRUE);
    goto stabilize;
  }

  //stay here for a while to check
  if(_now(this) - 2 * GST_SECOND < this->monitoring_started){
    goto done;
  }

  this->desired_bitrate = this->max_target_point + this->monitored_bitrate;
  if(_is_near_to_congestion_point(this) || 1. < _BiFCorr(this)){
    _set_bitrate_flags(this, BITRATE_CHANGE | BITRATE_SLOW);
  }else{
    _set_bitrate_flags(this, BITRATE_CHANGE);
  }

  _switch_stage_to(this, STAGE_RAISE, FALSE);
  goto done;
stabilize:
  this->stabilize = TRUE;
done:
  return;
}


void
_raise_stage(
    SubflowRateController *this)
{
  if((.75 < _DeCorrT(this) && 1. < _DiCoeffT(this)) || AGGRESSIVITY_TRESHOLD < _discrate(this)){
    _add_congestion_point(this, _TR(this));
    this->min_target_point *= .95;
    _switch_stage_to(this, STAGE_MITIGATE, TRUE);
    goto stabilize;
  }
  //check signs of congestion
  if(_TRateCorr(this) < .9 || this->target_bitrate < this->max_target_point * .98){
  //if(_TRateCorr(this) < .9){
    goto done;
  }
  this->max_target_point = this->desired_bitrate;
stabilize:
  this->stabilize = TRUE;
done:
  return;
}

void
_overused_state(
    SubflowRateController *this)
{
  //Refresh congestion time
  this->last_congestion_detected = _now(this);

  //Execute stage
  this->state_action(this);
  if(!this->settled) goto done;

  //supervise state
  if(_lost(this)){
    if(_state_t1(this) != STATE_OVERUSED){
      goto done;
    }
  }

  this->settled = FALSE;
  _reset_monitoring(this);
  _transit_state_to(this, STATE_STABLE);

done:
  return;
}

void
_stable_state(
    SubflowRateController *this)
{
  //supervise stage actions
  if(_lost(this) || _discard(this) || (2. < _DeCorrT(this) && 2. < _DiCoeffT(this))){
    _undershoot(this, TRUE);
    _transit_state_to(this, STATE_OVERUSED);
    goto done;
  }

  this->state_action(this);
  if(this->distorted){
    this->distorted = FALSE;
    _undershoot(this, TRUE);
    _transit_state_to(this, STATE_OVERUSED);
    goto done;
  }
  if(0 < this->max_rate && this->max_rate < this->target_bitrate){
    goto done;
  }
  if(this->steady){
    this->steady = FALSE;
    _setup_monitoring(this);
    _transit_state_to(this, STATE_MONITORED);
  }
done:
  return;
}

void
_monitored_state(
    SubflowRateController *this)
{
  if(_lost(this) || _discard(this)){
    _undershoot(this, TRUE);
    _transit_state_to(this, STATE_OVERUSED);
    goto done;
  }

  //  _transit_state_to(this, STATE_STABLE);
  this->state_action(this);
  if(this->stabilize){
    this->stabilize = FALSE;
    _disable_monitoring(this);
    _transit_state_to(this, STATE_STABLE);
  }
done:
  return;
}

void _set_bitrate_flags(
    SubflowRateController *this,
    guint flags)
{
  this->bitrate_flags = flags;
}

void
_transit_state_to(
    SubflowRateController *this,
    State target)
{
  switch(target){
    case STATE_OVERUSED:
      mprtps_path_set_congested(this->path);
      this->state_controller = _overused_state;
      _switch_stage_to(this, STAGE_RESTRICT, FALSE);
    break;
    case STATE_STABLE:
      mprtps_path_set_non_congested(this->path);
      this->state_controller = _stable_state;
      this->min_rate_mitigated = FALSE;
      _switch_stage_to(this, STAGE_CHECK, FALSE);
    break;
    case STATE_MONITORED:
      this->state_controller = _monitored_state;
      _switch_stage_to(this, STAGE_PROBE, FALSE);
    break;
    default:
      g_warning("The desired state not exists.");
      break;
  }
  _mt0(this)->state = target;
}


void _switch_stage_to(
    SubflowRateController *this,
    Stage target,
    gboolean execute)
{
  SubRateCtrler stage = this->state_action;

  switch(target){
     case STAGE_CHECK:
       this->state_action = _check_stage;
     break;
     case STAGE_RESTRICT:
       this->state_action = _restrict_stage;
     break;
     case STAGE_RELEASE:
       this->state_action = _release_stage;
     break;
     case STAGE_MITIGATE:
       this->state_action = _mitigate_stage;
     break;
     case STAGE_RAISE:
       this->state_action = _raise_stage;
     break;
     case STAGE_PROBE:
       this->state_action = _probe_stage;
     break;
   }
  _mt0(this)->stage = target;
  if(execute){
    if(stage == this->state_action){
      g_debug("At Subrate procedure, the previous executed stage is exactly the same as the new one");
    }
    this->state_action(this);
  }
}


void _set_pacing_bitrate(SubflowRateController *this,
                         guint32 pacing_bitrate,
                         gboolean enable_pacing)
{
  this->pacing_bitrate = MAX(this->min_rate * 1.2, pacing_bitrate);
  mprtps_path_set_pacing_bitrate(this->path,
                                 this->pacing_bitrate,
                                 400 * GST_MSECOND);

  _set_path_pacing(this, enable_pacing);
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
  this->path_is_paced = enable_pacing;
  mprtps_path_set_pacing(this->path, enable_pacing);
}

gdouble _adjust_bitrate(SubflowRateController *this)
{
  gdouble drate = 0; //delta rate
  gdouble actual_rate = _TR(this);
  gdouble frac = (gdouble) RATE_ADJUST_INTERVAL / (gdouble) (1 * GST_SECOND);
  gdouble max_increasement;;
  frac *= this->desired_bitrate < this->target_bitrate ? 3. : 1.;
  max_increasement = RAMP_UP_MAX_SPEED * frac;
  if((this->bitrate_flags & BITRATE_CHANGE) == 0){
   goto done;
  }

  drate = this->desired_bitrate - this->target_bitrate;
  drate *= frac;
    if(this->target_bitrate < this->desired_bitrate && this->target_bitrate < this->last_congestion_point * .95)
      drate *= MAX(.1, _get_congestion_influence(this));
//    if(this->target_bitrate < this->desired_bitrate)
//      drate *= MAX(.3, _get_congestion_influence(this));

  drate /= this->bitrate_flags & BITRATE_SLOW ? 3. : 1.;
  drate = MIN(max_increasement, drate);

  if(this->target_fraction < .9 && 0. < drate){
    drate = 0.;
  }else if(drate < 0. && 1.1 < this->target_fraction){
    drate = 0.;
  }
done:
  return actual_rate + drate;
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

  scl =  (gdouble) this->min_rate;
  scl /= (gdouble)(_TR(this) - this->min_rate * .5);
//  scl /= (gdouble)(_SR(this) - this->min_rate );
  scl *= scl;
//  scl *= 4.;
  scl = MIN(1., MAX(1./14., scl));
  plus_rate = _TR(this) * scl * _get_congestion_influence(this);
     //cc point

  //considering congestion point


  _append_to_log (this,
    "####################### S%d Monitoring interval ################################\n"
    "last congestion point: %-10d| detected %lu ms ago|\n"
    "scl: %-5.3f|  plus: %-10.3f|\n"
    "######################################################################################\n",
    this->id,
    this->last_congestion_point,
    GST_TIME_AS_MSECONDS(_now(this) - this->last_congestion_detected),

    scl, plus_rate

    );
  plus_rate = MIN(plus_rate, MAX_MONITORING_RATE);
  interval = _calculate_monitoring_interval(this, plus_rate);
  if(_is_near_to_congestion_point(this)){
     interval = MAX(12, interval);
  }else if(interval < this->monitoring_interval - 2){
    interval = this->monitoring_interval - 2;
  }

  this->monitoring_interval = CONSTRAIN(min_interval, max_interval, interval);
  _set_monitoring_interval(this, interval);

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
  gint32 max_rate, min_rate;
  max_rate = !this->max_rate ? this->max_target_point : MIN(this->max_target_point, this->max_rate);
  min_rate = !this->min_rate ? this->min_target_point : MAX(this->min_target_point, this->min_rate);
  this->target_bitrate = CONSTRAIN(min_rate, max_rate, new_target);
}

gboolean _is_near_to_congestion_point(SubflowRateController *this)
{
  gint32 cc_point;
  cc_point = this->last_congestion_point;
  if(!cc_point) return FALSE;
  if( _TR(this) * .9 < cc_point && cc_point < _TR(this) * 1.1) return TRUE;
  return FALSE;
}

gdouble _get_congestion_influence(SubflowRateController *this)
{
  gdouble result;
  gdouble cc_point = this->last_congestion_point;
  gdouble actual_rate;
  if(!this->last_congestion_point) return 1.;
  actual_rate =_TR(this);
  result = (actual_rate - cc_point) / (.25 * actual_rate);
  result *= result;
//  result  *= 0. < result ? 2. : -.5;
  return CONSTRAIN(.01, 1., result);
}

void
_add_congestion_point(
    SubflowRateController *this, gint32 rate)
{
  this->last_congestion_detected = _now(this);
  this->last_congestion_point = rate;
}

gboolean _open_cwnd(SubflowRateController *this)
{
  mprtps_path_clear_queue(this->path);
  _disable_pacing(this);
  return TRUE;
}

static void _ir_stat_pipe(gpointer data,    NumsTrackerStatData *stat)
{
  SubflowRateController *this = data;
  this->ir_sum = stat->sum;
}

static void _tr_stat_pipe(gpointer data,    NumsTrackerStatData *stat)
{
  SubflowRateController *this = data;
  this->tr_sum = stat->sum;
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
          "pth_cong:   %-10d| pth_lssy:%-10d| pth_slow:%-10d| pacing:  %-10d|\n"
          "DeCorrH:    %-10.3f| DeCorrL: %-10.3f| DeCorrT: %-10.3f| DiCoeff: %-10.6f|\n"
          "RateCorr:   %-10.3f| TRateCorr:%-10.3f| BiFCorr: %-10.3f|\n"
          "target_br:  %-10d| min_tbr: %-10d| max_tbr: %-10d| dis_br:  %-10d|\n"
          "br_flags:   %-10d| stage:   %-10d| near2cc: %-10d| exp_lst: %-10d|\n"
          "mon_br:     %-10d| mon_int: %-10d| pacing_br:  %-10d| lc_rate: %-10d|\n"
          "SR:         %-10d| IR:      %-10d| disc_rate:  %-10.3f| ci:      %-10.3f\n"
          "l:          %-10d| rl:      %-10d| d:          %-10d| rd:      %-10d\n"
          "abs_max:    %-10d| abs_min: %-10d|\n"
          "############################ Seconds since setup: %lu ##########################################\n",
          this->id, _state(this),
          this->disable_controlling > 0 ? GST_TIME_AS_MSECONDS(this->disable_controlling - _now(this)) : 0,
          _mt0(this)->controlled,

          _rlost(this),_rdiscard(this),_lost(this),_discard(this),

          !mprtps_path_is_non_congested(this->path),
          !mprtps_path_is_non_lossy(this->path),
          !mprtps_path_is_not_slow(this->path),
          this->path_is_paced,

          0., 0.,
          _DeCorrT(this), _DiCoeffT(this),

          _RateCorr(this), _TRateCorr(this),_BiFCorr(this),

          this->target_bitrate, this->min_target_point,
          this->max_target_point, this->desired_bitrate,

          this->bitrate_flags, _stage(this),
          _is_near_to_congestion_point(this),
          _mt0(this)->has_expected_lost,

          this->monitored_bitrate, this->monitoring_interval,
          this->pacing_bitrate, this->last_congestion_point,

          _mt0(this)->sender_bitrate, _mt0(this)->incoming_bitrate,
          _mt0(this)->discard_rate, _get_congestion_influence(this),

          _mt0(this)->lost, _mt0(this)->recent_lost,
          _mt0(this)->discard,_mt0(this)->recent_discard,

          this->max_rate, this->min_rate,

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

