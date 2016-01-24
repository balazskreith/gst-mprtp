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
#define TARGET_BITRATE_MIN 500000
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


typedef struct{
  gdouble avg;
  gdouble x1;
  gdouble a0,a1;
}RData;

typedef struct _Moment Moment;

typedef enum{
  STATE_OVERUSED      = -1,
  STATE_STABLE         =  0,
  STATE_MONITORED      =  1,
}State;

typedef enum{
  STAGE_RESTRICT          = -3,
  STAGE_BOUNCEBACK        = -2,
  STAGE_OCHECK            = -1,
  STAGE_SCHECK            =  0,
  STAGE_MITIGATE          =  1,
  STAGE_RAISE             =  2,
  STAGE_MCHECK            =  3,
}Stage;

//ToDO Apply it
typedef enum{
  BITRATE_DIRECT_UP    =  3,
  BITRATE_FAST_UP      =  2,
  BITRATE_SLOW_UP      =  1,
  BITRATE_STAY         =  0,
  BITRATE_DRIVEN_DOWN  = -1,
  BITRATE_FORCED_DOWN  = -2,
  BITRATE_DIRECT_DOWN  = -3,
}BitrateAim;


struct _Moment{
  GstClockTime    time;
  guint64         recent_delay;
  guint64         RTT;
  guint64         ltt_delays_th;
  gint32          max_target_point;
  gint32          min_target_point;
  guint64         ltt_delays_target;
  guint32         jitter;
  guint32         lost;
  gboolean        has_expected_lost;
  guint32         discarded_bits;
  gint32          receiver_rate;
  gint32          dreceiver_rate;
  gint32          sender_rate;
  gint32          dsender_rate;
  gint32          incoming_rate;
  gint32          dincoming_rate;
  gint32          goodput;
  gint32          bytes_newly_acked;
  gint32          bytes_in_flight_ack;
  gint32          dbytes_in_flight_ack;
  gint64          bytes_in_flight_ested;
  gdouble         BiF_off;
  guint32         bytes_in_queue;
  guint64         max_bytes_in_flight;

  gboolean        recent_discard;
  gboolean        recent_lost;
  gboolean        path_is_lossy;
  gboolean        path_is_slow;

  //derivatives
  gdouble         discard_rate;
  gdouble         corrh_owd;
//  gdouble         rate_corr;
  gdouble         del_corr;
//  gdouble         BiF_off_target;
  gdouble         off_target;
  BitrateAim      bitrate_aim;
  //OWD trend indicates incipient congestion. Initial value: 0.0
  gdouble         owd_trend;
  gdouble         BiF_corr;
  gdouble         congestion_indicator;
  gint32          delta_rate;
//  gint32          delta_cwnd;

  //application
  State           state;
  Stage           stage;
  gboolean        controlled;

};



//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

 void subratectrler_finalize (GObject * object);


// const gdouble ST_ = 1.1; //Stable treshold
// const gdouble OT_ = 2.;  //Overused treshold
// const gdouble DT_ = 1.5; //Down Treshold


//--------------------MEASUREMENTS-----------------------

#define _actual_rate(this) (MAX(_mt0(this)->sender_rate, _mt0(this)->receiver_rate))
#define _ramp_up_speed(this) (MIN(RAMP_UP_SPEED, this->target_bitrate))
#define _now(this) (gst_clock_get_time(this->sysclock))
#define _get_moment(this, n) ((Moment*)(this->moments + n * sizeof(Moment)))
#define _pmtn(index) (index == MOMENTS_LENGTH ? MOMENTS_LENGTH - 1 : index == 0 ? MOMENTS_LENGTH - 1 : index - 1)
#define _mt0(this) _get_moment(this, this->moments_index)
#define _mt1(this) _get_moment(this, _pmtn(this->moments_index))
#define _mt2(this) _get_moment(this, _pmtn(_pmtn(this->moments_index)))
#define _mt3(this) _get_moment(this, _pmtn(_pmtn(_pmtn(this->moments_index))))

//FixME: apply trend calculation instead of recent discard
#define _rdiscard(this) (!_mt0(this)->path_is_slow && _mt0(this)->recent_discard)
//FixME: apply trend calculation instead of recent discard
#define _discard(this) (_mt0(this)->discard_rate > .25)
#define _discard_rate(this) _mt0(this)->discard_rate
#define _lost(this)  (!_mt0(this)->has_expected_lost && !_mt0(this)->path_is_lossy && _mt0(this)->lost > 10)
#define _rlost(this) (!_mt0(this)->has_expected_lost && !_mt0(this)->path_is_lossy && _mt0(this)->recent_lost > 10)
#define _corrH(this) (_mt0(this)->corrh_owd)
#define _state_t1(this) _mt1(this)->state
#define _state(this) _mt0(this)->state
#define _stage(this) _mt0(this)->stage
#define _otrend(this) _mt0(this)->owd_trend
#define _GP(this) _mt0(this)->goodput
#define _BiFCorr(this) _mt0(this)->BiF_corr
#define _BiFCorr_t1(this) _mt1(this)->BiF_corr
#define _BiFCorr_t2(this) _mt2(this)->BiF_corr
#define _BiF(this) _mt0(this)->bytes_in_flight_ack
#define _BiF_t1(this) _mt1(this)->bytes_in_flight_ack
#define _BiF_t2(this) _mt2(this)->bytes_in_flight_ack
#define _GP_t1(this) _mt1(this)->goodput
#define _IR(this) _mt0(this)->incoming_rate
#define _IR_t1(this) _mt1(this)->incoming_rate
#define _IR_t2(this) _mt2(this)->incoming_rate
#define _IR_t3(this) _mt3(this)->incoming_rate
#define _RR(this) _mt0(this)->receiver_rate
#define _RR_t1(this) _mt1(this)->receiver_rate
#define _RR_t2(this) _mt2(this)->receiver_rate
#define _RR_t3(this) _mt3(this)->receiver_rate
#define _SR(this) _mt0(this)->sender_rate
#define _SR_t1(this) _mt1(this)->sender_rate
#define _SR_t2(this) _mt2(this)->sender_rate
#define _SR_t3(this) _mt3(this)->sender_rate
#define _dSR(this) _mt0(this)->dsender_rate
#define _TR(this) this->target_bitrate
#define _TR_t1(this) this->target_bitrate
#define _CI(this) _mt0(this)->congestion_indicator
#define _CI_t1(this) _mt1(this)->congestion_indicator
#define _rateCorr(this) (gdouble)_SR(this) / (gdouble)_RR(this)
#define _rateCorr_t1(this) (gdouble)_SR_t1(this) / (gdouble)_RR_t1(this)
#define _rateCorr_t2(this) (gdouble)_SR_t2(this) / (gdouble)_RR_t2(this)
#define _maxCorr(this) (gdouble)_RR(this) / (gdouble)_IR(this)
#define _sCorr(this)  (gdouble) _SR(this) / (gdouble) _TR(this)
#define _sCorr_t1(this)  (gdouble) _SR_t1(this) / (gdouble) _TR_t1(this)
#define _tCorr(this) (gdouble)_TR(this) / (gdouble)this->max_target_point
#define _delcorr(this) _mt0(this)->del_corr
#define _delcorr_t1(this) _mt1(this)->del_corr
#define _delcorr_t2(this) _mt2(this)->del_corr
#define _delcorr2(this) (_delcorr(this) * _delcorr(this))
#define _delcorr2_t1(this) (_delcorr_t1(this) * _delcorr_t1(this))
#define _delcorr2_t2(this) (_delcorr_t2(this) * _delcorr_t2(this))
#define _dCorr(this) (_delcorr(this)) / (_delcorr_t1(this))

//#define _reset_target_points(this) numstracker_reset(this->target_points)
#define _add_target_point(this, target) numstracker_add(this->target_points, target)

#define _set_bitrate_aim(this, aim) _mt0(this)->bitrate_aim = aim
#define _bitrate_aim(this) _mt0(this)->bitrate_aim
#define _bitrate_aim_t1(this) _mt1(this)->bitrate_aim
#define _skip_frames_for(this, duration) mprtps_path_set_skip_duration(this->path, duration);



 static Moment*
_m_step(
    SubflowRateController *this);

 static void
_set_rate_controller(SubflowRateController *this, BitrateAim aim, gboolean execute);

static void
_update_bitrate(
    SubflowRateController *this);

static void
_restricted_stage(
    SubflowRateController *this);

static void
_bounceback_stage(
    SubflowRateController *this);

