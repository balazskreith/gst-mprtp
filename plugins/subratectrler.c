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
//Headroom for limitation of CWND. Default value: 1.1
#define MAX_BYTES_IN_FLIGHT_HEAD_ROOM 1.1
//Gain factor for congestion window adjustment. Default value:  1.0
#define GAIN_UP 1.0
//Gain factor for congestion window adjustment. Default value:  1.0
#define GAIN_DOWN 1.0
//Initial cwnd regarding to minimal bitrate
#define INIT_CWND 5000
// Max video rampup speed in bps/s (bits per second increase per second)
#define RAMP_UP_SPEED 200000.0f // bps/s
//CWND scale factor due to loss event. Default value: 0.6
#define BETA 0.6
// Target rate scale factor due to loss event. Default value: 0.8
#define BETA_R 0.8
//Additional slack [%]  to the congestion window. Default value: 10%
#define BYTES_IN_FLIGHT_SLACK .1
//Interval between video bitrate adjustments. Default value: 0.2s ->200ms
#define RATE_ADJUST_INTERVAL 200 * GST_MSECOND /* ms */
//Video coder frame period [s]
#define FRAME_PERIOD 0
//Min target_bitrate [bps]
#define TARGET_BITRATE_MIN 500000
//Max target_bitrate [bps]
#define TARGET_BITRATE_MAX 0
//Timespan [s] from lowest to highest bitrate. Default value: 10s->10000ms
#define RAMP_UP_TIME 10000
//Guard factor against early congestion onset.
//A higher value gives less jitter possibly at the
//expense of a lower video bitrate. Default value: 0.0..0.2
#define PRE_CONGESTION_GUARD 0.0
//Guard factor against RTP queue buildup. Default value: 0.0..2.0
#define TX_QUEUE_SIZE_FACTOR 1.0


typedef struct{
  gdouble avg;
  gdouble x1;
  gdouble a0,a1;
}RData;

typedef struct _Moment Moment;

typedef enum{
  STATE_OVERUSED       = -1,
  STATE_STABLE         =  0,
  STATE_MONITORED       = 1,
}State;


struct _Moment{
  guint16         PiT;
  guint64         delay;
  guint64         RTT;
  guint64         ltt_delays_th;
  guint64         ltt_delays_target;
  guint32         jitter;
  guint32         lost;
  guint32         discard;
  guint32         receiver_rate;
  guint32         sender_rate;
  guint32         goodput;
  guint32         bytes_newly_acked;
  guint32         bytes_in_flight;
  guint32         bytes_in_queue;
  guint64         max_bytes_in_flight;

  //derivatives
  guint32         receiver_rate_std;
  gdouble         corrd;
  gdouble         discard_rate;
  gdouble         corrh_owd;
  gdouble         goodput_ratio;
  gdouble         delay_ratio;
  gdouble         corr_rate;
  gdouble         corr_rate_dev;
  gdouble         owd_fraction;
  gboolean        can_cwnd_increase;
  gboolean        can_bitrate_increase;
  gdouble         off_target;
  //OWD trend indicates incipient congestion. Initial value: 0.0
  gdouble         owd_trend;
//  gint32          delta_cwnd;

  //application
  GstClockTime    time;
  State           state;
  gboolean        undershooted;

};



//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void subratectrler_finalize (GObject * object);


//static const gdouble ST_ = 1.1; //Stable treshold
static const gdouble OT_ = 2.;  //Overused treshold
static const gdouble DT_ = 1.5; //Down Treshold


//--------------------MEASUREMENTS-----------------------

#define _actual_rate(this) (MAX(_mt0(this)->sender_rate, _mt0(this)->receiver_rate))
#define _ramp_up_speed(this) (MIN(RAMP_UP_SPEED, this->target_bitrate))
#define _now(this) (gst_clock_get_time(this->sysclock))
#define _get_moment(this, n) ((Moment*)(this->moments + n * sizeof(Moment)))
#define _pmtn(index) (index == MOMENTS_LENGTH ? MOMENTS_LENGTH - 1 : index == 0 ? MOMENTS_LENGTH - 1 : index - 1)
#define _mt0(this) _get_moment(this, this->moments_index)
#define _mt1(this) _get_moment(this, _pmtn(this->moments_index))
#define _mt2(this) _get_moment(this, _pmtn(_pmtn(this->moments_index)))

