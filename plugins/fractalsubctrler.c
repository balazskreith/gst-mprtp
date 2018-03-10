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

// Indicate the default sending rate at the beginning
#define START_SENDING_RATE 128000

//determine the minimum interval in seconds must be stay in probe stage
//before the target considered to be accepted
#define MIN_APPROVE_INTERVAL 50 * GST_MSECOND

//determine the minimum multiplying factor for aprovements
//before the target considered to be accepted
#define APPROVE_MIN_TIME 0.2

//determine the maximum multiplying factor for aprovements
//before the target considered to be accepted
#define APPROVE_MAX_TIME 0.6

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
#define MAX_EVALUATION_WINDOW_INTERVAL 0.5

//determines the minimum value for the evaluation window interval in s
#define MIN_EVALUATION_WINDOW_INTERVAL 0.1

// determines the factor for reducing target considering rate differences
#define REDUCE_DRATE_FACTOR 1.5

// Determine the threshold that a continous lost happens that considered to be a tcp flow
#define TCP_LOST_THRESHOLD 0.1

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

  gdouble             reduce_drate_factor;

  gdouble             tcp_lost_threshold;

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

#define _reduce_drate(this)           _priv(this)->reduce_drate_factor

#define _tcp_lost_threshold(this)     _priv(this)->tcp_lost_threshold
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

// static gdouble
// _skew_corr(
//     FRACTaLSubController *this);

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

static gboolean
_is_sr_approved(
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
_execute_stage(
    FRACTaLSubController *this);

static void
_fire(
    FRACTaLSubController *this,
    Event event);

static void
_probe_helper(
    FRACTaLSubController *this);

static void
_increase_helper(
    FRACTaLSubController *this);


#define _disable_monitoring(this) _start_monitoring(this, 0)

static void
_check_tcp(
    FRACTaLSubController *this);

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

  klass->approved_increasement = 5;
  g_print("HERE %d", klass->approved_increasement);
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

  {
    FRACTaLSubControllerClass* klass = (FRACTaLSubControllerClass*) this->object.g_type_instance.g_class;
    --klass->subflows_num;
  }
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

  _priv(this)->reduce_drate_factor              = REDUCE_DRATE_FACTOR;

  _priv(this)->tcp_lost_threshold               = TCP_LOST_THRESHOLD;
  {
    FRACTaLSubControllerClass* klass = (FRACTaLSubControllerClass*)this->object.g_type_instance.g_class;
    ++klass->subflows_num;
  }
}

FRACTaLSubController *make_fractalsubctrler(SndTracker *sndtracker, SndSubflow *subflow)
{
  FRACTaLSubController *this   = g_object_new (FRACTALSUBCTRLER_TYPE, NULL);

  this->sndtracker          = g_object_ref(sndtracker);
  this->subflow             = subflow;
  this->made                = _now(this);
  this->stat                = g_malloc0(sizeof(FRACTaLStat));
  this->fbprocessor         = make_fractalfbprocessor(sndtracker, subflow, this->stat);

  sndsubflow_set_state(subflow, SNDSUBFLOW_STATE_STABLE);
  _switch_stage_to(this, STAGE_KEEP, FALSE);

  sndtracker_add_on_packet_sent_with_filter(this->sndtracker,
      (ListenerFunc) _on_rtp_sending,
      (ListenerFilterFunc) _rtp_sending_filter,
      this);

  fractalfbprocessor_set_evaluation_window_margins(this->fbprocessor,
      _min_ewi(this) * GST_SECOND, _max_ewi(this) * GST_SECOND);

  _change_sndsubflow_target_bitrate(this, START_SENDING_RATE);
  return this;
}


void fractalsubctrler_enable(FRACTaLSubController *this)
{
  this->enabled = TRUE;
}

void fractalsubctrler_disable(FRACTaLSubController *this)
{
  _switch_stage_to(this, STAGE_KEEP, FALSE);
  _change_sndsubflow_target_bitrate(this, 0);
  _stop_monitoring(this);
  this->enabled = FALSE;
}

