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
#define RESTRICTIVITY_FACTOR 0.5

//determine the minimum interval in seconds must be stay in probe stage
//before the target considered to be accepted
#define MIN_APPROVE_INTERVAL 1.0

//determine the maximum interval in seconds must be stay in probe stage
//before the target considered to be accepted
#define MAX_APPROVE_INTERVAL 3.0

//determines the minimum monitoring interval
#define MIN_MONITORING_INTERVAL 2

//determines the maximum monitoring interval
#define MAX_MONITORING_INTERVAL 14

//determines the minimum ramp up bitrate
#define RAMP_UP_MIN_SPEED 50000

//determines the maximum ramp up bitrate
#define RAMP_UP_MAX_SPEED 150000

//Min target_bitrate [bps]
//#define TARGET_BITRATE_MIN 500000
#define TARGET_BITRATE_MIN (SUBFLOW_DEFAULT_SENDING_RATE>>1)

//Max target_bitrate [bps] - 0 means infinity
#define TARGET_BITRATE_MAX 0

//determines the treshold for utilization, in which below the path considered to be congested
#define DISCARD_CONGESTION_TRESHOLD 0.25

//determines the treshold for utilization, in which below the path considered to be distorted
#define DISCARD_DISTORTION_TRESHOLD 0.0

//determines a treshold for trend calculations, in which above the KEEP stage not let it to PROBE
#define OWD_CORR_DISTORTION_TRESHOLD 1.75

//determines a treshold for trend calculations, in which above the path considered to be congested
#define OWD_CORR_CONGESTION_TRESHOLD 2.0

//determines weather the pacing allowed or not
#define THROTTLING_ALLOWED FALSE

//approvement epsilon
#define APPROVEMENT_EPSILON 0.25

//Stability treshold
#define STABILITY_TRESHOLD 0.5



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
  gboolean            tr_approved;
  GstClockTime        tr_approved_time;
  gboolean            gp_approved;
  GstClockTime        gp_approved_time;
  gboolean            tr_gp_approved;
  GstClockTime        tr_gp_approve_started;

  GstClockTime        adjust_th;

  gdouble             discarded_rate;
  gdouble             avg_rtp_payload;
  gdouble             rtt;

  gboolean            bcongestion;

  gdouble             reduce_trend;
  GstClockTime        reduce_started;

  gint32              corrigate_num;

  //Possible parameters
  gint32              min_monitoring_interval;
  gint32              max_monitoring_interval;
  gdouble             min_approve_interval;
  gdouble             max_approve_interval;
  gdouble             restrictivity_factor;
  gint32              min_ramp_up_bitrate;
  gint32              max_ramp_up_bitrate;
  gint32              min_target_bitrate;
  gint32              max_target_bitrate;
  gdouble             approvement_epsilon;
  gdouble             stability_treshold;

  gdouble             discad_cong_treshold;
  gdouble             discad_dist_treshold;

  gdouble             owd_corr_dist_th;
  gdouble             owd_corr_cng_th;

};

#define _priv(this) ((Private*)this->priv)
#define _fbstat(this) this->fbstat
#define _throttler(this) _priv(this)->throttler

#define _bcongestion(this) _priv(this)->bcongestion
#define _btlp(this) this->bottleneck_point
#define _mon_int(this) this->monitoring_interval
#define _mon_br(this) this->monitored_bitrate
#define _owd_stt_median(this) _fbstat(this).owd_stt_median
#define _owd_ltt_median(this) _fbstat(this).owd_ltt_median
#define _qdelay_processed(this) _fbstat(this).owd_processed
#define _rdiscards(this) _fbstat(this).recent_discards
#define _RTT(this) (_priv(this)->rtt == 0. ? (gdouble)GST_SECOND : _priv(this)->rtt)
#define _FD(this) _priv(this)->discarded_rate

#define _state(this) mprtps_path_get_state(this->path)
#define _state_t1(this) this->state_t1
#define _stage(this) _priv(this)->stage
#define _stage_t1(this) _priv(this)->stage_t1
#define _event(this) _priv(this)->event
#define _set_event(this, e) _event(this) = e
#define _set_pending_event(this, e) this->pending_event = e