static Moment*
_m_step(
    SubflowRateController *this);

static void
_update_bitrate(
    SubflowRateController *this);

static void
_cwnd_overused_state(
    SubflowRateController *this);

static void
_cwnd_stable_state(
    SubflowRateController *this);

static void
_cwnd_monitored_state(
    SubflowRateController *this);

//static void
//_cwnd_monitored_state(
//    SubflowRateController *this);

static void
_undershoot(
    SubflowRateController *this);

static void
_transit_to(
    SubflowRateController *this,
    State target);

static void
_validate_and_set_cwnd(
    SubflowRateController *this);

static void
_reduce_bitrate(
    SubflowRateController *this);

static void
_reduce_cwnd(
    SubflowRateController *this);

static gdouble
_consume_extra_bitrate(
    SubflowRateController *this);

static gdouble
_raise_up_bitrate(
    SubflowRateController *this);

static void
_raise_up_cwnd(
    SubflowRateController *this);

static void
_adjust_cwnd(
    SubflowRateController *this);

static void
_gain_up_cwnd(
    SubflowRateController *this);

static void
_gain_down_cwnd(
    SubflowRateController *this);

static gdouble
_adjust_bitrate(
    SubflowRateController *this);

#define _disable_monitoring(this) _set_monitoring_interval(this, 0)

static gboolean
_monitoring_is_allowed(
    SubflowRateController *this);

static void
_set_monitoring_interval(
    SubflowRateController *this,
    guint interval);


//static guint
//_calculate_monitoring_interval(
//    SubflowRateController *this,
//    guint32 desired_bitrate);


static void
_set_owd_trend(
    SubflowRateController *this);

static gdouble
_get_cwnd_scl_i(
    SubflowRateController *this);

static gdouble
_get_bitrate_scl_i(
    SubflowRateController *this);

static gdouble
_get_off_target(
    SubflowRateController *this);

static gboolean
_cwnd_can_increase(SubflowRateController *this);

static void
_adjust_bitrate_inflection(SubflowRateController *this);

static void
_ltt_delays_th_stats_pipe(gpointer data, PercentileTrackerPipeData *pdata);

static void
_change_target_bitrate(SubflowRateController *this, gint32 delta);

static void
_ltt_delays_target_stats_pipe(gpointer data, PercentileTrackerPipeData *pdata);

static void
_receiver_rate_variance_pipe(gpointer data, gdouble value);

static void
_target_rate_i_max_pipe(gpointer data, guint64 value);

static void
_owd_trend_max_pipe(gpointer data, guint64 value);

static void
_bights_in_flight_max_pipe(gpointer data, guint64 value);

static void
_print_overused_state(SubflowRateController *this);

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

  this->owd_fraction_hist = make_floatnumstracker(20, 60 * GST_SECOND);
  this->bytes_in_flight_history = make_numstracker(16, 10 * GST_SECOND);
  numstracker_add_plugin(this->bytes_in_flight_history,
                         (NumsTrackerPlugin*) make_numstracker_minmax_plugin(_bights_in_flight_max_pipe, this, NULL, NULL));
  this->receiver_rate_history = make_numstracker(16, 5 * GST_SECOND);
  numstracker_add_plugin(this->receiver_rate_history,
                         (NumsTrackerPlugin*) make_numstracker_variance_plugin(_receiver_rate_variance_pipe, this));
  this->target_bitrate_i_history = make_numstracker(100, 30 * GST_SECOND);
  numstracker_add_plugin(this->target_bitrate_i_history,
                         (NumsTrackerPlugin*) make_numstracker_minmax_plugin(_target_rate_i_max_pipe, this, NULL, NULL));

  this->owd_trend_history = make_numstracker(16, 10 * GST_SECOND);
  numstracker_add_plugin(this->owd_trend_history,
                         (NumsTrackerPlugin*) make_numstracker_minmax_plugin(_owd_trend_max_pipe, this, NULL, NULL));
}


