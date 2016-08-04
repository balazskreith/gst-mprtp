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


//determines the treshold for utilization, in which below the path considered to be congested
#define DISCARD_CONGESTION_MAX_TRESHOLD 0.1

//determines the treshold for utilization, in which below the path considered to be distorted
#define DISCARD_DISTORTION_MAX_TRESHOLD 0.0

//determines a treshold for trend calculations, in which above the KEEP stage not let it to PROBE
#define OWD_CORR_DISTORTION_TRESHOLD 1.05

//determines a treshold for trend calculations, in which above the path considered to be congested
#define OWD_CORR_CONGESTION_TRESHOLD 1.2

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

typedef struct{
  gint32  gp;
  gint32  sr;
  gint32  fec;
  gdouble tend;
}TargetItem;

typedef struct{
  TargetItem items[256];
  gint       item_index;
}TargetController;

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

//  gdouble             discarded_rate;
  gdouble             avg_rtp_payload;
  gdouble             rtt;

  gboolean            bcongestion;

  gint32              corrigate_num;

  //Possible parameters

  gdouble             discad_cong_treshold;
  gdouble             discad_dist_treshold;

  gdouble             owd_corr_dist_th;
  gdouble             owd_corr_cng_th;

  GstClockTime        owd_dist_th;
  GstClockTime        owd_cng_th;

};



#define _priv(this) ((Private*)this->priv)
#define _fbstat(this) this->fbstat
#define _throttler(this) _priv(this)->throttler

#define _bcongestion(this) _priv(this)->bcongestion
#define _btlp(this) this->bottleneck_point
#define _mon_int(this) this->monitoring_interval
#define _mon_br(this) this->monitored_bitrate
#define _owd_stt_median(this) _fbstat(this).owd_stt_median
#define _owd_ltt_median(this) _fbstat(this).owd_ltt80
#define _qdelay_processed(this) _fbstat(this).owd_processed
#define _rdiscards(this) _fbstat(this).recent_discards
//#define _RTT(this) (_priv(this)->rtt == 0. ? (gdouble)GST_SECOND : _priv(this)->rtt)
#define _RTT(this) (_priv(this)->rtt == 0. ? (gdouble)GST_SECOND : MAX(100 * GST_MSECOND, _priv(this)->rtt))
#define _FD(this) _fbstat(this).discarded_rate

#define _state(this) mprtps_path_get_state(this->path)
#define _state_t1(this) this->state_t1
#define _stage(this) _priv(this)->stage
#define _stage_t1(this) _priv(this)->stage_t1
#define _event(this) _priv(this)->event
#define _set_event(this, e) _event(this) = e
#define _set_pending_event(this, e) this->pending_event = e

#define _stability_th(this) _priv(this)->stability_treshold

#define _TR(this) this->targetctrler->target_bitrate
#define _SR(this) (_fbstat(this).sent_bytes_in_1s * 8)
#define _GP(this) (_fbstat(this).goodput_bytes * 8)
#define _GP_t1(this) (_priv(this)->goodput_bitrate_t1)
#define _min_br(this) MIN(_SR(this), _TR(this))
#define _max_br(this) MAX(_SR(this), _TR(this))
#define _owd_corr(this) _fbstat(this).owd_corr

#define _owd_corr_cng_th(this)        _priv(this)->owd_corr_cng_th
#define _owd_corr_dist_th(this)       _priv(this)->owd_corr_dist_th

#define _appr_eps(this)               _priv(this)->approvement_epsilon

#define _FD_cong_th(this)             _priv(this)->discad_cong_treshold
#define _FD_dist_th(this)             _priv(this)->discad_dist_treshold



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

#define _disable_monitoring(this) _set_monitoring_interval(this, 0)


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
      "FBRA+ Subflow Rate Controller");

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
  g_object_unref(this->targetctrler);
}


