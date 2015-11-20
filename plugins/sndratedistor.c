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
#include "sndratedistor.h"
//#include "mprtpspath.h"
#include <math.h>
#include <gst/gst.h>
#include <stdlib.h>
#include "streamtracker.h"
#include <string.h>


GST_DEBUG_CATEGORY_STATIC (sndrate_distor_debug_category);
#define GST_CAT_DEFAULT sndrate_distor_debug_category
#define MEASUREMENT_LENGTH 3
G_DEFINE_TYPE (SendingRateDistributor, sndrate_distor, G_TYPE_OBJECT);

typedef struct _Subflow Subflow;
typedef struct _Measurement Measurement;
typedef struct _KalmanFilter KalmanFilter;
typedef void (*Procedure)(SendingRateDistributor*,Subflow*);
typedef enum{
  STATE_OVERUSED       = -2,
  STATE_UNSTABLE       = -1,
  STATE_HALT           =  1,
  STATE_STABLE         =  2,
}State;

struct _Measurement{
  gdouble         corrl_owd;
  gdouble         corrh_owd;
  gdouble         variance;
  guint32         goodput;
  guint32         jitter;
  guint32         receiver_rate;
  State           state;
};

struct _KalmanFilter{
  gdouble P;
  gdouble P_hat;
  gdouble R;
  gdouble SR;
  gdouble K;
};


struct _Subflow{
  guint8          id;
  MPRTPSPath*     path;
  gboolean        available;
  gdouble         fallen_rate;

  guint8          joint_subflow_ids[SNDRATEDISTOR_MAX_NUM];
  guint32         extra_bytes;
  guint           monitoring_interval;
  guint           monitoring_time;
  guint32         monitored_bytes;
  gint32          delta_sr;
  guint32         given_bytes;
  guint32         sending_rate;
  gdouble         weight;

  guint           waited;
  Procedure       check;
  Procedure       procedure;
  State           state;

  KalmanFilter    kalman_filter;
  gboolean        measured;
  Measurement     measurements[MEASUREMENT_LENGTH];
  guint8          measurements_index;
};

//----------------------------------------------------------------------
//-------- Private functions belongs to the object ----------
//----------------------------------------------------------------------

static const gdouble ST_ = 1.1; //Stable treshold
static const gdouble OT_ = 2.;  //Overused treshold
static const gdouble DT_ = 1.5; //Down Treshold
static const gdouble MT_ = 1.2; //Monitoring Overused treshold

static void
sndrate_distor_finalize (
    GObject * object);


//--------------------MEASUREMENTS-----------------------
static Measurement*
_mt0(Subflow* this);

static Measurement*
_mt1(Subflow* this);

//static Measurement*
//_mt2(Subflow* this);

static Measurement*
_m_step(Subflow *this);

//----------------------STATES-------------------

static void
_check_null(
    SendingRateDistributor *this,
    Subflow *subflow);

static void
_check_stablility(
    SendingRateDistributor *this,
    Subflow *subflow);

static void
_state_overused(
    SendingRateDistributor *this,
    Subflow *subflow);

static void
_state_halt(
    SendingRateDistributor *this,
    Subflow *subflow);

static void
_state_unstable(
    SendingRateDistributor *this,
    Subflow *subflow);

static void
_state_stable(
    SendingRateDistributor *this,
    Subflow *subflow);

static gboolean
_handle_given_bytes(
    SendingRateDistributor *this,
    Subflow *subflow);
//-----------------------ACTIONS------------------
static void
_transit_to(
    Subflow *subflow,
    State target);


static void
_disable_monitoring(
    Subflow* subflow);

static void
_enable_monitoring(
    Subflow *subflow);

static void
_recalc_monitoring(
    Subflow *subflow);


static gint32
_bounce_back(
    SendingRateDistributor *this,
    Subflow *subflow);

static gint32
_undershoot(
    SendingRateDistributor *this,
    Subflow *subflow);

static guint32
_supply_bytes(
    SendingRateDistributor *this,
    Subflow *subflow);

static guint32
_take_bytes(
    SendingRateDistributor *this,
    Subflow *subflow);

static void
_kalman_filter_measurement_update(
    Subflow *this,
    gdouble RR,
    gdouble R);

static void
_kalman_filter_init(
    Subflow *this);

