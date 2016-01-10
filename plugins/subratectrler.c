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
#define GAIN 1.0
//CWND scale factor due to loss event. Default value: 0.6
#define BETA 0.6
// Target rate scale factor due to loss event. Default value: 0.8
#define BETA_R 0.8
//Additional slack [%]  to the congestion window. Default value: 10%
#define BYTES_IN_FLIGHT_SLACK .1
//Interval between video bitrate adjustments. Default value: 0.1s ->100ms
#define RATE_ADJUST_INTERVAL 100
//Video coder frame period [s]
#define FRAME_PERIOD 0
//Min target_bitrate [bps]
#define TARGET_BITRATE_MIN 0
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
  guint64         ltt_delays_median;
  guint32         jitter;
  guint32         lost;
  guint32         discard;
  guint32         receiver_rate;
  guint32         sender_rate;
  guint32         goodput;
  guint32         bytes_newly_acked;

  //derivatives
  gdouble         corrd;
  gdouble         discard_rate;
  gdouble         corrh_owd;
  gdouble         goodput_ratio;
  gdouble         delay_ratio;
  gdouble         corr_rate;
  gdouble         corr_rate_dev;

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


static const gdouble ST_ = 1.1; //Stable treshold
static const gdouble OT_ = 2.;  //Overused treshold
static const gdouble DT_ = 1.5; //Down Treshold


//--------------------MEASUREMENTS-----------------------

#define _now(this) (gst_clock_get_time(this->sysclock))
#define _get_moment(this, n) ((Moment*)(this->moments + n * sizeof(Moment)))
#define _pmtn(index) (index == MOMENTS_LENGTH ? MOMENTS_LENGTH - 1 : index - 1)
#define _mt0(this) _get_moment(this, this->moments_index)
#define _mt1(this) _get_moment(this, _pmtn(this->moments_index))

static Moment*
_m_step(SubflowRateController *this);

//----------------------STATES-------------------

static void
_check_overused(
    SubflowRateController *this);

static void
_check_stable(
    SubflowRateController *this);

static void
_check_monitored(
    SubflowRateController *this);

//-----------------------ACTIONS------------------
static void
_transit_to(
    SubflowRateController *this,
    State target);

#define _disable_monitoring(subflow) _setup_monitoring(subflow, 0)

static void
_setup_monitoring(
    Subflow *subflow,
    guint interval);

static gboolean
_is_monitoring_done(
        SendingRateDistributor *this,
        Subflow *subflow);

static gboolean
_enable_monitoring(
        SendingRateDistributor *this,
        Subflow *subflow);

static guint32
_action_undershoot(
    SubflowRateController *this);

static guint32
_action_bounce_back(
    SubflowRateController *this);

static guint32
_action_bounce_up(
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
  g_object_unref(this->sysclock);
}

void
subratectrler_init (SubflowRateController * this)
{
  this->moments = g_malloc0(sizeof(Moment) * MOMENTS_LENGTH);
  this->sysclock = gst_system_clock_obtain();
  g_rw_lock_init (&this->rwmutex);
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
  if(!this->initialized){
    this->ltt_delays_th = make_percentiletracker(100, 80);
    percentiletracker_set_treshold(this->ltt_delays_th, GST_SECOND * 60);
    this->ltt_delays_target = make_percentiletracker(100, 50);
    percentiletracker_set_treshold(this->ltt_delays_target, GST_SECOND * 60);
    this->owd_fraction_hist = make_floatnumstracker(20, 60 * GST_SECOND);
    this->owd_norm_hist_20 = make_floatnumstracker(20, 60 * GST_SECOND);
    this->owd_norm_hist_100 = make_floatnumstracker(100, 60 * GST_SECOND);
    this->initialized = TRUE;
  }else{
    percentiletracker_reset(this->ltt_delays_th);
    percentiletracker_reset(this->ltt_delays_target);
  }
  this->path = g_object_ref(path);
  memset(this->moments, 0, sizeof(Moment) * MOMENTS_LENGTH);

  this->owd_target = OWD_TARGET_LO;
  this->owd_fraction_avg = 0.;
  //Vector of the last 20 owd_fraction
  this->owd_fraction_hist;
  this->owd_trend = 0.;
  //Vector of the last 100 owd
  this->owd_norm_hist;
  this->mss = 1400;
  this->min_cwnd = 2 * this->mss;
  this->in_fast_start = TRUE;
  //COngestion window
  this->cwnd;
  this->cwnd_i = 1;
  this->bytes_newly_acked = 0;
  this->send_wnd = 0;
  this->t_pace = 1;
  //A vector of the  last 20 RTP packet queue delay samples.Array
  this->age_vec;
  this->frame_skip_intensity = 0.;
  this->since_last_frame_skip = 0;
  this->consecutive_frame_skips = 0;
  this->target_bitrate = sending_target * 8;
  this->target_bitrate_i = 1;
  this->rate_transmit = 0.;
  this->rate_acked = 0.;
  this->s_rtt = 0.;
  //Size of RTP packets in queue [bits].
  this->rtp_queue_size;
  this->rtp_size = 0;
  this->frame_skip = FALSE;

  _transit_to(this, STATE_STABLE);
  THIS_WRITEUNLOCK(this);
}

