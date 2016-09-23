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
#include "fbratargetctrler.h"
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

GST_DEBUG_CATEGORY_STATIC (fbratargetctrler_debug_category);
#define GST_CAT_DEFAULT fbratargetctrler_debug_category

G_DEFINE_TYPE (FBRATargetCtrler, fbratargetctrler, G_TYPE_OBJECT);

//determine the minimum interval in seconds must be stay in probe stage
//before the target considered to be accepted
#define MIN_APPROVE_INTERVAL 50 * GST_MSECOND

//determine the minimum multiplying factor for aprovements
//before the target considered to be accepted
#define APPROVE_MIN_FACTOR 1.0

//determine the maximum multiplying factor for aprovements
//before the target considered to be accepted
#define APPROVE_MAX_FACTOR 2.0

//determines the minimum ramp up bitrate
#define RAMP_UP_MIN_SPEED 50000

//determines the maximum ramp up bitrate
#define RAMP_UP_MAX_SPEED 250000

//Min target_bitrate [bps]
//#define TARGET_BITRATE_MIN 500000
#define TARGET_BITRATE_MIN SUBFLOW_DEFAULT_SENDING_RATE

//Max target_bitrate [bps] - 0 means infinity
#define TARGET_BITRATE_MAX 0

//approvement epsilon
#define APPROVEMENT_EPSILON 0.25

//interval epsilon
#define INTERVAL_EPSILON 0.25

//determines the minimum monitoring interval
#define MIN_MONITORING_INTERVAL 2

//determines the maximum monitoring interval
#define MAX_MONITORING_INTERVAL 14



typedef struct _Private Private;

typedef struct{
  gint32  gp;
  gint32  sr;
  gint32  fec;
//  gdouble tend;
}TargetItem;

typedef struct{
  TargetItem items[256];
  gint       item_index;
}TargetController;

struct _Private{

  GstClockTime        min_approve_interval;
  gdouble             approve_min_factor;
  gdouble             approve_max_factor;
  gint32              min_ramp_up_bitrate;
  gint32              max_ramp_up_bitrate;
  gint32              min_target_bitrate;
  gint32              max_target_bitrate;
  gdouble             approvement_epsilon;

  gint32              min_monitoring_interval;
  gint32              max_monitoring_interval;

  gdouble             interval_epsilon;

  TargetItem          items[256];
  guint8              item_index;

  gint32              gp_median,fec_median,sr_median;
  gdouble             tend_median;
//  gdouble             fraction_discarded;

  gdouble             avg_rtp_payload;

  GstClockTime        RTT;

};



#define _priv(this) ((Private*)this->priv)
#define _RTT(this) (_priv(this)->RTT == 0 ? (gdouble)GST_SECOND : _priv(this)->RTT)
#define _btlp(this) this->bottleneck_point

#define _appr_eps(this)               _priv(this)->approvement_epsilon
#define _interval_eps(this)           _priv(this)->interval_epsilon
#define _min_appr_int(this)           _priv(this)->min_approve_interval
#define _appr_min_fact(this)          _priv(this)->approve_min_factor
#define _appr_max_fact(this)          _priv(this)->approve_max_factor
#define _min_ramp_up(this)            _priv(this)->min_ramp_up_bitrate
#define _max_ramp_up(this)            _priv(this)->max_ramp_up_bitrate
#define _min_target(this)             _priv(this)->min_target_bitrate
#define _max_target(this)             _priv(this)->max_target_bitrate
#define _mon_min_int(this)            _priv(this)->min_monitoring_interval
#define _mon_max_int(this)            _priv(this)->max_monitoring_interval

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

 void fbratargetctrler_finalize (GObject * object);


// const gdouble ST_ = 1.1; //Stable treshold
// const gdouble OT_ = 2.;  //Overused treshold
// const gdouble DT_ = 1.5; //Down Treshold


#define _now(this) (gst_clock_get_time(this->sysclock))


 static void _refresh_target_approvement(FBRATargetCtrler* this);
//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
fbratargetctrler_class_init (FBRATargetCtrlerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fbratargetctrler_finalize;

  GST_DEBUG_CATEGORY_INIT (fbratargetctrler_debug_category, "fbratargetctrler", 0,
      "FBRA+MARC Subflow Rate Controller");

}