static void
_ocheck_stage(
    SubflowRateController *this);

static void
_scheck_stage(
    SubflowRateController *this);

static void
_mitigate_stage(
    SubflowRateController *this);

static void
_raise_stage(
    SubflowRateController *this);

static void
_mcheck_stage(
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

static void
_undershoot(
    SubflowRateController *this,
    gboolean disable_controlling);

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
_set_owd_trend(
    SubflowRateController *this);

static gdouble
_get_off_target(
    SubflowRateController *this);

static void
_calculate_congestion_indicator(
    SubflowRateController *this);

static void
_rate_ctrler_direct_down(
    SubflowRateController *this);

static void
_rate_ctrler_forced_down(
    SubflowRateController *this);

static void
_rate_ctrler_driven_down(
    SubflowRateController *this);

static void
_rate_ctrler_direct_up(
    SubflowRateController *this);

static void
_rate_ctrler_fast_up(
    SubflowRateController *this);

static void
_rate_ctrler_slow_up(
    SubflowRateController *this);

static void
_rate_ctrler_stay(
    SubflowRateController *this);

// gboolean
//_cwnd_can_increase(SubflowRateController *this);

static void
_ltt_delays_th_stats_pipe(gpointer data, PercentileTrackerPipeData *pdata);

static void
_change_target_bitrate(SubflowRateController *this, gint32 new_target);

static void
_ltt_delays_target_stats_pipe(gpointer data, PercentileTrackerPipeData *pdata);

static void
_target_points_max_pipe(gpointer data, guint64 value);

static void
_target_points_min_pipe(gpointer data, guint64 value);

static void
_reset_target_points(SubflowRateController *this);


static void
_bights_in_flight_max_pipe(gpointer data, guint64 value);


static gint32
_get_congestion_point(
    SubflowRateController *this);

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
  g_rw_lock_init (&this->rwmutex);
  this->ltt_delays_th = make_percentiletracker(100, 80);
  percentiletracker_set_treshold(this->ltt_delays_th, GST_SECOND * 60);
  percentiletracker_set_stats_pipe(this->ltt_delays_th,
                                   _ltt_delays_th_stats_pipe, this);
  this->ltt_delays_target = make_percentiletracker(100, 50);
  percentiletracker_set_treshold(this->ltt_delays_target, GST_SECOND * 60);
  percentiletracker_set_stats_pipe(this->ltt_delays_target,
                                   _ltt_delays_target_stats_pipe, this);

  this->owd_fraction_hist = make_floatnumstracker(20, 5 * GST_SECOND);

  this->bytes_in_flight_history = make_numstracker(16, 10 * GST_SECOND);
  numstracker_add_plugin(this->bytes_in_flight_history,
                         (NumsTrackerPlugin*) make_numstracker_minmax_plugin(_bights_in_flight_max_pipe, this, NULL, NULL));

  this->target_points = make_numstracker(16, 5 * GST_SECOND);
  numstracker_add_plugin(this->target_points,
                         (NumsTrackerPlugin*) make_numstracker_minmax_plugin(_target_points_max_pipe, this, _target_points_min_pipe, this));

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
                         guint32 sending_target)
{
  THIS_WRITELOCK(this);
  percentiletracker_reset(this->ltt_delays_th);
  percentiletracker_reset(this->ltt_delays_target);
  this->path = g_object_ref(path);
  memset(this->moments, 0, sizeof(Moment) * MOMENTS_LENGTH);

  this->id = mprtps_path_get_id(this->path);
  this->setup_time = _now(this);
  this->monitoring_interval = 3;
  this->owd_fraction_avg = 0.;
  this->BiF_ested_avg = 0.;
  this->delay_fluctuation_avg = 0.;
  this->mss = 1400;
  this->cwnd_min = INIT_CWND;
  this->cwnd_i = 1;
  this->pacing_bitrate = INIT_CWND;
  this->target_bitrate = sending_target * 8;
  numstracker_add(this->target_points, this->target_bitrate);
  this->s_rtt = 0.;
  this->min_rate = TARGET_BITRATE_MIN;
  this->max_rate = TARGET_BITRATE_MAX;
  _add_target_point(this, MIN(TARGET_BITRATE_MIN, sending_target * 8));
  _add_target_point(this, MAX(sending_target * 8, TARGET_BITRATE_MAX));
  this->min_target_point = MIN(TARGET_BITRATE_MIN, sending_target * 8);
  this->max_target_point = MAX(TARGET_BITRATE_MAX, sending_target * 8);
  _transit_state_to(this, STATE_STABLE);
  _set_rate_controller(this, BITRATE_DIRECT_UP, TRUE);
  this->disable_controlling = _now(this) + 5 * GST_SECOND;
  THIS_WRITEUNLOCK(this);
}

void subratectrler_unset(SubflowRateController *this)
{
  THIS_WRITELOCK(this);
  g_object_unref(this->path);
  this->path = NULL;
  THIS_WRITEUNLOCK(this);
}

void subratectrler_extract_stats(SubflowRateController *this,
                                  gint32 *sender_bitrate,
                                  gint32 *goodput,
                                  gint32  *monitored_bits,
                                  gint32 *target_bitrate,
                                  guint32 *queued_bits,
                                  guint64 *target_delay,
                                  guint64 *ltt80th_delay,
                                  guint64 *recent_delay)
{
  if(sender_bitrate){
    if(this->path)
      *sender_bitrate = mprtps_path_get_sent_bytes_in1s(this->path, NULL) * 8;
    else
      g_warning("No Path at Subratectrler");
  }
  if(goodput){
    *goodput = _mt0(this)->goodput * 8.;
  }
  if(monitored_bits){
    *monitored_bits = this->monitored_bitrate;
  }
  if(target_bitrate){
    *target_bitrate = this->target_bitrate;
  }
  if(queued_bits){
      if(this->path)
        *queued_bits = mprtps_path_get_bytes_in_queue(this->path) * 8;
      else
        g_warning("No Path at Subratectrler");
  }
  if(target_delay){
    *target_delay = _mt0(this)->ltt_delays_target;
  }
  if(ltt80th_delay){
    *ltt80th_delay = _mt0(this)->ltt_delays_th;
  }
  if(recent_delay){
    *recent_delay = _mt0(this)->recent_delay;
  }
}

void subratectrler_time_update(
                         SubflowRateController *this,
                         gint32 *target_bitrate,
                         gint32 *extra_bitrate,
                         UtilizationSubflowReport *rep)
{

  gint32 new_sender_rate;
  gint64 new_incoming_rate;
  new_sender_rate = mprtps_path_get_sent_bytes_in1s(this->path, &new_incoming_rate) * 8;
  new_incoming_rate *= 8;

  _mt0(this)->dsender_rate        = new_sender_rate - _mt0(this)->sender_rate;
  _mt0(this)->sender_rate         = new_sender_rate;
  _mt0(this)->dincoming_rate      = new_incoming_rate - _mt0(this)->incoming_rate;
  _mt0(this)->incoming_rate       = new_incoming_rate;
  _mt0(this)->bytes_in_queue      = mprtps_path_get_bytes_in_queue(this->path);

  if(this->moments_num == 0){
      this->bytes_in_queue_avg = _mt0(this)->bytes_in_queue;
  }else{
      this->bytes_in_queue_avg *= .5;
      this->bytes_in_queue_avg += _mt0(this)->bytes_in_queue * .5;
  }
  this->s_SR = .8 * this->s_SR + .2 * _mt0(this)->sender_rate;

  mprtps_path_get_bytes_in_flight(this->path, NULL, &_mt0(this)->bytes_in_flight_ested);
  if(0 < _mt0(this)->bytes_in_flight_ested){
      gdouble off;
      this->BiF_ested_avg = .8* this->BiF_ested_avg + .2* (gdouble)_mt0(this)->bytes_in_flight_ested;
      off = (this->BiF_ested_avg - this->BiF_acked_avg) / this->BiF_ested_avg;
      _mt0(this)->BiF_off = off;
      this->BiF_off_avg = .1 * off + .9 * this->BiF_off_avg;

//    _mt0(this)->BiF_off_target =    MAX(-1., MIN(1., this->BiF_off_ested_avg));
  }

  if(!numstracker_get_num(this->target_points)){
    //something goes wrong if we haven't found
    //a target point in 10s
      _add_target_point(this, _SR(this) * .8);
      _add_target_point(this, _SR(this));
  }

  DISABLE_LINE  _update_bitrate(this);
  this->rate_controller(this);

  if(rep){
    rep->lost_bytes = _mt0(this)->lost;
    rep->discarded_bytes = _mt0(this)->discarded_bits;
    rep->owd = _mt0(this)->recent_delay;
    rep->max_rate = this->max_rate;
  }
  if(target_bitrate)
    *target_bitrate = this->target_bitrate;
  if(extra_bitrate)
    *extra_bitrate = this->monitored_bitrate;

  return;
}