#define _get_subflow(this, n) ((Subflow*)(this->subflows + n * sizeof(Subflow)))
//static Subflow*
//_get_subflow(
//    SendingRateDistributor *this,
//    guint8 id);
//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
sndrate_distor_class_init (SendingRateDistributorClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = sndrate_distor_finalize;

  GST_DEBUG_CATEGORY_INIT (sndrate_distor_debug_category, "sndrate_distor", 0,
      "MpRTP Sending Rate Distributor");
}

void
sndrate_distor_finalize (GObject * object)
{
  SendingRateDistributor * this;
  this = SNDRATEDISTOR(object);
  g_object_unref(this->sysclock);
  g_free(this->subflows);
  while(!g_queue_is_empty(this->free_ids)){
    g_free(g_queue_pop_head(this->free_ids));
  }
}


void
sndrate_distor_init (SendingRateDistributor * this)
{
  gint i;
  this->sysclock = gst_system_clock_obtain();
  this->free_ids = g_queue_new();
  this->counter = 0;
  this->media_rate = 0.;
  this->subflows = g_malloc0(sizeof(Subflow)*SNDRATEDISTOR_MAX_NUM);
  for(i=0; i<SNDRATEDISTOR_MAX_NUM; ++i){
      _get_subflow(this, i)->id = i;
    _get_subflow(this, i)->available = FALSE;
    _get_subflow(this, i)->joint_subflow_ids[i] = 1;
  }
}



SendingRateDistributor *make_sndrate_distor(void)
{
  SendingRateDistributor *result;
  result = g_object_new (SNDRATEDISTOR_TYPE, NULL);
  return result;
}

guint8 sndrate_distor_request_id(SendingRateDistributor *this,
                                 MPRTPSPath *path,
                                 guint32 sending_rate)
{
  guint8 result = 0;
  guint8* id;
  if(this->counter > SNDRATEDISTOR_MAX_NUM){
    g_error("TOO MANY SUBFLOW IN CONTROLLER. ERROR!");
    goto exit;
  }
  if(g_queue_is_empty(this->free_ids)){
    result = this->max_id;
    id = &result;
    ++this->max_id;
    goto done;
  }
  id = g_queue_pop_head(this->free_ids);
  result = *id;
  g_free(id);
done:
  memset(_get_subflow(this, *id), 0, sizeof(Subflow));
  _get_subflow(this, *id)->id = *id;
  _get_subflow(this, *id)->available = TRUE;
  _get_subflow(this, *id)->sending_rate = sending_rate;
  _get_subflow(this, *id)->path = g_object_ref(path);
  _kalman_filter_init(_get_subflow(this, *id));
  _transit_to(_get_subflow(this, *id), STATE_STABLE);
  ++this->counter;
exit:

  return result;
}

void sndrate_distor_remove_id(SendingRateDistributor *this, guint8 id)
{
  guint8 *free_id;
  if(!_get_subflow(this, id)->available) goto done;
  free_id = g_malloc(sizeof(guint8));
  *free_id = id;
  if(_get_subflow(this, id)->monitoring_interval){
    mprtps_path_set_monitor_interval(_get_subflow(this, id)->path, 0);
  }
  g_object_unref(_get_subflow(this, id)->path);
  _get_subflow(this, id)->available = FALSE;
  g_queue_push_tail(this->free_ids, free_id);
done:
  return;
}

void sndrate_distor_measurement_update(SendingRateDistributor *this,
                                       guint8 id,
                                       guint32 goodput,
                                       guint32 receiver_rate,
                                       gdouble variance,
                                       gdouble corrh_owd,
                                       gdouble corrl_owd)
{
  Subflow *subflow;
  subflow = _get_subflow(this, id);
  if(!subflow->available) goto done;
  if(!subflow->measured) _m_step(subflow);
  subflow->measured = TRUE;
  _mt0(subflow)->corrh_owd = corrh_owd;
  _mt0(subflow)->corrl_owd = corrl_owd;
  _mt0(subflow)->goodput = goodput;
  //  _mt0(subflow)->variance = variance;
    _mt0(subflow)->variance = .1;
  _mt0(subflow)->receiver_rate = receiver_rate;
  _kalman_filter_measurement_update(subflow, receiver_rate, .1);

//  g_print("Subflow %d-%d<-%p measurement update.\n"
//          "State: %d, CorrH: %f, CorrL: %f, GP: %u, V: %f\n",
//          id,
//          subflow->id,
//          subflow,
//          _mt0(subflow)->state,
//          _mt0(subflow)->corrh_owd,
//          _mt0(subflow)->corrl_owd,
//          _mt0(subflow)->goodput,
//          _mt0(subflow)->variance);
done:
  return;
}