SubflowRateController *make_subratectrler(void)
{
  SubflowRateController *result;
  result = g_object_new (SUBRATECTRLER_TYPE, NULL);
  return result;
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

//  this->owd_target = OWD_TARGET_LO;
  this->owd_fraction_avg = 0.;
  //Vector of the last 20 owd_fraction
  this->mss = 1400;
  this->cwnd_min = 3 * this->mss;
  this->in_fast_start = FALSE;
  this->n_fast_start = 1;
  //COngestion window
  this->cwnd_i = 1;
  this->cwnd = INIT_CWND;
//  this->bytes_newly_acked = 0;
  //A vector of the  last 20 RTP packet queue delay samples.Array
  this->target_bitrate = sending_target * 8;
  this->target_bitrate_i = 1;
  this->s_rtt = 0.;
  this->min_rate = TARGET_BITRATE_MIN;
  this->max_rate = TARGET_BITRATE_MAX;

  _transit_to(this, STATE_STABLE);
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
                                  guint64 *median_delay,
                                  gint32  *sender_rate,
                                  gdouble *target_rate,
                                  gdouble *goodput,
                                  gdouble *next_target)
{
  if(median_delay)
    *median_delay = percentiletracker_get_stats(this->ltt_delays_th, NULL, NULL, NULL);
  if(sender_rate)
    *sender_rate = _mt0(this)->sender_rate;
  if(target_rate)
    *target_rate = this->target_bitrate / 8;
  if(goodput)
    *goodput = _mt0(this)->goodput;
  if(next_target)
    *next_target = 0;
}