void subratectrler_measurement_update(
                         SubflowRateController *this,
                         RRMeasurement * measurement)
{
  gint i;
//  g_print_rrmeasurement(measurement);
  if(measurement->goodput <= 0.) goto done;
  _m_step(this);

  _mt0(this)->time                = measurement->time;
  _mt0(this)->recent_delay        = measurement->recent_delay;
  _mt0(this)->RTT                 = measurement->RTT;
  _mt0(this)->discarded_bits      = measurement->late_discarded_bytes * 8;
  _mt0(this)->lost                = measurement->lost;
  _mt0(this)->recent_lost         = measurement->recent_lost;
  _mt0(this)->recent_discard      = measurement->recent_discard;
  _mt0(this)->path_is_lossy       = !mprtps_path_is_non_lossy(this->path);
  _mt0(this)->path_is_slow        = 1 || !mprtps_path_is_not_slow(this->path);
  _mt0(this)->goodput             = measurement->goodput * 8;
  _mt0(this)->receiver_rate       = measurement->receiver_rate * 8;
  _mt0(this)->jitter              = measurement->jitter;
  _mt0(this)->bytes_newly_acked   = measurement->expected_payload_bytes;
  _mt0(this)->bytes_in_flight_ack = measurement->bytes_in_flight_acked;

  _mt0(this)->has_expected_lost   = _mt1(this)->has_expected_lost;
  _mt0(this)->BiF_off             = _mt1(this)->BiF_off;
  _mt0(this)->bitrate_aim         = _mt1(this)->bitrate_aim;
  _mt0(this)->bytes_in_flight_ested = _mt1(this)->bytes_in_flight_ested;
  _mt0(this)->sender_rate         = _mt1(this)->sender_rate;
  _mt0(this)->incoming_rate       = _mt1(this)->incoming_rate;
  _mt0(this)->bytes_in_queue      = _mt1(this)->bytes_in_queue;
  _mt0(this)->max_bytes_in_flight = _mt1(this)->max_bytes_in_flight;
  _mt0(this)->ltt_delays_th       = _mt1(this)->ltt_delays_th;
  _mt0(this)->ltt_delays_target   = _mt1(this)->ltt_delays_target;
  _mt0(this)->state               = _mt1(this)->state;
  _mt0(this)->stage               = _mt1(this)->stage;

  _mt0(this)->discard_rate        = 1. - measurement->goodput / measurement->receiver_rate;
//  _mt0(this)->max_corr            = MIN((gdouble) _RR(this) / (gdouble)_SR(this), (gdouble) _RR_t1(this) / (gdouble) _SR(this));
//  _mt0(this)->max_corr            = (gdouble)(_RR(this) + _RR_t1(this)) / (this->s_SR * 2.);
//  _mt0(this)->rate_corr            = (gdouble)(_RR(this) ) / (this->s_SR);

//  _mt0(this)->rr_fluctuation      = log(_RR(this)) - log(_RR_t1(this));

  _mt0(this)->dbytes_in_flight_ack = _mt0(this)->bytes_in_flight_ack - _mt1(this)->bytes_in_flight_ack;
//  _mt0(this)->dbytes_in_flight_ack *=8;
  _mt0(this)->dreceiver_rate = _mt0(this)->receiver_rate - _mt1(this)->receiver_rate;

  if(measurement->expected_lost)
    _mt0(this)->has_expected_lost   = 2;
  else
    --_mt0(this)->has_expected_lost;

  if(1 < this->moments_num)
    this->s_rtt = measurement->RTT * .125 + this->s_rtt * .875;
  else
    this->s_rtt = measurement->RTT;

  if(_mt1(this)->bytes_in_flight_ack){
    _mt0(this)->BiF_corr = (gdouble) _mt0(this)->bytes_in_flight_ack / (gdouble)_mt1(this)->bytes_in_flight_ack;
  }else{
    _mt0(this)->BiF_corr = 1.;
  }

  if(numstracker_get_num(this->bytes_in_flight_history) < 1){
      _mt0(this)->max_bytes_in_flight = measurement->bytes_in_flight_acked;
  }

  percentiletracker_obsolate(this->ltt_delays_target);
  if(!percentiletracker_get_num(this->ltt_delays_target)){
      percentiletracker_add(this->ltt_delays_target, OWD_TARGET_LO);
  }

  percentiletracker_obsolate(this->ltt_delays_th);
  if(!percentiletracker_get_num(this->ltt_delays_th)){
      percentiletracker_add(this->ltt_delays_th, OWD_TARGET_HI);
  }

  this->BiF_acked_avg = .2* this->BiF_acked_avg + .8* (gdouble)_mt0(this)->bytes_in_flight_ack;

  for(i=0; i<measurement->rle_delays.length; ++i){
    gdouble owd_fraction, delay;
    gdouble delay_fluctuation;

    delay = measurement->rle_delays.values[i];
    owd_fraction = delay/(gdouble)_mt0(this)->ltt_delays_target;
    floatnumstracker_add(this->owd_fraction_hist, owd_fraction);
    this->owd_fraction_avg = .9* this->owd_fraction_avg + .1* owd_fraction;
    this->delay_t3 = this->delay_t2;
    this->delay_t2 = this->delay_t1;
    this->delay_t1 = this->delay_t0;
    this->delay_t0 = delay;

    if(0. < this->delay_t0 && 0. < this->delay_t1) {
      delay_fluctuation = log(this->delay_t0) - log(this->delay_t1);
      this->delay_fluctuation_avg *= .5;
      this->delay_fluctuation_avg += delay_fluctuation * .5;
      this->delay_fluctuation_var_avg *= .5;
      this->delay_fluctuation_var_avg += pow(delay_fluctuation,2) * .5;
    }
  }
  _mt0(this)->del_corr = (.25 * this->delay_t2 + .25 * this->delay_t1 + .5 * this->delay_t0) / ((gdouble)_mt0(this)->ltt_delays_target);

  if(!measurement->rle_delays.length){
    gdouble owd_fraction;
    owd_fraction = (gdouble)measurement->recent_delay/(gdouble)_mt0(this)->ltt_delays_target;
    this->owd_fraction_avg = .9* this->owd_fraction_avg + .1* owd_fraction;
    floatnumstracker_add(this->owd_fraction_hist, owd_fraction);
  }

  if(1. < _delcorr2(this)){
    _add_congestion_point(this, _SR(this));
    if(_delcorr2_t1(this) < 1.){
      _add_target_point(this, (_SR(this) + _SR_t1(this)) / 2);
    }
  }

  _set_owd_trend(this);
  mprtps_path_set_delay(this->path, _mt0(this)->ltt_delays_target);

  if(!_mt0(this)->ltt_delays_th) _mt0(this)->ltt_delays_th = OWD_TARGET_HI;
  _mt0(this)->corrh_owd = (gdouble)_mt0(this)->recent_delay / (gdouble)_mt0(this)->ltt_delays_th;

  _mt0(this)->off_target = _get_off_target(this);

  if(0 < this->disable_controlling && this->disable_controlling < _now(this)){
    this->disable_controlling = 0;
  }
  _calculate_congestion_indicator(this);
  if(this->disable_controlling == 0LU){
    this->state_controller(this);
    _mt0(this)->controlled = TRUE;
  }

  _log_measurement_update_state(this);

  if(_mt0(this)->state != STATE_OVERUSED){
    percentiletracker_add(this->ltt_delays_th, measurement->recent_delay);
    percentiletracker_add(this->ltt_delays_target, measurement->recent_delay);
    numstracker_add(this->bytes_in_flight_history, measurement->bytes_in_flight_acked);
    this->packet_obsolation_treshold = OWD_TARGET_HI - _mt0(this)->ltt_delays_target;
    for(i=0; i<measurement->rle_delays.length; ++i){
      guint64 delay;
      delay = measurement->rle_delays.values[i];
      if(!measurement->rle_delays.values[i]) continue;
      //Percentiletracker has a tiny problem with similar value handling.
      //That is why we add some random ns value, in order to
      //get a reasonable median target value
      delay+= g_random_int_range(0, 1000);
      percentiletracker_add(this->ltt_delays_th, delay);
      percentiletracker_add(this->ltt_delays_target, delay);
    }
  }
done:
  return;
}