void sndrate_distor_time_update(SendingRateDistributor *this, guint32 media_rate)
{
  gint id;
  Subflow *subflow;
  if(media_rate)
    this->media_rate = media_rate;
  else
    this->media_rate = 512000;
  //1. Init
  this->stable_sr_sum = 0;
  this->taken_bytes = 0;
  this->supplied_bytes = 0;
  this->fallen_bytes = 0;

  //2. Perform Overused and Underused and check and calculate sending rates
  //regarding to the kalman filter
  for(id=0; id < SNDRATEDISTOR_MAX_NUM; ++id){
    KalmanFilter *kf;
    subflow = _get_subflow(this, id);
    if(!subflow->available) continue;
    kf = &subflow->kalman_filter;
    kf->P+=.1;
    subflow->sending_rate = kf->SR;
    if((gint)subflow->state != STATE_STABLE)
      subflow->procedure(this, subflow);
    else
      subflow->check(this, subflow);
    if(subflow->state == STATE_STABLE)
      this->stable_sr_sum+=subflow->sending_rate;
  }

  //3. Give bytes by the controller
  if(!this->fallen_bytes)
    goto perform;

  for(id=0; id < SNDRATEDISTOR_MAX_NUM; ++id){
      subflow = _get_subflow(this, id);
      if(!subflow->available) continue;
      if(subflow->state != STATE_STABLE) continue;
      subflow->given_bytes = 0;
      if(subflow->extra_bytes > 0){
        if(subflow->extra_bytes < this->fallen_bytes){
          subflow->given_bytes = this->fallen_bytes;
          this->fallen_bytes = 0;
          goto perform;
        }
        subflow->given_bytes += subflow->extra_bytes;
        this->fallen_bytes -= subflow->given_bytes;
      }
      if(!subflow->monitored_bytes) continue;
      if(subflow->monitored_bytes < this->fallen_bytes){
        subflow->given_bytes+=this->fallen_bytes;
        goto perform;
      }
      subflow->given_bytes+=subflow->monitored_bytes;
      this->fallen_bytes-=subflow->monitored_bytes;
    }

  perform:
  //4. Perform Stable and Halt procedures
  for(id=0; id < SNDRATEDISTOR_MAX_NUM; ++id){
    subflow = _get_subflow(this, id);
    if(!subflow->available) continue;
    if((gint)subflow->state != STATE_STABLE) continue;
    subflow->procedure(this, subflow);
  }

  //5. Calculate next sending rates
  for(id=0; id < SNDRATEDISTOR_MAX_NUM; ++id){
    subflow = _get_subflow(this, id);
    if(!subflow->available) continue;
    g_print("S%d subflow->SR: %u + %d = %u\n",
            subflow->id,
            subflow->sending_rate,
            subflow->delta_sr,
            subflow->sending_rate + subflow->delta_sr);
    subflow->sending_rate = MAX(0, subflow->sending_rate + subflow->delta_sr);
    subflow->delta_sr = 0;
    subflow->given_bytes = 0;
    subflow->measured = FALSE;
  }


  return;
}

guint32 sndrate_distor_get_sending_rate(SendingRateDistributor *this, guint8 id)
{
  return _get_subflow(this, id)->sending_rate;
}



Measurement* _mt0(Subflow* this)
{
  return &this->measurements[this->measurements_index];
}

Measurement* _mt1(Subflow* this)
{
  guint8 index;
  if(this->measurements_index == 0) index = MEASUREMENT_LENGTH-1;
  else index = this->measurements_index-1;
  return &this->measurements[index];
}


//Measurement* _mt2(Subflow* this)
//{
//  guint8 index;
//  if(this->measurements_index == 1) index = MEASUREMENT_LENGTH-1;
//  else if(this->measurements_index == 0) index = MEASUREMENT_LENGTH-2;
//  else index = this->measurements_index-2;
//  return &this->measurements[index];
//}

Measurement* _m_step(Subflow *this)
{
  if(++this->measurements_index == MEASUREMENT_LENGTH){
    this->measurements_index = 0;
  }
  memset(&this->measurements[this->measurements_index], 0, sizeof(Measurement));
  return _mt0(this);
}

void
_check_null(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  //nothing to check
}

