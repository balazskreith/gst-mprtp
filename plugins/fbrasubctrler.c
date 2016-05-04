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
#define BOTTLENECK_EPSILON .15

//if target bitrate is close to the bottleneck, monitoring interval is requested for this interval
//note if the target/interval is higher than the maximum ramp up speed, then monitoring is
//restricted to the max_ramp_up
#define BOTTLENECK_MONITORING_INTERVAL 10

//determine the minimum interval in seconds must be stay in probe stage
//before the target considered to be accepted
#define NORMAL_PROBE_INTERVAL 0.2

//determine the maximum interval in seconds must be stay in probe stage
//before the target considered to be accepted
#define BOTTLENECK_PROBE_INTERVAL 0.5

//determines the minimum monitoring interval
#define MIN_MONITORING_INTERVAL 8

//determines the maximum monitoring interval
#define MAX_MONITORING_INTERVAL 14

//determines the maximum keep time in second before mitigation applied
//if qdelay is distorted in keep stage
#define MAX_DISTORTION_KEEP_TIME 1

//determines the minimum ramp up bitrate
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

//determines the treshold for utilization, in which below the path considered to be congested
#define DISCARD_CONGESTION_TRESHOLD 0.0

//determines a treshold for trend calculations, in which above the KEEP stage not let it to PROBE
#define PROBE_TREND_TRESHOLD 1.5

//determines a treshold for trend calculations, in which above the path considered to be distorted
#define DISTORTION_TREND_TRESHOLD 1.8

//determines a treshold for trend calculations, in which above the path considered to be congested
#define CONGESTION_TREND_TRESHOLD 2.0

//determines the qdelay trend treshold considered to be congestion
#define QDELAY_CONGESTION_TRESHOLD 250

//determines weather the pacing allowed or not
#define PACING_ALLOWED_DEFAULT FALSE

//determines the constrict time in s for pacing
#define PACING_CONSTRICT_TIME 0.0

//determines the deflate time in s for pacing
#define PACING_DEFLATE_TIME 5.0

//A factor used to multiply the remb at distortion
#define ALPHA 1.0

//A factor used to multiply the remb at congestion
#define BETHA 1.0;


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

typedef enum{
  PACING_STATE_ACTIVE           =  1,
  PACING_STATE_DEACTIVATED      =  0,
  PACING_STATE_DEACTIVE         = -1,
}RestrictorState;

struct _Private{
  GstClockTime        time;

  Event               event;
  Stage               stage;
  Stage               stage_t1;
  GstClockTime        stage_changed;
  gboolean            controlled;
  gdouble             rtt;
  gboolean            tr_correlated;
  gboolean            tr_approved;
  GstClockTime        tr_approved_time;
  gboolean            rr_approved;
  GstClockTime        rr_approved_time;
  gboolean            gp_correlated;

  gboolean            fcongestion;
  gboolean            bcongestion;
  gboolean            fdistortion;

  gdouble             reduce_trend;
  GstClockTime        reduce_started;

  PacketsSndTracker*  packetstracker;

  gboolean            pacing_allowed;
  gdouble             pacing_constrict_time;
  gdouble             pacing_deflate_time;
  struct{
    RestrictorState state;
    gboolean  (*approver)(FBRASubController*,GstBuffer*);
    GstClockTime deactivated, activated;
    GstClockTime last_sent;
    guint        group_size;
    guint32      last_approved_ts;
    gboolean     slack_allowed;
  }restrictor;

  //Possible parameters
  gint32              min_monitoring_interval;
  gint32              max_monitoring_interval;
  gdouble             bottleneck_epsilon;
  gdouble             normal_monitoring_interval;
  gdouble             bottleneck_monitoring_interval;
  gint32              max_distortion_keep_time;
  gdouble             bottleneck_increasement_factor;
  gint32              min_ramp_up_bitrate;
  gint32              max_ramp_up_bitrate;
  gint32              min_target_bitrate;
  gint32              max_target_bitrate;
  gdouble             reduce_target_factor;

  GstClockTime        qdelay_congestion_treshold;
  gdouble             discad_cong_treshold;

  gdouble             distorted_trend_th;
  gdouble             probe_trend_th;
  gdouble             congestion_trend_th;


};

#define _priv(this) ((Private*)this->priv)
#define _rmdi(this) this->rmdi_result
#define _pacer(this) _priv(this)->restrictor