void
fbratargetctrler_finalize (GObject * object)
{
  FBRATargetCtrler *this;
  this = FBRATARGETCTRLER(object);
  mprtp_free(this->priv);
  g_object_unref(this->sysclock);
  g_object_unref(this->path);
}


void
fbratargetctrler_init (FBRATargetCtrler * this)
{
  this->priv = mprtp_malloc(sizeof(Private));
  this->sysclock = gst_system_clock_obtain();
  g_rw_lock_init (&this->rwmutex);

  //Initial values
  this->target_bitrate   = SUBFLOW_DEFAULT_SENDING_RATE;

  _priv(this)->approve_min_factor               = APPROVE_MIN_FACTOR;
  _priv(this)->approve_max_factor               = APPROVE_MAX_FACTOR;
  _priv(this)->min_approve_interval             = MIN_APPROVE_INTERVAL;
  _priv(this)->min_ramp_up_bitrate              = RAMP_UP_MIN_SPEED;
  _priv(this)->max_ramp_up_bitrate              = RAMP_UP_MAX_SPEED;
  _priv(this)->min_target_bitrate               = TARGET_BITRATE_MIN;
  _priv(this)->max_target_bitrate               = TARGET_BITRATE_MAX;

  _priv(this)->approvement_epsilon              = APPROVEMENT_EPSILON;
  _priv(this)->interval_epsilon                 = INTERVAL_EPSILON;

  _priv(this)->min_monitoring_interval          = MIN_MONITORING_INTERVAL;
  _priv(this)->max_monitoring_interval          = MAX_MONITORING_INTERVAL;
}
#define _item_cmpfnc(field) \
static gint _item_##field##_cmp(gpointer pa, gpointer pb) \
{ \
  TargetItem *a,*b; \
  a = pa; b = pb; \
  if(a->field == b->field) return 0; \
  return a->field < b->field ? -1 : 1; \
} \

_item_cmpfnc(gp);
_item_cmpfnc(sr);
_item_cmpfnc(fec);



#define _item_median_pipe(field) \
static void _item_##field##_pipe(gpointer udata, swpercentilecandidates_t *candidates) \
{ \
  FBRATargetCtrler *this = udata;  \
  TargetItem *left,*right;         \
                                   \
  if(!candidates->processed){      \
    _priv(this)->field##_median = 0;    \
    return;                        \
  }                                \
                                   \
  left = candidates->left;         \
  right = candidates->right;       \
                                   \
  if(!candidates->left){           \
    _priv(this)->field##_median = right->field; \
  }else if(!candidates->right){         \
    _priv(this)->field##_median = left->field;  \
  }else{                                \
    _priv(this)->field##_median = left->field;  \
    _priv(this)->field##_median += right->field;\
    _priv(this)->field##_median /=2;         \
  }                                     \
} \


_item_median_pipe(gp)
_item_median_pipe(sr)
_item_median_pipe(fec)

FBRATargetCtrler *make_fbratargetctrler(MPRTPSPath *path)
{
  FBRATargetCtrler *result;
  result                      = g_object_new (FBRATARGETCTRLER_TYPE, NULL);
  result->path                = g_object_ref(path);
  result->id                  = mprtps_path_get_id(result->path);
  result->items               = make_slidingwindow(256, GST_SECOND);

  slidingwindow_add_plugins(result->items,
                            make_swpercentile(50, _item_gp_cmp, _item_gp_pipe, result),
                            make_swpercentile(50, _item_sr_cmp, _item_sr_pipe, result),
                            make_swpercentile(50, _item_fec_cmp, _item_fec_pipe, result),
                            NULL);

  return result;
}

void fbratargetctrler_set_initial(FBRATargetCtrler *this, gint32 target_bitrate)
{
  this->target_bitrate = target_bitrate;
}