void
_check_stablility(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  if(_mt0(subflow)->corrh_owd > DT_){
    _disable_monitoring(subflow);
    subflow->delta_sr-=_undershoot(this, subflow);
    g_print("S%d stable fall, SR: %u DSR: %d\n",
            subflow->id,
            subflow->sending_rate,
            subflow->delta_sr);
    _transit_to(subflow, STATE_OVERUSED);
    goto done;
  }
  if(_mt0(subflow)->corrh_owd > ST_){
    _disable_monitoring(subflow);
    _transit_to(subflow, STATE_UNSTABLE);
    goto done;
  }
done:
  return;
}

void
_state_overused(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  g_print("S%d is OVERUSED\n",
          subflow->id);
  if(subflow->waited < 1){
    ++subflow->waited;
    goto done;
  }
  subflow->waited = 0;
  if(_mt0(subflow)->corrh_owd > OT_){
    subflow->delta_sr-=_undershoot(this, subflow);
    goto done;
  }
  subflow->delta_sr+=_bounce_back(this, subflow);
  _transit_to(subflow, STATE_HALT);
done:
  return;
}

void
_state_halt(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  g_print("S%d CHECK HALT\n",
           subflow->id);

  subflow->extra_bytes = 0;
  _transit_to(subflow, STATE_STABLE);
  return;
}

void
_state_unstable(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  g_print("S%d CHECK UNSTABLE\n",
           subflow->id);

  if(_mt0(subflow)->corrh_owd > ST_){
    subflow->delta_sr-=_undershoot(this, subflow);
    _transit_to(subflow, STATE_OVERUSED);
    goto done;
  }
  _transit_to(subflow, STATE_STABLE);
done:
  return;
}


void _state_stable(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  gboolean monitoring_controlled = FALSE;

  g_print("S%d RB: %u GB: %u FB: %u\n",
          subflow->id,
          this->requested_bytes,
          subflow->given_bytes,
          this->fallen_bytes);
  if(this->requested_bytes > 0){
    guint32 supplied_bytes;
    supplied_bytes = _supply_bytes(this, subflow);
    subflow->extra_bytes+=supplied_bytes;
    subflow->delta_sr-=supplied_bytes;
  }
  if(subflow->given_bytes > 0){
   monitoring_controlled = _handle_given_bytes(this, subflow);
  }
  if(this->fallen_bytes > 0){
    guint32 taken_bytes;
    taken_bytes = _take_bytes(this, subflow);
    g_print("S%d: Took bytes: %u\n", subflow->id, taken_bytes);
    subflow->delta_sr+=taken_bytes;
    _disable_monitoring(subflow);
    goto done;
  }
  if(_mt1(subflow)->state != STATE_STABLE || monitoring_controlled){
    goto done;
  }
  if(subflow->monitoring_interval > 0){
    subflow->monitored_bytes = 1./(gdouble)subflow->monitoring_interval * subflow->sending_rate;
    _recalc_monitoring(subflow);
  }else{
    _enable_monitoring(subflow);
  }

done:
  return;
}

gboolean _handle_given_bytes(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  gboolean monitoring_controlled = FALSE;
  subflow->delta_sr+=subflow->given_bytes;
  if(subflow->given_bytes < subflow->extra_bytes){
    subflow->extra_bytes-=subflow->given_bytes;
    goto done;
  }
  subflow->given_bytes-=subflow->extra_bytes;
  subflow->extra_bytes = 0;
  if(!subflow->monitoring_interval)
    goto done;
  monitoring_controlled = TRUE;
  if(subflow->monitored_bytes <= subflow->given_bytes){
    _disable_monitoring(subflow);
    goto done;
  }
  //recalc monitored bytes
  {
    guint DMB,NMB;
    DMB = subflow->monitored_bytes - subflow->given_bytes;
    do{
      ++subflow->monitoring_interval;
      NMB = 1./(gdouble) (subflow->monitoring_interval) * (gdouble)subflow->sending_rate;
    }while(subflow->monitoring_interval < 14 && DMB<NMB);
    if(subflow->monitoring_interval > 13){
      _disable_monitoring(subflow);
    }
    else{
      mprtps_path_set_monitor_interval(subflow->path, subflow->monitoring_interval);
    }
  }
done:
  subflow->given_bytes=0;
  return monitoring_controlled;
}

//------------------------ACTIONS-----------------------------

void
_transit_to(
    Subflow *subflow,
    State target)
{
  switch(target){
    case STATE_HALT:
      subflow->procedure = _state_halt;
      subflow->check = _check_null;
    break;
    case STATE_OVERUSED:
      subflow->procedure = _state_overused;
      subflow->check = _check_null;
    break;
    case STATE_STABLE:
      subflow->procedure = _state_stable;
      subflow->check = _check_stablility;
    break;
    case STATE_UNSTABLE:
      subflow->procedure = _state_unstable;
      subflow->check = _check_null;
    break;
  }
  _mt0(subflow)->state = subflow->state = target;
}


