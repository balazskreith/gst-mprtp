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
#define APPROVE_MIN_TIME 0.2

//determine the maximum multiplying factor for aprovements
//before the target considered to be accepted
#define APPROVE_MAX_TIME 1.0

//determines the minimum ramp up bitrate
#define RAMP_UP_MIN_SPEED 50000

//determines the maximum ramp up bitrate
#define RAMP_UP_MAX_SPEED 200000

//Min target_bitrate [bps]
//#define TARGET_BITRATE_MIN 500000
#define TARGET_BITRATE_MIN 100000

//Max target_bitrate [bps] - 0 means infinity
#define TARGET_BITRATE_MAX 0

//epsilon value for target or monitoring time approvement
#define APPROVEMENT_EPSILON 0.2

//mininmal pacing bitrate
#define MIN_PACING_BITRATE 10000

//determines the minimum monitoring interval
#define MIN_MONITORING_INTERVAL 2

//determines the maximum monitoring interval
#define MAX_MONITORING_INTERVAL 14

//determines the maximal treshold for fractional lost
#define MAX_FL_TRESHOLD 0.2

//determines the maximum value for the evaluation window interval in s
#define MAX_EVALUATION_WINDOW_INTERVAL 1.0

//determines the minimum value for the evaluation window interval in s
#define MIN_EVALUATION_WINDOW_INTERVAL 0.3

//determines the distortion threshold for the ratio of sent/received bytes
#define PIPE_STABILITY_DISTORTION_THRESHOLD 1.3

//determines the skew distortion threshold
#define SKEW_DISTORTION_THRESHOLD 1000

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

  GstClockTime        min_approve_interval;

  gdouble             approve_min_time;
  gdouble             approve_max_time;

  gint32              min_ramp_up_bitrate;
  gint32              max_ramp_up_bitrate;
  gint32              min_target_bitrate;
  gint32              max_target_bitrate;
  gdouble             appr_interval_epsilon;
  gint32              min_monitoring_interval;
  gint32              max_monitoring_interval;
  gdouble             min_pacing_bitrate;

  gdouble             max_FL_treshold;
  gdouble             min_evaluation_window_interval;
  gdouble             max_evaluation_window_interval;

  gdouble             pipe_stability_distortion_th;
  guint32             skew_distortion_threshold;

};

#define _priv(this) ((Private*)this->priv)
#define _stat(this) this->stat
#define _subflow(this) (this->subflow)

#define _stage(this) _priv(this)->stage
#define _event(this) _priv(this)->event
#define _set_event(this, e) _event(this) = e
#define _set_pending_event(this, e) this->pending_event = e

#define _min_pacing_bitrate(this)     _priv(this)->min_pacing_bitrate
#define _appr_interval_eps(this)      _priv(this)->appr_interval_epsilon
#define _min_appr_int(this)           _priv(this)->min_approve_interval

#define _appr_min_time(this)          _priv(this)->approve_min_time
#define _appr_max_time(this)          _priv(this)->approve_max_time

#define _min_ramp_up(this)            _priv(this)->min_ramp_up_bitrate
#define _max_ramp_up(this)            _priv(this)->max_ramp_up_bitrate
#define _min_target(this)             _priv(this)->min_target_bitrate
#define _max_target(this)             _priv(this)->max_target_bitrate
#define _mon_min_int(this)            _priv(this)->min_monitoring_interval
#define _mon_max_int(this)            _priv(this)->max_monitoring_interval
#define _max_FL_th(this)              _priv(this)->max_FL_treshold

#define _min_ewi(this)                _priv(this)->min_evaluation_window_interval
#define _max_ewi(this)                _priv(this)->max_evaluation_window_interval

#define _psi_dist_th(this)            _priv(this)->pipe_stability_distortion_th
#define _skew_dist_th(this)           _priv(this)->skew_distortion_threshold

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

void fractalsubctrler_finalize (GObject * object);

#define _now(this) (gst_clock_get_time(this->sysclock))

 static gboolean
 _rtp_sending_filter(
     FRACTaLSubController* this,
     SndPacket *packet);

 static void
 _on_rtp_sending(
     FRACTaLSubController* this,
     SndPacket *packet);

 static gdouble
 _sensitivity(
     FRACTaLSubController *this,
     gdouble at_bottleneck,
     gdouble far_from_bottleneck);

 static gdouble
 _skew_corr(
     FRACTaLSubController *this);

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

