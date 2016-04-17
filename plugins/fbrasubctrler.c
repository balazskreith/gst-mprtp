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
#include "fbrasubctrler.h"
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

GST_DEBUG_CATEGORY_STATIC (fbrasubctrler_debug_category);
#define GST_CAT_DEFAULT fbrasubctrler_debug_category

G_DEFINE_TYPE (FBRASubController, fbrasubctrler, G_TYPE_OBJECT);

//if target close to the bottleneck, the increasement will be multiplied by this factor
#define BOTTLENECK_INCREASEMENT_FACTOR 1.0

//determine the epsilon factor around the target rate indicate weather we close to the bottleneck or not.
#define BOTTLENECK_EPSILON .25

//if target bitrate is close to the bottleneck, monitoring interval is requested for this interval
//note if the target/interval is higher than the maximum ramp up speed, then monitoring is
//restricted to the max_ramp_up
#define BOTTLENECK_MONITORING_INTERVAL 5

//determine the minimum interval in seconds must be stay in probe stage
//before the target considered to be accepted
#define NORMAL_PROBE_INTERVAL 1

//determine the maximum interval in seconds must be stay in probe stage
//before the target considered to be accepted
#define BOTTLENECK_PROBE_INTERVAL 3

//determines the maximum time in seconds the target can be mitigated after it is increased in probe stage
#define INCREASEMENT_MITIGATION_TRESHOLD 5

//determines the qdelay trend treshold considered to be distortion at probe stage
#define QDELAY_PROBE_TRESHOLD 0.001

//determines the qdelay trend treshold considered to be distortion at keep stage
#define QDELAY_KEEP_TRESHOLD  0.005

//determines the qdelay trend treshold considered to be congestion
#define QDELAY_CONGESTION_TRESHOLD 0.1

//determines the minimum monitoring interval
#define MIN_MONITORING_INTERVAL 2

//determines the maximum monitoring interval
#define MAX_MONITORING_INTERVAL 14

//determines the maximum keep time in second before mitigation applied
//if qdelay is distorted in keep stage
#define MAX_DISTORTION_KEEP_TIME 1

//determines the maximum ramp up bitrate
#define RAMP_UP_MIN_SPEED 10000

//determines the maximum ramp up bitrate
#define RAMP_UP_MAX_SPEED 500000

//Min target_bitrate [bps]
//#define TARGET_BITRATE_MIN 500000
#define TARGET_BITRATE_MIN (SUBFLOW_DEFAULT_SENDING_RATE>>1)

//Max target_bitrate [bps] - 0 means infinity
#define TARGET_BITRATE_MAX 0

//determines the reduction factor applies on the target bitrate if it is in reduced stage
#define REDUCE_TARGET_FACTOR 0.6

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

#define SR_TR_ARRAY_LENGTH 3

struct _Private{
  GstClockTime        time;

  Event               event;
  Stage               stage;
  Stage               stage_t1;
  GstClockTime        stage_changed;
  gboolean            controlled;
  gdouble             rtt;
  gboolean            tr_correlated;

  gboolean            fcongestion;
  gboolean            bcongestion;
  gboolean            fdistortion;
  gboolean            bdistortion;

  //Possible parameters
  gint32              min_monitoring_interval;
  gint32              max_monitoring_interval;
  gdouble             bottleneck_epsilon;
  gint32              bottleneck_monitoring_interval;
  gint32              normal_probe_interval;
  gint32              bottleneck_probe_interval;
  gdouble             qdelay_probe_treshold;
  gdouble             qdelay_keep_treshold;
  gdouble             qdelay_congestion_treshold;
  gint32              max_distortion_keep_time;
  gint32              increasement_mitigation_treshold;
  gdouble             bottleneck_increasement_factor;
  gint32              min_ramp_up_bitrate;
  gint32              max_ramp_up_bitrate;
  gint32              min_target_bitrate;
  gint32              max_target_bitrate;
  gdouble             reduce_target_factor;

  gdouble            *gcong;
  gdouble            *gkeep;
  gdouble            *gprobe;
};

#define _priv(this) ((Private*)this->priv)
#define _rmdi(this) this->rmdi_result
#define _corrH(this) _rmdi(this).corrH //todo: elliminate this one as well
//Todo: elliminate it.
#define _q1(this) _rmdi(this).g1
#define _q2(this) _rmdi(this).g2
#define _q3(this) _rmdi(this).g3
#define _q4(this) _rmdi(this).g4