void
fbrasubctrler_init (FBRASubController * this)
{
  this->priv = mprtp_malloc(sizeof(Private));
  this->sysclock = gst_system_clock_obtain();
  g_rw_lock_init (&this->rwmutex);

  //Initial values
  _priv(this)->adjust_th = 20 * GST_MSECOND;

  _priv(this)->discad_cong_treshold             = DISCARD_CONGESTION_MAX_TRESHOLD;
  _priv(this)->discad_dist_treshold             = DISCARD_DISTORTION_MAX_TRESHOLD;
  _priv(this)->owd_corr_cng_th                  = OWD_CORR_CONGESTION_TRESHOLD;
  _priv(this)->owd_corr_dist_th                 = OWD_CORR_DISTORTION_TRESHOLD;

  _priv(this)->tr_approved                      = TRUE;

}
//
//static gboolean pacing = FALSE;
//static gint32 cwnd_max;
//static GstClockTime last_sent;
//gboolean fbrasubctrler_path_approver(gpointer data, GstRTPBuffer *rtp)
//{
//  FBRASubController *this = data;
//
//  if(!pacing && mprtps_path_is_non_congested(this->path)){
//    goto approve;
//  }
//
//  if(mprtps_path_get_state(this->path) != MPRTPS_PATH_STATE_OVERUSED){
//    pacing = FALSE;
//    goto approve;
//  }
//  if(!pacing){
//    cwnd_max = _fbstat(this).bytes_in_flight * 8;
//    pacing = TRUE;
//    goto approve;
//  }
//
//  {
//    gdouble pace_bitrate;
//    gdouble t_pace;
//    gdouble rtt_in_sec;
//    gdouble cwnd;
//
////    owd_in_sec = (gdouble)_fbstat(this).owd_ltt80 * .000000001;
//    cwnd = cwnd_max * CONSTRAIN(.6, .9, 3.0 - _fbstat(this).owd_corr);
//    rtt_in_sec = (gdouble)_fbstat(this).RTT * .000000001;
//    pace_bitrate = MAX(50000, cwnd / rtt_in_sec);
//    t_pace = (gdouble)(this->last_rtp_size * 8) / pace_bitrate;
//    if(_now(this) < last_sent + t_pace * GST_SECOND){
//      goto disapprove;
//    }
//  }
//
//approve:
//  this->last_rtp_size = gst_rtp_buffer_get_payload_len(rtp);
//  fbratargetctrler_update_rtpavg(this->targetctrler, this->last_rtp_size);
//  last_sent = _now(this);
////  g_print("rtp: %hu approved\n", gst_rtp_buffer_get_seq(rtp));
//  return TRUE;
//disapprove:
////  g_print("rtp: %hu disapproved\n", gst_rtp_buffer_get_seq(rtp));
//  return FALSE;
//}

gboolean fbrasubctrler_path_approver(gpointer data, GstRTPBuffer *rtp)
{
  FBRASubController *this = data;

  this->last_rtp_size = gst_rtp_buffer_get_payload_len(rtp);
  fbratargetctrler_update_rtpavg(this->targetctrler, this->last_rtp_size);
  g_print("BiF: %d->%d (%1.3f - %1.3f)\n",
          _fbstat(this).sent_bytes_in_1s * 8,
          _fbstat(this).bytes_in_flight * 8,
          (gdouble)_fbstat(this).bytes_in_flight / (gdouble)_fbstat(this).sent_bytes_in_1s,
          (gdouble)_fbstat(this).sent_bytes_in_1s * (gdouble)(_fbstat(this).owd_stt * .000000001) * 8);
  return TRUE;
}


FBRASubController *make_fbrasubctrler(MPRTPSPath *path)
{
  FBRASubController *result;
  result                      = g_object_new (FBRASUBCTRLER_TYPE, NULL);
  result->path                = g_object_ref(path);
  result->targetctrler        = make_fbratargetctrler(path);

  result->id                  = mprtps_path_get_id(result->path);
  result->monitoring_interval = 3;
  result->made                = _now(result);
  result->fbprocessor         = make_fbrafbprocessor(result->id);

  _switch_stage_to(result, STAGE_KEEP, FALSE);
  mprtps_path_set_state(result->path, MPRTPS_PATH_STATE_STABLE);
  mprtps_path_set_packetstracker(result->path, fbrafbprocessor_track, result->fbprocessor);

  return result;
}