#define _stability_th(this) _priv(this)->stability_treshold

#define _TR(this) this->target_bitrate
#define _TR_t1(this) this->target_bitrate_t1
#define _SR(this) (_fbstat(this).sent_bytes_in_1s * 8)
#define _GP(this) (_fbstat(this).goodput_bytes * 8)
#define _GP_t1(this) (_priv(this)->goodput_bitrate_t1)
#define _min_br(this) MIN(_SR(this), _TR(this))
#define _max_br(this) MAX(_SR(this), _TR(this))
#define _owd_corr(this) _fbstat(this).owd_corr
#define _owd_stability(this) _fbstat(this).stability

#define _owd_corr_cng_th(this)        _priv(this)->owd_corr_cng_th
#define _owd_corr_dist_th(this)       _priv(this)->owd_corr_dist_th

#define _appr_eps(this)               _priv(this)->approvement_epsilon

#define _FD_cong_th(this)             _priv(this)->discad_cong_treshold
#define _FD_dist_th(this)             _priv(this)->discad_dist_treshold

#define _min_appr_int(this)           _priv(this)->min_approve_interval
#define _max_appr_int(this)           _priv(this)->max_approve_interval
#define _mon_min_int(this)            _priv(this)->min_monitoring_interval
#define _mon_max_int(this)            _priv(this)->max_monitoring_interval
#define _min_ramp_up(this)            _priv(this)->min_ramp_up_bitrate
#define _max_ramp_up(this)            _priv(this)->max_ramp_up_bitrate
#define _min_target(this)             _priv(this)->min_target_bitrate
#define _max_target(this)             _priv(this)->max_target_bitrate
#define _restrict_fac(this)           _priv(this)->restrictivity_factor


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
_start_increasemet(
    FBRASubController *this,
    gint32 *increased_target_rate);

#define _disable_monitoring(this) _set_monitoring_interval(this, 0)

static void
_reset_monitoring(
    FBRASubController *this);

static void
_start_monitoring(
    FBRASubController *this);

static void
_set_monitoring_interval(
    FBRASubController *this,
    guint interval);

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
  g_object_unref(this->fbprocessor);
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
  _priv(this)->adjust_th = 20 * GST_MSECOND;

  _priv(this)->min_monitoring_interval          = MIN_MONITORING_INTERVAL;
  _priv(this)->max_monitoring_interval          = MAX_MONITORING_INTERVAL;

  _priv(this)->restrictivity_factor             = RESTRICTIVITY_FACTOR;
  _priv(this)->min_approve_interval             = MIN_APPROVE_INTERVAL;
  _priv(this)->max_approve_interval             = MAX_APPROVE_INTERVAL;
  _priv(this)->min_ramp_up_bitrate              = RAMP_UP_MIN_SPEED;
  _priv(this)->max_ramp_up_bitrate              = RAMP_UP_MAX_SPEED;
  _priv(this)->min_target_bitrate               = TARGET_BITRATE_MIN;
  _priv(this)->max_target_bitrate               = TARGET_BITRATE_MAX;
  _priv(this)->discad_cong_treshold             = DISCARD_CONGESTION_TRESHOLD;
  _priv(this)->discad_dist_treshold             = DISCARD_DISTORTION_TRESHOLD;
  _priv(this)->owd_corr_cng_th                  = OWD_CORR_CONGESTION_TRESHOLD;
  _priv(this)->owd_corr_dist_th                 = OWD_CORR_DISTORTION_TRESHOLD;

  _priv(this)->approvement_epsilon              = APPROVEMENT_EPSILON;
  _priv(this)->stability_treshold               = STABILITY_TRESHOLD;

  _priv(this)->tr_approved                      = TRUE;

}


gboolean fbrasubctrler_path_approver(gpointer data, GstRTPBuffer *rtp)
{
  FBRASubController *this = data;
  _priv(this)->avg_rtp_payload *= .9;
  _priv(this)->avg_rtp_payload += gst_rtp_buffer_get_payload_len(rtp) * .1;

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
  result->fbprocessor         = make_fbrafbprocessor();
  _switch_stage_to(result, STAGE_KEEP, FALSE);
  mprtps_path_set_state(result->path, MPRTPS_PATH_STATE_STABLE);
  mprtps_path_set_packetstracker(result->path, fbrafbprocessor_track, result->fbprocessor);

  return result;
}