gboolean _rtp_sending_filter(FRACTaLSubController* this, SndPacket *packet)
{
  return this->subflow->id == packet->subflow_id;
}

void _on_rtp_sending(FRACTaLSubController* this, SndPacket *packet)
{
  ++this->sent_packets;
  if(!this->enabled || this->stat->measurements_num < 10){
    return;
  }

  if(0 < this->monitoring_interval && this->sent_packets % this->monitoring_interval == 0){
    this->subflow->monitoring_interval = this->monitoring_interval;
    sndsubflow_monitoring_request(this->subflow);
  }

  if (this->set_border_packet) {
    this->border_packet_seq = packet->subflow_seq;
    this->set_border_packet = FALSE;
  }
}

static gboolean header_printed = FALSE;
static void _stat_print(FRACTaLSubController *this)
{
  FRACTaLStat *stat = this->stat;
  gchar result[1024];
  memset(result, 0, 1024);

  if (!header_printed) {
    sprintf(result,
        "Subflow ID,"             // 1
        "Measurements,"           // 2
        "Elapsed time in sec,"    // 3
        "Estimated Receiver Rate,"        // 4
        "Rate difference,"        // 5
        "Queued Bytes,"           // 6
        "FEC bitrate,"            // 7
        "FEC target,"             // 8
        "FEC interval,"           // 9
        "Sending Rate [kbps],"    // 10
        "Receiver Rate [kbps],"   // 11
        "Received in EWI [kbps]," // 12
        "Target Rate [kbps],"     // 13
        "Bottleneck Point [kbps],"// 14
        "Set Target [kbps],"      // 15
        "Congested Target [kbps],"// 16
        "dRate [kbps],"           // 17
        "FRACTaL Stage,"          // 18
        "Subflow State,"          // 19
        "Monitoring Approved,"    // 20
        "Increasing Approved,"    // 21
        "Fractional Lost,"        // 22
        "TCP Flow Presented,"     // 23
        "Fractional Lost Threshold," // 24
        "Queue Delay Stability,"     // 25
        "Queue Delay Variance Stability," // 26
        "QD Avg,"                    // 27
        "Lost or Discarded Nums,"    // 28
        "QD Min,"                    // 29
        "QD Max,"                    // 30
        "Arrived Packets,"           // 31
        "QD New Stability Stab,"     // 32
        "QD New Stability Avg,"      // 33
        );
    g_print("Stat:%s\n",result);
    header_printed = TRUE;
    memset(result, 0, 1024);
  }

  sprintf(result,
          "%2d,"     // 1
          "%3d,"     // 2
          "%3.1f,"   // 3
          "%4f,"     // 4
          "%4f,"     // 5
          "%3f,"     // 6
          "%4d,"     // 7
          "%4d,"     // 8
          "%2d,"     // 9
          "%4d,"     // 10
          "%4d,"     // 11
          "%4d,"     // 12
          "%4d,"     // 13
          "%4d,"     // 14
          "%4d,"     // 15
          "%4d,"     // 16
          "%4d,"     // 17
          "%1d,"     // 18
          "%1d,"     // 19
          "%1d,"     // 20
          "%1d,"     // 21
          "%1.2f,"   // 22
          "%1.2f,"   // 23
          "%1.2f,"   // 24
          "%1.2f,"   // 25
          "%1.2f,"   // 26
          "%1.2f,"   // 27
          "%d,"      // 28
          "%d,"      // 29
          "%d,"      // 30
          "%d,"      // 31
          "%f,"      // 32
          "%f,"      // 33
          ,
          this->subflow->id,                         // 1
          stat->measurements_num,                    // 2
          GST_TIME_AS_MSECONDS(_now(this) - this->made) / 1000., // 3
          _stat(this)->rr_hat / 1000,                // 4
          _stat(this)->drr,                          // 5
          _stat(this)->rtpq_delay,                   // 6
          stat->fec_bitrate / 1000,                  // 7
          this->monitoring_target_bitrate / 1000,    // 8
          this->monitoring_interval,                 // 9
          (gint32)(stat->sr_avg / 1000),             // 10
          (gint32)(stat->rr_avg / 1000),             // 11
          (gint32)(stat->rcved_bytes_in_ewi / 125),  // 12
          this->target_bitrate / 1000,               // 13
          this->bottleneck_point / 1000,             // 14
          this->set_target / 1000,                   // 15
          this->congested_bitrate / 1000,            // 16
          (gint32)(_stat(this)->drate_avg / 1000),   // 17
          _priv(this)->stage,                        // 18
          this->subflow->state,                      // 19
          this->monitoring_approved,                 // 20
          this->increasing_approved,                 // 21
          stat->fraction_lost,                       // 22
          this->tcp_flow_presented,                  // 23
          stat->FL_th,                               // 24
          _stat(this)->qdelay_stability,             // 25
          _stat(this)->qdelay_var_stability,         // 26
          _stat(this)->avg_qd,                       // 27
          _stat(this)->lost_or_discarded,            // 28
          _stat(this)->qd_min,                       // 29
          _stat(this)->qd_max,                       // 30
          _stat(this)->arrived_packets,              // 31
          _stat(this)->qdelay_stability_stab,         // 32
          _stat(this)->qdelay_stability_avg        // 33
          );

  g_print("Stat:%s\n",result);
}