#define _fcongestion(this) _priv(this)->fcongestion
#define _bcongestion(this) _priv(this)->bcongestion
#define _fdistortion(this) _priv(this)->fdistortion
#define _tr_is_corred(this) _priv(this)->tr_correlated
#define _btlp(this) this->bottleneck_point
#define _mon_int(this) this->monitoring_interval
#define _mon_br(this) this->monitored_bitrate
#define _UF(this) _rmdi(this).utilized_fraction
#define _qdelay_actual(this) _rmdi(this).qdelay_actual
#define _qdelay_median(this) _rmdi(this).qdelay_median
#define _qdelay_processed(this) _rmdi(this).owd_processed
#define _rdiscards(this) _rmdi(this).recent_discards


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
#define _trend(this) _rmdi(this).owd_corr
#define _remb(this) _rmdi(this).rcv_est_max_bitrate


#define _dist_trend_th(this)          _priv(this)->distorted_trend_th
#define _cng_trend_th(this)           _priv(this)->congestion_trend_th
#define _probe_trend_th(this)         _priv(this)->probe_trend_th

#define _btl_inc_fac(this)            _priv(this)->bottleneck_increasement_factor
#define _btl_eps(this)                _priv(this)->bottleneck_epsilon
#define _btl_mon_int(this)            _priv(this)->bottleneck_monitoring_interval
#define _norm_mon_int(this)           _priv(this)->normal_monitoring_interval

#define _qdelay_cong_th(this)         _priv(this)->qdelay_congestion_treshold
#define _discard_cong_th(this)        _priv(this)->discad_cong_treshold

#define _mon_min_int(this)            _priv(this)->min_monitoring_interval
#define _mon_max_int(this)            _priv(this)->max_monitoring_interval
#define _max_dist_keep(this)          _priv(this)->max_distortion_keep_time
#define _min_ramp_up(this)            _priv(this)->min_ramp_up_bitrate
#define _max_ramp_up(this)            _priv(this)->max_ramp_up_bitrate
#define _rdc_target_fac(this)         _priv(this)->reduce_target_factor
#define _min_target(this)             _priv(this)->min_target_bitrate
#define _max_target(this)             _priv(this)->max_target_bitrate


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
_activate_pacer(
    FBRASubController *this,
    gboolean slack_allowed);

static void
_deactivate_pacer(
    FBRASubController *this);


static gboolean
_active_pacing_mode(
    FBRASubController *this,
    GstBuffer *buffer);

static gboolean
_deactivated_pacing_mode(
    FBRASubController *this,
    GstBuffer *buffer);

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
  _priv(this)->normal_monitoring_interval       = NORMAL_PROBE_INTERVAL;
  _priv(this)->bottleneck_monitoring_interval   = BOTTLENECK_PROBE_INTERVAL;
  _priv(this)->qdelay_congestion_treshold       = QDELAY_CONGESTION_TRESHOLD * GST_MSECOND;
  _priv(this)->min_monitoring_interval          = MIN_MONITORING_INTERVAL;
  _priv(this)->max_monitoring_interval          = MAX_MONITORING_INTERVAL;
  _priv(this)->max_distortion_keep_time         = MAX_DISTORTION_KEEP_TIME;
  _priv(this)->min_ramp_up_bitrate              = RAMP_UP_MIN_SPEED;
  _priv(this)->max_ramp_up_bitrate              = RAMP_UP_MAX_SPEED;
  _priv(this)->min_target_bitrate               = TARGET_BITRATE_MIN;
  _priv(this)->max_target_bitrate               = TARGET_BITRATE_MAX;
  _priv(this)->reduce_target_factor             = REDUCE_TARGET_FACTOR;
  _priv(this)->discad_cong_treshold             = DISCARD_CONGESTION_TRESHOLD;
  _priv(this)->distorted_trend_th               = DISTORTION_TREND_TRESHOLD;
  _priv(this)->congestion_trend_th              = CONGESTION_TREND_TRESHOLD;
  _priv(this)->probe_trend_th                   = PROBE_TREND_TRESHOLD;

  _priv(this)->pacing_allowed                   = PACING_ALLOWED_DEFAULT;
  _priv(this)->pacing_constrict_time            = PACING_CONSTRICT_TIME;
  _priv(this)->pacing_deflate_time              = PACING_DEFLATE_TIME;

  _priv(this)->tr_approved                      = TRUE;

  _pacer(this).approver = _deactivated_pacing_mode;
  _pacer(this).state    = PACING_STATE_DEACTIVE;

}