static gdouble
_scale_t(
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
_change_cwnd(
    FRACTaLSubController* this,
    gint32 new_cwnd);

static gdouble
_get_estimated_capacity(
    FRACTaLSubController *this);

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

  g_free(this->priv);

  g_object_unref(this->fbprocessor);
  g_free(this->stat);
  g_object_unref(this->sndtracker);
  g_object_unref(this->sysclock);
}


void
fractalsubctrler_init (FRACTaLSubController * this)
{
  this->priv     = g_malloc0(sizeof(Private));
  this->sysclock = gst_system_clock_obtain();

  _priv(this)->approve_min_time                 = APPROVE_MIN_TIME;
  _priv(this)->approve_max_time                 = APPROVE_MAX_TIME;

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

  _priv(this)->min_evaluation_window_interval   = MIN_EVALUATION_WINDOW_INTERVAL;
  _priv(this)->max_evaluation_window_interval   = MAX_EVALUATION_WINDOW_INTERVAL;

  _priv(this)->pipe_stability_distortion_th     = PIPE_STABILITY_DISTORTION_THRESHOLD;
  _priv(this)->skew_distortion_threshold        = SKEW_DISTORTION_THRESHOLD;
}

FRACTaLSubController *make_fractalsubctrler(SndTracker *sndtracker, SndSubflow *subflow)
{
  FRACTaLSubController *this   = g_object_new (FRACTALSUBCTRLER_TYPE, NULL);

  this->sndtracker          = g_object_ref(sndtracker);
  this->subflow             = subflow;
  this->made                = _now(this);
  this->stat                = g_malloc0(sizeof(FRACTaLStat));
  this->fbprocessor         = make_fractalfbprocessor(sndtracker, subflow, this->stat);
  this->cwnd                = _min_pacing_bitrate(this) * 5;

  sndsubflow_set_state(subflow, SNDSUBFLOW_STATE_STABLE);
  _switch_stage_to(this, STAGE_KEEP, FALSE);

  sndtracker_add_on_packet_sent_with_filter(this->sndtracker,
      (ListenerFunc) _on_rtp_sending,
      (ListenerFilterFunc) _rtp_sending_filter,
      this);

  fractalfbprocessor_set_evaluation_window_margins(this->fbprocessor,
      _min_ewi(this) * GST_SECOND, _max_ewi(this) * GST_SECOND);

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
  if(!this->enabled || this->stat->reference_num < 10){
    return;
  }

  srtt_in_s = _stat(this)->srtt * .000000001;
  pacing_bitrate = 0. < srtt_in_s ? this->cwnd / srtt_in_s : 50000.;
  pacing_time = (gdouble)packet->payload_size / pacing_bitrate;
//  {
//    gdouble alpha =  (gdouble)(_stat(this)->bytes_in_flight * 8.) / this->cwnd;
//    GstClockTime plus_time = 1. < alpha ? MIN(.05, (alpha - 1.) * pacing_time) : 0;
//    pacing_time += plus_time;
//  }
  this->subflow->pacing_time = _now(this) + pacing_time * GST_SECOND;
//  g_print("pacing_time: %f/%f=%f\n", (gdouble)packet->payload_size, pacing_bitrate, pacing_time);

  if(0 < this->monitoring_interval && this->sent_packets % this->monitoring_interval == 0){
    this->subflow->monitoring_interval = this->monitoring_interval;
    sndsubflow_monitoring_request(this->subflow);
  }
}