void fbrasubctrler_enable(FBRASubController *this)
{
  this->enabled             = TRUE;
  this->disable_end         = _now(this) + 0 * GST_SECOND;
  this->target_bitrate      = mprtps_path_get_target_bitrate(this->path);
  this->last_distorted      = _now(this);
  this->last_tr_changed     = _now(this);
}

void fbrasubctrler_disable(FBRASubController *this)
{
  _switch_stage_to(this, STAGE_KEEP, FALSE);
  mprtps_path_set_state(this->path, MPRTPS_PATH_STATE_STABLE);
  this->enabled = FALSE;
  mprtps_path_set_approval_process(this->path, NULL, NULL);
}


static void _update_rate_correlations(FBRASubController *this)
{
  if(_priv(this)->tr_approved){
    _priv(this)->tr_approved = this->last_tr_changed < _priv(this)->tr_approved_time;
  }else{
    _priv(this)->tr_approved = _TR(this) * (1. - _appr_eps(this)) < _SR(this) &&
                               _SR(this) < _TR(this) * (1. + _appr_eps(this));
    if(_priv(this)->tr_approved){
      _priv(this)->tr_approved_time = _now(this);
    }
  }

  if(_priv(this)->gp_approved){
    _priv(this)->gp_approved = this->last_tr_changed < _priv(this)->gp_approved_time;
  }else{
    _priv(this)->gp_approved = _TR(this) * (1. - _appr_eps(this)) < _GP(this) &&
                               _GP(this) < _TR(this) * (1. + _appr_eps(this));
    if(_priv(this)->gp_approved){
      _priv(this)->gp_approved_time = _now(this);
    }
  }

//  g_print("TR appr: %d, %f - %d - %f\n", _priv(this)->tr_approved, _TR(this) * (1. - _appr_eps(this)), _SR(this), _TR(this) * (1. + _appr_eps(this)));
//  g_print("GP appr: %d, %f - %d - %f\n", _priv(this)->gp_approved, _TR(this) * (1. - _appr_eps(this)), _GP(this), _TR(this) * (1. + _appr_eps(this)));

  if(_priv(this)->tr_approved && _priv(this)->gp_approved){
    if(!_priv(this)->tr_gp_approved){
      _priv(this)->tr_gp_approve_started = _now(this);
    }
    _priv(this)->tr_gp_approved = TRUE;
  }else{
    _priv(this)->tr_gp_approved = FALSE;
  }
//  g_print("changed %lu tr approved: %d time: %lu, rr approved: %d time: %lu\n",
//          GST_TIME_AS_MSECONDS(this->last_tr_changed),
//          _priv(this)->tr_approved, GST_TIME_AS_MSECONDS(_priv(this)->tr_approved_time),
//          _priv(this)->rr_approved, GST_TIME_AS_MSECONDS(_priv(this)->rr_approved_time));
}


//static gboolean _does_near_to_bottleneck_point(FBRASubController *this)
//{
//  if(!this->bottleneck_point){
//    return FALSE;
//  }
//  if(this->target_bitrate < this->bottleneck_point * (1.-_btl_eps(this))){
//    return FALSE;
//  }
//  if(this->bottleneck_point * (1.+_btl_eps(this)) < this->target_bitrate){
//    return FALSE;
//  }
//  return TRUE;
//}

static gdouble _off_target(FBRASubController *this)
{
  gint32 refpoint;
  gdouble result;

  refpoint = MAX(_min_target(this), this->bottleneck_point);
  result = _TR(this) - refpoint;
  result /= _TR(this);
  result = MAX(0., result);

  return result;
}

static gdouble _off2_target(FBRASubController *this)
{
  gint32 refpoint;
  gdouble result;

  refpoint = MAX(_min_target(this), this->bottleneck_point);
  result = _TR(this) - refpoint;
  result /= _TR(this) * .25;
  result = CONSTRAIN(0.,1., result * result);

  return result;
}