void fractalsubctrler_time_update(FRACTaLSubController *this)
{
  if(!this->enabled || _stat(this)->measurements_num < 1){
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
  } else if(this->backward_congestion) {
    goto done;
  }

  // check the frequency and command to send packet if it is not enough
  // request fec packets if it is not enough.

  fractalfbprocessor_time_update(this->fbprocessor);

  if(_stat(this)->measurements_num < 10){
//    this->bottleneck_point = this->target_bitrate = _stat(this)->sender_bitrate;
    goto done;
  }

  switch(this->subflow->state){
    case SNDSUBFLOW_STATE_CONGESTED:
      {

      }
      break;
    case SNDSUBFLOW_STATE_STABLE:
      {

      }
      break;
    case SNDSUBFLOW_STATE_INCREASING:
    {

    }
      break;
    default:
      break;
  }

  // For testing purpose
//  if (this->target_bitrate < 950000) {
//    gint32 delta = (1000000 - this->target_bitrate) * .2;
//    _change_sndsubflow_target_bitrate(this, this->target_bitrate + MIN(delta, 10000) );
//  } else if (this->target_bitrate){
//    _change_sndsubflow_target_bitrate(this, 600000);
//  }


  DISABLE_LINE _stat_print(this);

done:
  return;
}

static gboolean _approve_measurement(FRACTaLSubController *this){
  if(_subflow(this)->state == SNDSUBFLOW_STATE_CONGESTED) {
    if (this->last_distorted < _now(this) - 2 * GST_SECOND) {
      this->obligated_approvement = _now(this);
    }
    goto done;
  }

  this->FL_th = _stat(this)->FL_th;
  if (this->FL_th < _stat(this)->fraction_lost) {
    goto done;
  }

  return TRUE;
done:
  return FALSE || _now(this) < this->obligated_approvement + 10 * GST_SECOND;
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
  if(_stat(this)->measurements_num < 10){
    _keep_stage(this);
    goto done;
  }
  _check_tcp(this);

  DISABLE_LINE _stat_print(this);
  _stat_print(this);

  this->approve_measurement = FALSE;
  _execute_stage(this);
  // This was some kind of threshold for sliding window to get enough measurmeent if the conditions are bad
  // not sure if needed anymore
//  this->obligated_approvement = _now(this);
//  this->last_distorted = _now(this);

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


static gboolean _congestion(FRACTaLSubController *this)
{
//  this->FL_th = _stat(this)->FL_th;
//  gdouble alpha = _stat(this)->qdelay_var_stability;
//  if (0.6 < _stat(this)->qdelay_var_stability || .5 < _stat(this)->fraction_lost_avg) {
//    return _stat(this)->FL_th < _stat(this)->fraction_lost;
//  }
//  this->FL_th = _stat(this)->FL_th + MIN(_stat(this)->fraction_lost_avg, .1) *  (1.-alpha);
  this->FL_th = _stat(this)->FL_th + this->tcp_flow_presented * .05;
  return this->FL_th < _stat(this)->fraction_lost;
}

static void _undershoot(FRACTaLSubController *this, gint32 turning_point) {
  gint32 new_target;
  if (this->target_bitrate < _stat(this)->sr_avg - _max_ramp_up(this)) {
    this->congested_bitrate =  MIN(_stat(this)->sr_avg, _stat(this)->rr_avg);
    this->bottleneck_point = MIN(_stat(this)->sr_avg, _stat(this)->rr_avg);
    this->reducing_sr_reached = 0;
    this->reducing_approved = FALSE;
    return; // already overshooted
  }
  this->congested_bitrate = turning_point;
  this->bottleneck_point = 0;
  this->reducing_sr_reached = 0;
  this->reducing_approved = FALSE;

  {
    gdouble factor = 0. < _stat(this)->fraction_lost ? CONSTRAIN(.1, .6, _stat(this)->drr) : CONSTRAIN(.1, .2, _stat(this)->drr);
    gint32 decrease;
    decrease = MAX(_min_ramp_up(this), this->congested_bitrate * factor);
    new_target = MIN(turning_point - decrease, _stat(this)->rr_avg - _min_ramp_up(this));
  }

//  {
//    gdouble factor = _stat(this)->drate_avg / _stat(this)->rr_avg;
//    gint32 decrease;
//    factor = CONSTRAIN(.1, .4, factor);
//    decrease = MAX(_min_ramp_up(this), this->congested_bitrate * factor);
//    new_target = MIN(turning_point - decrease, _stat(this)->rr_avg - _min_ramp_up(this));
//  }
  _change_sndsubflow_target_bitrate(this, MIN(this->target_bitrate, new_target));
}

void
_reduce_stage(
    FRACTaLSubController *this)
{
  gint32 new_target;

  if (_congestion(this)) {
    if (0. < _stat(this)->drate_avg) {
      gint32 decrease = _stat(this)->drate_avg * _reduce_drate(this);
      new_target = _stat(this)->sr_avg + _stat(this)->fec_bitrate - decrease;
      this->reducing_sr_reached = 0;
      this->reducing_approved = FALSE;
      _change_sndsubflow_target_bitrate(this, MIN(this->target_bitrate, new_target));
    } else {
      this->bottleneck_point = _stat(this)->sr_avg;
    }
    goto done;
  }

  _refresh_reducing_approvement(this);
  if(!this->reducing_approved) {
    goto done;
  }

  if (this->bottleneck_point == 0) {
    this->bottleneck_point = this->target_bitrate;
  }

  _switch_stage_to(this, STAGE_KEEP, FALSE);
done:
  return;

}

void
_keep_stage(
    FRACTaLSubController *this)
{
  GstClockTime time_boundary;

  if(_congestion(this)){
    if(_subflow(this)->state != SNDSUBFLOW_STATE_STABLE){
      _undershoot(this, MIN(_stat(this)->sr_avg, _stat(this)->rr_avg));
      _set_event(this, EVENT_CONGESTION);
      _switch_stage_to(this, STAGE_REDUCE, FALSE);
    }else{
      _set_event(this, EVENT_DISTORTION);
    }
    goto done;
  } else if(_subflow(this)->state != SNDSUBFLOW_STATE_STABLE) {
    _set_event(this, EVENT_SETTLED);
    goto done;
  }

  time_boundary = _now(this) - CONSTRAIN(300 * GST_MSECOND, GST_SECOND, 2 * _stat(this)->srtt);
  if(time_boundary < MAX(this->last_settled, this->last_distorted)) {
    goto done;
  } else if(.1 < _stat(this)->rtpq_delay) {
    goto done;
  }

  _start_monitoring(this);
  sndsubflow_set_stable_target_rate(this->subflow, this->target_bitrate);
  _switch_stage_to(this, STAGE_PROBE, FALSE);

  this->set_target = this->target_bitrate;
done:
  return;
}

void
_probe_stage(
    FRACTaLSubController *this)
{
  if(_congestion(this)){
    _stop_monitoring(this);
    _set_event(this, EVENT_CONGESTION);
    _undershoot(this, _stat(this)->sr_avg + _stat(this)->fec_bitrate);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    goto done;
  }

  DISABLE_LINE _probe_helper(this);
  _probe_helper(this);
  if (_max_ramp_up(this) / 2 < _stat(this)->fec_bitrate && this->monitoring_interval < _get_monitoring_interval(this)) {
    _start_monitoring(this);
    goto done;
  }

  _refresh_monitoring_approvement(this);
  if(!this->monitoring_approved) {
    goto done;
  } else if (0 && !_is_sr_approved(this)) {
    goto done;
  }

  _set_event(this, EVENT_READY);
  _start_increasement(this);
  _switch_stage_to(this, STAGE_INCREASE, FALSE);

  this->set_target = MAX(this->set_target * .95, this->target_bitrate);
//  g_print("set target: %d | target: %d\n", this->set_target, this->target_bitrate);
done:
  return;
}

void
_increase_stage(
    FRACTaLSubController *this)
{
  if(_congestion(this)){
    _stop_monitoring(this);
    _set_event(this, EVENT_CONGESTION);
    _undershoot(this, _stat(this)->sr_avg + _stat(this)->fec_bitrate);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    goto done;
  }

  DISABLE_LINE _increase_helper(this);
  _increase_helper(this);
  if (this->target_bitrate < this->set_target - this->increasement || 0
//      (this->increasing_started < _now(this) - _stat(this)->srtt * _sensitivity(this, 20, 10))
   ) {
    _set_event(this, EVENT_SETTLED);
    _start_monitoring(this);
    _switch_stage_to(this, STAGE_PROBE, FALSE);
    this->set_target -= this->increasement;
    this->bottleneck_point = MAX(this->target_bitrate, _stat(this)->sr_avg);
    _change_sndsubflow_target_bitrate(this, this->set_target);
//    g_print("set target: %d | target: %d\n", this->set_target, this->target_bitrate);
    goto done;
  }

  _refresh_increasing_approvement(this);
  if (!this->increasing_approved) {
    goto done;
  }
//  if(.1 < _stat(this)->rtpq_delay) {
//    goto done;
//  }
  this->increasement = 0;
  _set_event(this, EVENT_SETTLED);
  _start_monitoring(this);
  _switch_stage_to(this, STAGE_PROBE, FALSE);
  sndsubflow_set_stable_target_rate(this->subflow, this->target_bitrate);
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
    case SNDSUBFLOW_STATE_CONGESTED:
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
          this->congested_bitrate   = 0;
          this->bottleneck_cwnd     = 0;
          this->distortion_num      = 0;
          this->set_target          = 0;
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
          this->tracked_target      = 0;
          this->last_distorted      = _now(this);
          this->reducing_approved   = FALSE;
          this->reducing_sr_reached = 0;
          sndsubflow_set_state(this->subflow, SNDSUBFLOW_STATE_CONGESTED);
          break;
        case EVENT_READY:
          this->last_increased = _now(this);
          sndsubflow_set_state(this->subflow, SNDSUBFLOW_STATE_INCREASING);
          break;
        case EVENT_SETTLED:
          this->distortion_num      = 0;
          this->last_settled = _now(this);
          break;
        case EVENT_FI:
        default:
        break;
      }
    break;
    case SNDSUBFLOW_STATE_INCREASING:
      switch(event){
        case EVENT_DISTORTION:
        case EVENT_CONGESTION:
          ++this->distortion_num;
          this->last_distorted = _now(this);
          this->reducing_approved   = FALSE;
          this->reducing_sr_reached = 0;
          this->tracked_target = 0;
          sndsubflow_set_state(this->subflow, SNDSUBFLOW_STATE_CONGESTED);
        break;
        case EVENT_SETTLED:
          this->distortion_num = 0;
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
  gint32 monitoring_target_bitrate;
  if(this->monitoring_approved){
    return;
  }
  monitoring_target_bitrate = _sensitivity(this, _min_ramp_up(this), _max_ramp_up(this)) * .9;
  if(_stat(this)->fec_bitrate < monitoring_target_bitrate * .9) {
    if (_now(this) - GST_SECOND < this->monitoring_started) {
      return;
    }
  }

  if (_stat(this)->sr_avg < this->target_bitrate - _max_ramp_up(this)) {
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
//  gint32 boundary;

  if(this->increasing_approved){
    return;
  }

//  boundary = CONSTRAIN(_min_ramp_up(this) / 4, _min_ramp_up(this) * 2, this->increasement * .1) ;
//  if(_stat(this)->sr_avg < this->set_target - boundary){
//    this->increasing_approved = FALSE;
//    return;
//  }

  if(_stat(this)->sr_avg < this->target_bitrate - _min_ramp_up(this) / 2){
//    this->increasing_approved = FALSE;
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

gboolean _is_sr_approved(FRACTaLSubController *this)
{
  gint32 boundary;
  gdouble alpha = _scale_t(this) * _stat(this)->qdelay_stability;
  gdouble room = 1. * alpha + 3. * (1.-alpha);
  if(this->last_approved_sr - _max_ramp_up(this) / room < this->target_bitrate &&
      this->target_bitrate < this->last_approved_sr + _max_ramp_up(this) / 2){
    return TRUE;
  }

  boundary = CONSTRAIN(_min_ramp_up(this) / room, _max_ramp_up(this), this->target_bitrate * .1);
  if(_stat(this)->sr_avg < this->target_bitrate - boundary || this->target_bitrate + boundary < _stat(this)->sr_avg){
    return FALSE;
  }

  this->last_approved_sr = this->target_bitrate;
  return TRUE;
}

static gint
_cmp_seq (guint16 x, guint16 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 32768) return -1;
  if(x > y && x - y > 32768) return -1;
  if(x < y && y - x > 32768) return 1;
  if(x > y && x - y < 32768) return 1;
  return 0;
}

void _refresh_reducing_approvement(FRACTaLSubController *this)
{
  gint32 boundary;

  if(this->reducing_approved){
    return;
  }
  boundary = CONSTRAIN(_min_ramp_up(this), _max_ramp_up(this), this->bottleneck_point * .1);
  if(this->target_bitrate + boundary < _stat(this)->sr_avg) {
    return;
  }

  if(!this->reducing_sr_reached){
    this->reducing_sr_reached = _now(this);
    this->set_border_packet = TRUE;
    return;
  }

  if (this->set_border_packet) {
    return;
  }

  if (_cmp_seq(_stat(this)->HSN, this->border_packet_seq) < 0) {
    if(_now(this) - GST_SECOND < this->reducing_sr_reached){
      return;
    }
  }
//  interval = _stat(this)->srtt;
//  if(_now(this) - interval < this->reducing_sr_reached){
//    return;
//  }

  this->reducing_approved = TRUE;
}

void _start_increasement(FRACTaLSubController *this)
{
  gint32 increasement = CONSTRAIN(_min_ramp_up(this), _max_ramp_up(this), _stat(this)->fec_bitrate);
//  increasement *= _sensitivity(this, .5, 1.);
  this->increasement             = increasement;
  this->increasing_started       = _now(this);
  this->increasing_sr_reached    = 0;
  this->increasing_approved      = FALSE;
  this->approvement_interval     = _get_approvement_interval(this);
  _change_sndsubflow_target_bitrate(this, this->target_bitrate + this->increasement);
}

gdouble _scale_t(FRACTaLSubController *this)
{
  gdouble result   = 0.;
  gint32 refpoint = MAX(_min_target(this), this->bottleneck_point);
  gint32 drate = MAX(this->target_bitrate - refpoint, refpoint - this->target_bitrate);
  gint32 epspoint;

  if (this->target_bitrate < refpoint) {
    epspoint = 1.5 * _max_ramp_up(this);
  } else {
    epspoint = _max_ramp_up(this) * 1;
  }

  if (!refpoint || epspoint < drate) {
    return 1.;
  }
  result = drate;
  result /= (gdouble) epspoint;
  result *= result;
  return CONSTRAIN(0., 1., result);
}


guint _get_approvement_interval(FRACTaLSubController* this)
{
  GstClockTime min,max;
  gdouble interval;
  gdouble scale_t = _scale_t(this);

  min = MAX(_appr_min_time(this) * GST_SECOND, _stat(this)->srtt * 1);
  max = MIN(_appr_max_time(this) * GST_SECOND, _stat(this)->srtt * 3);

  interval = scale_t * min + (1.-scale_t) * max;
  return interval;
}

guint _get_monitoring_interval(FRACTaLSubController* this)
{
  gint result;
  gint32 monitoring_target_bitrate;
//  gdouble qd_var_stability = _stat(this)->qdelay_var_stability;
  gdouble qd_var_stability = 1.-this->tcp_flow_presented;
  monitoring_target_bitrate = _sensitivity(this, _min_ramp_up(this), _max_ramp_up(this));
  monitoring_target_bitrate = monitoring_target_bitrate * qd_var_stability +
      (_max_ramp_up(this) + _max_ramp_up(this)) * .5 * (1.-qd_var_stability);
  result = CONSTRAIN(_mon_min_int(this), _mon_max_int(this), this->target_bitrate / monitoring_target_bitrate);
  return result;

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


void _probe_helper(FRACTaLSubController *this) {
  gdouble alpha = _stat(this)->qdelay_stability;
  gdouble beta = 1.-MAX(_stat(this)->drate_avg, 0.) / _stat(this)->sr_avg;
  gint32 max_target = MAX(this->target_bitrate, this->set_target);
  alpha = MIN(alpha, beta);
  if (0){
  if (this->bottleneck_point + 3 * _max_ramp_up(this) < this->target_bitrate) {
    this->bottleneck_point = this->target_bitrate - 3 * _max_ramp_up(this);
  } else if (alpha < .99){
    this->bottleneck_point += (max_target - this->bottleneck_point) * (1.-alpha);
  }}

//  alpha = MIN(1., alpha / MAX(0.25, _stat(this)->qdelay_var_stability));
  alpha = MIN(1., alpha / (1.-this->tcp_flow_presented));
  if (this->tcp_flow_presented < .5 && .5 < _stat(this)->qdelay_var_stability)
  {
    gint32 upper_limit = this->target_bitrate;
    gdouble room = (.95 * alpha + .85 * (1.-alpha));
    gint32 lower_limit = _stat(this)->sr_avg * room;
    gint32 tr_hat = upper_limit * alpha + lower_limit * (1.-alpha);
    _change_sndsubflow_target_bitrate(this, tr_hat);
  }
}


void _increase_helper(FRACTaLSubController *this) {
  gdouble alpha = _stat(this)->qdelay_stability;
  gint32 max_target = MAX(this->target_bitrate, this->set_target);
  if (0){
  if (this->bottleneck_point + 3 * _max_ramp_up(this) < this->target_bitrate) {
    this->bottleneck_point = this->target_bitrate - 3 * _max_ramp_up(this);
  } else if (alpha < .99){
    this->bottleneck_point += (max_target - this->bottleneck_point) * (1.-alpha);
  }}

//  alpha = MIN(1., alpha / MAX(0.25, _stat(this)->qdelay_var_stability));
  alpha = MIN(1., alpha / (1.-this->tcp_flow_presented));
  if (this->tcp_flow_presented < .5 && .5 < _stat(this)->qdelay_var_stability)
  {

    gint32 upper_limit = MIN(this->set_target, this->target_bitrate + 5000);
    gdouble room = (1. * alpha + 2. * (1.-alpha));
    gint32 lower_limit = this->set_target - this->increasement * room;
    gint32 tr_hat = upper_limit * alpha + lower_limit * (1.-alpha);
    _change_sndsubflow_target_bitrate(this, tr_hat);
  }
}

void _check_tcp(FRACTaLSubController *this){
  gdouble alpha = 1. - MIN(pow((_tcp_lost_threshold(this) - _stat(this)->fraction_lost_avg) / _tcp_lost_threshold(this), 2.), 1.);
  this->tcp_flow_presented = alpha * (1.-_stat(this)->qdelay_var_stability);
}