void fbratargetctrler_signal_update(FBRATargetCtrler *this, MPRTPSubflowFECBasedRateAdaption *params)
{
  MPRTPSubflowFBRA2CngCtrlerParams *cngctrler;
  cngctrler = &params->cngctrler;
  _min_appr_int(this)                           = cngctrler->min_approve_interval;
  _appr_min_fact(this)                          = cngctrler->approve_min_factor;
  _appr_max_fact(this)                          = cngctrler->approve_max_factor;
  _min_ramp_up(this)                            = cngctrler->min_ramp_up_bitrate;
  _max_ramp_up(this)                            = cngctrler->max_ramp_up_bitrate;
  _min_target(this)                             = cngctrler->min_target_bitrate;
  _max_target(this)                             = cngctrler->max_target_bitrate;
  _appr_eps(this)                               = cngctrler->approvement_epsilon;
}

void fbratargetctrler_signal_request(FBRATargetCtrler *this, MPRTPSubflowFECBasedRateAdaption *result)
{
  MPRTPSubflowFBRA2CngCtrlerParams *cngctrler;
  cngctrler = &result->cngctrler;
  cngctrler->min_approve_interval           = _min_appr_int(this);
  cngctrler->approve_max_factor             = _appr_max_fact(this);
  cngctrler->approve_min_factor             = _appr_min_fact(this);
  cngctrler->min_ramp_up_bitrate            = _min_ramp_up(this);
  cngctrler->max_ramp_up_bitrate            = _max_ramp_up(this);
  cngctrler->min_target_bitrate             = _min_target(this);
  cngctrler->max_target_bitrate             = _max_target(this);
  cngctrler->approvement_epsilon            = _appr_eps(this);

}


void fbratargetctrler_update(FBRATargetCtrler* this, FBRAFBProcessorStat* stat)
{
  TargetItem *item;
  guint32 packets_num;

  item = &_priv(this)->items[_priv(this)->item_index];
  ++_priv(this)->item_index;
  memset(item, 0, sizeof(TargetItem));

  item->fec  = mprtps_path_get_monitored_bitrate(this->path, &packets_num);
  item->sr   = stat->sent_bytes_in_1s * 8;
  item->gp   = stat->goodput_bytes * 8;

  this->owd_corr = stat->owd_corr;

  slidingwindow_add_data(this->items, item);
  this->required_fb = CONSTRAIN(5, 20, _priv(this)->gp_median / 100000);
  slidingwindow_set_act_limit(this->items, this->required_fb);

  _priv(this)->RTT = CONSTRAIN( _min_appr_int(this), GST_SECOND, stat->RTT);
  ++this->rcved_fb;

//  _priv(this)->fraction_discarded = stat->discarded_rate;
//  stat->tendency = _priv(this)->tend_median;
}

static gdouble _off_target(FBRATargetCtrler *this, gint pow, gdouble eps)
{
  gint32 refpoint;
  gdouble result;
  gint i;
  refpoint = MAX(_min_target(this), this->bottleneck_point);
  if(this->target_bitrate <= refpoint){
    return 0.;
  }
  result = this->target_bitrate - refpoint;
  result /= this->target_bitrate * eps;

  for(i=1; i<pow; ++i) result*=result;

  result = CONSTRAIN(0.,1., result);

  return result;
}


static guint _get_monitoring_interval(FBRATargetCtrler *this)
{
  guint interval;
  gdouble off;
  gdouble epsilon;
  //  off = _off_target(this, 1, _mon_eps(this));

  {
    gdouble refpoint;
    refpoint = MAX(_min_target(this), this->bottleneck_point);
    epsilon = MIN(.25, (gdouble) _max_ramp_up(this) / refpoint);
  }

//  off = _off_target(this, 2, _mon_eps(this));
  off = _off_target(this, 2, epsilon);

  interval = off * _mon_min_int(this) + (1.-off) * _mon_max_int(this);

  while(this->target_bitrate / interval < _min_ramp_up(this) && _mon_min_int(this) < interval){
    --interval;
  }
  while(_max_ramp_up(this) < this->target_bitrate / interval && interval < _mon_max_int(this)){
    ++interval;
  }

  return interval;
}

void fbratargetctrler_probe(FBRATargetCtrler *this)
{
  this->monitoring_started  = this->probed = _now(this);
  this->monitoring_interval = _get_monitoring_interval(this);
  this->probe_approvement   = FALSE;
  this->monitoring_reached  = 0;
  this->stable_point        = this->target_bitrate;
  mprtps_path_set_monitoring_interval(this->path, this->monitoring_interval);
}