gboolean fbrasubctrler_path_approver(gpointer data, GstBuffer *buffer)
{
  FBRASubController *this = data;
  return _pacer(this).approver(this, buffer);
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

  _priv(result)->packetstracker         = mprtps_path_ref_packetstracker(result->path);

  return result;
}

void fbrasubctrler_enable(FBRASubController *this)
{
  this->enabled             = TRUE;
  this->disable_controlling = TRUE;
  this->disable_interval    = 10 * GST_SECOND;
  this->target_bitrate      = mprtps_path_get_target_bitrate(this->path);
  this->last_distorted      = _now(this);
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
  _fcongestion(this) = FALSE;

  if(_cng_trend_th(this) < _trend(this)){
    _fcongestion(this) = TRUE;
  }

  if(_qdelay_median(this) + _qdelay_cong_th(this) < _qdelay_actual(this)){
    _fcongestion(this) = TRUE;
  }

  if(_UF(this) < _discard_cong_th(this)){
    _fcongestion(this) = TRUE;
  }

}

static void _update_tr_corr(FBRASubController *this)
{
  gdouble tr_corr;
  gdouble gp_corr;

  tr_corr =  (gdouble) _rmdi(this).sender_bitrate / (gdouble)this->target_bitrate;
  gp_corr =  (gdouble) _rmdi(this).goodput_bitrate / (gdouble) _rmdi(this).sender_bitrate;
  _priv(this)->gp_correlated = .9 < gp_corr && gp_corr < 1.1;

  if(.9 < tr_corr && tr_corr < 1.1){
    _priv(this)->tr_correlated = TRUE;
  }else{
    _priv(this)->tr_correlated = FALSE;
    this->last_tr_uncorr_point = _now(this);
  }
}


static GstClockTime _get_probe_interval(FBRASubController *this)
{
  if(!this->bottleneck_point){
    return _norm_mon_int(this) * GST_SECOND;
  }
  if(this->target_bitrate < this->bottleneck_point * (1.-_btl_eps(this))){
    return _norm_mon_int(this) * GST_SECOND;
  }
  if(this->bottleneck_point * (1.+_btl_eps(this)) < this->target_bitrate){
    return _norm_mon_int(this) * GST_SECOND;
  }
  return _btl_mon_int(this) * GST_SECOND;
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
  gint32 increasement;

  if(_does_near_to_bottleneck_point(this)){
      increasement = this->monitored_bitrate * _btl_inc_fac(this);
  }else{
    increasement = this->monitored_bitrate;
  }
  return CONSTRAIN(_min_ramp_up(this), _max_ramp_up(this), increasement);
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
  //Todo: backward congestion detect here
//  _bcongestion(this) = this->last_report_arrived < _now(this) - MAX(500 * GST_MSECOND, 3 * this->report_interval);

done:
  return;
}

void fbrasubctrler_signal_update(FBRASubController *this, MPRTPSubflowFECBasedRateAdaption *params)
{
  MPRTPSubflowFBRA2CngCtrlerParams *cngctrler;
  cngctrler = &params->cngctrler;

  _priv(this)->bottleneck_increasement_factor   = cngctrler->bottleneck_increasement_factor;
  _priv(this)->bottleneck_epsilon               = cngctrler->bottleneck_epsilon;
  _priv(this)->normal_monitoring_interval       = cngctrler->normal_monitoring_interval;
  _priv(this)->bottleneck_monitoring_interval   = cngctrler->bottleneck_monitoring_interval;
  _priv(this)->qdelay_congestion_treshold       = cngctrler->qdelay_congestion_treshold;
  _priv(this)->min_monitoring_interval          = cngctrler->min_monitoring_interval;
  _priv(this)->max_monitoring_interval          = cngctrler->max_monitoring_interval;
  _priv(this)->max_distortion_keep_time         = cngctrler->max_distortion_keep_time;
  _priv(this)->min_ramp_up_bitrate              = cngctrler->min_ramp_up_bitrate;
  _priv(this)->max_ramp_up_bitrate              = cngctrler->max_ramp_up_bitrate;
  _priv(this)->min_target_bitrate               = cngctrler->min_target_bitrate;
  _priv(this)->max_target_bitrate               = cngctrler->max_target_bitrate;
  _priv(this)->reduce_target_factor             = cngctrler->reduce_target_factor;
  _priv(this)->discad_cong_treshold             = cngctrler->discad_cong_treshold;
  _priv(this)->distorted_trend_th               = cngctrler->distorted_trend_th;
  _priv(this)->probe_trend_th                   = cngctrler->keep_trend_th;
  _priv(this)->pacing_allowed                   = cngctrler->pacing_allowed;
  _priv(this)->pacing_deflate_time              = cngctrler->pacing_deflate_time;
  _priv(this)->pacing_constrict_time            = cngctrler->pacing_constrict_time;

}