#define _fcongestion(this) _priv(this)->fcongestion
#define _bcongestion(this) _priv(this)->bcongestion
#define _fdistortion(this) _priv(this)->fdistortion
#define _bdistortion(this) _priv(this)->bdistortion
#define _tr_is_corred(this) _priv(this)->tr_correlated
#define _btlp(this) this->bottleneck_point
#define _mon_int(this) this->monitoring_interval
#define _mon_br(this) this->monitored_bitrate
#define _UF(this) _rmdi(this).utilized_fraction

#define _state(this) mprtps_path_get_state(this->path)
#define _state_t1(this) this->state_t1
#define _stage(this) _priv(this)->stage
#define _stage_t1(this) _priv(this)->stage_t1
#define _event(this) _priv(this)->event
#define _set_event(this, e) _event(this) = e
#define _set_pending_event(this, e) this->pending_event = e

#define _TR(this) this->target_bitrate
#define _TR_t1(this) this->target_bitrate_t1
#define _SR(this) (_rmdi(this).sender_bitrate)
#define _GP(this) (_rmdi(this).goodput_bitrate)
#define _GP_t1(this) (_priv(this)->goodput_bitrate_t1)
#define _min_br(this) MIN(_SR(this), _TR(this))
#define _max_br(this) MAX(_SR(this), _TR(this))

#define _btl_inc_fac(this)     _priv(this)->bottleneck_increasement_factor
#define _btl_eps(this)         _priv(this)->bottleneck_epsilon
#define _btl_mon_int(this)     _priv(this)->bottleneck_monitoring_interval
#define _btl_probe_int(this)   _priv(this)->bottleneck_probe_interval
#define _norm_pbobe_int(this)  _priv(this)->normal_probe_interval
#define _inc_mit_th(this)      _priv(this)->increasement_mitigation_treshold
#define _gprobe_th(this)       _priv(this)->qdelay_probe_treshold
#define _gkeep_th(this)        _priv(this)->qdelay_keep_treshold
#define _gcong_th(this)        _priv(this)->qdelay_congestion_treshold
#define _mon_min_int(this)     _priv(this)->min_monitoring_interval
#define _mon_max_int(this)     _priv(this)->max_monitoring_interval
#define _max_dist_keep(this)   _priv(this)->max_distortion_keep_time
#define _min_ramp_up(this)     _priv(this)->min_ramp_up_bitrate
#define _max_ramp_up(this)     _priv(this)->max_ramp_up_bitrate
#define _rdc_target_fac(this)  _priv(this)->reduce_target_factor

#define _gkeep(this)           *(_priv(this)->gkeep)
#define _gprobe(this)          *(_priv(this)->gprobe)
#define _gcong(this)         *(_priv(this)->gcong)

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

 void fbrasubctrler_finalize (GObject * object);


// const gdouble ST_ = 1.1; //Stable treshold
// const gdouble OT_ = 2.;  //Overused treshold
// const gdouble DT_ = 1.5; //Down Treshold



#define _now(this) (gst_clock_get_time(this->sysclock))

static void
_reduce_stage(
    FBRASubController *this);

static void
_keep_stage(
    FBRASubController *this);

static void
_probe_stage(
    FBRASubController *this);

static void
_increase_stage(
    FBRASubController *this);

static void
_switch_stage_to(
    FBRASubController *this,
    Stage target,
    gboolean execute);

#define _transit_state_to(this, target) \
   this->state_t1 = this->state;        \
   this->state = target;                \


static void
_fire(
    FBRASubController *this,
    Event event);

static void
_disable_controlling(
    FBRASubController *this);

#define _disable_monitoring(this) _set_monitoring_interval(this, 0)

static void
_reset_monitoring(
    FBRASubController *this);

static void
_setup_monitoring(
    FBRASubController *this);

static void
_set_monitoring_interval(
    FBRASubController *this,
    guint interval);

static guint
_calculate_monitoring_interval(
    FBRASubController *this,
    guint32 desired_bitrate);

static void
_change_target_bitrate(FBRASubController *this, gint32 new_target);


static void
_params_out(
    FBRASubController *this);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
fbrasubctrler_class_init (FBRASubControllerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fbrasubctrler_finalize;

  GST_DEBUG_CATEGORY_INIT (fbrasubctrler_debug_category, "fbrasubctrler", 0,
      "FBRA+MARC Subflow Rate Controller");

}

void
fbrasubctrler_finalize (GObject * object)
{
  FBRASubController *this;
  this = FBRASUBCTRLER(object);
  mprtp_free(this->priv);
  g_object_unref(this->fb_processor);
  g_object_unref(this->sysclock);
  g_object_unref(this->path);
}

