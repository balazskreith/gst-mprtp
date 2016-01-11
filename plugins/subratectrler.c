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
#define BETA_R 0.9
//Additional slack [%]  to the congestion window. Default value: 10%
#define BYTES_IN_FLIGHT_SLACK .1
//Interval between video bitrate adjustments. Default value: 0.1s ->100ms
#define RATE_ADJUST_INTERVAL 100
//Video coder frame period [s]
#define FRAME_PERIOD 0
//Min target_bitrate [bps]
#define TARGET_BITRATE_MIN 30000
//Max target_bitrate [bps]
#define TARGET_BITRATE_MAX 0
//Timespan [s] from lowest to highest bitrate. Default value: 10s->10000ms
#define RAMP_UP_TIME 10000
//Guard factor against early congestion onset.
//A higher value gives less jitter possibly at the
//expense of a lower video bitrate. Default value: 0.0..0.2
#define PRE_CONGESTION_GUARD 0.
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
  guint32         max_bytes_in_flight;
  gboolean        ECN;
  gboolean        ECN_acked;

  //derivatives
  gdouble         corrd;
  gdouble         discard_rate;
  gdouble         corrh_owd;
  gdouble         goodput_ratio;
  gdouble         delay_ratio;
  gdouble         corr_rate;
  gdouble         corr_rate_dev;
  gdouble         owd_fraction;
  gboolean        can_increase;
  gboolean        increased;
  gdouble         off_target;
  //OWD trend indicates incipient congestion. Initial value: 0.0
  gdouble         owd_trend;
  gint32          delta_cwnd;

  //application
  GstClockTime    time;
  gint32          target_rate;
  gint32          delta;
  State           state;

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
#define _now(this) (gst_clock_get_time(this->sysclock))
#define _get_moment(this, n) ((Moment*)(this->moments + n * sizeof(Moment)))
#define _pmtn(index) (index == MOMENTS_LENGTH ? MOMENTS_LENGTH - 1 : index - 1)
#define _mt0(this) _get_moment(this, this->moments_index)
#define _mt1(this) _get_moment(this, _pmtn(this->moments_index))

static Moment*
_m_step(SubflowRateController *this);

//----------------------STATES-------------------

static void
_cwnd_overused_state(
    SubflowRateController *this);

static void
_cwnd_stable_state(
    SubflowRateController *this);

static void
_check_monitored(
    SubflowRateController *this);

//-----------------------ACTIONS------------------
static void
_transit_to(
    SubflowRateController *this,
    State target);

#define _disable_monitoring(this) _setup_monitoring(this, 0)

static void
_setup_monitoring(
    SubflowRateController *this,
    guint interval);


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
  this->ltt_delays_target = make_percentiletracker(100, 50);
  percentiletracker_set_treshold(this->ltt_delays_target, GST_SECOND * 60);
  this->owd_fraction_hist = make_floatnumstracker(20, 60 * GST_SECOND);
  this->bytes_in_flight_history = make_numstracker_with_tree(100, GST_SECOND);

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

  this->owd_target = OWD_TARGET_LO;
  this->owd_fraction_avg = 0.;
  //Vector of the last 20 owd_fraction
  this->owd_fraction_hist;
  this->mss = 1400;
  this->cwnd_min = 3 * this->mss;
  this->in_fast_start = TRUE;
  //COngestion window
  this->cwnd_i = 1;
  this->cwnd = INIT_CWND;
//  this->bytes_newly_acked = 0;
  //A vector of the  last 20 RTP packet queue delay samples.Array
  this->target_bitrate = sending_target * 8;
  this->target_bitrate_i = 1;
  this->s_rtt = 0.;

  _transit_to(this, STATE_STABLE);
  THIS_WRITEUNLOCK(this);
}

void subratectrler_unset(SubflowRateController *this)
{
  THIS_WRITELOCK(this);
  this->path = g_object_unref(this->path);
  THIS_WRITEUNLOCK(this);
}