static void _stat_print(FRACTaLSubController *this)
{
  FRACTaLStat *stat = this->stat;
  g_print("%d:MsN:%d - %1.1f|"
          "QB:%-4d|"
          "BiF:%-3d(%-3.0f)->%-3.0f|%-3.0f|"
          "psi: %-1.1f(%1.1f)|%-3d(%-1.3f)|"
          "FEC:%-4d|SR:%-4d|RR:%-4d|%-4d|"
          "Tr:%-4d|Btl:%-4d|IP:%-4d|"
          "%d-%d-%d-%d|"
          "D:%-3u+%-3u<-%-3ld|"
          "RTPQ:%-1.3f|"
          "L:%-1.2f(%-1.2f)<-%-1.2f|"
          "S:%1.2f|%1.2f|%d|%1.2f\n",
      this->subflow->id,
      stat->reference_num,
      GST_TIME_AS_MSECONDS(_now(this) - this->made) / 1000.,

      stat->queued_bytes_in_srtt / 125,

      stat->bytes_in_flight / 125,
      stat->BiF_std / 125,
      this->cwnd / 1000.,
      this->bottleneck_cwnd / 1000,

      stat->psi,
      stat->max_psi,
      stat->extra_bytes / 125,
//      stat->max_extra_bytes / 125,
//      _stat(this)->extra_bytes_80th / 104, //1.2 * extra_bytes_80th
      stat->psi_std,

      stat->fec_bitrate / 1000,
      (gint32)(stat->sr_avg / 1000),
      (gint32)(stat->rr_avg / 1000),
      (gint32)(this->est_capacity / 1000),

      this->target_bitrate / 1000,
      this->bottleneck_point / 1000,
      this->inflection_point / 1000,

      _priv(this)->stage,
      this->subflow->state,
      this->monitoring_approved,
      this->increasing_approved,

      (guint32)_stat(this)->skew_80th,
      (guint32)_stat(this)->skew_std,
      _stat(this)->last_skew,

      _stat(this)->rtpq_delay,

      stat->fl_avg,
      stat->fl_std,
      stat->fraction_lost,

      _scale_t(this),
      _skew_corr(this),
      this->monitoring_interval,
      (gdouble)this->approvement_interval / (gdouble)GST_SECOND
      );
}

void fractalsubctrler_time_update(FRACTaLSubController *this)
{
  if(!this->enabled){
    goto done;
  }

  if(!this->backward_congestion && this->last_report < _now(this) - MAX(1.5 * GST_SECOND, 3 * _stat(this)->srtt)){
    GST_WARNING_OBJECT(this, "Backward congestion on subflow %d", this->subflow->id);
    g_print("backward congestion at %d\n", this->subflow->id);
    _stop_monitoring(this);
    _set_event(this, EVENT_DISTORTION);
    _switch_stage_to(this, STAGE_KEEP, FALSE);
    _change_sndsubflow_target_bitrate(this, MIN(_stat(this)->sr_avg, _stat(this)->rr_avg) * .9);
    this->backward_congestion = TRUE;
    goto done;
  }else if(this->backward_congestion){
    goto done;
  }

  fractalfbprocessor_time_update(this->fbprocessor);

  if(_stat(this)->reference_num < 10){
    this->bottleneck_point = this->target_bitrate = _stat(this)->sender_bitrate;
    goto done;
  }

  switch(this->subflow->state){
    case SNDSUBFLOW_STATE_OVERUSED:
      {

      }
      break;
    case SNDSUBFLOW_STATE_STABLE:
      {

      }
      break;
    case SNDSUBFLOW_STATE_UNDERUSED:
    {

    }
      break;
    default:
      break;
  }

  DISABLE_LINE _stat_print(this);

done:
  return;
}

static gboolean _approve_measurement(FRACTaLSubController *this){
  gboolean approving = FALSE;
  if(_subflow(this)->state == SNDSUBFLOW_STATE_OVERUSED){
    goto done;
  }
  if(_stat(this)->skew_80th + _stat(this)->skew_std * _sensitivity(this, 2.0, 5.0) < _stat(this)->last_skew){
    goto done;
  }
  approving = TRUE;
done:
  return approving || _now(this) < this->obligated_approvement + 10 * GST_SECOND;
}
void fractalsubctrler_report_update(
                         FRACTaLSubController *this,
                         GstMPRTCPReportSummary *summary)
{
  if(!this->enabled){
    goto done;
  }

  this->last_report = _now(this);

  if(this->backward_congestion){
    this->backward_congestion = FALSE;
    this->last_distorted      = _now(this);
    goto done;
  }

  fractalfbprocessor_report_update(this->fbprocessor, summary);
  this->est_capacity = _get_estimated_capacity(this);

  DISABLE_LINE _stat_print(this);
//  _stat_print(this);

  this->approve_measurement = FALSE;
  if(10 < _stat(this)->reference_num){
    _execute_stage(this);
  }else{
    this->obligated_approvement = _now(this);
    this->last_distorted = _now(this);
  }

  if(_approve_measurement(this)){
    fractalfbprocessor_approve_feedback(this->fbprocessor);
    this->last_approved = _now(this);
  }

done:
  return;
}