void
fbrasubctrler_init (FBRASubController * this)
{
  this->priv = mprtp_malloc(sizeof(Private));
  this->sysclock = gst_system_clock_obtain();
  g_rw_lock_init (&this->rwmutex);

  //Initial values
  this->target_bitrate   = SUBFLOW_DEFAULT_SENDING_RATE;

  _priv(this)->bottleneck_increasement_factor   = BOTTLENECK_INCREASEMENT_FACTOR;
  _priv(this)->bottleneck_epsilon               = BOTTLENECK_EPSILON;
  _priv(this)->bottleneck_monitoring_interval   = BOTTLENECK_MONITORING_INTERVAL;
  _priv(this)->normal_probe_interval            = NORMAL_PROBE_INTERVAL;
  _priv(this)->bottleneck_probe_interval        = BOTTLENECK_PROBE_INTERVAL;
  _priv(this)->increasement_mitigation_treshold = INCREASEMENT_MITIGATION_TRESHOLD;
  _priv(this)->qdelay_probe_treshold            = QDELAY_PROBE_TRESHOLD;
  _priv(this)->qdelay_keep_treshold             = QDELAY_KEEP_TRESHOLD;
  _priv(this)->qdelay_congestion_treshold       = QDELAY_CONGESTION_TRESHOLD;
  _priv(this)->min_monitoring_interval          = MIN_MONITORING_INTERVAL;
  _priv(this)->max_monitoring_interval          = MAX_MONITORING_INTERVAL;
  _priv(this)->max_distortion_keep_time         = MAX_DISTORTION_KEEP_TIME;
  _priv(this)->min_ramp_up_bitrate              = RAMP_UP_MIN_SPEED;
  _priv(this)->max_ramp_up_bitrate              = RAMP_UP_MAX_SPEED;
  _priv(this)->min_target_bitrate               = TARGET_BITRATE_MIN;
  _priv(this)->max_target_bitrate               = TARGET_BITRATE_MAX;
  _priv(this)->reduce_target_factor             = REDUCE_TARGET_FACTOR;

  _priv(this)->gkeep                            = &_rmdi(this).g1;
  _priv(this)->gprobe                           = &_rmdi(this).g2;
  _priv(this)->gcong                            = &_rmdi(this).g4;


}


gboolean fbrasubctrler_path_approver(gpointer data, GstBuffer *buffer)
{
//  FBRASubController *this = data;
  return TRUE;
}

FBRASubController *make_fbrasubctrler(MPRTPSPath *path)
{
  FBRASubController *result;
  result                      = g_object_new (FBRASUBCTRLER_TYPE, NULL);
  result->path                = g_object_ref(path);
  result->id                  = mprtps_path_get_id(result->path);
  result->monitoring_interval = 3;
  result->made                = _now(result);
  result->fb_processor        = make_rmdi_processor(path);
  result->report_interval     = 200 * GST_MSECOND;
  _switch_stage_to(result, STAGE_KEEP, FALSE);
  mprtps_path_set_state(result->path, MPRTPS_PATH_STATE_STABLE);

  return result;
}

void fbrasubctrler_enable(FBRASubController *this)
{
  this->enabled = TRUE;
  this->disable_controlling = _now(this) + 10 * GST_SECOND;
  this->target_bitrate = mprtps_path_get_target_bitrate(this->path);
}

void fbrasubctrler_disable(FBRASubController *this)
{
  _switch_stage_to(this, STAGE_KEEP, FALSE);
  mprtps_path_set_state(this->path, MPRTPS_PATH_STATE_STABLE);
  this->enabled = FALSE;
  mprtps_path_set_approval_process(this->path, NULL, NULL);
}

static void _reset_congestion_indicators(FBRASubController *this)
{
  _priv(this)->fcongestion = FALSE;
  _priv(this)->fdistortion  = FALSE;

}

static void _update_tr_corr(FBRASubController *this)
{
  gdouble tr_corr;

  tr_corr =  (gdouble)_rmdi(this).sender_bitrate / (gdouble)this->target_bitrate;

  _priv(this)->tr_correlated = .9 < tr_corr && tr_corr < 1.1;

}


static gint32 _get_probe_interval(FBRASubController *this)
{
  if(!this->bottleneck_point){
    return _norm_pbobe_int(this);
  }
  if(this->target_bitrate < this->bottleneck_point * (1.-_btl_eps(this))){
    return _norm_pbobe_int(this);
  }
  if(this->bottleneck_point * (1.+_btl_eps(this)) < this->target_bitrate){
    return _norm_pbobe_int(this);
  }
  return _btl_probe_int(this);
}