void subratectrler_unset(SubflowRateController *this)
{
  THIS_WRITELOCK(this);
  this->path = g_object_unref(this->path);
  THIS_WRITEUNLOCK(this);
}

void subratectrler_measurement_update(
                         SubflowRateController *this,
                         RRMeasurement * measurement)
{

  _m_step(this);
  _mt0(this)->state = _mt1(this)->state;

  _mt0(this)->delay         = measurement->median_delay;
  _mt0(this)->discard       = measurement->late_discarded_bytes;
  _mt0(this)->lost          = measurement->lost;
  _mt0(this)->goodput       = measurement->goodput;
  _mt0(this)->receiver_rate = measurement->receiver_rate;
  _mt0(this)->sender_rate   = measurement->sender_rate;
  _mt0(this)->jitter        = measurement->jitter;

  _mt0(this)->bytes_newly_acked   = measurement->expected_payload_bytes;

  if(!percentiletracker_get_num(this->ltt_delays_th)){
    _mt0(this)->ltt_delays_th = measurement->max_delay;
    _mt0(this)->ltt_delays_median = measurement->median_delay;
  }else{
    _mt0(this)->ltt_delays_th = percentiletracker_get_stats(this->ltt_delays_th, NULL, NULL, NULL);
    _mt0(this)->ltt_delays_median = percentiletracker_get_stats(this->ltt_delays_target, NULL, NULL, NULL);
  }

  //Weather the subflow is overused or not.
  this->checker(this);

  _update_cwnd(this);


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


void subratectrler_time_update(SubflowRateController *this)
{
  gdouble owd_fraction;
  THIS_WRITELOCK(this);
  owd_fraction = _mt0(this)->delay/this->owd_target;
  floatnumstracker_add(this->owd_fraction_hist, owd_fraction);
  this->owd_fraction_avg = .9* this->owd_fraction_avg + .1* owd_fraction;

  THIS_WRITEUNLOCK(this);
}



typedef struct{
  gdouble avg;
  gdouble x1;
  gdouble a0,a1;
}RData;

static void _iterator_process(gpointer data, gdouble owd_fraction)
{
  RData *rdata = data;
  gdouble x0;
  x0 = owd_fraction - rdata->avg;
  rdata->a0 += x0 * x0;
  rdata->a1 += x0 * rdata->x1;
  rdata->x1 = x0;
}

gdouble _get_a(SubflowRateController *this)
{
  RData rdata;
  rdata.a0 = rdata.a1 = rdata.x1 = 0.;
  floatnumstracker_get_stats(this->owd_fraction_hist, NULL, &rdata.avg);
  floatnumstracker_iterate(this->owd_fraction_hist, _iterator_process, &rdata);
  if(rdata.a0 > 0.) return rdata.a1 / rdata.a0;
  else return 0.;
}



void
_update_cwnd(SubflowRateController *this)
{
  gdouble a, off_target, scl_i;
  gdouble increasement;
  a = _get_a(this);
  this->owd_trend = MAX(0., MIN(1., a * this->owd_fraction_avg));
  off_target = (gdouble)(this->owd_target - _mt0(this)->delay) / (gdouble) this->owd_target;

  scl_i = abs(this->cwnd-this->cwnd_i);
  scl_i/= (gdouble) (this->cwnd_i*4);
  scl_i *= scl_i;
  if(1. < scl_i) scl_i = 1.;
  if(scl_i < .2) scl_i = .2;

  if(this->in_fast_start) {
    this->cwnd = this->cwnd + this->bytes_newly_acked * scl_i;
    if(.2 <= this->owd_trend){
      this->in_fast_start = FALSE;
      this->cwnd_i = this->cwnd;
    }
  }
  else if(off_target > 0.){
    gdouble gain;
    gain = GAIN*(1.0 + max(0.0, 1.0 - this->owd_trend/ 0.2));
    //Limiting the gain when near congestion is detected
    gain *= scl_i;
    increasement = gain * off_target * this->bytes_newly_acked * this->mss / this->cwnd;
  }else if(off_target < 0.){
    this->cwnd += GAIN*off_target * this->bytes_newly_acked * this->mss/this->cwnd;
  }

}

void
_check_overused(
    SubflowRateController *this)
{
  if(_mt0(this)->corrh_owd > OT_){
    _reduce_cwnd(this);
    goto done;
  }

  this->in_fast_start = TRUE;
  _transit_to(this, STATE_STABLE);
done:
  _validate_cwnd(this);
  return;
}


void
_check_stable(
    SubflowRateController *this)
{
  //explicit congestion check

  if(_mt0(this)->discard_rate > 0.25 || _mt0(this)->corrh_owd > DT_){
     _reduce_cwnd(this);
    _transit_to(this, STATE_OVERUSED);
    goto done;
  }
  _mt0(this)->off_target = _get_off_target(this);
  _mt0(this)->increasable = _can_increase(this);

  if(this->in_fast_start){
    //In fast start we ramp up the rate directly.
    _fast_gain_up(this);
    goto done;
  }
  if(_mt0(this)->off_target > 0.){
    increasement = _gain_up(this);
  }else{
    _gain_down(this);
  }

  if(increasement > 0){
    //enable monitoring
    _transit_to(this, STATE_MONITORED);
  }
  //calculate off target and then rate controlling common with monitoring.
done:
  _validate_cwnd(this);
  return;
}

void
_check_monitored(
    SubflowRateController *this)
{

  if(_mt0(this)->corrh_owd > ST_ || _mt0(this)->discard_rate > .25){
    _reduce_cwnd(this);
    _transit_to(this,  STATE_OVERUSED);
    goto done;
  }

  _mt0(this)->off_target = _get_off_target(this);

  //calculate rate calculation and off target,
  //but in here we accept increasement only if trend doesn't show negative...
  if(_is_monitoring_done(this)){
    //increase

    this->requested_bytes +=_action_bounce_up(this);
    _transit_to(this, STATE_STABLE);
  }
done:
  _validate_cwnd(this);
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
      this->checker = _check_overused;
    break;
    case STATE_STABLE:
      mprtps_path_set_non_congested(this->path);
      this->checker = _check_stable;

    break;
    case STATE_MONITORED:
      this->checker = _check_monitored;
    break;
  }
  _mt0(this)->state = target;
}