void subratectrler_time_update(
                         SubflowRateController *this,
                         gint32 *delta_bitrate)
{

  _mt0(this)->bytes_in_flight     = mprtps_path_get_bytes_in_flight(this->path);
  numstracker_add(this->bytes_in_flight_history, _mt0(this)->bytes_in_flight);
  numstracker_get_stats(this->bytes_in_flight_history, NULL, &_mt0(this)->max_bytes_in_flight, NULL);

  _mt0(this)->sender_rate         = mprtps_path_get_sender_rate(this->path);
  _mt0(this)->bytes_in_queue      = mprtps_path_get_bytes_in_queue(this->path);

  if(this->moments_num == 0){
      this->bytes_in_flight_avg = _mt0(this)->bytes_in_flight;
  }else{
    this->bytes_in_flight_avg *= .5;
    this->bytes_in_flight_avg += _mt0(this)->bytes_in_flight * .5;
  }




done:
  return;
}

void subratectrler_measurement_update(
                         SubflowRateController *this,
                         RRMeasurement * measurement)
{
  _m_step(this);
  _mt0(this)->state = _mt1(this)->state;

  _mt0(this)->delay               = measurement->median_delay;
  _mt0(this)->discard             = measurement->late_discarded_bytes;
  _mt0(this)->lost                = measurement->lost;
  _mt0(this)->goodput             = measurement->goodput;
  _mt0(this)->receiver_rate       = measurement->receiver_rate;
  _mt0(this)->jitter              = measurement->jitter;
  _mt0(this)->bytes_newly_acked   = measurement->expected_payload_bytes;

  _mt0(this)->sender_rate         = _mt1(this)->sender_rate;
  _mt0(this)->bytes_in_flight     = _mt1(this)->bytes_in_flight;
  _mt0(this)->bytes_in_queue      = _mt1(this)->bytes_in_queue;
  _mt0(this)->max_bytes_in_flight = _mt1(this)->max_bytes_in_flight;

  _mt0(this)->owd_fraction        = _mt0(this)->delay/this->owd_target;
  this->owd_fraction_avg = .9* this->owd_fraction_avg + .1* _mt0(this)->owd_fraction;

  floatnumstracker_add(this->owd_fraction_hist, _mt0(this)->owd_fraction);
  _set_owd_trend(this);

  this->owd_trend_mem = MAX(this->owd_trend_mem*0.99, _mt0(this)->owd_trend);

  if(0 < this->moments_num)
    this->s_rtt = measurement->RTT * .125 + this->s_rtt * .875;
  else
    this->s_rtt = measurement->RTT;

  if(!percentiletracker_get_num(this->ltt_delays_th)){
    _mt0(this)->ltt_delays_th = measurement->max_delay;
    _mt0(this)->ltt_delays_target = measurement->median_delay;
  }else{
    _mt0(this)->ltt_delays_th = percentiletracker_get_stats(this->ltt_delays_th, NULL, NULL, NULL);
    _mt0(this)->ltt_delays_target = percentiletracker_get_stats(this->ltt_delays_target, NULL, NULL, NULL);
  }

  _mt0(this)->corrh_owd = _mt0(this)->delay / _mt0(this)->ltt_delays_target;
  //Weather the subflow is overused or not.
  this->cwnd_controller(this);

  if(_mt0(this)->state == STATE_STABLE){
    percentiletracker_add(this->ltt_delays_th, measurement->median_delay);
    percentiletracker_add(this->ltt_delays_target, measurement->median_delay);
  }

}


Moment* _m_step(SubflowRateController *this)
{
  if(++this->moments_index == MOMENTS_LENGTH){
    this->moments_index = 0;
  }
  memset(_mt0(this), 0, sizeof(Moment));
  return _mt0(this);
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
  rdata.a0 = rdata.a1 = rdata.x1 = 0.;
  floatnumstracker_get_stats(this->owd_fraction_hist, NULL, &rdata.avg);
  floatnumstracker_iterate(this->owd_fraction_hist, _iterator_process, &rdata);
  if(rdata <= 0.) goto done;

  _mt0(this)->owd_trend = MAX(0.0f, MIN(1.0f, (rdata.a1 / rdata.a0)*this->owd_fraction_avg));

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
  off_target = (_mt0(this)->ltt_delays_target - _mt0(this)->delay) / (gfloat)_mt0(this)->ltt_delays_target;
  return off_target;
}