static gboolean _does_near_to_bottleneck_point(FBRASubController *this)
{
  if(!this->bottleneck_point){
    return FALSE;
  }
  if(this->target_bitrate < this->bottleneck_point * (1.-_btl_eps(this))){
    return FALSE;
  }
  if(this->bottleneck_point * (1.+_btl_eps(this)) < this->target_bitrate){
    return FALSE;
  }
  return TRUE;
}

static gint32 _get_increasement(FBRASubController *this)
{
  if(_does_near_to_bottleneck_point(this)){
    return this->monitored_bitrate * _btl_inc_fac(this);
  }
  return this->monitored_bitrate;
}

void fbrasubctrler_time_update(FBRASubController *this)
{
  if(!this->enabled){
    goto done;
  }
  //check backward congestion
  this->monitored_bitrate = mprtps_path_get_monitored_bitrate(this->path, &this->monitored_packets);
  if(_max_ramp_up(this) < this->monitored_bitrate){
      this->monitoring_interval = CONSTRAIN(_mon_min_int(this), _mon_max_int(this), this->monitoring_interval + 1);
      _set_monitoring_interval(this, this->monitoring_interval);
  }

  if(_bcongestion(this)){
    if(this->last_report_arrived < _now(this) - 2 * this->report_interval){
      goto done;
    }
    if(this->last_report_arrived_t1 < _now(this) - 2 * this->report_interval){
      goto done;
    }
    _bcongestion(this) = FALSE;
    goto done;
  }
  if(0 < this->disable_controlling && this->disable_controlling < _now(this)){
    goto done;
  }
  _bcongestion(this) = this->last_report_arrived < _now(this) - MAX(500 * GST_MSECOND, 3 * this->report_interval);

done:
  return;
}

void fbrasubctrler_signal_update(FBRASubController *this, MPRTPSubflowFECBasedRateAdaption *params)
{

}

void fbrasubctrler_signal_request(FBRASubController *this, MPRTPSubflowFECBasedRateAdaption *result)
{

}

void fbrasubctrler_report_update(
                         FBRASubController *this,
                         GstMPRTCPReportSummary *summary)
{
  if(!this->enabled){
    goto done;
  }
  if(summary->RR.processed && summary->RR.RTT){
    _priv(this)->rtt = summary->RR.RTT;
  }
  if(!summary->XR.processed){
    goto done;
  }
  this->last_report_arrived_t1 = this->last_report_arrived;
  this->last_report_arrived    = _now(this);
  this->report_interval        = this->last_report_arrived - this->last_report_arrived_t1;

  rmdi_processor_do(this->fb_processor, summary, &this->rmdi_result);
  _update_tr_corr(this);
  _reset_congestion_indicators(this);

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
    _priv(this)->event = EVENT_FI;
    _priv(this)->controlled = TRUE;
  }else{
    _priv(this)->controlled = FALSE;
  }

  _priv(this)->stage_t1 = _priv(this)->stage;

  ++this->measurements_num;
done:
  return;
}


static gint32 _undershoot(FBRASubController *this)
{
  gint32 target_rate = this->target_bitrate;
//  if(_gcong_th(this) < MIN(_q3(this),_q4(this))){
//    target_rate = MIN(_GP(this) * .85, _TR(this) * _rdc_target_fac(this));
//  }else{
//    target_rate = MIN(_GP(this) * .85, _TR(this) * _UF(this));
//  }

  if(_TR(this) < _SR(this) * .9){
    target_rate = MIN(_GP(this) * .85, target_rate);
  }else if(_gcong_th(this) < _gcong(this)){
    target_rate = MIN(_GP(this) * .85, _TR(this) * _rdc_target_fac(this));
  }else{
    target_rate = MIN(_GP(this) * .85, _TR(this) * _UF(this));
  }
  return target_rate;
}


static void
_reduce_stage_helper(
    FBRASubController *this)
{
  gboolean congestion = FALSE;
  gboolean distortion = FALSE;
  if(_corrH(this) < 1.2 && _UF(this) == 1.){
    goto done;
  }

  if(_gcong_th(this) < _gcong(this)){
    congestion = TRUE;
  }else if(_UF(this) < .8){
    congestion = TRUE;
  }else if(1.5 < _corrH(this) && _priv(this)->stage_changed < _now(this) - 5 * GST_SECOND){
    congestion = TRUE;
  }

  if(1.5 < _corrH(this)){
    distortion = TRUE;
  }else if(_gkeep_th(this) < _gkeep(this)){
    distortion = TRUE;
  }
done:
  _fcongestion(this) = congestion;
  _fdistortion(this) = distortion;
}