static GstClockTime _get_approval_interval(FBRASubController *this)
{
  gdouble off;
  gdouble interval;
  off = _off2_target(this);
  interval = off * _min_appr_int(this) + (1.-off) * _max_appr_int(this);
  return MIN(GST_SECOND, interval * _RTT(this));
}

static void _execute_stage(FBRASubController *this)
{
  if(_now(this) < this->disable_end || this->measurements_num < 5){
    goto done;
  }

  if(_now(this) - _priv(this)->adjust_th < this->last_executed){
    goto done;
  }

  if(this->pending_event != EVENT_FI){
    _fire(this, this->pending_event);
    this->pending_event = EVENT_FI;
  }
  //Execute stage
  this->stage_fnc(this);
  _fire(this, _event(this));
  _priv(this)->event = EVENT_FI;
  _priv(this)->controlled = TRUE;
  this->last_executed = _now(this);

done:
  return;
}

void fbrasubctrler_time_update(FBRASubController *this)
{
  GstClockTime fbinterval_th;
  if(!this->enabled || _now(this) < this->disable_end || this->measurements_num < 5){
    goto done;
  }

  //check weather monitoring interval doesn't exceed max ramp up limit.
  this->monitored_bitrate = mprtps_path_get_monitored_bitrate(this->path, &this->monitored_packets);

  fbinterval_th = CONSTRAIN(100 * GST_MSECOND, 300 * GST_MSECOND, fbrafbprocessor_get_fbinterval(this->fbprocessor) * 3);

  if(!_bcongestion(this) && this->last_fb_arrived < _now(this) - fbinterval_th){
    _disable_monitoring(this);
    _set_event(this, EVENT_CONGESTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    _change_target_bitrate(this, MIN(_TR(this) * .9, _TR_t1(this)));
    g_print("backward congestion fbinterval: %lu\n", fbinterval_th);
    _bcongestion(this) = TRUE;
    goto done;
  }

done:
  return;
}

void fbrasubctrler_signal_update(FBRASubController *this, MPRTPSubflowFECBasedRateAdaption *params)
{
  MPRTPSubflowFBRA2CngCtrlerParams *cngctrler;
  cngctrler = &params->cngctrler;

  _min_appr_int(this)                           = cngctrler->min_approve_interval;
  _max_appr_int(this)                           = cngctrler->max_approve_interval;
  _min_ramp_up(this)                            = cngctrler->min_ramp_up_bitrate;
  _max_ramp_up(this)                            = cngctrler->max_ramp_up_bitrate;
  _min_target(this)                             = cngctrler->min_target_bitrate;
  _max_target(this)                             = cngctrler->max_target_bitrate;
  _restrict_fac(this)                           = cngctrler->restrictivity_factor;
  _appr_eps(this)                               = cngctrler->approvement_epsilon;

  _FD_cong_th(this)                             = cngctrler->discard_cong_treshold;
  _FD_dist_th(this)                             = cngctrler->discard_dist_treshold;
  _stability_th(this)                           = cngctrler->stability_treshold;
  _owd_corr_cng_th(this)                        = cngctrler->owd_corr_cng_th;
  _owd_corr_dist_th(this)                       = cngctrler->owd_corr_dist_th;


}

void fbrasubctrler_signal_request(FBRASubController *this, MPRTPSubflowFECBasedRateAdaption *result)
{
  MPRTPSubflowFBRA2CngCtrlerParams *cngctrler;
  cngctrler = &result->cngctrler;

  cngctrler->min_approve_interval           = _min_appr_int(this);
  cngctrler->max_approve_interval           = _max_appr_int(this);
  cngctrler->min_ramp_up_bitrate            = _min_ramp_up(this);
  cngctrler->max_ramp_up_bitrate            = _max_ramp_up(this);
  cngctrler->min_target_bitrate             = _min_target(this);
  cngctrler->max_target_bitrate             = _max_target(this);

  cngctrler->restrictivity_factor           = _restrict_fac(this);
  cngctrler->approvement_epsilon            = _appr_eps(this);

  cngctrler->discard_dist_treshold          = _FD_dist_th(this);
  cngctrler->discard_cong_treshold          = _FD_cong_th(this);
  cngctrler->stability_treshold             = _stability_th(this);
  cngctrler->owd_corr_cng_th                = _owd_corr_cng_th(this);
  cngctrler->owd_corr_dist_th               = _owd_corr_dist_th(this);

}

static void _update_fraction_discarded(FBRASubController *this)
{
  if(_fbstat(this).received_packets_in_1s == 0 || _fbstat(this).discarded_packets_in_1s == 0){
    _priv(this)->discarded_rate = 0.;
  }
  _priv(this)->discarded_rate = (gdouble)_fbstat(this).discarded_packets_in_1s;
  _priv(this)->discarded_rate /= (gdouble)_fbstat(this).received_packets_in_1s;
//  g_print("disc packets: %d / received packets: %d = %f\n",
//          _fbstat(this).discarded_packets_in_1s,
//          _fbstat(this).received_packets_in_1s,
//          _priv(this)->discarded_rate);
}

/*
                             s
X_Bps = -----------------------------------------------
        R * (sqrt(2*p/3) + 12*sqrt(3*p/8)*p*(1+32*p^2))
 * */
static gint32 _get_tfrc(FBRASubController *this)
{
  gdouble result = 0.;
  gdouble rtt,p;
  rtt = _fbstat(this).RTT;
  rtt /= (gdouble) GST_SECOND;
  p   =  _FD(this);
  if(p == 0.) p = 0.01;
  result = _priv(this)->avg_rtp_payload * 8;
  result /= rtt * (sqrt(2.*p/3.)) + 12.*sqrt(3.*p/8.) * p * (1+32*p*p);
//  result /= rtt * sqrt((2*p)/3);
//  g_print("TFRC: %f=%f/(%f * (sqrt(2*%f/3) + 12*sqrt(3*%f/8)*p*(1+32*%f^2)))\n",
//          result, _priv(this)->avg_rtp_payload * 8,
//          rtt, p, p, p);
  return result;
}

//static gint32 _get_tfrc2(FBRASubController *this)
//{
//  gdouble result = 0.;
//  gdouble rtt,p;
//  rtt = _fbstat(this).RTT;
//  rtt /= (gdouble) GST_SECOND;
//  p   =  _FD(this);
//  if(p == 0. ) p= 0.001;
//  result = _priv(this)->avg_rtp_payload + 48;
//  result /= (rtt * (sqrt((2.*p)/3.)));
//  return result * 8;
//}

void fbrasubctrler_report_update(
                         FBRASubController *this,
                         GstMPRTCPReportSummary *summary)
{
  if(!this->enabled){
    goto done;
  }
  if(summary->RR.processed){
    if(_priv(this)->rtt == 0.){
      _priv(this)->rtt = summary->RR.RTT;
    }else{
      _priv(this)->rtt = .9 * _priv(this)->rtt + .1 * summary->RR.RTT;
    }
  }
  if(!summary->XR.processed){
    goto done;
  }
  this->last_fb_arrived    = _now(this);
  _bcongestion(this)       = FALSE;

  fbrafbprocessor_update(this->fbprocessor, summary);
  fbrafbprocessor_get_stats(this->fbprocessor, &this->fbstat);

  _update_rate_correlations(this);
  _update_fraction_discarded(this);
  {
    gdouble alpha;
    alpha = MIN(.5, _fbstat(this).stability / 2.);
    this->gp_hat = alpha * _GP(this) + (1.-alpha) * this->gp_hat;
//    g_print("gp_hat: %f (alpha: %f) gp: %d\n", this->gp_hat, alpha, _GP(this));
  }

  this->owd_approvement = FALSE;
  _execute_stage(this);
  this->owd_approvement |= _priv(this)->stage != STAGE_REDUCE;

//
//    g_print("TR: %-7d|GP:%-7d|tr_appred: %1d|owd_stt: %-3lu/owd_ltt: %-3lu=%3.2f|SR: %-7d|stb: %3.2f|stg: %d|sta: %d|FD: %-7f|rtt: %-3.2f\n",
//            _TR(this),
//            _fbstat(this).goodput_bytes * 8,
//            _priv(this)->tr_gp_approved,
//            GST_TIME_AS_MSECONDS(_fbstat(this).owd_stt_median),
//            GST_TIME_AS_MSECONDS(_fbstat(this).owd_ltt_median),
//            _fbstat(this).owd_corr,
//            _fbstat(this).sent_bytes_in_1s  * 8,
//            _fbstat(this).stability,
//            _priv(this)->stage,
//            mprtps_path_get_state(this->path),
//            _FD(this),
//            GST_TIME_AS_MSECONDS(_RTT(this))
//        );

  if(this->owd_approvement){
    fbrafbprocessor_approve_owd(this->fbprocessor);
  }
  ++this->measurements_num;

done:
  return;
}

static void _reduce_target(FBRASubController *this, gint32 *target)
{
  gint32 result;
  gdouble factor;
//  g_print("tfrc: %d, rtt: %f\n", _get_tfrc(this), _RTT(this));
  this->bottleneck_point = MIN(_TR(this), _GP(this));
  factor = (_FD(this) < 0.02) ? (_fbstat(this).stability / _stability_th(this)) : (1.-_FD(this)/2.);
  result = CONSTRAIN(.6, .9, factor) * _SR(this);
  *target = result;
}

static void _corrigate_taget(FBRASubController *this, gint32 *target)
{

  if(_priv(this)->corrigate_num <= 2){
    *target *= .9;
  }else if(_priv(this)->corrigate_num <= 4){
    *target *= .8;
  }else{
    *target *= .6;
  }
  ++_priv(this)->corrigate_num;
}

void
_reduce_stage(
    FBRASubController *this)
{
  gint32   target_rate = this->target_bitrate;
  GstClockTime approve_interval = MIN(GST_SECOND, _RTT(this));

  if(_owd_corr_cng_th(this) < _owd_corr(this)){
    if(_GP(this) < _TR(this) && this->last_corrigated < _now(this) - _RTT(this)){
      this->bottleneck_point = _GP(this);
      _corrigate_taget(this, &target_rate);
      this->last_corrigated = _now(this);
    }
    goto done;
  }else if(!_priv(this)->tr_gp_approved || _now(this) - approve_interval < _priv(this)->tr_gp_approve_started){
    goto done;
  }

//  this->owd_approvement = TRUE;
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
  gint32       target_rate = this->target_bitrate;
  GstClockTime now = _now(this);

  if(_FD_cong_th(this) < _FD(this) || _owd_corr_cng_th(this)  < _owd_corr(this)){
    _set_event(this, EVENT_CONGESTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    _reduce_target(this, &target_rate);
    goto done;
  }

  if(_owd_stability(this) < _stability_th(this) || _owd_corr_dist_th(this) < _owd_corr(this) || _FD_dist_th(this) < _FD(this)){
    _set_event(this, EVENT_DISTORTION);
    this->last_distorted = now;
  }else if(_state(this) != MPRTPS_PATH_STATE_STABLE){
    _set_event(this, EVENT_SETTLED);
    goto done;
  }

  if(now - _RTT(this) < this->last_distorted){
    goto done;
  }else if(!_priv(this)->tr_gp_approved || now - _RTT(this) < _priv(this)->tr_gp_approve_started){
    goto done;
  }

  _start_monitoring(this);
  _switch_stage_to(this, STAGE_PROBE, FALSE);
done:
  _change_target_bitrate(this, target_rate);
//  return;
}

void
_probe_stage(
    FBRASubController *this)
{
  gint32   target_rate = this->target_bitrate;

  if(_FD_cong_th(this) < _FD(this) || _owd_corr_cng_th(this)  < _owd_corr(this)){
    _disable_monitoring(this);
    _set_event(this, EVENT_CONGESTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    _reduce_target(this, &target_rate);
    goto done;
  }

  if(_owd_stability(this) < _stability_th(this) || _owd_corr_dist_th(this) < _owd_corr(this) || _FD_dist_th(this) < _FD(this)){
    this->bottleneck_point = MIN(_TR(this), _GP(this));
    target_rate = this->bottleneck_point * .85;
    _disable_monitoring(this);
    _set_event(this, EVENT_DISTORTION);
    _switch_stage_to(this, STAGE_KEEP, FALSE);
    this->last_distorted = _now(this);
    goto done;
  }

  if(_now(this) - _get_approval_interval(this) < this->monitoring_started){
    goto done;
  }

  _start_increasemet(this, &target_rate);
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

  if(_FD_cong_th(this) < _FD(this) || _owd_corr_cng_th(this)  < _owd_corr(this)){
    _disable_monitoring(this);
    _set_event(this, EVENT_CONGESTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    _reduce_target(this, &target_rate);
    goto done;
  }

  if(_owd_stability(this) < _stability_th(this) || _owd_corr_dist_th(this) < _owd_corr(this) || _FD_dist_th(this) < _FD(this)){
    _disable_monitoring(this);
    _set_event(this, EVENT_DISTORTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    target_rate = MIN(_TR_t1(this), _GP(this) * .85);
    this->bottleneck_point = MIN(_TR(this), _GP(this));
    goto done;
  }

  if(!_priv(this)->tr_gp_approved){
    goto done;
  }else if(_now(this) - _get_approval_interval(this) < _priv(this)->tr_gp_approve_started){
    goto done;
  }else if(_now(this) - _get_approval_interval(this) < this->increasement_started){
    goto done;
  }

  _start_monitoring(this);
  _switch_stage_to(this, STAGE_PROBE, FALSE);
done:
  _change_target_bitrate(this, target_rate);
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
        case EVENT_SETTLED:
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
       _priv(this)->corrigate_num = 0;
       this->congestion_detected = _now(this);
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


void _start_increasemet(FBRASubController *this, gint32 *increased_target_rate)
{
  gint32 increasement = 0;
  gdouble restriction;
  gdouble off;

//  *increased_target_rate = MIN(_GP(this), _SR(this));
//  *increased_target_rate = MAX(_GP(this), _SR(this));
  *increased_target_rate = (_GP(this) + _SR(this))>>1;
  off = _off2_target(this);
  restriction = (1.-off) * _restrict_fac(this);

//  g_print("btl: %d, tr: %d, restriction: %f\n", this->bottleneck_point, this->target_bitrate, restriction);

  increasement = this->monitored_bitrate * (1.-restriction);
  if(_max_ramp_up(this)){
    increasement = CONSTRAIN(_min_ramp_up(this), _max_ramp_up(this), increasement);
  }else{
    increasement = MAX(_min_ramp_up(this), increasement);
  }
  *increased_target_rate += increasement;
  this->increasement_started = _now(this);
}

void _reset_monitoring(FBRASubController *this)
{
  this->monitoring_interval = 0;
  this->monitoring_started = 0;
  this->monitored_bitrate = 0;
}

void _start_monitoring(FBRASubController *this)
{
  guint interval;
  gdouble off;
  off = _off_target(this);
  interval = (1.-off) * _mon_min_int(this) + off * _mon_max_int(this);

  while(_TR(this) / interval < _min_ramp_up(this) && _mon_min_int(this) < interval){
    --interval;
  }
  while(_max_ramp_up(this) < _TR(this) / interval && interval < _mon_max_int(this)){
    ++interval;
  }

//  g_print("btl: %d, tr: %d, interval: %d\n", this->bottleneck_point, this->target_bitrate, interval);

//  g_print("tr: %d interval: %d btl: %d\n", _TR(this), interval, this->bottleneck_point);
  _set_monitoring_interval(this, interval);
  this->monitoring_started = _now(this);
}


void _set_monitoring_interval(FBRASubController *this, guint interval)
{
  this->monitoring_interval = interval;
//  if(interval > 0)
//    this->monitored_bitrate = (gdouble)_TR(this) / (gdouble)interval;
//  else
//    this->monitored_bitrate = 0;
  this->monitored_bitrate = 0;
  mprtps_path_set_monitoring_interval(this->path, this->monitoring_interval);
  return;
}

void _change_target_bitrate(FBRASubController *this, gint32 new_target)
{
  if(this->target_bitrate == new_target){
    goto done;
  }
  this->target_bitrate_t1 = this->target_bitrate;
  this->target_bitrate    = new_target;
  this->last_tr_changed   = _now(this);
  if(0 < _priv(this)->min_target_bitrate){
    gint32 min_target;
    min_target = MAX(_get_tfrc(this), _priv(this)->min_target_bitrate);
    this->target_bitrate = MAX(min_target, this->target_bitrate);
  }
  if(0 < _priv(this)->max_target_bitrate){
    this->target_bitrate = MIN(_priv(this)->max_target_bitrate, this->target_bitrate);
  }
done:
  mprtps_path_set_target_bitrate(this->path, this->target_bitrate);
}


void fbrasubctrler_logging(FBRASubController *this)
{
  gchar filename[255];
  sprintf(filename, "fbractrler_%d.log", this->id);
  mprtp_logger(filename,
               "############## S%d | State: %-2d | Stage: %1d | Ctrled: %d ###################\n"
               "# Goodput:                     %-10d| Sending Rate:               %-10d#\n"
               "# Actual Target:               %-10d| tr_is_corred:               %-10d#\n"
               "# Minimum Bitrate:             %-10d| OWD Median:                 %-10lu#\n"
               "# Maximum Bitrate:             %-10d| OWD Actual:                 %-10lu#\n"
               "# Bottleneck point:            %-10d| OWD Corr:                   %-10f#\n"
               "# Monitoring Interval:         %-10d| tr_is_approved:             %-10d#\n"
               "# Monitoring Bitrate:          %-10d| rr_is_approved:             %-10d#\n"
               "# Recent Discarded:            %-10d| stability:                  %-10.6f#\n"
               "###################### MSeconds since setup: %lu ######################\n"
               ,

               this->id,
               _state(this),
               _stage(this),
               _priv(this)->controlled,

               _GP(this),
               _SR(this),

               _TR(this),
               0,

               _priv(this)->min_target_bitrate,
               _owd_ltt_median(this),

               _priv(this)->max_target_bitrate,
               _owd_stt_median(this),

               _btlp(this),
               _fbstat(this).owd_corr,

               _mon_int(this),
               _priv(this)->tr_approved,

               _mon_br(this),
               _priv(this)->gp_approved,

               _fbstat(this).recent_discarded,
               _fbstat(this).stability,

               GST_TIME_AS_MSECONDS(_now(this) - this->made)

               );

  DISABLE_LINE _params_out(this);
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
   "| normal_monitoring_interval                    | %-18.3f|\n"
   "|                                               |                   |\n"
   "| bottleneck_monitoring_interval                | %-18.3f|\n"
   "|                                               |                   |\n"
   "| min_monitoring_interval                       | %-18d|\n"
   "|                                               |                   |\n"
   "| max_monitoring_interval                       | %-18d|\n"
   "|                                               |                   |\n"
   "| min_ramp_up_bitrate                           | %-18d|\n"
   "|                                               |                   |\n"
   "| max_ramp_up_bitrate                           | %-18d|\n"
   "|                                               |                   |\n"
   "| min_target_bitrate                            | %-18d|\n"
   "|                                               |                   |\n"
   "| max_target_bitrate                            | %-18d|\n"
   "+-----------------------------------------------+-------------------+\n",

   _priv(this)->restrictivity_factor   ,
   _priv(this)->min_approve_interval       ,
   _priv(this)->max_approve_interval   ,
   _priv(this)->min_monitoring_interval          ,
   _priv(this)->max_monitoring_interval          ,
   _priv(this)->min_ramp_up_bitrate              ,
   _priv(this)->max_ramp_up_bitrate              ,
   _priv(this)->min_target_bitrate               ,
   _priv(this)->max_target_bitrate

   );
}

void fbrasubctrler_logging2csv(FBRASubController *this)
{
  gchar filename[255];
  THIS_READLOCK(this);

  sprintf(filename, "snd_%d_ratestat.csv", this->id);

  mprtp_logger(filename, "%d,%d,%d,%f,%d\n",
               this->target_bitrate / 1000,
               _SR(this),
               (_SR(this) + 48 * 8 * _fbstat(this).sent_packets_in_1s) / 1000,
               0.,
               (this->monitored_bitrate + 40 * 8 * this->monitored_packets) / 1000
               );

  THIS_READUNLOCK(this);
}