void fbrasubctrler_enable(FBRASubController *this)
{
  this->enabled             = TRUE;
  this->disable_end         = _now(this) + 0 * GST_SECOND;
  this->last_distorted      = _now(this);
  this->last_tr_changed     = _now(this);

  fbratargetctrler_set_initial(this->targetctrler, mprtps_path_get_target_bitrate(this->path));
  fbratargetctrler_probe(this->targetctrler);
}

void fbrasubctrler_disable(FBRASubController *this)
{
  _switch_stage_to(this, STAGE_KEEP, FALSE);
  mprtps_path_set_state(this->path, MPRTPS_PATH_STATE_STABLE);
  this->enabled = FALSE;
  mprtps_path_set_approval_process(this->path, NULL, NULL);
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

  this->monitored_bitrate = mprtps_path_get_monitored_bitrate(this->path, &this->monitored_packets);
  fbinterval_th = 150 * GST_MSECOND;

  if(!_bcongestion(this) && this->last_fb_arrived < _now(this) - fbinterval_th){
    fbratargetctrler_break(this->targetctrler);
    _switch_stage_to(this, STAGE_KEEP, FALSE);
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

  fbratargetctrler_signal_update(this->targetctrler, params);

//  _FD_cong_th(this)                             = cngctrler->discard_cong_treshold;
//  _FD_dist_th(this)                             = cngctrler->discard_dist_treshold;
  _owd_corr_cng_th(this)                        = cngctrler->owd_corr_cng_th;
  _owd_corr_dist_th(this)                       = cngctrler->owd_corr_dist_th;


}

void fbrasubctrler_signal_request(FBRASubController *this, MPRTPSubflowFECBasedRateAdaption *result)
{
  MPRTPSubflowFBRA2CngCtrlerParams *cngctrler;
  cngctrler = &result->cngctrler;

  fbratargetctrler_signal_request(this->targetctrler, result);

//  cngctrler->discard_dist_treshold          = _FD_dist_th(this);
//  cngctrler->discard_cong_treshold          = _FD_cong_th(this);
  cngctrler->owd_corr_cng_th                = _owd_corr_cng_th(this);
  cngctrler->owd_corr_dist_th               = _owd_corr_dist_th(this);

}

static void _update_fraction_discarded(FBRASubController *this)
{
  _FD_cong_th(this) = MIN(DISCARD_CONGESTION_MAX_TRESHOLD, this->fbstat.FD_median * 2.);
  _FD_dist_th(this) = MIN(DISCARD_DISTORTION_MAX_TRESHOLD, this->fbstat.FD_median * 2.);
}

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
  fbratargetctrler_update(this->targetctrler, &this->fbstat);
  _update_fraction_discarded(this);

  _execute_stage(this);

//  if(_FD(this) <= _FD_cong_th(this) && _fbstat(this).tendency < .2 && -.2 < _fbstat(this).tendency){
//    fbrafbprocessor_approve_owd_ltt(this->fbprocessor);
//  }

  if(_state(this) != MPRTPS_PATH_STATE_OVERUSED){
      fbrafbprocessor_approve_owd_ltt(this->fbprocessor);
      this->last_approved = _now(this);
  }else if(this->last_approved < _now(this) - _RTT(this)){
      fbrafbprocessor_approve_owd_ltt(this->fbprocessor);
  }

  _priv(this)->owd_corr_cng_th  = (gdouble)(_fbstat(this).owd_ltt80 + _fbstat(this).owd_th_cng) / (gdouble)_fbstat(this).owd_ltt80;
  _priv(this)->owd_corr_dist_th = (gdouble)(_fbstat(this).owd_ltt80 + _fbstat(this).owd_th_dist) / (gdouble)_fbstat(this).owd_ltt80;

  mprtp_logger("fbrasubctrler.log",
               "TR: %-7d|GP:%-7d|corh: %-3lu/%-3lu=%3.2f (%1.2f - %1.2f)|SR: %-7d|FEC:%-7d|tend: %3.2f|stg: %d|sta: %d|FD: %1.2f(%1.2f-%1.2f-%1.2f)|rtt: %-3.2f\n",
            _TR(this),
            _fbstat(this).goodput_bytes * 8,
            GST_TIME_AS_MSECONDS(_fbstat(this).owd_stt),
            GST_TIME_AS_MSECONDS(_fbstat(this).owd_ltt80),
            _fbstat(this).owd_corr,
            _fbstat(this).owd_th_cng,
            _fbstat(this).owd_th_dist,
            _fbstat(this).sent_bytes_in_1s  * 8,
            this->monitored_bitrate,
            _fbstat(this).tendency,
            _priv(this)->stage,
            mprtps_path_get_state(this->path),
            _FD(this),
            _FD_dist_th(this),
            _FD_cong_th(this),
            this->fbstat.FD_median,
            GST_TIME_AS_MSECONDS(_RTT(this))
        );

  ++this->measurements_num;

done:
  return;
}