void
_reduce_stage(
    FBRASubController *this)
{
  gint32   target_rate  = this->target_bitrate;

  _reduce_stage_helper(this);

  if(!_fcongestion(this)){
    this->bottleneck_point = (_TR(this) + _GP(this)) / 2;
    if(!_fdistortion(this) && !_bcongestion(this)){
      _switch_stage_to(this, STAGE_KEEP, FALSE);
    }
    goto done;
  }

  target_rate = _undershoot(this);

  _change_target_bitrate(this, target_rate);
  _reset_monitoring(this);
  _disable_controlling(this);

done:
  return;
}


static void
_keep_stage_helper(
    FBRASubController *this)
{
  gboolean congestion = FALSE;
  gboolean distortion = FALSE;
  if(_corrH(this) < 1.2 && _UF(this) == 1.){
    goto done;
  }

  if(_gcong_th(this) < _gcong(this)){
    congestion = TRUE;
  }else if(_UF(this) < .8){
    congestion = TRUE;
  }

  if(_gkeep_th(this) < _gkeep(this)){
    distortion = TRUE;
  }else if(1.5 < _rmdi(this).corrH){
    distortion = TRUE;
  }else if(_UF(this) < 1.){
    distortion = TRUE;
  }
done:
  _fcongestion(this) = congestion;
  _fdistortion(this) = distortion;
}