void _reduce_cwnd(SubflowRateController *this)
{
  _mt0(this)->ECN = TRUE;
  this->cwnd_i = this->cwnd;
  this->cwnd = MAX(this->cwnd_min, (guint) (BETA * this->cwnd));
  this->last_congestion_detected = _now(this);
  this->in_fast_start = FALSE;
}

void _fast_gain_up(SubflowRateController *this)
{
  gdouble th;
  th = 0.2f;
  if (is_competing_flows(this))
     th = 0.5f;
  else if (this->n_fast_start > 1)
     th = 0.1f;

  if (this->owd_trend < th) {
     if (can_increase)
       this->cwnd += MIN(10 * this->mss, (guint)(this->bytes_newly_acked * scl_i));
  } else {
      this->in_fast_start = FALSE;
      this->last_congestion_detected = _now(this);
      this->cwnd_i = this->cwnd;
      this->was_cwnd_increase = TRUE;
  }
}

void _validate_cwnd(SubflowRateController *this)
{

}

void _gain_up(SubflowRateController *this)
{
  gfloat gain;
  /*
   * OWD below target
   */
  this->was_cwnd_increase = TRUE;
  /*
   * Limit growth if OWD shows an increasing trend
   */
  gain = CWND_GAIN_UP*(1.0f + MAX(0.0f, 1.0f - this->owd_trend / 0.2f));
  gain *= scl_i;

  if (can_increase)
    this->cwnd += (gint)(gain * off_target * this->bytes_newly_acked * this->mss / this->cwnd + 0.5f);
}