gboolean _can_increase(SubflowRateController *this)
{
  gfloat alpha = 1.25f+2.75f*(1.0f-this->owd_trend_mem);
  return this->cwnd <= alpha*_mt0(this)->bytes_in_flight;

}

void
_bitrate_overused_state(
    SubflowRateController *this)
{
  if(_mt0(this)->ECN && !_mt0(this)->ECN_acked){
    _reduce_bitrate(this);
    _mt0(this)->ECN_acked = TRUE;
    goto done;
  }

done:
  return;
}

void
_bitrate_stable_state(
    SubflowRateController *this)
{
  //if explicit congestion occured it is at overused state;

  gdouble queue_rate;
  guint32 target_bitrate_was;
  guint32 delta_bitrate;
  guint32 extra_bytes = 0;

  queue_rate = (gdouble)_mt0(this)->bytes_in_queue / (gdouble) this->target_bitrate;

  if(this->in_fast_start && queue_rate < .1){
    _raise_bitrate(this);
    goto done;
  }
  target_bitrate_was = this->target_bitrate;
  _adjust_bitrate(this);
  delta_bitrate = this->target_bitrate - target_bitrate_was;
  if(this->target_bitrate == target_bitrate_was) goto done;

  if(this->target_bitrate < target_bitrate_was){
    if(0 < this->monitored_bytes)
      extra_bytes = _reduce_monitoring(this, delta_bitrate / 8);
    this->target_bitrate += extra_bytes * 8;
    goto done;
  }

  if(this->in_fast_start) goto done;

  _calculate_monitoring_interval(this, this->monitored_bytes + delta_bitrate);

  if(0 < this->monitoring_interval){
    this->target_bitrate = target_bitrate_was;
    _transit_to(this, STATE_MONITORED);
  }
done:
  return;
}

void
_bitrate_monitored_state(
    SubflowRateController *this)
{

done:
  return;
}


void
_cwnd_overused_state(
    SubflowRateController *this)
{
  if(_mt0(this)->corrh_owd > OT_){
    _reduce_cwnd(this);
    goto done;
  }

  //wait....

  this->in_fast_start = TRUE;
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
    _reduce_cwnd(this);
    _transit_to(this, STATE_OVERUSED);
    goto done;
  }
  _mt0(this)->off_target = _get_off_target(this);
  _mt0(this)->can_increase = _can_increase(this);

  if(this->in_fast_start){
    //In fast start we ramp up the rate directly.
    _raise_cwnd(this);
    goto done;
  }
  if(_mt0(this)->off_target < 0.){
    _gain_down_cwnd(this);
  }else{
    _gain_up_cwnd(this);
  }

  //calculate off target and then rate controlling common with monitoring.
done:
  return;
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
      this->rate_controller = _bitrate_overused_state;
    break;
    case STATE_STABLE:
      mprtps_path_set_non_congested(this->path);
      this->cwnd_controller = _cwnd_stable_state;
      this->rate_controller = _bitrate_stable_state;
    break;
    case STATE_MONITORED:
      this->cwnd_controller = _cwnd_stable_state;
      this->rate_controller = _bitrate_monitored_state;
    break;
  }
  _mt0(this)->state = target;
}


void _use_monitored_bytes(SubflowRateController *this)
{

  if(0 < this->max_rate){
    if(this->max_rate < _actual_rate(this) * .9)
      goto done;
  }
  _mt0(this)->delta_cwnd = this->monitored_bytes;
  _validate_and_set_cwnd(this);
done:
  return;
}