void _stop_probe_and_acceleration(FBRATargetCtrler *this)
{
  this->monitoring_started  = 0;
  this->monitoring_interval = 0;
  this->probe_approvement   = FALSE;
  this->monitoring_reached  = 0;
  mprtps_path_set_monitoring_interval(this->path, 0);

  this->dtarget = 0;
}

void fbratargetctrler_update_rtpavg(FBRATargetCtrler* this, gint32 payload_length)
{
  if(_priv(this)->avg_rtp_payload == 0.){
      _priv(this)->avg_rtp_payload = payload_length;
      return;
  }
  _priv(this)->avg_rtp_payload *= .9;
  _priv(this)->avg_rtp_payload += .1 * payload_length;
}



static void _corrigate(FBRATargetCtrler *this)
{
  gdouble off;
  gdouble tend;
  gdouble bitrate;

  off = _off_target(this, 2, .25);

  if(1. <= off){
    return;
  }

  tend = CONSTRAIN(-.5, .5, _priv(this)->tend_median);
  bitrate  = _min_ramp_up(this) / (gdouble)this->required_fb * (1.-off) * tend;

  this->target_bitrate += (tend < 0 ? .01 : -.5) * bitrate;

//  g_print("[DEBUG] :: tend_median(%f), factor(%f)\n", _priv(this)->tend_median, factor);

}


//We know that we are in KEEP stage and we have distortion, so corrigate based on GP and tendency
void fbratargetctrler_halt(FBRATargetCtrler* this)
{
  gdouble OF; //Overusing Factor
  _stop_probe_and_acceleration(this);
  OF = (gdouble)this->target_bitrate / (gdouble)(_priv(this)->gp_median);

  if(1.1 < OF){
    this->bottleneck_point = _priv(this)->gp_median;
    this->target_bitrate = this->bottleneck_point * .8;
    goto reduced;
  }

  this->bottleneck_point = this->target_bitrate;
  _corrigate(this);

reduced:
  this->changed = this->halted = _now(this);
  this->target_approvement = FALSE;
  this->rcved_fb = 0;
  this->reached  = 0;
}

//We know that we have distortion so the probe is failed go back to the stable point
void fbratargetctrler_revert(FBRATargetCtrler* this)
{
  gdouble OF; //Overusing Factor
  _stop_probe_and_acceleration(this);
  OF = (gdouble)this->target_bitrate / (gdouble)(_priv(this)->gp_median + _priv(this)->fec_median);

  if(1.1 < OF){
    this->bottleneck_point = _priv(this)->gp_median + _priv(this)->fec_median / 2;
    this->target_bitrate = this->bottleneck_point * .85;
    goto reduced;
  }

  this->bottleneck_point = (this->stable_point + _priv(this)->gp_median ) / 2;
  this->target_bitrate = this->bottleneck_point * .85;

reduced:
  this->changed = this->reverted = _now(this);
  this->target_approvement = FALSE;
  this->rcved_fb = 0;
  this->reached  = 0;
}

//we know that this situation is serius
void fbratargetctrler_break(FBRATargetCtrler* this)
{
  gdouble OF; //Overusing Factor
  _stop_probe_and_acceleration(this);
  OF = (gdouble)this->target_bitrate / (gdouble)(_priv(this)->gp_median + _priv(this)->fec_median);

  if(1.1 < OF){
    this->bottleneck_point = _priv(this)->gp_median;
    this->target_bitrate = this->bottleneck_point * .6;
    this->undershoot_target = this->target_bitrate * .2;
    goto reduced;
  }

  if(this->probed < this->broke){
    _refresh_target_approvement(this);
    if(!this->target_approvement){
      goto done;
    }
  }

  this->bottleneck_point = MIN(_priv(this)->gp_median, this->target_bitrate) * .9;
  this->target_bitrate = this->bottleneck_point * .85;
  this->undershoot_target = this->target_bitrate * .2;

reduced:
  this->changed = this->broke = _now(this);
  this->target_approvement = FALSE;
  this->rcved_fb = 0;
  this->reached  = 0;

done:
  return;

}