void _gain_down(SubflowRateController *this)
{
  /*
   * OWD above target
   */
  if (this->was_cwnd_increase) {
      this->was_cwnd_increase = FALSE;
      this->cwnd_i = this->cwnd;
  }
  this->cwnd += (gint)(CWND_GAIN_DOWN * off_target * this->bytes_newly_acked * this->mss / this->cwnd);
  this->last_congestion_detected = _now(this);
}



void _setup_monitoring(SubflowRateController *subflow)
{
  if(!subflow->available) goto done;
  subflow->monitoring_time = 0;
  mprtps_path_set_monitor_interval(subflow->path, interval);
done:
  return;
}


gboolean _enable_monitoring(
    SubflowRateController *this)
{
  gdouble rate;
  gdouble target;
  gdouble actual;
  guint64 max_target = 0;

  subflow->monitoring_interval = 0;
  if(!subflow->available) goto exit;

  if(this->actual_rate < this->target_rate * .9 || _mt0(subflow)->sender_rate < subflow->target_rate * .9){
    g_print("S%d: Not monitoring because of .9 * this->actual_rate \n", subflow->id);
    goto exit;
  }
  if(this->target_rate * 1.1 < this->actual_rate || _mt0(subflow)->sender_rate * 1.1 < subflow->target_rate){
    g_print("S%d: Not monitoring because of 1.1 * this->actual_rate \n", subflow->id);
    goto exit;
  }
  floatnumstracker_get_stats(subflow->targets, NULL, &max_target, NULL);
  if(subflow->target_rate < max_target * .9){
    target = max_target * 1.1;
    actual = subflow->target_rate;
    this->slope = 2;
    goto determine;
  }
  this->slope = 1;

  if(0 < subflow->max_rate){
    if(subflow->max_rate <= subflow->target_rate) {
      goto exit;
    }
//    g_print("target_rate point %d\n", subflow->target_rate);
    target = subflow->max_rate;
    actual = subflow->target_rate;
    goto determine;
  }

  if(0 < this->max_rate){
    if(this->max_rate <= this->target_rate){
      goto exit;
    }
    target = this->max_rate - subflow->target_rate;
    actual = this->target_rate;
    goto determine;
  }

  if(_mt0(subflow)->corrh_owd > 1. && _mt1(subflow)->corrh_owd > 1.){
    subflow->monitoring_interval = 14;
  }else if(_mt0(subflow)->corrh_owd > 1. || _mt1(subflow)->corrh_owd > 1.){
    subflow->monitoring_interval = 10;
  }else{
    subflow->monitoring_interval = 5;
  }


  goto determined;

determine:
  rate = target / actual;
  if(rate > 2.) subflow->monitoring_interval = 2;
  else if(rate > 1.5) subflow->monitoring_interval = 3;
  else if(rate > 1.25) subflow->monitoring_interval = 4;
  else if(rate > 1.2) subflow->monitoring_interval = 5;
  else if(rate > 1.16) subflow->monitoring_interval = 6;
  else if(rate > 1.14) subflow->monitoring_interval = 7;
  else if(rate > 1.12) subflow->monitoring_interval = 8;
  else if(rate > 1.11) subflow->monitoring_interval = 9;
  else if(rate > 1.10) subflow->monitoring_interval = 10;
  else if(rate > 1.09) subflow->monitoring_interval = 11;
  else if(rate > 1.08) subflow->monitoring_interval = 12;
  else if(rate > 1.07) subflow->monitoring_interval = 13;
  else subflow->monitoring_interval = 14;

determined:
  subflow->turning_point = MIN(subflow->turning_point, 3);
  subflow->monitoring_interval+=subflow->turning_point;
  subflow->monitoring_interval*=this->slope;
  subflow->monitoring_interval = MIN(14, subflow->monitoring_interval);
exit:
  _setup_monitoring(subflow, subflow->monitoring_interval);
  return subflow->monitoring_interval > 0;
}