void _validate_and_set_cwnd(SubflowRateController *this)
{
  /*
  * Congestion window validation, checks that the congestion window is
  * not considerably higher than the actual number of bytes in flight
  */
  this->cwnd += _mt0(this)->delta_cwnd;
  if (_mt0(this)->max_bytes_in_flight > INIT_CWND) {
     this->cwnd = MIN(this->cwnd, _mt0(this)->max_bytes_in_flight);
  }

  this->cwnd = MAX(this->cwnd_min, this->cwnd);
  mprtps_path_setup_cwnd(this->path,
                         this->cwnd,
                         _mt0(this)->off_target > 0,
                         GST_TIME_AS_MSECONDS(this->s_rtt));
  /*
  * Make possible to enter fast start if OWD has been low for a while
  */
  //deleted here, 1393th line available at https://github.com/EricssonResearch/openwebrtc-gst-plugins/blob/master/gst/scream/gstscreamcontroller.c
}

void _reduce_bitrate(SubflowRateController *this)
{
  if(_mt0(this)->ECN_acked){
    goto done;
  }
  _mt0(this)->ECN_acked = TRUE;
  if(this->last_target_bitrate_i_adjust + 5 * GST_SECOND < _now(this))
  {
      this->target_bitrate_i = _mt0(this)->receiver_rate;
      this->last_target_bitrate_i_adjust = _now(this);
  }
  this->target_bitrate = MAX(TARGET_BITRATE_MIN, this->target_bitrate * BETA_R);
  this->last_target_bitrate_adjust  = _now(this);
  goto done;
done:
  _validate_and_update_bitrate;
  return;
}

void _reduce_cwnd(SubflowRateController *this)
{
  _mt0(this)->ECN = TRUE;
  //check if monitored bytes...
  this->cwnd_i = this->cwnd;
  this->cwnd = MAX(this->cwnd_min, (guint) (BETA * this->cwnd));
  this->last_congestion_detected = _now(this);
  this->in_fast_start = FALSE;
  _mt0(this)->increased = FALSE;
  _disable_monitoring(this);
  _validate_and_set_cwnd(this);
}

void _raise_bitrate(SubflowRateController *this)
{
  gdouble increment = 0., scl_i, ramp_up_speed, tmp = 1.;

  ramp_up_speed = MIN(RAMP_UP_SPEED, this->target_bitrate);
  scl_i = _get_bitrate_scl_i(this);
  /*
   * Compute rate increment
  */
  increment = ramp_up_speed * (RATE_ADJUST_INTERVAL/1000000.0) *
      (1.0f - MIN(1.0f, _mt0(this)->owd_trend /0.2f * tmp));
  /*
   * Limit increase rate near the last known highest bitrate
   */
  increment *= scl_i;
  /*
   * Add increment
   */
  this->target_bitrate += increment;

  /*
   * Put an extra cap in case the OWD starts to increase
   */
  this->target_bitrate *= 1.0f - PRE_CONGESTION_GUARD * _mt0(this)->owd_trend * tmp;
  this->was_fast_start = TRUE;
}

void _raise_cwnd(SubflowRateController *this)
{
  gdouble th, scl_i;

  _mt0(this)->increased = TRUE;
  _mt0(this)->delta_cwnd = 0;

  th = this->n_fast_start > 1 ? .1 : .2;
  scl_i = _get_cwnd_scl_i(this);

  if (_mt0(this)->owd_trend < th) {
     if (_mt0(this)->can_increase)
       _mt0(this)->delta_cwnd = MIN(10 * this->mss, (guint)(_mt0(this)->bytes_newly_acked * scl_i));
  } else {
      this->in_fast_start = FALSE;
      this->last_congestion_detected = _now(this);
      this->cwnd_i = this->cwnd;
  }

  ++this->n_fast_start;

  _validate_and_set_cwnd(this);
}