void subratectrler_time_update(
                         SubflowRateController *this,
                         gint32 *target_bitrate,
                         gint32 *extra_bitrate,
                         UtilizationSubflowReport *rep)
{


  _mt0(this)->sender_rate         = mprtps_path_get_sender_rate(this->path) * 8;
  _mt0(this)->bytes_in_queue      = mprtps_path_get_bytes_in_queue(this->path);

  if(this->moments_num == 0){
      this->bytes_in_queue_avg = _mt0(this)->bytes_in_queue;
  }else{
      this->bytes_in_queue_avg *= .5;
      this->bytes_in_queue_avg += _mt0(this)->bytes_in_queue * .5;
  }
  _update_bitrate(this);
  if(rep){
    rep->lost_bytes = _mt0(this)->lost;
    rep->discarded_bytes = _mt0(this)->discard;
    rep->owd = _mt0(this)->delay;
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
//  g_print_rrmeasurement(measurement);
  if(measurement->goodput <= 0.) goto done;
  _m_step(this);

  _mt0(this)->delay               = measurement->median_delay;
  _mt0(this)->RTT                 = measurement->RTT;
  _mt0(this)->discard             = measurement->late_discarded_bytes;
  _mt0(this)->lost                = measurement->lost;
  _mt0(this)->goodput             = measurement->goodput;
  _mt0(this)->receiver_rate       = measurement->receiver_rate * 8;
  _mt0(this)->jitter              = measurement->jitter;
  _mt0(this)->bytes_newly_acked   = measurement->expected_payload_bytes;
  _mt0(this)->bytes_in_flight     = measurement->bytes_in_flight;

  _mt0(this)->sender_rate         = _mt1(this)->sender_rate;
  _mt0(this)->bytes_in_queue      = _mt1(this)->bytes_in_queue;
  _mt0(this)->max_bytes_in_flight = _mt1(this)->max_bytes_in_flight;
  _mt0(this)->ltt_delays_th       = _mt1(this)->ltt_delays_th;
  _mt0(this)->ltt_delays_target   = _mt1(this)->ltt_delays_target;
  _mt0(this)->state               = _mt1(this)->state;

  _mt0(this)->discard_rate        = 1. - measurement->goodput / measurement->receiver_rate;

  numstracker_add(this->receiver_rate_history, _mt0(this)->receiver_rate);

  if(1 < this->moments_num)
    this->s_rtt = measurement->RTT * .125 + this->s_rtt * .875;
  else
    this->s_rtt = measurement->RTT;

  if(numstracker_get_num(this->bytes_in_flight_history) < 1){
      _mt0(this)->max_bytes_in_flight = measurement->bytes_in_flight;
  }

  if(!_mt0(this)->ltt_delays_th){
      _mt0(this)->ltt_delays_th = OWD_TARGET_HI;
    }

  if(!_mt0(this)->ltt_delays_target){
      _mt0(this)->ltt_delays_target = OWD_TARGET_LO;
    }

  _mt0(this)->owd_fraction        = _mt0(this)->delay/_mt0(this)->ltt_delays_target;
  this->owd_fraction_avg = .9* this->owd_fraction_avg + .1* _mt0(this)->owd_fraction;

  floatnumstracker_add(this->owd_fraction_hist, _mt0(this)->owd_fraction);
  _set_owd_trend(this);

  this->owd_trend_mem = MAX(this->owd_trend_mem*0.99, _mt0(this)->owd_trend);

  _mt0(this)->corrh_owd = (gdouble)_mt0(this)->delay / (gdouble)_mt0(this)->ltt_delays_th;

  _mt0(this)->can_bitrate_increase = _mt0(this)->state != STATE_OVERUSED;
  _mt0(this)->can_bitrate_increase &= this->target_bitrate < _mt0(this)->receiver_rate * 1.1;

  _mt0(this)->off_target = _get_off_target(this);

  g_print("TB: %u, TBi: %u, BiF: %u cwnd: %d "
      "d: %lu td: %lu thd: %lu trend: %f, can_b_up: %d, RR: %u, RRD: %u"
      "off: %f\n",
          this->target_bitrate,
          this->target_bitrate_i,
          _mt0(this)->bytes_in_flight,
          this->cwnd,
          _mt0(this)->delay,
          _mt0(this)->ltt_delays_target,
          _mt0(this)->ltt_delays_th,
          _mt0(this)->owd_trend,
          _mt0(this)->can_bitrate_increase,
          _mt0(this)->receiver_rate,
          _mt0(this)->receiver_rate_std,
          _mt0(this)->off_target);

  this->cwnd_controller(this);

  if(_mt0(this)->state == STATE_STABLE){
    percentiletracker_add(this->ltt_delays_th, measurement->median_delay);
    percentiletracker_add(this->ltt_delays_target, measurement->median_delay);
    numstracker_add(this->bytes_in_flight_history, measurement->bytes_in_flight);
    this->packet_obsolation_treshold = OWD_TARGET_HI - _mt0(this)->ltt_delays_target;
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
_update_bitrate(SubflowRateController *this)
{
  gdouble queue_ratio;
  if (_now(this) - RATE_ADJUST_INTERVAL < this->last_target_bitrate_adjust) {
    goto exit;
  }

  queue_ratio = (gdouble)(this->bytes_in_queue_avg * 8) / (gdouble) this->target_bitrate;
  if(.5 < queue_ratio && this->last_queue_clear < _now(this) - 5 * GST_SECOND ){
    mprtps_path_clear_queue(this->path);
    this->last_queue_clear = _now(this);
    goto done;
  }

  if(this->in_fast_start){
    gboolean allowed = this->moments_num < 3;
    allowed |= _mt0(this)->can_bitrate_increase || _mt0(this)->state == STATE_OVERUSED;
    allowed |= _mt1(this)->can_bitrate_increase || _mt1(this)->state == STATE_OVERUSED;
    allowed |= _mt2(this)->can_bitrate_increase || _mt2(this)->state == STATE_OVERUSED;
    if(allowed){
      _change_target_bitrate(this, _raise_up_bitrate(this));
      goto done;
    }else{
      this->in_fast_start = FALSE;
      this->rise_up_max = 0;
    }
  }
  if(_mt0(this)->undershooted){
    goto done;
  }
  _change_target_bitrate(this, _adjust_bitrate(this));
done:
  this->last_target_bitrate_adjust = _now(this);
exit:
  return;
}

void
_cwnd_overused_state(
    SubflowRateController *this)
{
  gdouble owd_th = .5;
  gdouble disc_th = .25;
  guint64 min_wait = MAX(3 * _mt0(this)->RTT, 2 * GST_SECOND);

  _print_overused_state(this);
  if(_now(this) - min_wait < this->last_congestion_detected){
    goto done;
  }
  if(_mt0(this)->corrh_owd > OT_){
    _undershoot(this);
    goto done;
  }

  if(owd_th < _mt0(this)->owd_trend || disc_th < _mt0(this)->discard_rate){
    goto done;
  }
  mprtps_path_set_pacing(this->path, FALSE);
  if(_mt0(this)->bytes_in_queue){
    goto done;
  }
  if(this->target_bitrate * 1.1 < _mt0(this)->receiver_rate){
    goto done;
  }
  this->in_fast_start = TRUE;
  ++this->n_fast_start;
  _transit_to(this, STATE_STABLE);
done:
  return;
}


void
_cwnd_stable_state(
    SubflowRateController *this)
{
  //explicit congestion check
  if(_mt0(this)->discard_rate > 0.25 || _mt0(this)->corrh_owd > DT_){
    _undershoot(this);
    _transit_to(this, STATE_OVERUSED);
    goto done;
  }
  _mt0(this)->can_cwnd_increase = _cwnd_can_increase(this);

  if(this->in_fast_start){
    //In fast start we ramp up the rate directly.
    _raise_up_cwnd(this);
    goto done;
  }

  _adjust_cwnd(this);

  //allow monitoring if state is stable enough and max rate was not reached.
  if(_monitoring_is_allowed(this)){
    _set_monitoring_interval(this, 4);
    _transit_to(this, STATE_MONITORED);
    goto done;
  }
  //Monitoring

done:
  return;
}

void
_cwnd_monitored_state(SubflowRateController *this)
{
  guint64 min_wait = MAX(3 * _mt0(this)->RTT, 2 * GST_SECOND);
  g_print("STATE MONITORED\n");
  if(_mt0(this)->discard_rate > 0.25 || _mt0(this)->corrh_owd > DT_){
    _undershoot(this);
    _transit_to(this, STATE_OVERUSED);
    goto done;
  }

  _adjust_cwnd(this);

  if(.1 < _mt0(this)->owd_trend || _mt0(this)->off_target < -.1){
    this->acked_monitored_bitrate = 0;
    _disable_monitoring(this);
    _transit_to(this, STATE_STABLE);
    goto done;
  }

  if( _now(this) - min_wait < this->monitoring_started){
    goto done;
  }
  numstracker_add(this->target_bitrate_i_history,
                  _mt0(this)->receiver_rate +
                  this->monitored_bitrate * .95);
  this->monitored_bitrate = 0;
  this->rise_up_max = this->target_bitrate_i;
  this->in_fast_start = TRUE;
  ++this->n_fast_start;
  _transit_to(this, STATE_STABLE);
done:
  return;
}

void _undershoot(SubflowRateController *this)
{
  if(0 < this->monitored_bitrate || 0 < this->acked_monitored_bitrate){
    _disable_monitoring(this);
    _adjust_bitrate_inflection(this);
  }else{
    _reduce_cwnd(this);
    _reduce_bitrate(this);
  }
  mprtps_path_set_pacing(this->path, TRUE);
  _mt0(this)->undershooted = TRUE;
  this->acked_monitored_bitrate = 0;
  this->in_fast_start = FALSE;
}

void
_transit_to(
    SubflowRateController *this,
    State target)
{

  switch(target){
    case STATE_OVERUSED:
      mprtps_path_set_congested(this->path);
      this->cwnd_controller = _cwnd_overused_state;
    break;
    case STATE_STABLE:
      mprtps_path_set_non_congested(this->path);
      this->cwnd_controller = _cwnd_stable_state;
    break;
    case STATE_MONITORED:
      this->cwnd_controller = _cwnd_monitored_state;
    break;
  }
  _mt0(this)->state = target;
}


void _validate_and_set_cwnd(SubflowRateController *this)
{
  guint32 pacing_bitrate;
  /*
  * Congestion window validation, checks that the congestion window is
  * not considerably higher than the actual number of bytes in flight
  */
  if (_mt0(this)->max_bytes_in_flight > INIT_CWND) {
     this->cwnd = MIN(this->cwnd, _mt0(this)->max_bytes_in_flight);
  }

  this->cwnd = MAX(this->cwnd_min, this->cwnd);
  pacing_bitrate = this->cwnd * 8 * (gdouble) GST_SECOND / (gdouble) _mt0(this)->ltt_delays_target;
  mprtps_path_set_pacing_bitrate(this->path,
                                 pacing_bitrate,
                                 this->packet_obsolation_treshold);
//  mprtps_path_setup_cwnd(this->path, 0, 0, 0);
  /*
  * Make possible to enter fast start if OWD has been low for a while
  */
  //deleted here, 1393th line available at https://github.com/EricssonResearch/openwebrtc-gst-plugins/blob/master/gst/scream/gstscreamcontroller.c
}

void _reduce_bitrate(SubflowRateController *this)
{
  if(this->last_target_bitrate_i_adjust < _now(this) - 5 * GST_SECOND)
  {
      numstracker_add(this->target_bitrate_i_history, _mt0(this)->receiver_rate);
      this->last_target_bitrate_i_adjust = _now(this);
  }
  _change_target_bitrate(this, -1 * this->target_bitrate * (1. - BETA_R));
  return;
}

void _reduce_cwnd(SubflowRateController *this)
{
  //check if monitored bytes...
  this->cwnd_i = this->cwnd;
  this->cwnd = MAX(this->cwnd_min, (guint) (BETA * this->cwnd));
  this->last_congestion_detected = _now(this);
  this->in_fast_start = FALSE;
  this->cwnd_was_increased = FALSE;
  _disable_monitoring(this);
  _validate_and_set_cwnd(this);
}

gdouble _consume_extra_bitrate(SubflowRateController *this)
{
  gdouble increment;
  gdouble ramp_up_speed = _ramp_up_speed(this) * (gdouble) RATE_ADJUST_INTERVAL / (gdouble) GST_SECOND;
//  gdouble ramp_up_speed = _ramp_up_speed(this);
  if(this->acked_monitored_bitrate < ramp_up_speed){
    increment = this->acked_monitored_bitrate;
  }else{
    increment = ramp_up_speed;
  }
  this->acked_monitored_bitrate-=increment;
  return increment;
}

gdouble _raise_up_bitrate(SubflowRateController *this)
{
  gdouble increment = 0., scl_i, ramp_up_speed, tmp = 1.;
  ramp_up_speed = _ramp_up_speed(this);
  scl_i = _get_bitrate_scl_i(this);

  if(0) _consume_extra_bitrate(this);

  /*
   * Compute rate increment
  */
  g_print("raise up ");
  increment = ramp_up_speed * ((gdouble)RATE_ADJUST_INTERVAL/(gdouble)GST_SECOND) *
      (1.0f - MIN(1.0f, _mt0(this)->owd_trend /0.2f * tmp));
  /*
   * Limit increase rate near the last known highest bitrate
   * if it is not a direct ramp up
   */
  if(!this->rise_up_max){
    increment *= scl_i;
  }else if(this->rise_up_max * 1.1 < this->target_bitrate){
    increment = 0;
  }
  /*
   * Add increment
   */
  if(!_mt0(this)->can_bitrate_increase)
    increment = 0.;

  /*
   * Put an extra cap in case the OWD starts to increase
   */
  increment-=this->target_bitrate * PRE_CONGESTION_GUARD * _mt0(this)->owd_trend * tmp;

  if(0 < this->rise_up_max &&
     this->rise_up_max < _mt0(this)->receiver_rate * .95){
    this->in_fast_start = FALSE;
    increment = 0;
  }
  this->bitrate_was_incrased = TRUE;
  return increment;
}

void _raise_up_cwnd(SubflowRateController *this)
{
  gdouble th, scl_i;

  th = this->n_fast_start > 1 ? .2 : .2;
  scl_i = _get_cwnd_scl_i(this);

  if (_mt0(this)->owd_trend < th && -.1 < _mt0(this)->off_target) {
     if (_mt0(this)->can_cwnd_increase)
       this->cwnd += MIN(10 * this->mss, (guint)(_mt0(this)->bytes_newly_acked * scl_i));
  } else{
      this->in_fast_start = FALSE;
      this->cwnd_was_increased = TRUE;
      this->last_congestion_detected = _now(this);
      this->cwnd_i = this->cwnd;
  }

  _validate_and_set_cwnd(this);
}

void _adjust_cwnd(SubflowRateController *this)
{
  if(_mt0(this)->off_target < 0.){
    _gain_down_cwnd(this);
  }else{
    _gain_up_cwnd(this);
  }
}

void _gain_up_cwnd(SubflowRateController *this)
{
  gfloat gain;
  gdouble scl_i;

  this->cwnd_was_increased = TRUE;
  if (!_mt0(this)->can_cwnd_increase) goto done;

  scl_i = _get_cwnd_scl_i(this);
  /*
   * Limit growth if OWD shows an increasing trend
   */
  gain = GAIN_UP*(1.0f + MAX(0.0f, 1.0f - _mt0(this)->owd_trend / 0.2f));
  gain *= scl_i;

  this->cwnd += (gint)(gain * _mt0(this)->off_target * _mt0(this)->bytes_newly_acked * this->mss / this->cwnd + 0.5f);
  done:
    _validate_and_set_cwnd(this);
}

void _gain_down_cwnd(SubflowRateController *this)
{
  /*
   * OWD above target
   */
  if (this->cwnd_was_increased) {
      this->cwnd_was_increased = FALSE;
      this->cwnd_i = this->cwnd;
  }

  this->cwnd += (gint)(GAIN_DOWN * _mt0(this)->off_target * _mt0(this)->bytes_newly_acked * this->mss / this->cwnd);
  this->last_congestion_detected = _now(this);
  _validate_and_set_cwnd(this);
}



gdouble _adjust_bitrate(SubflowRateController *this)
{
  gdouble increment = 0., scl, scl_i, ramp_up_speed, tmp = .5;

  ramp_up_speed = _ramp_up_speed(this);
  _adjust_bitrate_inflection(this);
  scl = MIN(1.0f, MAX(0.0f, this->owd_fraction_avg - 0.3f) / 0.7f);
  scl += _mt0(this)->owd_trend;
//  scl = MIN(1.0, (this->owd_fraction_avg - 0.3f) / 0.7f + _mt0(this)->owd_trend);
  g_print("adjusted ");
  increment = _actual_rate(this)*(1.0f - PRE_CONGESTION_GUARD * scl * tmp)-
      TX_QUEUE_SIZE_FACTOR * (gdouble)(this->bytes_in_queue_avg * 8) * tmp +
      mprtps_path_get_bytes_in_flight(this->path) * tmp -
      this->target_bitrate * (1. - _mt0(this)->off_target);
  if (increment < 0) {
    _adjust_bitrate_inflection(this);
    increment *= MIN(1.0f,this->owd_fraction_avg);
  }

  this->bitrate_was_incrased = TRUE;
  scl_i = _get_bitrate_scl_i(this);
  increment *= scl_i;
  increment = MAX(-.95 * this->target_bitrate, increment);
  increment = MIN(increment,(gfloat)(ramp_up_speed*((gdouble)RATE_ADJUST_INTERVAL/(gdouble)GST_SECOND)));

  return increment;
}


gboolean _monitoring_is_allowed(SubflowRateController *this)
{
  if(_now(this) - GST_SECOND < this->last_congestion_detected){
    return FALSE;
  }
  if(.05 < _mt0(this)->owd_trend){
    return FALSE;
  }
  return TRUE;
}


void _set_monitoring_interval(SubflowRateController *this, guint interval)
{
  this->monitoring_started = _now(this);
  this->monitoring_interval = interval;
  if(interval > 0)
    this->monitored_bitrate = (gdouble)_actual_rate(this) / (gdouble)interval;
  else
    this->monitored_bitrate = 0;
  mprtps_path_set_monitor_interval(this->path, interval);
  return;
}
//
//guint _calculate_monitoring_interval(SubflowRateController *this, guint32 desired_bitrate)
//{
//  gdouble actual, target, rate;
//  guint monitoring_interval = 0;
//  if(desired_bitrate <= 0){
//     goto exit;
//   }
//  actual = _actual_rate(this);
//  target = actual + (gdouble) desired_bitrate;
//  rate = target / actual;
//
//  if(rate > 2.) monitoring_interval = 2;
//  else if(rate > 1.5) monitoring_interval = 3;
//  else if(rate > 1.25) monitoring_interval = 4;
//  else if(rate > 1.2) monitoring_interval = 5;
//  else if(rate > 1.16) monitoring_interval = 6;
//  else if(rate > 1.14) monitoring_interval = 7;
//  else if(rate > 1.12) monitoring_interval = 8;
//  else if(rate > 1.11) monitoring_interval = 9;
//  else if(rate > 1.10) monitoring_interval = 10;
//  else if(rate > 1.09) monitoring_interval = 11;
//  else if(rate > 1.08) monitoring_interval = 12;
//  else if(rate > 1.07) monitoring_interval = 13;
//  else if(rate > 1.04) monitoring_interval = 14;
//  else monitoring_interval = 0;
//
//exit:
//  return monitoring_interval;
//}


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
  numstracker_add(this->owd_trend_history, trend * 1000.);
  _mt0(this)->owd_trend = trend;
done:
  return;
}

gdouble _get_cwnd_scl_i(SubflowRateController *this)
{
  gdouble scl_i;
  scl_i = (this->cwnd - this->cwnd_i) / ((gfloat)(this->cwnd_i));
  scl_i *= 4.0f;
  scl_i = MAX(0.1f, MIN(1.0f, scl_i * scl_i));
  return scl_i;
}

gdouble _get_bitrate_scl_i(SubflowRateController *this)
{
  gdouble scl_i;
  scl_i = (this->target_bitrate - this->target_bitrate_i) / ((gfloat)(this->target_bitrate_i));
  scl_i *= 4.0f;
  scl_i = MAX(0.1f, MIN(1.0f, scl_i * scl_i));
  return scl_i;
}

gdouble _get_off_target(SubflowRateController *this)
{
  gdouble off_target;
  off_target = (gdouble)((gdouble)_mt0(this)->ltt_delays_target - (gdouble)_mt0(this)->delay) / (gfloat)_mt0(this)->ltt_delays_target;
  return off_target;
}

gboolean _cwnd_can_increase(SubflowRateController *this)
{
  gfloat alpha = 1.25f+2.75f*(1.0f-this->owd_trend_mem);
  return this->cwnd <= alpha*_mt0(this)->bytes_in_flight;

}

void _adjust_bitrate_inflection(SubflowRateController *this)
{
  if(!this->bitrate_was_incrased) return;
  this->bitrate_was_incrased = FALSE;
  if (this->last_target_bitrate_i_adjust < _now(this) - 5 * GST_SECOND) {
      numstracker_add(this->target_bitrate_i_history, _mt0(this)->receiver_rate);
      this->last_target_bitrate_i_adjust = _now(this);
  }

}

void _change_target_bitrate(SubflowRateController *this, gint32 delta)
{
  gint32 new_target = this->target_bitrate;
  new_target+=delta;
  if(0 < this->max_rate){
    new_target = MIN(new_target, this->max_rate);
  }
  if(0 < this->min_rate){
    new_target = MAX(new_target, this->min_rate);
  }
  g_print("Target bitrate changed from %d to: %d, monitoring bitrate: %d extra bitrate: %d\n",
          this->target_bitrate, new_target, this->monitored_bitrate, this->acked_monitored_bitrate);
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

void _receiver_rate_variance_pipe(gpointer data, gdouble value)
{
  SubflowRateController *this = data;
  _mt0(this)->receiver_rate_std = sqrt(value);
//  g_print("receiver_rate_variance: %u-%u-%f\n", this->target_bitrate, _mt0(this)->receiver_rate, sqrt(value));
}

void _target_rate_i_max_pipe(gpointer data, guint64 value)
{
  SubflowRateController *this = data;
  this->target_bitrate_i = value;
}

void _owd_trend_max_pipe(gpointer data, guint64 value)
{
  SubflowRateController *this = data;
  this->owd_trend_mem = (gdouble)value / 990.;
}


void _bights_in_flight_max_pipe(gpointer data, guint64 value)
{
  SubflowRateController *this = data;
  _mt0(this)->max_bytes_in_flight = value;
}

void _print_overused_state(SubflowRateController *this)
{
  goto done;
  g_print (
        "#################### S%d OVERUSED STATE ##########################\n"
      "corrh_owd: %f | owd_trend: %f | bytes_in_flight: %u |\n"
      "bytes_in_queue: %u\n"
        "#################################################################\n",
        this->id,
        _mt0(this)->corrh_owd,
        _mt0(this)->owd_trend,
        _mt0(this)->bytes_in_flight,
        _mt0(this)->bytes_in_queue);
done:
  return;
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