void fbrasubctrler_signal_request(FBRASubController *this, MPRTPSubflowFECBasedRateAdaption *result)
{
  MPRTPSubflowFBRA2CngCtrlerParams *cngctrler;
  cngctrler = &result->cngctrler;

  cngctrler->min_target_bitrate             = _min_target(this);
  cngctrler->max_target_bitrate             = _max_target(this);
  cngctrler->bottleneck_epsilon             = _btl_eps(this);
  cngctrler->normal_monitoring_interval     = _norm_mon_int(this);
  cngctrler->bottleneck_monitoring_interval = _btl_mon_int(this);
  cngctrler->max_distortion_keep_time       = _max_dist_keep(this);
  cngctrler->bottleneck_increasement_factor = _btl_inc_fac(this);
  cngctrler->min_ramp_up_bitrate            = _min_ramp_up(this);
  cngctrler->max_ramp_up_bitrate            = _max_ramp_up(this);
  cngctrler->max_monitoring_interval        = _mon_max_int(this);
  cngctrler->min_monitoring_interval        = _mon_min_int(this);
  cngctrler->reduce_target_factor           = _rdc_target_fac(this);

  cngctrler->qdelay_congestion_treshold     = _qdelay_cong_th(this);
  cngctrler->discad_cong_treshold           = _discard_cong_th(this);

  cngctrler->distorted_trend_th             = _dist_trend_th(this);
  cngctrler->keep_trend_th                  = _probe_trend_th(this);

  cngctrler->pacing_allowed                 = _priv(this)->pacing_allowed;
  cngctrler->pacing_deflate_time            = _priv(this)->pacing_deflate_time;
  cngctrler->pacing_constrict_time          = _priv(this)->pacing_constrict_time;

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
    rmdi_processor_set_rtt(this->fb_processor, _priv(this)->rtt);
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

  if(this->disable_controlling){
    if(!this->disable_end || !_priv(this)->tr_approved){
      this->disable_end = _now(this) + this->disable_interval;
    }
    this->disable_controlling = _now(this) < this->disable_end;
  }

  if(5 < this->measurements_num && !this->disable_controlling){
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

//  _priv(this)->stage_t1 = _priv(this)->stage;
  if(_priv(this)->stage != STAGE_REDUCE){
//      rmdi_processor_approve_owd(this->fb_processor);
  }
  rmdi_processor_approve_owd(this->fb_processor);
  ++this->measurements_num;
done:
  return;
}

static gint32
_get_reduced_target(FBRASubController *this)
{
  gint32 target_rate = this->target_bitrate;

  if(_bcongestion(this)){
    goto done;
  }

//  target_rate = MIN(_TR(this), _remb(this) * CONSTRAIN(.6, .95, 2.-_trend(this)/2.));
  this->bottleneck_point = target_rate = MIN(_TR(this), _remb(this) );
  _priv(this)->rr_approved = FALSE;
  _priv(this)->reduce_trend = _trend(this);
  _priv(this)->reduce_started = _now(this);

done:
  return target_rate;
}

void
_reduce_stage(
    FBRASubController *this)
{
  gint32   target_rate = this->target_bitrate;

  if(!_priv(this)->rr_approved){
    _priv(this)->rr_approved = target_rate * .95 < _SR(this) && _SR(this) < target_rate * 1.05;
    if(_priv(this)->rr_approved){
      _priv(this)->rr_approved_time = _now(this);
    }
  }

  if(_priv(this)->reduce_trend * 1.5 < _trend(this) && _priv(this)->rr_approved){
    this->bottleneck_point =  target_rate = _get_reduced_target(this);
    goto done;
  }else{
    this->bottleneck_point =  target_rate = MIN(_TR(this), _remb(this));
  }

  if(_dist_trend_th(this) < _trend(this)){
    target_rate = MIN(_TR(this), _remb(this));
    goto done;
  }


  if(_now(this) - 2 * _priv(this)->rtt < _priv(this)->rr_approved_time){
    goto done;
  }

  _set_event(this, EVENT_SETTLED);
  _switch_stage_to(this, STAGE_KEEP, FALSE);
  _reset_monitoring(this);
done:
  _change_target_bitrate(this, target_rate);
  return;
}

void
_keep_stage(
    FBRASubController *this)
{
  gint32   target_rate = this->target_bitrate;

  if(_bcongestion(this) || _fcongestion(this)){
    _set_event(this, EVENT_CONGESTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    target_rate = _get_reduced_target(this) * BETHA;
    goto done;
  }

  if(_dist_trend_th(this)  < _trend(this)){
    _set_event(this, EVENT_DISTORTION);
    target_rate = _get_reduced_target(this) * ALPHA;
    goto done;
  }else if(_state(this) != MPRTPS_PATH_STATE_STABLE){
    _set_event(this, EVENT_SETTLED);
    goto done;
  }

  if(_probe_trend_th(this)  < _trend(this) || _pacer(this).state != PACING_STATE_DEACTIVE){
    this->last_distorted = _now(this);
    goto done;
  }

  if(_now(this) - MIN(1 * _priv(this)->rtt, 1.5 * GST_SECOND) < this->last_distorted){
    goto done;
  }

  _setup_monitoring(this);
  _switch_stage_to(this, STAGE_PROBE, FALSE);
  this->rand_factor =  g_random_double_range(1.0, 2.0);
done:
  _change_target_bitrate(this, target_rate);
  return;
}

static gboolean _check_monitoring(FBRASubController *this)
{
  GstClockTime wait_time;
  wait_time = _get_probe_interval(this) * this->rand_factor;
  if(_now(this) - wait_time < this->monitoring_started){
    return FALSE;
  }
  return TRUE;
}

static gint32
_get_next_target(FBRASubController *this)
{
  gint32 target_rate = this->target_bitrate;

//  target_rate = MIN(_remb(this), _SR(this)) + _get_increasement(this);
  target_rate += _get_increasement(this);
  _priv(this)->tr_approved = FALSE;
  _priv(this)->rr_approved = FALSE;

  return target_rate;
}

void
_probe_stage(
    FBRASubController *this)
{
  gint32   target_rate = this->target_bitrate;

  if(_bcongestion(this) || _fcongestion(this)){
    _disable_monitoring(this);
    _set_event(this, EVENT_CONGESTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    target_rate = _get_reduced_target(this) * BETHA;
    goto done;
  }
  if(_dist_trend_th(this) < _trend(this)){
    _disable_monitoring(this);
    _set_event(this, EVENT_DISTORTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    target_rate = _get_reduced_target(this) * ALPHA;
    goto done;
  }

  if(_probe_trend_th(this) < _trend(this)){
    _disable_monitoring(this);
    _set_event(this, EVENT_SETTLED);
    _switch_stage_to(this, STAGE_KEEP, FALSE);
    target_rate = _TR(this) * 1.0;
    goto done;
  }

  if(!_check_monitoring(this)){
    goto done;
  }

  target_rate = _get_next_target(this);

  _switch_stage_to(this, STAGE_INCREASE, FALSE);
  _set_event(this, EVENT_READY);
done:
  _change_target_bitrate(this, target_rate);
  return;
}

void
_increase_stage(
    FBRASubController *this)
{
  gint32 target_rate = this->target_bitrate;

  if(_bcongestion(this) || _fcongestion(this)){
    _disable_monitoring(this);
    _set_event(this, EVENT_CONGESTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    target_rate = _get_reduced_target(this) * BETHA;
    goto done;
  }

  if(_dist_trend_th(this) < _trend(this)){
    _disable_monitoring(this);
    _set_event(this, EVENT_DISTORTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    target_rate = _get_reduced_target(this) * ALPHA;
    goto done;
  }

  if(!_priv(this)->tr_approved){
    _priv(this)->tr_approved = target_rate * .95 < _SR(this) && _SR(this) < target_rate * 1.05;
    if(_priv(this)->tr_approved){
      _priv(this)->tr_approved_time = _now(this);
    }
    goto done;
  }

  if(_min_target(this) * 10 < _TR(this) && !_priv(this)->rr_approved){
    _priv(this)->rr_approved = target_rate * .9 < _remb(this) && _remb(this) < target_rate * 1.1;
    if(_priv(this)->rr_approved){
      _priv(this)->rr_approved_time = _now(this);
    }
    goto done;
  }

  if(_min_target(this) * 5 < _TR(this) && _now(this) -  this->rand_factor * _priv(this)->rtt < _priv(this)->tr_approved_time){
    goto done;
  }

  if(_min_target(this) * 10 < _TR(this) && _now(this) -  this->rand_factor * _priv(this)->rtt < _priv(this)->rr_approved_time){
    goto done;
  }

  _switch_stage_to(this, STAGE_PROBE, FALSE);
  this->rand_factor =  g_random_double_range(1.0, 2.0);
  this->monitoring_started = _now(this);
done:
  _change_target_bitrate(this, target_rate);
  DISABLE_LINE _disable_controlling(this);
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
        _activate_pacer(this, FALSE);
        break;
        case EVENT_SETTLED:
          _deactivate_pacer(this);
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
          _activate_pacer(this, FALSE);
          mprtps_path_set_congested(this->path);
          mprtps_path_set_state(this->path, MPRTPS_PATH_STATE_OVERUSED);
        break;
        case EVENT_DISTORTION:
          _activate_pacer(this, TRUE);
          mprtps_path_set_state(this->path, MPRTPS_PATH_STATE_OVERUSED);
          break;
        case EVENT_READY:
          mprtps_path_set_state(this->path, MPRTPS_PATH_STATE_UNDERUSED);
          break;
        case EVENT_SETTLED:
        case EVENT_FI:
        default:
        break;
      }
    break;
    case MPRTPS_PATH_STATE_UNDERUSED:
      switch(event){
        case EVENT_CONGESTION:
          _activate_pacer(this, FALSE);
          mprtps_path_set_congested(this->path);
          mprtps_path_set_state(this->path, MPRTPS_PATH_STATE_OVERUSED);
        break;
        case EVENT_DISTORTION:
          _activate_pacer(this, TRUE);
          mprtps_path_set_state(this->path, MPRTPS_PATH_STATE_OVERUSED);
        break;
        case EVENT_SETTLED:
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
  this->disable_controlling = TRUE;
  if(0. < _priv(this)->rtt){
    this->disable_interval = CONSTRAIN(300 * GST_MSECOND, 2 * GST_SECOND, _priv(this)->rtt * 2.);
  }else{
    this->disable_interval = 1.5 * GST_SECOND;
  }
  this->disable_end = 0;
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
  if(this->target_bitrate == new_target){
    goto done;
  }
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
done:
  mprtps_path_set_target_bitrate(this->path, this->target_bitrate);
}

void _activate_pacer(FBRASubController *this, gboolean slack_allowed)
{
  if(!_priv(this)->pacing_allowed){
    _pacer(this).approver  = _deactivated_pacing_mode;
    return;
  }
  if(_pacer(this).state == PACING_STATE_ACTIVE){
    return;
  }
  _pacer(this).state         = PACING_STATE_ACTIVE;
  _pacer(this).activated     = _now(this);
  _pacer(this).approver      = _active_pacing_mode;
  _pacer(this).slack_allowed = slack_allowed;
}

void _deactivate_pacer(FBRASubController *this)
{
  if(!_priv(this)->pacing_allowed){
    _pacer(this).approver  = _deactivated_pacing_mode;
    return;
  }
  if(_pacer(this).state != PACING_STATE_ACTIVE){
    return;
  }
  _pacer(this).deactivated = _now(this);
  _pacer(this).state       = PACING_STATE_DEACTIVATED;
}

gboolean _active_pacing_mode(FBRASubController *this, GstBuffer *buffer)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  gdouble pacing_sec;
  GstClockTime pacing;
  gdouble dt;
  gdouble slack = 1.0;

  if(_pacer(this).state == PACING_STATE_ACTIVE){
    dt = GST_TIME_AS_MSECONDS(_now(this) - _pacer(this).activated);
    if(_pacer(this).slack_allowed && 0. < _priv(this)->pacing_constrict_time){
      slack = 2.1 - MIN(dt/(_priv(this)->pacing_constrict_time * 1000.), 1.0);
    }else{
      slack = 1.0;
    }
  }else if(_pacer(this).state == PACING_STATE_DEACTIVATED){
    dt = GST_TIME_AS_MSECONDS(_now(this) - _pacer(this).deactivated);
    if(_pacer(this).slack_allowed && 0. < _priv(this)->pacing_deflate_time){
      slack = 1.0 + MIN(dt/(_priv(this)->pacing_deflate_time * 1000.), 1.0);
    }else{
      slack = 1.0;
    }
    if(!_priv(this)->pacing_deflate_time || _priv(this)->pacing_deflate_time < dt){
      _pacer(this).approver  = _deactivated_pacing_mode;
      _pacer(this).state = PACING_STATE_DEACTIVE;
    }
  }
  if(!gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp)){
    return TRUE;
  }
  if(_pacer(this).last_approved_ts == gst_rtp_buffer_get_timestamp(&rtp)){
    goto send;
  }
  pacing_sec = (gdouble) _pacer(this).group_size / (gdouble) (_TR(this) * slack);
  pacing = GST_SECOND * pacing_sec;
//  g_print("group_size: %u tr: %d pacing: %lu\n", _pacer(this).group_size, _TR(this), pacing);
    if(0 && _now(this) - _pacer(this).last_sent < pacing){
      goto exit;
    }
//    g_print("bytes_in_flight: %u max_bytes_in_flight: %f\n",
//            _rmdi(this).bytes_in_flight,
//            _rmdi(this).max_bytes_in_flight * .8);
//      if(_rmdi(this).max_bytes_in_flight < _rmdi(this).bytes_in_flight * .8){
//        goto exit;
//      }
//    g_print("TR: %f SR: %d\n", _TR(this) * 1.2, _SR(this));
//  if(_TR(this) * 1.2 < _SR(this)){
//    goto exit;
//  }
  _pacer(this).group_size = 0;
  _pacer(this).last_approved_ts = gst_rtp_buffer_get_timestamp(&rtp);
send:
  _pacer(this).group_size += gst_rtp_buffer_get_payload_len(&rtp) * 8;
  _pacer(this).last_sent = _now(this);
  gst_rtp_buffer_unmap(&rtp);
  return TRUE;
exit:
  gst_rtp_buffer_unmap(&rtp);
  return FALSE;
}

gboolean _deactivated_pacing_mode(FBRASubController *this, GstBuffer *buffer)
{
  return TRUE;
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
               "# Minimum Bitrate:             %-10d| OWD Median:                 %-10lu#\n"
               "# Maximum Bitrate:             %-10d| OWD Actual:                 %-10lu#\n"
               "# Bottleneck point:            %-10d| OWD Corr:                   %-10f#\n"
               "# Monitoring Interval:         %-10d| tr_is_approved:             %-10d#\n"
               "# Monitoring Bitrate:          %-10d| rr_is_approved:             %-10d#\n"
               "# Delay avg:              %-10.3f| REMB:                       %-10d#\n"
               "###################### MSeconds since setup: %lu ######################\n"
               ,

               this->id,
               _state(this),
               _stage(this),
               this->disable_controlling ? GST_TIME_AS_MSECONDS(this->disable_end - _now(this)) : 0,
               _priv(this)->controlled,

               _GP(this),
               _SR(this),

               _fdistortion(this),
               0,

               _fcongestion(this),
               _bcongestion(this),

               _TR(this),
               _tr_is_corred(this),

               _priv(this)->min_target_bitrate,
               _qdelay_median(this),

               _priv(this)->max_target_bitrate,
               _qdelay_actual(this),

               _btlp(this),
               _rmdi(this).owd_corr,

               _mon_int(this),
               _priv(this)->tr_approved,

               _mon_br(this),
               _priv(this)->rr_approved,

               _rmdi(this).delay_avg,
               _remb(this),

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
   "| normal_monitoring_interval                    | %-18.3f|\n"
   "|                                               |                   |\n"
   "| bottleneck_monitoring_interval                | %-18.3f|\n"
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
   _priv(this)->normal_monitoring_interval       ,
   _priv(this)->bottleneck_monitoring_interval   ,
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

  mprtp_logger(filename, "%d,%d,%d,%f,%d,%ld\n",
               this->target_bitrate / 1000,
               _rmdi(this).goodput_bitrate,
               (_rmdi(this).sender_bitrate + 48 * 8 * _rmdi(this).sent_packets) / 1000,
               _rmdi(this).utilized_fraction,
               (this->monitored_bitrate + 40 * 8 * this->monitored_packets) / 1000,
               g_get_real_time () / (1000 * 100)
               );

  THIS_READUNLOCK(this);
}