void _adjust_bitrate(SubflowRateController *this)
{
  gdouble increment = 0., scl, scl_i, ramp_up_speed, tmp = 1.;

  ramp_up_speed = MIN(RAMP_UP_SPEED, this->target_bitrate);
  scl_i = _get_bitrate_scl_i(this);
  increment = 0.0f;
  if (this->was_fast_start) {
    this->was_fast_start = FALSE;
    if (this->last_target_bitrate_i_adjust < _now(this) - 5 * GST_SECOND) {
        this->target_bitrate_i = _mt0(this)->receiver_rate;
        this->last_target_bitrate_i_adjust = _now(this);
    }
  }

  scl = MIN(1.0f, MAX(0.0f, this->owd_fraction_avg - 0.3f) / 0.7f);
  scl += _mt0(this)->owd_trend;

  increment = _actual_rate(this)*(1.0f - PRE_CONGESTION_GUARD * scl * tmp)-
      TX_QUEUE_SIZE_FACTOR * _mt0(this)->bytes_in_queue * tmp -
      this->target_bitrate;


  if (increment < 0) {
      if (this->was_fast_start) {
          this->was_fast_start = FALSE;
          if (this->last_target_bitrate_i_adjust < _now(this) - 5 * GST_SECOND) {
              this->target_bitrate_i = _mt0(this)->receiver_rate;
              this->last_target_bitrate_i_adjust = _now(this);
          }
      }
      increment *= MIN(1.0f,this->owd_fraction_avg);
      goto done;
  }

  this->was_fast_start = TRUE;
  increment *= scl_i;
  increment = MIN(increment,(gfloat)(ramp_up_speed*(RATE_ADJUST_INTERVAL/1000000.0)));
  _calculate_monitoring_interval(this, increment / 8.);
  increment = 0;
done:
  this->target_bitrate += increment;
}

void _gain_up_cwnd(SubflowRateController *this)
{
  gfloat gain;
  gdouble scl_i;
  gboolean result = FALSE;

  _mt0(this)->delta_cwnd = 0;
  _mt0(this)->increased = TRUE;
  if (!_mt0(this)->can_increase) goto done;

  scl_i = _get_cwnd_scl_i(this);
  /*
   * Limit growth if OWD shows an increasing trend
   */
  gain = GAIN_UP*(1.0f + MAX(0.0f, 1.0f - _mt0(this)->owd_trend / 0.2f));
  gain *= scl_i;

  _mt0(this)->delta_cwnd = (gint)(gain * _mt0(this)->off_target * _mt0(this)->bytes_newly_acked * this->mss / this->cwnd + 0.5f);
  done:
    _validate_and_set_cwnd(this);
}

void _gain_down_cwnd(SubflowRateController *this)
{
  gint32 delta_bytes = 0;
  /*
   * OWD above target
   */
  _mt0(this)->increased = FALSE;
  if (_mt1(this)->increased) {
      this->cwnd_i = this->cwnd;
  }
  delta_bytes = (gint)(GAIN_DOWN * _mt0(this)->off_target * _mt0(this)->bytes_newly_acked * this->mss / this->cwnd);
  this->last_congestion_detected = _now(this);
  _mt0(this)->delta_cwnd = delta_bytes;
  _validate_and_set_cwnd(this);
}



void _setup_monitoring(SubflowRateController *this, guint interval)
{
  this->monitoring_time = 0;
  this->monitoring_interval = interval;
  this->monitored_bytes = (gdouble)_actual_rate(this) / (gdouble)interval;
  mprtps_path_set_monitor_interval(this->path, interval);
done:
  return;
}

void _reduce_monitoring(SubflowRateController *this, guint32 requested_bytes)
{
  gboolean result = FALSE;
  if(this->monitoring_interval < 1) goto done;
  if(this->monitored_bytes < requested_bytes){
    _disable_monitoring(this);
    goto done;
  }
  _calculate_monitoring_interval(this, this->monitored_bytes - requested_bytes * 1.1);
done:
  return;
}

void _calculate_monitoring_interval(SubflowRateController *this, guint32 desired_bytes)
{
  gdouble actual, target, rate;
  guint monitoring_interval = 0;
  if(desired_bytes <= 0){
     goto exit;
   }
  actual = _actual_rate(this);
  target = actual + (gdouble) desired_bytes;
  rate = target / actual;

  if(rate > 2.) monitoring_interval = 2;
  else if(rate > 1.5) monitoring_interval = 3;
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
  else if(rate > 1.04) monitoring_interval = 14;
  else monitoring_interval = 0;

exit:
  _setup_monitoring(this, monitoring_interval);
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