static gboolean _distortion(FBRASubController *this)
{
  GstClockTime owd_th;

  owd_th = _fbstat(this).owd_ltt80 + CONSTRAIN(30 * GST_MSECOND, 150 * GST_MSECOND, _fbstat(this).owd_th_cng);
  if(owd_th < _fbstat(this).owd_stt){
    return TRUE;
  }

  return FALSE;
}

static gboolean _congestion(FBRASubController *this)
{
  gdouble FD_th;
  FD_th = CONSTRAIN(.02, .1, _fbstat(this).FD_avg * 2);
  if(FD_th < _FD(this)){
    return TRUE;
  }
  return FALSE;
}

void
_reduce_stage(
    FBRASubController *this)
{
  if(_congestion(this)){
    fbratargetctrler_break(this->targetctrler);
  }else if(!fbratargetctrler_get_approvement(this->targetctrler)){
    goto done;
  }

  _set_event(this, EVENT_SETTLED);
  _switch_stage_to(this, STAGE_KEEP, FALSE);
done:
  fbratargetctrler_refresh_target(this->targetctrler);
  return;
}

void
_keep_stage(
    FBRASubController *this)
{
  GstClockTime now = _now(this);

  if(_congestion(this)){
    fbratargetctrler_break(this->targetctrler);
    _set_event(this, EVENT_CONGESTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    goto done;
  }

  if(_distortion(this)){
    fbratargetctrler_halt(this->targetctrler);
    _set_event(this, EVENT_DISTORTION);
    this->last_distorted = now;
    goto done;
  }else if(_state(this) != MPRTPS_PATH_STATE_STABLE){
    _set_event(this, EVENT_SETTLED);
    goto done;
  }

  if(!fbratargetctrler_get_approvement(this->targetctrler)){
    goto done;
  }else if(now - _RTT(this) < this->last_distorted){
    goto done;
  }

  fbratargetctrler_probe(this->targetctrler);
  _switch_stage_to(this, STAGE_PROBE, FALSE);
done:
  fbratargetctrler_refresh_target(this->targetctrler);
//  return;
}

void
_probe_stage(
    FBRASubController *this)
{
  if(_congestion(this)){
    fbratargetctrler_break(this->targetctrler);
    _set_event(this, EVENT_CONGESTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    goto done;
  }

  if(_distortion(this)){
    fbratargetctrler_revert(this->targetctrler);
    _set_event(this, EVENT_DISTORTION);
    _switch_stage_to(this, STAGE_KEEP, FALSE);
    this->last_distorted = _now(this);
    goto done;
  }

  if(!fbratargetctrler_get_approvement(this->targetctrler)){
    goto done;
  }

  fbratargetctrler_accelerate(this->targetctrler);
  fbratargetctrler_probe(this->targetctrler);
  _switch_stage_to(this, STAGE_INCREASE, FALSE);
  _set_event(this, EVENT_READY);
done:
  fbratargetctrler_refresh_target(this->targetctrler);
  return;
}

void
_increase_stage(
    FBRASubController *this)
{
  if(_congestion(this)){
    fbratargetctrler_break(this->targetctrler);
    _set_event(this, EVENT_CONGESTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    goto done;
  }

  if(_distortion(this)){
    fbratargetctrler_revert(this->targetctrler);
    _set_event(this, EVENT_DISTORTION);
    _switch_stage_to(this, STAGE_REDUCE, FALSE);
    goto done;
  }

  if(!fbratargetctrler_get_approvement(this->targetctrler)){
    goto done;
  }

  _switch_stage_to(this, STAGE_PROBE, FALSE);
done:
  fbratargetctrler_refresh_target(this->targetctrler);
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