void fbratargetctrler_accelerate(FBRATargetCtrler* this)
{
  gint32 new_target;
  this->changed = this->increased = _now(this);
  this->rcved_fb = 0;
  this->stable_point = MIN(this->target_bitrate, _priv(this)->gp_median);
  new_target = this->stable_point + _priv(this)->fec_median;

  this->gp_point = _priv(this)->gp_median;
  this->dtarget = this->target_bitrate < new_target ? new_target - this->target_bitrate : 0;
  this->target_bitrate = new_target;
//  this->target_bitrate = CONSTRAIN(this->stable_point, _priv(this)->gp_median * 2, this->target_bitrate + _priv(this)->fec_median);

  this->target_approvement = FALSE;
}


static void _refresh_interval(FBRATargetCtrler* this)
{
  gdouble off;
  gdouble interval;

  off = _off_target(this, 2, _interval_eps(this));
  interval = off * _appr_min_fact(this) + (1.-off) * _appr_max_fact(this);
  this->interval =  CONSTRAIN(.1 * GST_SECOND,  .6 * GST_SECOND, interval * _RTT(this));
}

void _refresh_target_approvement(FBRATargetCtrler* this)
{
  gint32 boundary;
  if(this->changed < this->reached && this->target_approvement){
    goto done;
  }

  if(this->rcved_fb < this->required_fb){
    goto done;
  }

  boundary = CONSTRAIN(10000,50000,this->target_bitrate * (1.-_priv(this)->approvement_epsilon));

  if(_priv(this)->gp_median < this->target_bitrate - boundary){
    goto done;
  }

  if(boundary + this->target_bitrate < _priv(this)->gp_median){
    goto done;
  }

  if(_priv(this)->sr_median < this->target_bitrate * (1.-_priv(this)->approvement_epsilon)){
    goto done;
  }

  if((1.+_priv(this)->approvement_epsilon) * this->target_bitrate < _priv(this)->sr_median){
    goto done;
  }

  boundary = MAX(this->dtarget - 20000, this->dtarget * (1.-_priv(this)->approvement_epsilon));
  if(0 < this->dtarget && _priv(this)->gp_median < this->gp_point + boundary){
    goto done;
  }

  if(this->reached < this->changed){
    this->reached = _now(this);
    goto done;
  }

  _refresh_interval(this);
  if(_now(this) < this->reached + this->interval){
    goto done;
  }
  this->target_approvement = TRUE;
done:
//  g_print("[DEBUG] :: _refresh_target_approvement :: approvement(%d)\n", this->target_approvement);
  return;
}


static void _refresh_probe_approvement(FBRATargetCtrler* this)
{
  if(this->monitoring_started < this->monitoring_reached && this->probe_approvement){
    goto done;
  }

  if(_priv(this)->fec_median < (gdouble)(this->target_bitrate * .9) / (gdouble)this->monitoring_interval){
    goto done;
  }

  if(this->monitoring_reached < this->monitoring_started){
    this->monitoring_reached = _now(this);
    goto done;
  }

  _refresh_interval(this);
  if(_now(this) < this->monitoring_reached + this->interval){
    goto done;
  }
  this->probe_approvement = TRUE;
done:
//  g_print("[DEBUG] :: _refresh_target_approvement :: approvement(%d)\n", this->target_approvement);
  return;
}

gboolean fbratargetctrler_get_approvement(FBRATargetCtrler* this)
{
  _refresh_target_approvement(this);
  if(!this->monitoring_started){
    return this->target_approvement;
  }
  _refresh_probe_approvement(this);
  return this->target_approvement && this->probe_approvement;
}

gboolean fbratargetctrler_get_target_approvement(FBRATargetCtrler* this)
{
  _refresh_target_approvement(this);
  return this->target_approvement;
}

gboolean fbratargetctrler_get_probe_approvement(FBRATargetCtrler* this)
{
  _refresh_probe_approvement(this);
  return this->probe_approvement;
}