guint32 _action_undershoot(
    SubflowRateController *this)
{
  gint32 supplied_bytes = 0;
//  gdouble r,gr;
  gint32 recent_monitored = 0;
  gint32 actual_rate;

  actual_rate = MIN(_mt0(subflow)->sender_rate, subflow->target_rate);

  if(subflow->target_rate <= subflow->min_rate){
    goto done;
  }

  subflow->bounce_point = 0;
  //spike detection
  if(subflow->target_rate + subflow->estimated_gp_dev * 4 < _mt0(subflow)->sender_rate){
    supplied_bytes = subflow->target_rate * .303;
    subflow->bounce_point = subflow->target_rate * .9;
    goto done;
  }

  if(0 < subflow->monitoring_interval){
    recent_monitored = (gdouble)actual_rate / (gdouble)subflow->monitoring_interval;
    subflow->bounce_point = (subflow->target_rate - recent_monitored) * .9;
    recent_monitored *=2;
  }else if(_mt1(subflow)->state == STATE_MONITORED ||
     _mt2(subflow)->state == STATE_MONITORED)
  {
    recent_monitored = _mt1(subflow)->delta;
    recent_monitored += _mt2(subflow)->delta;
    subflow->bounce_point = subflow->target_rate - recent_monitored;
    recent_monitored *= 2;
  }

  //mitigate
  if(0 < recent_monitored){
    if(_mt0(subflow)->discard_rate > .5)
      recent_monitored += _mt0(subflow)->discard * .5;
    for(; subflow->target_rate * .505 < recent_monitored; recent_monitored *= .75);
    supplied_bytes = recent_monitored;
    goto done;
  }

  if(_mt0(subflow)->discard_rate < .25){
      supplied_bytes = _mt0(subflow)->discard_rate * 2;
      subflow->bounce_point = (subflow->target_rate - _mt0(subflow)->discard_rate) * .9;
  }else if(_mt0(subflow)->discard_rate < .5){
      supplied_bytes = _mt0(subflow)->discard_rate;
      subflow->bounce_point = (subflow->target_rate - _mt0(subflow)->discard_rate) * .9;
  }else{
      supplied_bytes = subflow->target_rate * .505;
      subflow->bounce_point = subflow->target_rate * .707;
  }
//  if(_mt0(subflow)->corrh_owd < OT_)
//  {
//    if(_mt0(subflow)->discard_rate < .25)
//      supplied_bytes = _mt0(subflow)->discard * 1.5;
//    else if(_mt0(subflow)->discard_rate < .75)
//      supplied_bytes = actual_rate * .505;
//    goto done;
//  }
//
//  if(_mt0(subflow)->sender_rate == 0){
//    supplied_bytes = actual_rate * .101;
//    goto done;
//  }
//
//  r = _mt0(subflow)->receiver_rate / (gdouble)actual_rate;
//  if(1. < r){
//    supplied_bytes = .101 * actual_rate;
//    goto done;
//  }
//
//  gr = _mt0(subflow)->goodput / (gdouble)actual_rate;
//  if(gr < .5){
//    supplied_bytes = .707 * actual_rate;
//    goto done;
//  }
//
//  r = MAX(r, .5);
//  if(r < gr) r = gr;
//  //neglect
//  g_print("S%d Reduction %f-%f\n", subflow->id, r, gr);
//  if(0. < r && r < 1.) supplied_bytes = (1.-r) * actual_rate;
//  else if(1. < r) supplied_bytes = .101 * actual_rate;
//  else supplied_bytes = .707 * actual_rate;
//  if(gr < r && .75 < r) supplied_bytes*=1.1;
//  goto done;

done:
  g_print("S%d Undershoot by %d target rate was: %d\n", subflow->id, supplied_bytes, subflow->target_rate);
  return supplied_bytes;
}

guint32 _action_bounce_back(
    SubflowRateController *this)
{
//  gdouble bounce_point;
  guint32 requested_bytes = 0;
  if(subflow->bounce_point < subflow->target_rate) goto done;
  requested_bytes = subflow->bounce_point - subflow->target_rate;
  g_print("S%d Bounce up, requested bytes: %d target rate was: %d\n", subflow->id, requested_bytes, subflow->target_rate);
done:
  return requested_bytes;
}


guint32 _action_bounce_up(
    SubflowRateController *this)
{
  guint32 requested_bytes = 0;
  requested_bytes = subflow->target_rate /(gdouble) subflow->monitoring_interval;

//  g_print("BounceUp on S%d. SR: %d RB:%u\n", subflow->id, subflow->sending_rate, requested_bytes);
  return requested_bytes;
}


#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