gint32 subratectrler_get_target_bitrate(SubflowRateController *this)
{
  return this->target_bitrate;
}

void subratectrler_add_extra_rate(SubflowRateController *this,
                                  gint32 extra_rate)
{

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
_set_rate_controller(SubflowRateController *this, BitrateAim aim, gboolean execute)
{
  if(aim != _bitrate_aim(this)){
    this->init_rate_ctrler = FALSE;
  }
  _bitrate_aim(this) = aim;
  switch(_bitrate_aim(this))
  {
    case BITRATE_DIRECT_DOWN:
      this->rate_controller = _rate_ctrler_direct_down;
    break;
    case BITRATE_DRIVEN_DOWN:
      this->rate_controller = _rate_ctrler_driven_down;
    break;
    case BITRATE_FORCED_DOWN:
      this->rate_controller = _rate_ctrler_forced_down;
    break;
    case BITRATE_SLOW_UP:
      this->rate_controller = _rate_ctrler_slow_up;
    break;
    case BITRATE_FAST_UP:
      this->rate_controller = _rate_ctrler_fast_up;
    break;
    case BITRATE_DIRECT_UP:
      this->rate_controller = _rate_ctrler_direct_up;
    break;
    case BITRATE_STAY:
    default:
      this->rate_controller = _rate_ctrler_stay;
    break;
  }

  if(execute){
    this->rate_controller(this);
  }
}

void
_update_bitrate(SubflowRateController *this)
{
  gdouble queue_ratio;
  if (_now(this) - RATE_ADJUST_INTERVAL < this->last_target_bitrate_adjust) {
    goto exit;
  }

  queue_ratio = (gdouble)(this->bytes_in_queue_avg * 8) / (gdouble) this->target_bitrate;
  //Todo: Check weather we need this or not.
  if(.5 < queue_ratio){
    if(this->last_queue_clear < _now(this) - 5 * GST_SECOND){
      DISABLE_LINE mprtps_path_clear_queue(this->path);
      this->last_queue_clear = _now(this);
    }else if(this->last_skip_time < _now(this) - GST_SECOND){
      DISABLE_LINE mprtps_path_set_skip_duration(this->path, 500 * GST_MSECOND);
      this->last_skip_time = _now(this);
    }
  }

  _change_target_bitrate(this, _adjust_bitrate(this));

//done:
  this->last_target_bitrate_adjust = _now(this);
exit:
  return;
}

void
_restricted_stage(
    SubflowRateController *this)
{
  //check delay condition
  if(1. < _delcorr2_t1(this) &&
     1. < _delcorr2(this) &&
     _IR(this) * .9 < this->target_bitrate)
  {
    this->min_target_point *= .8;
  }

  if(2. < _corrH(this)){
    //if we haven't reached the inflection point then undershoot
    if(1.1 <= _dCorr(this)){
      if(_TR(this) < _SR(this) * 1.1){
        _undershoot(this, TRUE);
      }
    }
    goto done;
  }

  if(1.5 < _corrH(this)){
    if(this->bitrate_aim_is_reached){
      _undershoot(this, TRUE);
    }
    goto done;
  }

  _switch_stage_to(this, STAGE_BOUNCEBACK, TRUE);
done:
  return;
}

void
_bounceback_stage(
    SubflowRateController *this)
{
  //if it starts increasing again send back to restricted stage
  if(2. < _corrH(this)){
    _undershoot(this, TRUE);
    goto done;
  }

  if(_pacing_enabled(this)){
    _open_cwnd(this);
    goto done;
  }
  //until it not goes down below to 1.2 not open it further
  if(1.2 < _corrH(this)){
    _set_rate_controller(this, BITRATE_FAST_UP, TRUE);
    goto done;
  }else{
    _set_rate_controller(this, BITRATE_DIRECT_UP, TRUE);
  }
  _switch_stage_to(this, STAGE_OCHECK, FALSE);
done:
  return;
}

void
_ocheck_stage(
    SubflowRateController *this)
{
  if(2. < _corrH(this)){
    _undershoot(this, FALSE);
    goto done;
  }else if(_corrH(this) < 1.2){
    _set_rate_controller(this, BITRATE_FAST_UP, TRUE);
  }

  if(!this->bitrate_aim_is_reached){
    goto done;
  }
  this->settled = TRUE;

done:
  return;
}

void
_scheck_stage(
    SubflowRateController *this)
{
  //check signs of congestion
  if(1. < _delcorr2(this) ||  1.1 < _sCorr(this) || _CI(this) < PRE_CONGESTION_GUARD){
    _switch_stage_to(this, STAGE_MITIGATE, TRUE);
    goto done;
  }

  //if the congestion is alleviated then turn the bitrate up
  if(_bitrate_aim(this) < 0 || _bitrate_aim_t1(this) < 0){
    _set_rate_controller(this, BITRATE_SLOW_UP, TRUE);
//    //determine weather slow or fast up
//    if(_CI(this) < PRE_CONGESTION_GUARD * .5 || _is_near_to_congestion_point(this)){
//      _set_rate_controller(this, BITRATE_SLOW_UP, TRUE);
//    }else{
//      _set_rate_controller(this, BITRATE_FAST_UP, TRUE);
//    }
    goto done;
  }


  if(!this->bitrate_aim_is_reached){
    goto done;
  }

  _add_target_point(this, _SR(this));
  this->max_target_point = MAX(_TR(this), _SR(this)) * .99;
  this->min_target_point = MAX(_TR(this), _SR(this)) * .97;

  if(1. < _delcorr2_t1(this)){
    goto done;
  }

  this->steady = TRUE;

  if(_is_near_to_congestion_point(this)){
    _set_rate_controller(this, BITRATE_SLOW_UP, TRUE);
  }

done:
  return;
}

void
_mitigate_stage(
    SubflowRateController *this)
{
  //determine target points
  if(_delcorr2(this) < 1. && _delcorr2_t1(this) && !this->min_rate_mitigated){
    this->min_target_point *=.95;
    this->min_rate_mitigated = TRUE;
  }

  if(1. < _delcorr2(this)){
    if(1. < _corrH(this))
      _set_rate_controller(this, BITRATE_FORCED_DOWN, TRUE);
    else
      _set_rate_controller(this, BITRATE_DRIVEN_DOWN, TRUE);
    goto done;
  }

  if(1.1 < _sCorr(this) || _CI(this) < PRE_CONGESTION_GUARD){
    _set_rate_controller(this, BITRATE_DRIVEN_DOWN, TRUE);
    goto done;
  }

  //determine mitigation speed
done:
  _switch_stage_to(this, STAGE_SCHECK, FALSE);
  return;
}

void
_mcheck_stage(
    SubflowRateController *this)
{
  if(1. < _corrH(this) || 1. < _delcorr2(this)){
    this->min_target_point *= .95;
    _set_rate_controller(this, BITRATE_FORCED_DOWN, TRUE);
     this->monitoring_interval>>=1;
     this->stabilize = TRUE;
     goto stabilize;
  }

  if(_CI(this) < PRE_CONGESTION_GUARD){
    _add_congestion_point(this, _SR(this));
    this->min_target_point *= .95;
    _set_rate_controller(this, BITRATE_DRIVEN_DOWN, TRUE);
    ++this->monitoring_interval;
    goto stabilize;
  }

  this->extra_added = FALSE;
  _switch_stage_to(this, STAGE_RAISE, TRUE);
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
  //check signs of congestion
  gint32 ramp_up;

  if(this->extra_added){
    this->extra_added = FALSE;
    this->stabilize = TRUE;
    goto done;
  }

  ramp_up = MIN(this->monitored_bitrate, RAMP_UP_MAX_SPEED);
  this->max_target_point = MIN(_SR(this), _TR(this)) + ramp_up;
  _disable_monitoring(this);
  this->extra_added = TRUE;
  if(_is_near_to_congestion_point(this)){
    _set_rate_controller(this, BITRATE_SLOW_UP, TRUE);
  }else{
    _set_rate_controller(this, BITRATE_FAST_UP, TRUE);
  }

done:
  return;
}

void
_overused_state(
    SubflowRateController *this)
{
  //Refresh congestion time
  this->last_congestion_detected = _now(this);

  //supervise stage actions
  if(_rlost(this) || _discard(this)){
    if(_state_t1(this) == STATE_OVERUSED){
      _transit_state_to(this, STATE_STABLE);
      goto done;
    }
    if(_discard(this) && _lost(this)){
      _undershoot(this, TRUE);
    }else{
      _undershoot(this, FALSE);
    }
    goto done;
  }

  //Execute stage
  this->state_action(this);
  if(this->settled){
    this->settled = FALSE;
    _reset_monitoring(this);
    _transit_state_to(this, STATE_STABLE);
  }

done:
  return;
}

void
_stable_state(
    SubflowRateController *this)
{
  //supervise stage actions
  if(_lost(this)){
    if(_rlost(this)){
      _undershoot(this, TRUE);
      _transit_state_to(this, STATE_OVERUSED);
    }
    goto done;
  }
  if(_rdiscard(this)){
    _undershoot(this, TRUE);
    _transit_state_to(this, STATE_OVERUSED);
    goto done;
  }

  if(1.2 < _corrH(this)){
    if(_state_t1(this) != STATE_STABLE){
      goto done;
    }
    _undershoot(this, TRUE);
    _transit_state_to(this, STATE_OVERUSED);
    goto done;
  }

  this->state_action(this);
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
  if(_rlost(this) || _rdiscard(this)){
    _undershoot(this, TRUE);
    _disable_monitoring(this);
    _transit_state_to(this, STATE_OVERUSED);
    goto done;
  }
  if(_lost(this) || _discard(this)){
    _set_bitrate_aim(this, BITRATE_DRIVEN_DOWN);
    _disable_monitoring(this);
    _transit_state_to(this, STATE_STABLE);
    goto done;
  }
  if(_corrH(this) > 1.5){
    _undershoot(this, TRUE);
    _disable_monitoring(this);
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

void _undershoot(SubflowRateController *this, gboolean disable_controlling)
{
  gint32 possible_rate;
  gint32 target_rate;
  gint32 picked_target;
  gint32 dRate;
//  gdouble pfactor;

//  if(this->consecutive_undershoot == 1){
//    pfactor = .9;
//  }else if(this->consecutive_undershoot == 2){
//    pfactor = .6;
//  }else{
//    pfactor = pow(.6, this->consecutive_undershoot);
//  }
//  ++this->consecutive_undershoot;

  if(_GP_t1(this) < _SR(this)){
    dRate = _SR(this) - _GP_t1(this);
    picked_target = _GP_t1(this) * .9;
  }else if(_GP(this) < _SR(this)){
    dRate = _SR(this) - _GP(this);
    picked_target = _GP(this) * .9;
  }else{
    dRate = _SR(this) * .1;
    picked_target = _SR(this) * .9;
//    picked_target = _TR(this);
  }

//  picked_target *= pfactor;

  picked_target = MIN(picked_target, _TR(this) * .8);

  possible_rate = _SR(this) - 2 * dRate;
  if(_SR(this) < 2 * dRate || possible_rate < _SR(this) * .6){
    if(_state(this) == STATE_OVERUSED || picked_target < _SR(this) * .6){
      target_rate = _SR(this) * .6;
    }else{
      target_rate = _SR(this) * .9;
    }
  }else{
    target_rate = MAX(_SR(this) * .6, possible_rate * .9);
  }

  _append_to_log (this,
          "############################### S%d Undershooting #######################################\n"
          "GP:    %-10d| GP_t1:  %-10d| SR:       %-10d|\n"
          "dRate: %-10d| p_rate: %-10d| p_target: %-10d| target: %-10d|\n"
          "######################################################################################\n",
          this->id,

          _GP(this),_GP_t1(this), _SR(this),

          dRate, possible_rate, picked_target, target_rate
          );

//  target_rate = MIN(target_rate, _TR(this));

  if(target_rate < _SR(this) * .8){
    _skip_frames_for(this, 250 * GST_MSECOND);
  }

  _reset_target_points(this);
  _add_target_point(this, target_rate);
  _add_target_point(this, picked_target);
  _add_congestion_point(this, _SR(this));
  this->target_bitrate = target_rate;
  this->max_target_point = MAX(target_rate, picked_target);
  this->min_target_point = MIN(target_rate, picked_target);
  _set_rate_controller(this, BITRATE_DIRECT_DOWN, TRUE);
//  _set_bitrate_aim(this, BITRATE_DIRECT_DOWN);
//  _set_pacing_bitrate(this, target_rate * 1.1, TRUE);

  if(disable_controlling){
    _disable_controlling(this);
  }

  return;
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
      this->consecutive_undershoot = 1;
      _switch_stage_to(this, STAGE_SCHECK, FALSE);
    break;
    case STATE_MONITORED:
      this->state_controller = _monitored_state;
      _switch_stage_to(this, STAGE_MCHECK, FALSE);
    break;
  }
  _mt0(this)->state = target;
}

void _switch_stage_to(
    SubflowRateController *this,
    Stage target,
    gboolean execute)
{
  SubRateProc stage = this->state_action;

  switch(target){
     case STAGE_OCHECK:
       this->state_action = _ocheck_stage;
     break;
     case STAGE_RESTRICT:
       this->state_action = _restricted_stage;
     break;
     case STAGE_BOUNCEBACK:
       this->state_action = _bounceback_stage;
     break;
     case STAGE_SCHECK:
       this->state_action = _scheck_stage;
     break;
     case STAGE_MITIGATE:
       this->state_action = _mitigate_stage;
     break;
     case STAGE_RAISE:
       this->state_action = _raise_stage;
     break;
     case STAGE_MCHECK:
       this->state_action = _mcheck_stage;
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
  this->pacing_bitrate = MAX(this->min_rate * 1.1, pacing_bitrate);
  mprtps_path_set_pacing_bitrate(this->path,
                                 this->pacing_bitrate,
                                 this->packet_obsolation_treshold);

  _set_path_pacing(this, enable_pacing);
}

void _set_path_pacing(SubflowRateController *this, gboolean enable_pacing)
{
  this->path_is_paced = enable_pacing;
  mprtps_path_set_pacing(this->path, enable_pacing);
}

gdouble _adjust_bitrate(SubflowRateController *this)
{
  gdouble drate = 0; //delta rate
  gdouble actual_rate, target_rate;
  gdouble speed = 1.;
  gdouble max_increasement = RAMP_UP_MAX_SPEED * .25;
  gdouble min_target_rate = 0, max_target_rate = 0;

//  max_target_rate = MAX(this->min_target_point * 1.1, this->max_target_point);
//  min_target_rate = MIN(this->max_target_point * .9, this->min_target_point);

  actual_rate = _TR(this);
  //    acceleration = (gdouble)RATE_ADJUST_INTERVAL/(gdouble)GST_SECOND;

  switch(_bitrate_aim(this))
  {
    case BITRATE_DIRECT_DOWN:
//      target_rate = this->min_target_point;
      target_rate = min_target_rate;
      speed = 1.;
    break;
    case BITRATE_DRIVEN_DOWN:
//      target_rate = MAX(this->target_bitrate * .5, min_target_rate);
      target_rate = min_target_rate;
      speed = .33;
    break;
    case BITRATE_FORCED_DOWN:
//      target_rate = MAX(this->target_bitrate * .8, min_target_rate);
      target_rate = min_target_rate;
      speed = .11;
    break;
    case BITRATE_SLOW_UP:
      target_rate = max_target_rate;
      speed = .11;
    break;
    case BITRATE_FAST_UP:
      target_rate = max_target_rate;
      speed = .33;
    break;
    case BITRATE_DIRECT_UP:
//      target_rate = this->max_target_point;
      target_rate = max_target_rate;
      speed = 1.;
    break;
    default:
    case BITRATE_STAY:
      target_rate = _TR(this);
      speed = 1.;
        break;
  }

  drate =  (target_rate - actual_rate);
  drate *= speed;
  drate *= (1 - MAX(-.1, MIN(.1, this->BiF_off_avg)));
//  g_print("dRate * correction %f = %f\n", (1 - MAX(-.1, MIN(.1, this->BiF_off_avg))), drate);
  drate = MIN(max_increasement, drate);
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
  gint32 x,c;
  gdouble f;

  max_interval = MAX_MONITORING_INTERVAL;
  min_interval = MIN_MONITORING_INTERVAL;

  if(0 && _is_near_to_congestion_point(this)){
    interval = 14;
    goto done;
  }

  scl =  (gdouble) this->min_rate;
  scl /= (gdouble)(_SR(this) - this->min_rate * .5);
//  scl /= (gdouble)(_SR(this) - this->min_rate );
  scl *= scl;
//  scl *= 4.;
  scl = MIN(1., MAX(1./14., scl));
  plus_rate = _SR(this) * scl;
     //cc point

  //considering congestion point
   x = _SR(this);
   c = _get_congestion_point(this);
   f = (gdouble) (x-c) / (gdouble) (x * .2);
   f *= f;
   f = MIN(1, MAX(.125, f));
   if(0 && _is_near_to_congestion_point(this))
   {
     plus_rate *=f;
   }

  _append_to_log (this,
    "####################### S%d Monitoring interval ################################\n"
    "last congestion point: %-10d| detected %lu ms ago|\n"
    "scl_1: %-5.3f / %5.3f  = %5.3f|\n"
    "scl_2: %-5.3f * %5.3f  = %5.3f|\n"
    "scl: %-5.3f| plus_rate: %-5.3f| final plus_rate: %-5.3f|\n"
    "f:   %-5.3f| plus_rate*f: %-5.3f| final plus rate2: %-5.3f|\n"
    "######################################################################################\n",
    this->id,
    this->last_congestion_point,
    GST_TIME_AS_MSECONDS(_now(this) - this->last_congestion_detected),

    (gdouble) this->min_rate, (gdouble)(_SR(this) - this->min_rate * .5),
    (gdouble) this->min_rate / (gdouble)(_SR(this) - this->min_rate * .5),

    (gdouble) this->min_rate / (gdouble)(_SR(this) - this->min_rate * .5),
    (gdouble) this->min_rate / (gdouble)(_SR(this) - this->min_rate * .5),
    (gdouble) this->min_rate / (gdouble)(_SR(this) - this->min_rate * .5) *
    (gdouble) this->min_rate / (gdouble)(_SR(this) - this->min_rate * .5),

    scl, plus_rate, MIN(plus_rate, MAX_MONITORING_RATE),

    f, f * plus_rate, MIN(f * plus_rate, MAX_MONITORING_RATE)
    );
  plus_rate *= _get_congestion_influence(this);
  plus_rate = MIN(plus_rate, MAX_MONITORING_RATE);
  //we are around the congestion point;
  interval = _calculate_monitoring_interval(this, plus_rate);
  if(_is_near_to_congestion_point(this)){
     interval = MAX(12, interval);
  }else if(interval < this->monitoring_interval * .75){
    interval = this->monitoring_interval * .75;
  }
//  if(0 < this->monitoring_interval){
////    if(this->monitoring_interval < 10) ++this->monitoring_interval;
//    interval = MAX(this->monitoring_interval, interval);
//  }

  this->monitoring_interval = MAX(min_interval, MIN(max_interval, interval));
done:
  _set_monitoring_interval(this, interval);

}

void _set_monitoring_interval(SubflowRateController *this, guint interval)
{
  this->monitoring_started = _now(this);
  if(interval > 0)
    this->monitored_bitrate = (gdouble)_actual_rate(this) / (gdouble)interval;
  else
    this->monitored_bitrate = 0;
  mprtps_path_set_monitor_interval(this->path, interval);
  return;
}

guint _calculate_monitoring_interval(SubflowRateController *this, guint32 desired_bitrate)
{
  gdouble actual, target, rate;
  guint monitoring_interval = 0;
  if(desired_bitrate <= 0){
     goto exit;
   }
  actual = MIN(_SR(this), _TR(this));
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


 static void _iterator_process(gpointer data, gdouble owd_fraction)
{
  RData *rdata = data;
  gdouble x0;
  x0 = owd_fraction - rdata->avg;
  rdata->a0 += x0 * x0;
  rdata->a1 += x0 * rdata->x1;
  rdata->x1 = x0;
}

void _set_owd_trend(SubflowRateController *this)
{
  RData rdata;
  gdouble trend;
  rdata.a0 = rdata.a1 = rdata.x1 = 0.;
  floatnumstracker_get_stats(this->owd_fraction_hist, NULL, &rdata.avg);
  floatnumstracker_iterate(this->owd_fraction_hist, _iterator_process, &rdata);
  if(rdata.a0 <= 0.) goto done;
  trend = MAX(0.0f, MIN(1.0f, (rdata.a1 / rdata.a0)*this->owd_fraction_avg));
  _mt0(this)->owd_trend = trend;
done:
  return;
}

gdouble _get_off_target(SubflowRateController *this)
{
  gdouble off_target;
  off_target = (gdouble)((gdouble)_mt0(this)->ltt_delays_target - (gdouble)_mt0(this)->recent_delay) / (gfloat)_mt0(this)->ltt_delays_target;
  return off_target;
}

void _calculate_congestion_indicator(SubflowRateController *this)
{
  gdouble ci;
  gdouble x1,x2,x3,y1,y2,y3;
  gdouble w1,w2,w3;
//  gdouble rr_delay_cc, rr_delay_cc_abs, rr_delay_cc_norm; //crosscorrelation between RR and delay
//  gdouble BiF_delay_cc, BiF_delay_cc_abs, BiF_delay_cc_norm; //crosscorrelation between BiF and delay

  gdouble cc_abs, cc_norm,cc;
  w1 = .5; w2 = .25; w3 = .25;
  //  x1 = w1 * _mt0(this)->BiF_corr;
  //  x2 = w2 * _mt1(this)->BiF_corr;
  //  x3 = w3 * _mt2(this)->BiF_corr;
//  x1 = w1 * (log(_SR(this)) - log(_SR_t1(this)));
//  x2 = w2 * (log(_SR_t1(this)) - log(_SR_t2(this)));
//  x3 = w3 * (log(_SR_t2(this)) - log(_SR_t3(this)));
  x1 = w1 * (gdouble)_SR(this);
  x2 = w2 * (gdouble)_SR_t1(this);
  x3 = w3 * (gdouble)_SR_t2(this);
//  x1 = w1 * _mt0(this)->sender_rate;
//  x2 = w2 * _mt1(this)->sender_rate;
//  x3 = w3 * _mt2(this)->sender_rate;
  //  y1 = w1 * _mt0(this)->receiver_rate;
  //  y2 = w2 * _mt1(this)->receiver_rate;
  //  y3 = w3 * _mt2(this)->receiver_rate;
//  y1 = w1 /_rateCorr(this);
//  y2 = w2 / _rateCorr_t1(this);
//  y3 = w3 / _rateCorr_t2(this);
  //  y1 = w1 * (log(_RR(this)) - log(_RR_t1(this)));
  //  y2 = w2 * (log(_RR_t1(this)) - log(_RR_t2(this)));
  //  y3 = w3 * (log(_RR_t2(this)) - log(_RR_t3(this)));
    y1 = w1 * (gdouble)_RR(this);
    y2 = w2 * (gdouble)_RR_t1(this);
    y3 = w3 * (gdouble)_RR_t2(this);

//  cc_abs = x1 * y1 + x2 * y2 + x3 * y3;
//
//  cc_norm = x1 * x1 + x2 * x2 + x3 * x3;
//  cc_norm *= y1 * y1 + y2 * y2 + y3 * y3;
//  cc_norm = sqrt(cc_norm);
  cc_abs = y1 + y2 + y3;
  cc_norm = x1 + x2 + x3;
  cc = cc_abs / cc_norm;
  ci = 0. != cc_norm ? cc : 1.;

  //congestion indicator is a value btw -1 and 1.
  //crosscorrelation btw rr and delay
//

    _append_to_log (this,
            "################### S%d | Congestion Indicator #################\n"
      "dBiF0:   %-10d| dBif1:   %-10d| dBif2:   %-10d| abs:  %-10.3f\n"
      "dRR0:    %-10d| dRR1:    %-10d| dRR2:    %-10d| norm: %-10.3f\n"
      "ci:      %-10.3f|\n"
            "#####################################################################\n",
            this->id,
            _mt0(this)->dbytes_in_flight_ack,
            _mt1(this)->dbytes_in_flight_ack,
            _mt2(this)->dbytes_in_flight_ack,
            cc_abs,

            _mt0(this)->dreceiver_rate,
            _mt1(this)->dreceiver_rate,
            _mt2(this)->dreceiver_rate,
            cc_norm,

            ci

          );
//  if(_CI_t1(this) * 2. < ci) ci+=.1;

//  if(!_get_congestion_pont(this)) ci/=2;

//  ci = MIN(1.,MAX(-1.,ci));
  _CI(this) = ci;
}
//
//void _calculate_fluctuation(SubflowRateController *this)
//{
//
//}
//
//gboolean _cwnd_can_increase(SubflowRateController *this)
//{
//  gfloat alpha = 1.25f+2.75f*(1.0f-this->owd_trend_mem);
//  return this->cwnd <= alpha*_mt0(this)->bytes_in_flight_ack;
//}


 void
_rate_ctrler_direct_down(
    SubflowRateController *this)
{
  //congestion window set target will follow
  //init
  if(!this->init_rate_ctrler){
    _set_pacing_bitrate(this, this->min_target_point * 1., TRUE);
    this->init_rate_ctrler = TRUE;
    this->bitrate_aim_is_reached = FALSE;
  }

  _change_target_bitrate(this, this->min_target_point);
  if(this->min_target_point < _IR(this) * 1.1){
    goto done;
  }

  if(_pacing_enabled(this)){
    _open_cwnd(this);
    goto done;
  }

  _disable_pacing(this);
  this->bitrate_aim_is_reached = TRUE;
done:
  return;
}

 void
_rate_ctrler_forced_down(
    SubflowRateController *this)
{
  //congestion window set target follows
   gint32 drate;
   if(!this->init_rate_ctrler){
     _set_pacing_bitrate(this, _IR(this) * 1.2, TRUE);
     _change_target_bitrate(this, this->min_target_point);
     this->bitrate_aim_is_reached = FALSE;
     this->init_rate_ctrler = TRUE;
     goto done;
   }

   drate = this->pacing_bitrate - this->min_target_point;
   drate *= .25;
   this->pacing_bitrate -= drate;
   _set_pacing_bitrate(this, this->pacing_bitrate, TRUE);
   if(this->target_bitrate < _IR(this) * 1.1){
     goto done;
   }
//
//   if(this->pacing_bitrate * .1 < _mt0(this)->bytes_in_queue * 8){
//     _set_pacing_bitrate(this, this->pacing_bitrate * 1.05, TRUE);
//     goto done;
//   }
   if(!_open_cwnd(this)){
     goto done;
   }

   this->bitrate_aim_is_reached = TRUE;
 done:
   return;
}

 void
_rate_ctrler_driven_down(
    SubflowRateController *this)
{
 //target set congestion window stays
 gint32 drate;
 if(!this->init_rate_ctrler){
   this->bitrate_aim_is_reached = FALSE;
   this->init_rate_ctrler = TRUE;
 }

//  if(this->pacing_bitrate * .05 < _mt0(this)->bytes_in_queue * 8){
//    _set_pacing_bitrate(this, this->pacing_bitrate * 1.05, TRUE);
//    goto done;
//  }else if(0 < _mt0(this)->bytes_in_queue * 8){
//    _disable_pacing(this);
//    goto done;
//  }

  if(_pacing_enabled(this)){
    _open_cwnd(this);
    goto done;
  }

  drate = this->target_bitrate - this->min_target_point;
  drate *= .25;

 _change_target_bitrate(this, this->target_bitrate - drate);
  if(this->target_bitrate < _IR(this) * 1.1){
    goto done;
  }
 this->bitrate_aim_is_reached = TRUE;
 _disable_pacing(this);
done:
  return;
}

 void
_rate_ctrler_direct_up(
    SubflowRateController *this)
{
  //target directly set without congestion window
  if(!this->init_rate_ctrler){
    _change_target_bitrate(this, this->max_target_point);
    _disable_pacing(this);
    this->init_rate_ctrler = TRUE;
    this->bitrate_aim_is_reached = FALSE;
  }

  if(_IR(this) * 1.1 < this->max_target_point){
    goto done;
  }
  this->bitrate_aim_is_reached = TRUE;
done:
  return;
}

 void
_rate_ctrler_fast_up(
    SubflowRateController *this)
{
  //target increased without congestion window
  gint32 drate;
  gint32 max_drate = 100000;
  if(!this->init_rate_ctrler){
   this->init_rate_ctrler = TRUE;
   this->bitrate_aim_is_reached = FALSE;
  }

  if(_pacing_enabled(this)){
    _open_cwnd(this);
    goto done;
  }

//  if(this->pacing_bitrate * .1 < _mt0(this)->bytes_in_queue * 8){
//    _set_pacing_bitrate(this, this->pacing_bitrate * 1.1, TRUE);
//    goto done;
//  }else if(0 < _mt0(this)->bytes_in_queue * 8){
//    _disable_pacing(this);
//    goto done;
//  }

  drate = this->max_target_point - this->target_bitrate;
//  {
//    gdouble f;
//    gint32 c,x;
//    x = _SR(this);
//    c = _get_congestion_point(this);
//    f = (gdouble) (x-c) / (gdouble) (x * .5);
//    f *= f;
//    f = MIN(1, MAX(.025, f));
//    drate *= f;
//  }
  drate *=.25;
  drate *= (1 - MAX(-.1, MIN(.1, this->BiF_off_avg)));
  drate = MIN(drate, max_drate);
  _change_target_bitrate(this, this->target_bitrate + drate);

  if(_IR(this) * 1.1 < this->max_target_point){
    goto done;
  }
  this->bitrate_aim_is_reached = TRUE;
done:
  return;
}

 void
_rate_ctrler_slow_up(
    SubflowRateController *this)
{
  //target increased with congestion window
  gint32 drate;
  if(!this->init_rate_ctrler){
    this->bitrate_aim_is_reached = FALSE;
    this->init_rate_ctrler = TRUE;
  }
  //if there is queue in the bytes available we need to flush it first.
//  if(this->pacing_bitrate * .05 < _mt0(this)->bytes_in_queue * 8){
//   _set_pacing_bitrate(this, this->pacing_bitrate * 1.05, TRUE);
//   goto done;
//  }else if(0 < _mt0(this)->bytes_in_queue * 8){
//   _disable_pacing(this);
//   goto done;
//  }

  if(_pacing_enabled(this)){
    _open_cwnd(this);
    goto done;
  }

  drate = this->max_target_point - this->target_bitrate;
  drate *=.11;
  drate *= (1 - MAX(-.1, MIN(.1, this->BiF_off_avg)));
  _change_target_bitrate(this, this->target_bitrate + drate);

  if(_IR(this) * 1.1 < this->max_target_point){
   goto done;
  }
  this->bitrate_aim_is_reached = TRUE;
done:
 return;
}

 void
_rate_ctrler_stay(
    SubflowRateController *this)
{
   //target set with congestion window
   _change_target_bitrate(this, this->target_bitrate);
   _disable_pacing(this);
}

void _change_target_bitrate(SubflowRateController *this, gint32 new_target)
{
  gint32 actual_target = _TR(this);
  gint32 delta;
//  delta = 0;
  delta=new_target - actual_target;
  if(0 < this->max_rate){
    new_target = MIN(new_target, this->max_rate);
  }
  if(0 < this->min_rate){
    new_target = MAX(new_target, this->min_rate);
  }
//  g_print("current target: %d delta: %d new target: %d\n",
//          _TR(this), delta, new_target);
  _mt0(this)->delta_rate += delta;
  this->target_bitrate = new_target;
}

void _ltt_delays_target_stats_pipe(gpointer data, PercentileTrackerPipeData *pdata)
{
  SubflowRateController *this = data;
  _mt0(this)->ltt_delays_target = pdata->percentile;
}


void _ltt_delays_th_stats_pipe(gpointer data, PercentileTrackerPipeData *pdata)
{
  SubflowRateController *this = data;
  _mt0(this)->ltt_delays_th = pdata->percentile;
}

void _target_points_max_pipe(gpointer data, guint64 value)
{
  SubflowRateController *this = data;
  _mt0(this)->max_target_point = MAX(value, this->min_rate);
}

void _target_points_min_pipe(gpointer data, guint64 value)
{
  SubflowRateController *this = data;
  _mt0(this)->min_target_point = MAX(value, this->min_rate);
}

void _reset_target_points(SubflowRateController *this)
{
  numstracker_reset(this->target_points);
//  this->min_target_point = this->max_target_point = 0;
}


void _bights_in_flight_max_pipe(gpointer data, guint64 value)
{
  SubflowRateController *this = data;
  _mt0(this)->max_bytes_in_flight = value;

}

gint32 _get_congestion_point(SubflowRateController *this)
{
//  if(this->last_congestion_detected < _now(this) - 5 * GST_SECOND){
//    return 0;
//  }
  return this->last_congestion_point;
}

gboolean _is_near_to_congestion_point(SubflowRateController *this)
{
  gint32 cc_point;
  cc_point = _get_congestion_point(this);
  if(!cc_point) return FALSE;
  if(_now(this) - 10 * GST_SECOND < this->last_congestion_detected) return TRUE;
  if(MIN(_SR(this), _TR(this)) * .9 < cc_point && cc_point < MAX(_SR(this), _TR(this)) * 1.1) return TRUE;
  return FALSE;
}

gdouble _get_congestion_influence(SubflowRateController *this)
{
  gdouble result;
  gdouble cc_point = this->last_congestion_point;
  gdouble actual_rate;
  if(!this->last_congestion_point) return 1.;
  actual_rate = MAX(_TR(this), _SR(this));
  actual_rate /= 100000.;
  cc_point /= 100000.;
  result = (actual_rate - cc_point) / actual_rate;
//  result *= result;
  result  *= 0. < result ? 2. : -.5;
  _append_to_log(this,
                 "#######################################################################################\n"
                 "# get_congestion_influence: ((%-10.3f - %-10.3f) / %-10.3f)^2 = %-10.3f #"
                 "#######################################################################################\n",
                 actual_rate, cc_point, actual_rate, result
                 );
  return CONSTRAIN(.1, 1., result);
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
  gboolean result = !_pacing_enabled(this);
  gdouble queue_ratio;
  if(result) goto done;
  queue_ratio = _mt0(this)->bytes_in_queue * 8;
  queue_ratio /= (gdouble)this->target_bitrate;
  if(.05 < queue_ratio){
    mprtps_path_clear_queue(this->path);
    _set_pacing_bitrate(this, this->pacing_bitrate * 1.1, TRUE);
    goto done;
  }
  _disable_pacing(this);
  result = TRUE;
done:
  return result;
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
          "pth_cong:   %-10d| pth_lssy:%-10d| pth_slow:%-10d| con_inf: %-10.3f|\n"
          "corrH:      %-10.3f| target:  %-10.3f| rdelay:  %-10.3f| Del_cor: %-10.6f|\n"
          "GP:         %-10d| SR:      %-10d| RR:      %-10d| rateCor: %-10f|\n"
          "BiF_havg:   %-10.3f| BiF_avg: %-10.3f| BiF_off: %-10.3f| Boff_avg:%-10.3f|\n"
          "target_br:  %-10d| min_tbr: %-10d| max_tbr: %-10d| sCorr:   %-10.3f|\n"
          "br_aim:     %-10d| q_bits:  %-10d| BiFCorr: %-10.3f| dCorr:   %-10.6f|\n"
          "mon_br:     %-10d| mon_int: %-10d| maxCorr: %-10.3f| lc_rate: %-10d|\n"
          "pacing_br:  %-10d| inc_br:  %-10d| stage:   %-10d| CI:      %-10.6f|\n"
          "inc/sr:     %-10.3f| br_rched:%-10d| near2cc: %-10d| exp_lst: %-10d|\n"
          "############################ Seconds since setup: %lu ##########################################\n",
          this->id, _state(this),
          this->disable_controlling > 0 ? GST_TIME_AS_MSECONDS(this->disable_controlling - _now(this)) : 0,
          _mt0(this)->controlled,

          _rlost(this),_rdiscard(this),_lost(this),_discard(this),

          !mprtps_path_is_non_congested(this->path),
          !mprtps_path_is_non_lossy(this->path),
          !mprtps_path_is_not_slow(this->path),
          _get_congestion_influence(this),

          _corrH(this),
          (gdouble)GST_TIME_AS_MSECONDS(_mt0(this)->ltt_delays_target),
          (gdouble)_mt0(this)->recent_delay / (gdouble) GST_MSECOND,
          //_mt0(this)->off_target,
          _delcorr(this) * _delcorr(this),

          _GP(this), _SR(this), _RR(this), _rateCorr(this),

          this->BiF_ested_avg, this->BiF_acked_avg,
          _mt0(this)->BiF_off,  this->BiF_off_avg,

          this->target_bitrate, this->min_target_point,
          this->max_target_point, _sCorr(this),

          _mt0(this)->bitrate_aim, _mt0(this)->bytes_in_queue * 8,
          _BiFCorr(this), _dCorr(this),

          this->monitored_bitrate, this->monitoring_interval,
          _maxCorr(this), _get_congestion_point(this),

          this->pacing_bitrate, _mt0(this)->incoming_rate,
          _stage(this), _CI(this),

          (gdouble) _mt0(this)->incoming_rate / (gdouble)_mt0(this)->sender_rate,
          this->bitrate_aim_is_reached, _is_near_to_congestion_point(this),
          _mt0(this)->has_expected_lost,

          GST_TIME_AS_SECONDS(_now(this) - this->setup_time)

          );
  fclose(file);
}

void _log_abbrevations(SubflowRateController *this, FILE *file)
{
  fprintf(file,
  "############ Subflow %d abbrevations to measured values ###########################################\n"
  "#  State:      The actual state (Overused (-1), Stable (0), Monitored (1))                        #\n"
  "#  Ctrled:     Indicate weather the state is controlled or not                                    #\n"
  "#  rlost:      recent losts                                                                       #\n"
  "#  rdiscard:   recent discards                                                                    #\n"
  "#  lost:       any losts                                                                          #\n"
  "#  pth_lssy:   Indicate weather the path is lossy                                                 #\n"
  "#  pth_cong:   Indicate weather the path is congested                                             #\n"
  "#  pth_slow:   Indicate weather the path is slow                                                  #\n"
  "#  discard:    any discards                                                                       #\n"
  "#  corrH:      correlation value to the delay high treshold                                       #\n"
  "#  target:     the target delay                                                                   #\n"
  "#  rdelay:     the most recent delay                                                              #\n"
  "#  Del_cor:    recent delays correlation to the target delay                                      #\n"
  "#  GP:         calculated goodput                                                                 #\n"
  "#  SR:         recorded sender rate in the last 1s                                                #\n"
  "#  RR:         calculated receiver rate                                                           #\n"
  "#  rateCor:    correaltion between sender and receiver rate                                       #\n"
  "#  BiF_havg:   Average of the Estimated bytes in flights                                          #\n"
  "#  BiF_avg:    Average of the reported bytes in flights                                           #\n"
  "#  BiF_off:    The actual off of bytes in flight from the estimated                               #\n"
  "#  Boff_avg    The actual off of bytes in flight average from the estimated avg                   #\n"
  "#  target_br:  The actual target bitrate                                                          #\n"
  "#  min_tbr:    The minimal target used in mitigations                                             #\n"
  "#  max_tbr:    The maximal target used in ramping up                                              #\n"
  "#  sCorr:      The sending correaltion to the target                                              #\n"
  "#  br_aim:     The actual bitrate aim (Direct forced down (-3), Forced fast down (-2),            #\n"
  "#                                      Driven slow down (-1), Stay (0), Driven slow up (1),       #\n"
  "#                                      Driven Fast up (2),Driven direct up(3))                    #\n"
  "#  q_bits:     Bits in the pacing queue                                                           #\n"
  "#  BiFCorr:    Correlation between estimated and acknowledged averages of                         # \n"
  "#              values of bytes in flights                                                         #\n"
  "#  dCorr:      Coefficient of the last and the actual delay correlation values                    #\n"
  "#              (Used to determine weather we reached the inflection point in undershooting        #\n"
  "#  mon_br:     Monitored bitrate                                                                  #\n"
  "#  mon_int:    Monitoring interval                                                                #\n"
  "#  maxCorr:    The sender rate correlation to the maximal target bitrate                          #\n"
  "#  lc_rate:    Last registered congestion bitrate                                                 #\n"
  "#  pacing_br:  Pacing bitrate                                                                     #\n"
  "#  inc_br:     Incoming bitrate                                                                   #\n"
  "#  stage:      Actual Stage the state in. (Restrict (-3), BounceBack (-2), Overused Check (-1),   #\n"
  "#                                          Stable check (0), Mitigate (1), Monitored check (2),   #\n"
  "#                                          Raise (3)                                              #\n"
  "#  inc/sr:     Coefficient between incoming and sending rate                                      #\n"
  "#  br_rched:   Indicate weather the incoming rate reached the target bitrate                      #\n"
  "#  CI:         Congestion indicator used by PRE_CONGESTION_GUARD                                  #\n"
  "#  near2cc:    Indicate weather we are near to the last known congestion point                    #\n"
  "#  exp_lst:    Indicate weather the path has expected lost or not due to pacing buffer obsolation.#\n"
  "#  con_inf:    The influence value considering the last congestion point to the actual rate.      #\n"
  "###################################################################################################\n",
  this->id
  );
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