static gdouble _sensitivity( FRACTaLSubController *this, gdouble at_bottleneck, gdouble far_from_bottleneck) {
  gdouble alpha = _scale_t(this);
  return far_from_bottleneck * (alpha) + at_bottleneck * (1.-alpha);
}

gdouble _skew_corr(FRACTaLSubController *this) {
  if(_stat(this)->last_skew < 0){
      return 0.;
    }
    return (gdouble) (_stat(this)->last_skew) / (gdouble)(_stat(this)->last_skew + this->skew_th);
}


static gboolean _congestion(FRACTaLSubController *this)
{
  this->FL_th = CONSTRAIN(.0001, _max_FL_th(this), _stat(this)->fl_avg + _stat(this)->fl_std * 2);
  if (this->FL_th < _stat(this)->fraction_lost) {
    return TRUE;
  }

  {
    gdouble std_scaling = _sensitivity(this, 3.0, 1.5);
    gdouble avg_scaling = _sensitivity(this, 1.0, 2.0);
    this->skew_th = avg_scaling * _stat(this)->skew_80th + CONSTRAIN(200, 4000, _stat(this)->skew_std * std_scaling);
  }

  if (_stat(this)->last_skew < this->skew_th) {
    return FALSE;
  }

  return 1. + MAX(.5, 4. * _stat(this)->psi_std) < _stat(this)->psi;
//  {
//    gdouble ratio = (gdouble)_stat(this)->skew_80th / (gdouble)(_stat(this)->skew_80th + _stat(this)->skew_std);
//    gdouble psi_th = 1 + CONSTRAIN(.3, .9, ratio);
//    return psi_th < _stat(this)->psi;
//  }
}

static gboolean _distortion(FRACTaLSubController *this)
{
  {
    gdouble std_scaling = _sensitivity(this, 3.0, 1.5);
    gdouble avg_scaling = _sensitivity(this, 1.0, 1.0);
    gint64 skew_th = avg_scaling * _stat(this)->skew_80th + CONSTRAIN(200, 2000, _stat(this)->skew_std * std_scaling);
    if (skew_th < _stat(this)->last_skew) {
      return TRUE;
    }
  }

  if (1. + MAX(.05, 2. * _stat(this)->psi_std) < _stat(this)->max_psi) {
    return TRUE;
  }

//  if (80000 < _stat(this)->extra_bytes) {
//    return TRUE;
//  }

  if(.2 < _stat(this)->rtpq_delay) {
   return TRUE;
  }
  return FALSE;
}

static void _refresh_target(FRACTaLSubController *this) {
  gdouble e = MAX(exp(-1 * _stat(this)->max_psi + 1), .6);

  if(!this->bottleneck_point || this->bottleneck_point < this->target_bitrate) {
    this->bottleneck_point = MIN(this->target_bitrate, _stat(this)->rr_avg);
    this->bottleneck_point -= CONSTRAIN(2 * _min_ramp_up(this), _max_ramp_up(this), this->bottleneck_point * .1);
//    this->bottleneck_cwnd = this->cwnd;
//    return;
  }

  _change_sndsubflow_target_bitrate(this,
//          this->bottleneck_point  - MIN(this->bottleneck_point /**(1.-e)*/, _stat(this)->max_extra_bytes * 8));
          this->bottleneck_point);
  _change_cwnd(this, MAX(e * this->bottleneck_cwnd, this->cwnd));

}