void _disable_monitoring(Subflow* subflow)
{
  if(!subflow->available) goto done;
  subflow->monitored_bytes = 0;
  subflow->monitoring_interval = 0;
  mprtps_path_set_monitor_interval(subflow->path, 0);
done:
  return;
}

void _enable_monitoring(Subflow *subflow)
{
  guint8 monitoring_interval = 8;
  subflow->monitoring_time = 0;
  if(1) {subflow->monitoring_interval = 0; goto assign;}
  if(!subflow->available) goto done;
  if(subflow->fallen_rate == 0.) goto assign;
  monitoring_interval = MAX(8., -1./(2.*log2(subflow->fallen_rate)));
assign:
  subflow->monitoring_interval = monitoring_interval;
  mprtps_path_set_monitor_interval(subflow->path, subflow->monitoring_interval);
done:
  return;
}

void _recalc_monitoring(Subflow *subflow)
{
  if(1) goto done;
  if(_mt0(subflow)->corrl_owd > MT_){
    subflow->monitoring_time = 0;
    if(subflow->monitoring_interval < 14)
      ++subflow->monitoring_interval;
    goto done;
  }
  if(++subflow->monitoring_time < 3){
    goto done;
  }
  subflow->monitoring_time = 0;
  if(subflow->monitoring_interval > 2)
    --subflow->monitoring_interval;

done:
  mprtps_path_set_monitor_interval(subflow->path, subflow->monitoring_interval);
  return;
}


gint32 _bounce_back(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  gint32 requested_bytes;
  requested_bytes = _mt1(subflow)->goodput * .9 - subflow->sending_rate;
  if(requested_bytes > 0) this->requested_bytes+=requested_bytes;
  return requested_bytes;
}


gint32 _undershoot(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  gint32 fallen_bytes = 0;
  subflow->fallen_rate = 0.;
  if(subflow->sending_rate < _mt0(subflow)->goodput) goto done;
  fallen_bytes = subflow->sending_rate-_mt0(subflow)->goodput;
  subflow->fallen_rate = (gdouble)fallen_bytes / (gdouble) subflow->sending_rate;
  g_print("S%d: Undershooting, SR:%u fallen bytes: %u GP: %u\n",
          subflow->id, subflow->sending_rate,
          fallen_bytes, _mt0(subflow)->goodput);
done:
  if(fallen_bytes > 0) this->fallen_bytes+=fallen_bytes;
  return fallen_bytes;
}

guint32 _supply_bytes(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  guint32 supplied_bytes;
  gdouble rate;
  rate = (gdouble) subflow->sending_rate / (gdouble)this->stable_sr_sum;
  supplied_bytes = (gdouble)this->requested_bytes * rate;
  this->supplied_bytes+=supplied_bytes;
  return supplied_bytes;
}

guint32 _take_bytes(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  guint32 taken_bytes;
  gdouble rate;
  rate = (gdouble) subflow->sending_rate / (gdouble)this->stable_sr_sum;
  //limitation!!!
  taken_bytes = (gdouble)this->fallen_bytes * rate;
  this->taken_bytes+= taken_bytes;
  return taken_bytes;
}

void _kalman_filter_measurement_update(Subflow *this, gdouble RR, gdouble R)
{
  KalmanFilter* kf;
  gdouble SR;
  SR = (gdouble)this->sending_rate;
  kf = &this->kalman_filter;

//  g_print("S:%d->KF1: K: %f, SR: %f, P: %f, R: %f, RR: %f\n",
//          this->id, kf->K, kf->SR, kf->P, R, RR);

  kf->K = kf->P/(kf->P + R);
  kf->SR = SR + kf->K * (RR - SR);
  kf->P = (1.-kf->K) *  kf->P;

//  g_print("S:%d->KF2: K: %f, SR: %f, P: %f, R: %f, RR: %f\n",
//          this->id, kf->K, kf->SR, kf->P, R, RR);
}

void _kalman_filter_init(Subflow *this)
{
  KalmanFilter* kf;
  kf = &this->kalman_filter;
  kf->K = 0.;
  kf->P = 1.;
  kf->R = 0.;
  kf->P_hat = 1.;
  kf->SR = 0.;
}