gint32 fbratargetctrler_get_target_rate(FBRATargetCtrler* this)
{
  return this->target_bitrate;
}
/*
                             s
X_Bps = -----------------------------------------------
        R * (sqrt(2*p/3) + 12*sqrt(3*p/8)*p*(1+32*p^2))
 * */
static gint32 _get_tfrc(FBRATargetCtrler *this)
{
  gdouble result = 0.;
  gdouble rtt,p = 0.;
  rtt = _RTT(this);
  rtt /= (gdouble) GST_SECOND;
  //p   =  _FD(this);
//  p = _priv(this)->fraction_discarded;
//  p = 0.1;
  if(p == 0.) p = 0.05;
  result = _priv(this)->avg_rtp_payload * 8;
  result /= rtt * (sqrt(2.*p/3.)) + 12.*sqrt(3.*p/8.) * p * (1+32*p*p);
//  result /= rtt * sqrt((2*p)/3);
//  g_print("TFRC: %f=%f/(%f * (sqrt(2*%f/3) + 12*sqrt(3*%f/8)*p*(1+32*%f^2)))\n",
//          result, _priv(this)->avg_rtp_payload * 8,
//          rtt, p, p, p);
  return result;
}

void fbratargetctrler_undershooting(FBRATargetCtrler* this){
  if(this->undershooting){
    return;
  }
  this->undershooting_started = _now(this);
  this->undershooting = TRUE;
}

void fbratargetctrler_refresh_target(FBRATargetCtrler* this)
{

  _refresh_target_approvement(this);
  if(this->changed < this->reached){
    DISABLE_LINE _corrigate(this);
  }

  if(0 < _priv(this)->min_target_bitrate){
    gint32 min_target;
    min_target = MAX(_get_tfrc(this), _priv(this)->min_target_bitrate);
    this->target_bitrate = MAX(min_target, this->target_bitrate);
  }
  if(0 < _priv(this)->max_target_bitrate){
    this->target_bitrate = MIN(_priv(this)->max_target_bitrate, this->target_bitrate);
  }
//
//  g_print("[DEBUG] TR: %-7d|GP: %-7d|SR: %-7d|%1.2f|btl: %-7d|FEC: %-7d|owd: %1.2f|tend: %-1.3f|tapr: %d|papr: %d|stbl: %-7d|V %d|^ %d\n",
//          this->target_bitrate, _priv(this)->gp_median,  _priv(this)->sr_median,
//          (gdouble)_priv(this)->sr_median / (gdouble)_priv(this)->gp_median,
//          this->bottleneck_point,  _priv(this)->fec_median, this->owd_corr,
//          _priv(this)->tend_median,
//          this->target_approvement, this->probe_approvement, this->stable_point,
//          this->refreshed < this->broke, this->refreshed < this->increased);

//  if(this->undershooting){
//	this->undershoot_target = MAX(_priv(this)->min_target_bitrate, this->undershoot_target);
//	mprtps_path_set_target_bitrate(this->path, this->undershoot_target);
//	if(this->undershooting_started < _now(this) - CONSTRAIN(.05 * GST_SECOND, .3 * GST_SECOND, 2 * _RTT(this))){
//	  this->undershoot_target = (this->undershoot_target * 4 + this->target_bitrate) / 5;
//    }
//	if(this->undershooting_started < _now(this) - CONSTRAIN(.05 * GST_SECOND, .3 * GST_SECOND, 5 * _RTT(this))){
//		this->undershooting = FALSE;
//	}
//  }else{
//	mprtps_path_set_target_bitrate(this->path, this->target_bitrate);
//  }

  if(this->broke <= this->reached){
    mprtps_path_set_target_bitrate(this->path, this->target_bitrate);
  }else{
	this->undershoot_target = MAX(_priv(this)->min_target_bitrate, this->undershoot_target);
	mprtps_path_set_target_bitrate(this->path, this->undershoot_target);
	if(this->broke < _now(this) - CONSTRAIN(.05 * GST_SECOND, .3 * GST_SECOND, 2 * _RTT(this))){
      this->undershoot_target = (this->undershoot_target * 4 + this->target_bitrate) / 5;
	}

  }
  mprtps_path_set_target_bitrate(this->path, this->target_bitrate);

  this->refreshed = _now(this);
}