static void _undershoot(FRACTaLSubController *this)
{
  gint32 new_target;
  gdouble new_cwnd;

//  this->bottleneck_point = CONSTRAIN(_stat(this)->rr_avg - 2 * _max_ramp_up(this), _stat(this)->rr_avg * .9, this->est_capacity);
  this->bottleneck_point = this->est_capacity;
  new_target = this->bottleneck_point - MIN(_max_ramp_up(this), _stat(this)->max_extra_bytes * 8) * 2;
  _change_sndsubflow_target_bitrate(this, new_target);

  this->distorted_cwnd = this->cwnd;
  this->bottleneck_cwnd = this->distorted_cwnd;
  new_cwnd = this->bottleneck_cwnd / _stat(this)->max_psi;
  _change_cwnd(this, new_cwnd);

  this->deflate_time = MIN(GST_SECOND, exp(_stat(this)->psi) * _stat(this)->srtt);
}

//refresh target and cwnd until rtt is gone.
static void _reduce_target(FRACTaLSubController *this)
{
  GstClockTime elapsed = _now(this) - this->last_distorted;
  gdouble alpha;
  gint32 new_target;

  alpha = CONSTRAIN(.5, .9, (gdouble)elapsed / _stat(this)->ewi_in_s * GST_SECOND); // the more we close to srtt the more the estimation might be punctual
  this->bottleneck_point = this->est_capacity * alpha + this->bottleneck_point * (1.-alpha);
  new_target = this->bottleneck_point - MIN(_max_ramp_up(this), _stat(this)->max_extra_bytes * 8) * 2;
  _change_sndsubflow_target_bitrate(this, new_target);
}

static void _restrict_cwnd(FRACTaLSubController *this)
{
  gdouble new_cwnd;

  this->bottleneck_cwnd = this->distorted_cwnd / _stat(this)->max_psi;
  new_cwnd = this->bottleneck_cwnd / _stat(this)->max_psi;
  _change_cwnd(this, new_cwnd);
}

static void _bounce_target(FRACTaLSubController *this)
{
  if (this->target_bitrate < this->bottleneck_point - 2 * _min_ramp_up(this)) {
    _change_sndsubflow_target_bitrate(this, this->target_bitrate + _min_ramp_up(this));
  }
}

static void _refresh_cwnd(FRACTaLSubController *this) {
  _change_cwnd(this, (_stat(this)->queued_bytes_in_srtt  + this->cwnd * .875));
}

void
_reduce_stage(
    FRACTaLSubController *this)
{

  GstClockTime now = _now(this);

  if (now - _stat(this)->ewi_in_s * GST_SECOND < this->last_distorted) {
    _reduce_target(this);
    _restrict_cwnd(this);
    this->deflate_time = CONSTRAIN(this->deflate_time, GST_SECOND, exp(_stat(this)->psi) * _stat(this)->srtt);
    goto done;
  }

  _refresh_reducing_approvement(this);
  if(now - this->deflate_time < this->last_distorted) {
    goto done;
  } else if(_congestion(this)) {
    _undershoot(this);
    _set_event(this, EVENT_CONGESTION);
    goto done;
  } else if(!this->reducing_approved) {
    goto done;
  }

  _bounce_target(this);
  _switch_stage_to(this, STAGE_KEEP, FALSE);
done:
  return;

}

void
_keep_stage(
    FRACTaLSubController *this)
{
  GstClockTime time_boundary;

  _refresh_cwnd(this);

  if(_congestion(this)){
    if(_subflow(this)->state != SNDSUBFLOW_STATE_STABLE){
      _set_event(this, EVENT_CONGESTION);
      _switch_stage_to(this, STAGE_REDUCE, FALSE);
      _undershoot(this);
    }else{
      _set_event(this, EVENT_DISTORTION);
    }
    goto done;
  }else if(_subflow(this)->state != SNDSUBFLOW_STATE_STABLE){
    _set_event(this, EVENT_SETTLED);
    goto done;
  }

  time_boundary = _now(this) - CONSTRAIN(300 * GST_MSECOND, GST_SECOND, 2 * _stat(this)->srtt);
  if(time_boundary < MAX(this->last_settled, this->last_distorted)){
    goto done;
  }else if(.1 < _stat(this)->rtpq_delay){
    goto done;
  }

  _start_monitoring(this);
  _switch_stage_to(this, STAGE_PROBE, FALSE);
  _set_event(this, EVENT_READY);
done:
  return;
}