void
_keep_stage(
    FBRASubController *this)
{
  gint32 target_rate = this->target_bitrate;

  _keep_stage_helper(this);

  if(_fcongestion(this)){
    _set_event(this, EVENT_CONGESTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    _disable_controlling(this);
    target_rate = _undershoot(this);
    goto done;
  }

//  target_rate *= 1.-_q4(this);
  if(_fdistortion(this)){
    this->bottleneck_point = target_rate;
    if(this->last_settled < _now(this) - _max_dist_keep(this) * GST_SECOND){
      target_rate *= _tr_is_corred(this) ? 1. : .9;
      this->last_settled = _now(this);
    }
    _set_event(this, EVENT_DISTORTION);
    goto done;
  }

  if(_state(this) != MPRTPS_PATH_STATE_STABLE){
    _set_event(this, EVENT_SETTLED);
    goto done;
  }

  if(_bcongestion(this) || _gprobe_th(this) < _gprobe(this)){
    goto done;
  }

  _setup_monitoring(this);
  _switch_stage_to(this, STAGE_PROBE, FALSE);
done:
  _change_target_bitrate(this, target_rate);
  return;
}

static void
_probe_stage_helper(
    FBRASubController *this)
{
  gboolean congestion = FALSE;
  gboolean distortion = FALSE;
  if(_corrH(this) < 1.2 && _UF(this) == 1.){
    goto done;
  }

  if(_gcong_th(this) < _gcong(this)){
    congestion = TRUE;
  }else if(_UF(this) < .8){
    congestion = TRUE;
  }

  if(_gprobe_th(this) < _gprobe(this)){
    distortion = TRUE;
  }else if(1.5 < _rmdi(this).corrH){
    distortion = TRUE;
  }else if(_UF(this) < 1.){
    distortion = TRUE;
  }
done:
  _fcongestion(this) = congestion;
  _fdistortion(this) = distortion;
}

void
_probe_stage(
    FBRASubController *this)
{
  gint32   target_rate = this->target_bitrate;
  gint32   probe_interval;

  _probe_stage_helper(this);

  probe_interval = _get_probe_interval(this);

  if(_bcongestion(this) || _fcongestion(this)){
    _disable_monitoring(this);
    _disable_controlling(this);
    _set_event(this, EVENT_CONGESTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    target_rate = _undershoot(this);
    goto done;
  }

  if(_fdistortion(this)){
    _disable_monitoring(this);
    _set_event(this, EVENT_DISTORTION);
    _switch_stage_to(this, STAGE_KEEP, FALSE);
    if(_now(this) - _inc_mit_th(this) * GST_SECOND < this->last_increase){
      target_rate *= .95;
    }
    goto done;
  }

  if(_now(this) - probe_interval * GST_SECOND < _priv(this)->stage_changed){
    goto exit;
  }

  if(_priv(this)->tr_correlated){
    target_rate = MIN(1.5 * _GP(this), target_rate + _get_increasement(this));
  }else if(target_rate < _SR(this) * .9){
//    target_rate = MIN(1.5 * _GP(this), target_rate + _get_increasement(this));
    goto exit;
  }

  _switch_stage_to(this, STAGE_INCREASE, FALSE);
  _set_event(this, EVENT_READY);
done:
  _change_target_bitrate(this, target_rate);
exit:
  return;
}

static void
_increase_stage_helper(
    FBRASubController *this)
{
  gboolean congestion = FALSE;
  gboolean distortion = FALSE;
  if(_corrH(this) < 1.2 && _UF(this) == 1.){
    goto done;
  }

  if(_gcong_th(this) < _gcong(this)){
    congestion = TRUE;
  }else if(_UF(this) < .8){
    congestion = TRUE;
  }

  if(_gprobe_th(this) < _gprobe(this)){
    distortion = TRUE;
  }else if(1.5 < _rmdi(this).corrH){
    distortion = TRUE;
  }else if(_UF(this) < 1.){
    distortion = TRUE;
  }
done:
  _fcongestion(this) = congestion;
  _fdistortion(this) = distortion;
}

void
_increase_stage(
    FBRASubController *this)
{
  gint32   target_rate = this->target_bitrate;

  _increase_stage_helper(this);

  if(_bcongestion(this) || _fcongestion(this)){
    _disable_monitoring(this);
    _disable_controlling(this);
    _set_event(this, EVENT_CONGESTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    target_rate = _undershoot(this);
    goto done;
  }

  if(_fdistortion(this)){
    _disable_monitoring(this);
    _set_event(this, EVENT_DISTORTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    target_rate = MAX(_GP(this) * .95, _TR_t1(this));
    goto done;
  }

  if(!_priv(this)->tr_correlated){
    goto exit;
  }

  if(_does_near_to_bottleneck_point(this)){
    _switch_stage_to(this, STAGE_PROBE, FALSE);
    _set_event(this, EVENT_READY);
  }else if(_now(this) - _norm_pbobe_int(this) * GST_SECOND < _priv(this)->stage_changed){
    goto exit;
  }else{
    target_rate = MIN(1.5 * _GP(this), target_rate + this->monitored_bitrate);
    _setup_monitoring(this);
    _switch_stage_to(this, STAGE_INCREASE, FALSE);
  }
done:
  _change_target_bitrate(this, target_rate);
exit:
  return;
}

void
_fire(
    FBRASubController *this,
    Event event)
{
  switch(_state(this)){
    case MPRTPS_PATH_STATE_OVERUSED:
      switch(event){
        case EVENT_CONGESTION:
        break;
        case EVENT_SETTLED:
          this->last_settled = _now(this);
          mprtps_path_set_non_congested(this->path);
          mprtps_path_set_state(this->path, MPRTPS_PATH_STATE_STABLE);
        break;
        case EVENT_FI:
        default:
        break;
      }
    break;
    case MPRTPS_PATH_STATE_STABLE:
      switch(event){
        case EVENT_CONGESTION:
          mprtps_path_set_congested(this->path);
          mprtps_path_set_state(this->path, MPRTPS_PATH_STATE_OVERUSED);
        break;
        case EVENT_DISTORTION:
          mprtps_path_set_state(this->path, MPRTPS_PATH_STATE_OVERUSED);
          break;
        case EVENT_READY:
          mprtps_path_set_state(this->path, MPRTPS_PATH_STATE_UNDERUSED);
          break;
        case EVENT_FI:
        default:
        break;
      }
    break;
    case MPRTPS_PATH_STATE_UNDERUSED:
      switch(event){
        case EVENT_CONGESTION:
          mprtps_path_set_congested(this->path);
          mprtps_path_set_state(this->path, MPRTPS_PATH_STATE_OVERUSED);
        break;
        case EVENT_DISTORTION:
          mprtps_path_set_state(this->path, MPRTPS_PATH_STATE_OVERUSED);
        break;
        case EVENT_READY:
          mprtps_path_set_state(this->path, MPRTPS_PATH_STATE_STABLE);
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
    FBRASubController *this,
    Stage target,
    gboolean execute)
{
  switch(target){
     case STAGE_KEEP:
       this->last_settled = _now(this);
       this->stage_fnc = _keep_stage;
     break;
     case STAGE_REDUCE:
       this->stage_fnc = _reduce_stage;
     break;
     case STAGE_INCREASE:
       this->stage_fnc = _increase_stage;
     break;
     case STAGE_PROBE:
       this->stage_fnc = _probe_stage;
     break;
   }
  _priv(this)->stage         = target;
  _priv(this)->stage_changed = _now(this);
  if(execute){
      this->stage_fnc(this);
  }
}


void _disable_controlling(FBRASubController *this)
{
  if(_priv(this)->rtt){
    this->disable_controlling = _now(this) +  CONSTRAIN(200 * GST_MSECOND, 1.5 * GST_SECOND, _priv(this)->rtt * 2.);
  }
  this->disable_controlling = _now(this) +  CONSTRAIN(500 * GST_MSECOND, 1.5 * GST_SECOND, _priv(this)->rtt * 2.);
}

void _reset_monitoring(FBRASubController *this)
{
  this->monitoring_interval = 0;
  this->monitoring_started = 0;
  this->monitored_bitrate = 0;
}

void _setup_monitoring(FBRASubController *this)
{
  guint interval;
  gdouble plus_rate = 0, scl = 0;

  if(0 < this->bottleneck_point){
    scl  = _TR(this) - this->bottleneck_point;
    scl /= this->bottleneck_point * _btl_eps(this);
    scl *= scl;
  }else{
    scl =  (gdouble) _priv(this)->min_target_bitrate;
    scl /= (gdouble) _TR(this);
    scl *= scl;
  }
  scl = CONSTRAIN(1./(gdouble)_mon_max_int(this), 1./(gdouble)_mon_min_int(this), scl);
  plus_rate = _TR(this) * scl;
  plus_rate = CONSTRAIN(_min_ramp_up(this), _max_ramp_up(this), plus_rate);
  interval = _calculate_monitoring_interval(this, plus_rate);
  _set_monitoring_interval(this, interval);
}

void _set_monitoring_interval(FBRASubController *this, guint interval)
{
  this->monitoring_interval = interval;
  this->monitoring_started = _now(this);
//  if(interval > 0)
//    this->monitored_bitrate = (gdouble)_TR(this) / (gdouble)interval;
//  else
//    this->monitored_bitrate = 0;
  this->monitored_bitrate = 0;
  mprtps_path_set_monitoring_interval(this->path, this->monitoring_interval);
  return;
}

guint _calculate_monitoring_interval(FBRASubController *this, guint32 desired_bitrate)
{
  gdouble actual, target, ratio;
  guint monitoring_interval = 0;
  if(desired_bitrate <= 0){
     goto exit;
   }
  actual = _TR(this);
  target = actual + (gdouble) desired_bitrate;
  ratio = target / actual;

  if(ratio > 1.49) monitoring_interval = 2;
  else if(ratio > 1.329) monitoring_interval = 3;
  else if(ratio > 1.249) monitoring_interval = 4;
  else if(ratio > 1.19) monitoring_interval = 5;
  else if(ratio > 1.159) monitoring_interval = 6;
  else if(ratio > 1.139) monitoring_interval = 7;
  else if(ratio > 1.119) monitoring_interval = 8;
  else if(ratio > 1.109) monitoring_interval = 9;
  else if(ratio > 1.09) monitoring_interval = 10;
  else if(ratio > 1.089) monitoring_interval = 11;
  else if(ratio > 1.079) monitoring_interval = 12;
  else if(ratio > 1.069) monitoring_interval = 13;
  else  monitoring_interval = 14;

exit:
  return monitoring_interval;

}

void _change_target_bitrate(FBRASubController *this, gint32 new_target)
{
  this->target_bitrate_t1 = this->target_bitrate;
  this->target_bitrate = new_target;
  if(0 < _priv(this)->min_target_bitrate){
    this->target_bitrate = MAX(_priv(this)->min_target_bitrate, this->target_bitrate);
  }
  if(0 < _priv(this)->max_target_bitrate){
    this->target_bitrate = MIN(_priv(this)->max_target_bitrate, this->target_bitrate);
  }
  if(this->target_bitrate < this->target_bitrate_t1){
    this->last_decrease = _now(this);
  }else if(this->target_bitrate_t1 < this->target_bitrate){
    this->last_increase = _now(this);
  }

  mprtps_path_set_target_bitrate(this->path, this->target_bitrate);
}



void fbrasubctrler_logging(FBRASubController *this)
{
  gchar filename[255];
  sprintf(filename, "fbractrler_%d.log", this->id);
  mprtp_logger(filename,
               "######## S%d | State: %-2d | Stage: %1d | Disable time %lu | Ctrled: %d ###############\n"
               "# Goodput:                     %-10d| Sending Rate:               %-10d#\n"
               "# Forward distortion:          %-10d| Backward distortion:        %-10d#\n"
               "# Forward congestion:          %-10d| Backward congestion:        %-10d#\n"
               "# Actual Target:               %-10d| tr_is_corred:               %-10d#\n"
               "# Minimum Bitrate:             %-10d| R(100):                     %-10f#\n"
               "# Maximum Bitrate:             %-10d| R(200):                     %-10f#\n"
               "# Bottleneck point:            %-10d| R(400):                     %-10f#\n"
               "# Monitoring Interval:         %-10d| R(800):                     %-10f#\n"
               "# Monitoring Bitrate:          %-10d| UF:                         %-10f#\n"
               "# CorrH:                       %-10f|                                  #\n"
               "###################### MSeconds since setup: %lu ######################\n"
               ,

               this->id,
               _state(this),
               _stage(this),
               this->disable_controlling ? GST_TIME_AS_MSECONDS(this->disable_controlling - _now(this)) : 0,
               _priv(this)->controlled,

               _GP(this),
               _SR(this),

               _fdistortion(this),
               _bdistortion(this),

               _fcongestion(this),
               _bcongestion(this),

               _TR(this),
               _tr_is_corred(this),

               _priv(this)->min_target_bitrate,
               _q1(this),

               _priv(this)->max_target_bitrate,
               _q2(this),

               _btlp(this),
               _q3(this),

               _mon_int(this),
               _q4(this),

               _mon_br(this),
               _UF(this),

               _corrH(this),

               GST_TIME_AS_MSECONDS(_now(this) - this->made)

               );

  _params_out(this);
}


void _params_out(FBRASubController *this)
{
  gchar filename[255];
  sprintf(filename, "ccparams_%d.log", this->id);
  mprtp_logger_rewrite(filename,
   "+-----------------------------------------------+-------------------+\n"
   "| Name                                          | Value             |\n"
   "+-----------------------------------------------+-------------------+\n"
   "|                                               |                   |\n"
   "| bottleneck_increasement_factor                | %-18.3f|\n"
   "|                                               |                   |\n"
   "| bottleneck_epsilon                            | %-18.3f|\n"
   "|                                               |                   |\n"
   "| bottleneck_monitoring_interval                | %-18d|\n"
   "|                                               |                   |\n"
   "| normal_probe_interval                         | %-18d|\n"
   "|                                               |                   |\n"
   "| bottleneck_probe_interval                     | %-18d|\n"
   "|                                               |                   |\n"
   "| increasement_mitigation_treshold              | %-18d|\n"
   "|                                               |                   |\n"
   "| qdelay_probe_treshold                         | %-18.3f|\n"
   "|                                               |                   |\n"
   "| qdelay_keep_treshold                          | %-18.3f|\n"
   "|                                               |                   |\n"
   "| qdelay_congestion_treshold                    | %-18.3f|\n"
   "|                                               |                   |\n"
   "| min_monitoring_interval                       | %-18d|\n"
   "|                                               |                   |\n"
   "| max_monitoring_interval                       | %-18d|\n"
   "|                                               |                   |\n"
   "| max_distortion_keep_time                      | %-18d|\n"
   "|                                               |                   |\n"
   "| min_ramp_up_bitrate                           | %-18d|\n"
   "|                                               |                   |\n"
   "| max_ramp_up_bitrate                           | %-18d|\n"
   "|                                               |                   |\n"
   "| min_target_bitrate                            | %-18d|\n"
   "|                                               |                   |\n"
   "| max_target_bitrate                            | %-18d|\n"
   "|                                               |                   |\n"
   "| reduce_target_factor                          | %-18.3f|\n"
   "+-----------------------------------------------+-------------------+\n",

   _priv(this)->bottleneck_increasement_factor   ,
   _priv(this)->bottleneck_epsilon               ,
   _priv(this)->bottleneck_monitoring_interval   ,
   _priv(this)->normal_probe_interval            ,
   _priv(this)->bottleneck_probe_interval        ,
   _priv(this)->increasement_mitigation_treshold ,
   _priv(this)->qdelay_probe_treshold            ,
   _priv(this)->qdelay_keep_treshold             ,
   _priv(this)->qdelay_congestion_treshold       ,
   _priv(this)->min_monitoring_interval          ,
   _priv(this)->max_monitoring_interval          ,
   _priv(this)->max_distortion_keep_time         ,
   _priv(this)->min_ramp_up_bitrate              ,
   _priv(this)->max_ramp_up_bitrate              ,
   _priv(this)->min_target_bitrate               ,
   _priv(this)->max_target_bitrate               ,
   _priv(this)->reduce_target_factor
   );
}

void fbrasubctrler_logging2csv(FBRASubController *this)
{
  gchar filename[255];
  THIS_READLOCK(this);

  sprintf(filename, "snd_%d_ratestat.csv", this->id);

  mprtp_logger(filename, "%d,%d,%d,%f,%d\n",
               this->target_bitrate / 1000,
               _rmdi(this).goodput_bitrate,
               (_rmdi(this).sender_bitrate + 48 * 8 * _rmdi(this).sent_packets) / 1000,
               _rmdi(this).utilized_fraction,
               (this->monitored_bitrate + 40 * 8 * this->monitored_packets) / 1000);

  THIS_READUNLOCK(this);
}