void
_probe_stage(
    FRACTaLSubController *this)
{
  _refresh_cwnd(this);

  if(_congestion(this)){
    _stop_monitoring(this);
    _set_event(this, EVENT_CONGESTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    _undershoot(this);
    goto done;
  } else if (_now(this) - CONSTRAIN(300 * GST_MSECOND, GST_SECOND, 2 * _stat(this)->srtt) < this->last_distorted) {
    goto done;
  } else if (_distortion(this)) {
    _refresh_target(this);
    _start_monitoring(this);
    _set_event(this, EVENT_DISTORTION);
    goto done;
  }

  _refresh_monitoring_approvement(this);
  if(!this->monitoring_approved){
//    g_print("monitoring not approved at %d\n", this->subflow->id);
    goto done;
  }
  // _change_cwnd(this, this->cwnd * (1 + 1./(gdouble)this->monitoring_interval));
  _start_increasement(this);
  _switch_stage_to(this, STAGE_INCREASE, FALSE);
done:
  return;
}

void
_increase_stage(
    FRACTaLSubController *this)
{
  _refresh_cwnd(this);

  if(_congestion(this)){
    _stop_monitoring(this);
    _set_event(this, EVENT_CONGESTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    _undershoot(this);
    goto done;
  } else if (_distortion(this)) {
    _refresh_target(this);
    _start_monitoring(this);
    _set_event(this, EVENT_DISTORTION);
    _switch_stage_to(this, STAGE_PROBE, FALSE);
    goto done;
  }

  _refresh_increasing_approvement(this);
  if(!this->increasing_approved){
//    g_print("increasing is not approved at %d\n", this->subflow->id);
    goto done;
  }

  _start_monitoring(this);
  _set_event(this, EVENT_SETTLED);
  _switch_stage_to(this, STAGE_PROBE, FALSE);
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
          this->last_distorted      = _now(this);
          this->congestion_detected = _now(this);
          ++this->distortion_num;
          this->reducing_approved   = FALSE;
          this->reducing_sr_reached = 0;
        break;
        case EVENT_SETTLED:
          this->last_settled        = _now(this);
          this->congestion_detected = 0;
          this->increasement        = 0;
          this->inflection_point    = 0;
          this->bottleneck_cwnd     = 0;
          this->distortion_num      = 0;
          sndsubflow_set_state(this->subflow, SNDSUBFLOW_STATE_STABLE);
        break;
        case EVENT_FI:
        default:
        break;
      }
    break;
    case SNDSUBFLOW_STATE_STABLE:
      switch(event){
        case EVENT_DISTORTION:
        case EVENT_CONGESTION:
          ++this->distortion_num;
          this->last_distorted      = _now(this);
          this->reducing_approved   = FALSE;
          this->reducing_sr_reached = 0;
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
        case EVENT_DISTORTION:
          ++this->distortion_num;
          this->last_distorted = _now(this);
          break;
        case EVENT_CONGESTION:
          this->last_distorted = _now(this);
          this->reducing_approved   = FALSE;
          this->reducing_sr_reached = 0;
          sndsubflow_set_state(this->subflow, SNDSUBFLOW_STATE_OVERUSED);
        break;
        case EVENT_SETTLED:
          this->distortion_num = 0;
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
    FRACTaLSubController *this,
    Stage target,
    gboolean execute)
{
  switch(target){
    case STAGE_REDUCE:
      this->stage_fnc      = _reduce_stage;
    break;
    case STAGE_KEEP:
      this->stage_fnc      = _keep_stage;
     break;
     case STAGE_PROBE:
       this->stage_fnc = _probe_stage;
     break;
     case STAGE_INCREASE:
       this->stage_fnc = _increase_stage;
     break;
   }

  _priv(this)->stage = target;

  if(execute){
      this->stage_fnc(this);
  }
  this->rcved_fb_since_changed = 0;
}


void _refresh_monitoring_approvement(FRACTaLSubController *this)
{
  GstClockTime interval;
  gint32  boundary;
  gdouble ratio = 0.8 / (gdouble)this->monitoring_interval;
  if(this->monitoring_approved){
    return;
  }
  boundary = _stat(this)->sender_bitrate * ratio;
  if(_stat(this)->fec_bitrate < boundary){
    return;
  }
  if(!this->monitoring_approvement_started){
    this->monitoring_approvement_started = _now(this);
  }
  interval = this->approvement_interval;
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
  this->monitoring_approved  = FALSE;
  this->approvement_interval = _get_approvement_interval(this);
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

  interval = this->approvement_interval;
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
//  boundary = MAX(30000, this->target_bitrate * .1);
  boundary = CONSTRAIN(_min_ramp_up(this), _max_ramp_up(this), this->bottleneck_point * .1);
//  if(this->target_bitrate + boundary < MAX(_stat(this)->sr_avg, this->bottleneck_point)){
  if(this->target_bitrate + boundary < _stat(this)->sr_avg) {
//    g_print("%d - %f\n", this->target_bitrate + boundary, _stat(this)->sr_avg);
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

static gdouble _get_epsilon(FRACTaLSubController *this){
  return MIN(_appr_interval_eps(this), (gdouble)(_max_ramp_up(this) * 1) / (gdouble)this->target_bitrate);
}

static gint32 _get_refpoint(FRACTaLSubController *this){
//  gdouble off;
  if(!this->bottleneck_point){
    return 0;
  }
  if(this->target_bitrate < this->bottleneck_point){
    return this->bottleneck_point;
  }
  return this->bottleneck_point;
//  if(!this->inflection_point || this->inflection_point < this->bottleneck_point + _min_ramp_up(this)){
//    return this->bottleneck_point;
//  }
//  off = (gdouble) (this->inflection_point - this->bottleneck_point) / (gdouble) this->target_bitrate;
//  return .6 < off ? this->bottleneck_point : this->inflection_point;
}

gdouble _scale_t(FRACTaLSubController *this)
{
  gdouble result   = 0.;
  gdouble eps      = _get_epsilon(this);
  gint32 refpoint = MAX(_min_target(this), _get_refpoint(this));

  if(_now(this) - GST_SECOND < this->last_inflicted){
    return 0.;
  }

  result = this->target_bitrate - refpoint;
  result /= (gdouble)this->target_bitrate * eps;
  result *= result;
//done:
  return CONSTRAIN(0.,1., result);
}


guint _get_approvement_interval(FRACTaLSubController* this)
{
  GstClockTime min,max;
  gdouble interval;
  gdouble scale_t = _scale_t(this);

  min = MAX(_appr_min_time(this) * GST_SECOND, _stat(this)->srtt * 1);
  max = MIN(_appr_max_time(this) * GST_SECOND, _stat(this)->srtt * 5);

  interval = scale_t * min + (1.-scale_t) * max;
  return interval;
}

guint _get_monitoring_interval(FRACTaLSubController* this)
{
  guint16 mon_max_int;
  guint interval;
  gdouble scale_t;

  mon_max_int = MIN(MAX(_mon_min_int(this), _stat(this)->sent_packets_in_1s / 3), _mon_max_int(this));
  scale_t = _scale_t(this);
  interval = scale_t * _mon_min_int(this) + (1.-scale_t) * mon_max_int;

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
  if(0 < _max_target(this)){
    this->target_bitrate = CONSTRAIN(_min_target(this), _max_target(this), new_target);
  }else{
    this->target_bitrate = MAX(_min_target(this), new_target);
  }
  sndsubflow_set_target_rate(this->subflow, this->target_bitrate);
}

void _change_cwnd(FRACTaLSubController* this, gint32 new_cwnd)
{
  this->cwnd = MAX(_min_pacing_bitrate(this), new_cwnd);
//  g_print("cwnd: %d\n", new_cwnd);
}

gdouble _get_estimated_capacity(FRACTaLSubController *this)
{
  gdouble off;
  gdouble alpha;
  gdouble received_bits;
  gdouble est_rr;
  gint64 last_skew;

  received_bits = _stat(this)->received_bytes_in_ewi * 8.;
  if(!this->est_capacity){
    return received_bits;
  }
  last_skew = MAX(_stat(this)->last_skew, 0);
  alpha = (gdouble) last_skew / (gdouble)(last_skew + _stat(this)->skew_80th + _stat(this)->skew_std);
  off = _stat(this)->ewi_in_s;
  est_rr = received_bits / off;
  return MIN(this->est_capacity * (1.-alpha) + est_rr * alpha, _stat(this)->rr_avg);
}


