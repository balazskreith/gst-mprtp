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
//typedef struct _KalmanFilter KalmanFilter;


typedef enum{
  STATE_OVERUSED       = -1,
  STATE_UNSTABLE       = 0,
//  STATE_HALT           =  1,
  STATE_STABLE         =  1,
}State;

typedef void (*Controller)(SendingRateDistributor*,Subflow*);
typedef State (*Checker)(SendingRateDistributor*,Subflow*);

typedef struct _Utilization{
  gint     delta_sr;
  gboolean accepted;
}Utilization;

struct _Measurement{
  gdouble         corrPiT;
  gdouble         corrl_owd;
  gdouble         corrh_owd;
  gdouble         variance;
  guint32         goodput;
  gboolean        lost;
  gboolean        discard;
  guint32         jitter;
  guint32         receiver_rate;
  State           state;
};



struct _Subflow{
  guint8          id;
  MPRTPSPath*     path;
  gboolean        available;

  gint32          taken_bytes;
  gint32          supplied_bytes;
  gint32          sending_rate;

  //Need for monitoring
  guint           monitoring_interval;
  guint           monitoring_time;
  guint32         monitored_bytes;

  guint           disable_check;
  Checker         checking;

  //need for state transitions
  State           state;

  //Need for measurements
  gboolean        measured;
  Measurement     measurements[MEASUREMENT_LENGTH];
  guint8          measurements_index;

  //Talon
  gint32          fallen_bytes;

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

static State
_check_overused(
    SendingRateDistributor *this,
    Subflow *subflow);

static State
_check_unstable(
    SendingRateDistributor *this,
    Subflow *subflow);

static State
_check_stable(
    SendingRateDistributor *this,
    Subflow *subflow);

//-----------------------ACTIONS------------------
static void
_transit_to(
    Subflow *subflow,
    State target);

static void
_disable_checking(
    Subflow* subflow,
    guint disable_ticks);

static guint32
_get_monitoring_bytes(
    Subflow *this,
    guint32 limit);

static void
_disable_monitoring(
    Subflow* subflow);

static void
_enable_monitoring(
    Subflow *subflow);

static void
_change_monitoring(
    Subflow *subflow,
    guint interval);


static gint32
_action_request(
    SendingRateDistributor *this,
    Subflow *subflow);

static gint32
_action_fall(
    SendingRateDistributor *this,
    Subflow *subflow);

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

//  _mprtp_media_rate_utilization_signal =
//      g_signal_new ("mprtp-media-rate-utilization",
//                    G_TYPE_FROM_CLASS (klass),
//                    G_SIGNAL_RUN_LAST,
//                    G_STRUCT_OFFSET (SendingRateDistributorClass, media_rate_utilization),
//                    NULL, NULL, g_cclosure_marshal_generic, G_TYPE_BOOLEAN, 1,
//                    G_TYPE_INT);

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
//    _get_subflow(this, i)->joint_subflow_ids[i] = 1;
  }
}



SendingRateDistributor *make_sndrate_distor(SignalRequestFunc signal_request, gpointer controller)
{
  SendingRateDistributor *result;
  result = g_object_new (SNDRATEDISTOR_TYPE, NULL);
  result->signal_request = signal_request;
  result->signal_controller = controller;
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
                                       gdouble corrl_owd,
                                       gdouble corrPiT,
                                       gboolean lost,
                                       gboolean discard,
                                       guint8 lost_history,
                                       guint8 discard_history)
{
  Subflow *subflow;
  subflow = _get_subflow(this, id);
  if(!subflow->available) goto done;
  if(!subflow->measured) _m_step(subflow);
  subflow->measured = TRUE;
  _mt0(subflow)->corrPiT = corrPiT;
  _mt0(subflow)->corrh_owd = corrh_owd;
  _mt0(subflow)->corrl_owd = corrl_owd;
  _mt0(subflow)->goodput = goodput;
  _mt0(subflow)->lost = lost;
  _mt0(subflow)->discard = discard;
  _mt0(subflow)->variance = variance;
  _mt0(subflow)->receiver_rate = receiver_rate;
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
  gint id,i;
  Subflow *subflow;
  guint8 subflow_ids[SNDRATEDISTOR_MAX_NUM];
  guint8 subflow_ids_length = 0;
  guint32 SR_sum = 0;
  Utilization utilization = {0,FALSE};
  guint32 RR_sum = 0;

  if(media_rate)
    this->media_rate = media_rate;
  else if(!this->media_rate)
    this->media_rate = 512000;
  //1. Init
  this->stable_sr_sum = 0;
  this->taken_bytes = 0;
  this->supplied_bytes = 0;
  this->fallen_bytes = 0;
  this->requested_bytes = 0;

  //2. Init subflows, Check Stability and perform Overused and Unstable procedures
  for(id=0; id < SNDRATEDISTOR_MAX_NUM; ++id){
    subflow = _get_subflow(this, id);
    if(!subflow->available) continue;
    subflow_ids[subflow_ids_length++] = subflow->id;
    subflow->taken_bytes = 0;
    subflow->supplied_bytes = 0;
    RR_sum+=_mt0(subflow)->receiver_rate;
    if(subflow->disable_check > 0){
      --subflow->disable_check;
      continue;
    }
    _mt0(subflow)->state = subflow->checking(this, subflow);
  }

  //Utilization

//
//  //Underuse detection
//  if(this->supplied_bytes < this->requested_bytes)
//  {
//      guint32 not_supplied_bytes;
//      not_supplied_bytes = this->requested_bytes - this->supplied_bytes;
//      utilization.delta_sr += not_supplied_bytes;
//  }
//
//  //Overuse detection
//  if(this->taken_bytes < this->fallen_bytes)
//  {
//    guint32 not_taken_bytes;
//    not_taken_bytes = this->fallen_bytes - this->taken_bytes;
//    utilization.delta_sr -= not_taken_bytes;
//    //Notify the media rate producer. It must be reduced.
//  }

  //signaling
  utilization.delta_sr = this->requested_bytes - this->fallen_bytes;
  g_print("Utilization DSR = %d - %d = %d\n",
          this->requested_bytes,
          this->fallen_bytes,
          utilization.delta_sr);
  if(utilization.delta_sr != 0)
  {
    this->signal_request(this->signal_controller, &utilization);
    if(utilization.accepted)
      this->media_rate += utilization.delta_sr;
    utilization.delta_sr = 0;
  }


  //5. Calculate next sending rates.
  for(i=0; i < subflow_ids_length; ++i){
    gint32 delta_sr;
    id = subflow_ids[i];
    subflow = _get_subflow(this, id);
    delta_sr = subflow->taken_bytes - subflow->supplied_bytes;
    subflow->sending_rate+=delta_sr;
    SR_sum += subflow->sending_rate;
    subflow->measured = FALSE;
  }

  //6. calculate weights
  for(i=0; i < subflow_ids_length; ++i){
    gdouble weight;
    id = subflow_ids[i];
    subflow = _get_subflow(this, id);
    weight = (gdouble)subflow->sending_rate / (gdouble)SR_sum;
    subflow->sending_rate = weight * (gdouble)this->media_rate;

  }

  return;
}

guint32 sndrate_distor_get_sending_rate(SendingRateDistributor *this, guint8 id)
{
  return _get_subflow(this, id)->sending_rate;
}

gboolean sndrate_distor_congestion_event(SendingRateDistributor *this, guint8 id)
{
  return _get_subflow(this, id)->state == STATE_OVERUSED &&
         _mt1(_get_subflow(this, id))->state != STATE_OVERUSED;
}

gboolean sndrate_distor_settlemen_event(SendingRateDistributor *this, guint8 id)
{
  return _get_subflow(this, id)->state == STATE_STABLE &&
         _mt1(_get_subflow(this, id))->state != STATE_STABLE;
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
  _mt0(this)->state = _mt1(this)->state;
  return _mt0(this);
}

State
_check_overused(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  g_print("S%d: STATE OVERUSED\n", subflow->id);
  if(_mt0(subflow)->discard && _mt0(subflow)->lost){
    g_print("S%d: Discard and Lost happened\n", subflow->id);
    subflow->supplied_bytes = _action_fall(this, subflow);
    _disable_checking(subflow, 1);
    goto done;
  }
  if(_mt0(subflow)->corrh_owd > OT_){
    g_print("S%d: _mt0(subflow)->corrh_owd > OT_)\n", subflow->id);
    subflow->supplied_bytes = _action_fall(this, subflow);
    _disable_checking(subflow, 1);
    goto done;
  }
  subflow->taken_bytes = _action_request(this, subflow);
  _transit_to(subflow, STATE_UNSTABLE);
done:
  return subflow->state;
}

State
_check_unstable(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  g_print("S%d: STATE UNSTABLE\n", subflow->id);
  if(_mt0(subflow)->corrh_owd > DT_){
    g_print("S%d: _mt0(subflow)->corrh_owd > DT_\n", subflow->id);
    subflow->supplied_bytes=_action_fall(this, subflow);
    _disable_checking(subflow, 1);
    _disable_monitoring(subflow);
    _transit_to(subflow, STATE_OVERUSED);
    goto done;
  }
  if(_mt1(subflow)->state == STATE_OVERUSED){
    _transit_to(subflow, STATE_STABLE);
    goto done;
  }
  if(_mt0(subflow)->corrh_owd > ST_){
    g_print("S%d: _mt0(subflow)->corrh_owd > ST_\n", subflow->id);
    _disable_monitoring(subflow);
    _transit_to(subflow, STATE_STABLE);
    goto done;
  }
  //going up.
  if(subflow->monitoring_interval > 0 &&_mt0(subflow)->corrl_owd > MT_){
    if(++subflow->monitoring_interval == 15){
      g_print("S%d: subflow->monitoring_interval == 15\n", subflow->id);
      _disable_monitoring(subflow);
      _transit_to(subflow, STATE_STABLE);
      goto done;
    }
    subflow->monitored_bytes = (gdouble)subflow->sending_rate / (gdouble) subflow->monitoring_interval;
    g_print("S%d: subflow->monitored_bytes %u\n", subflow->id, subflow->monitored_bytes);
    _change_monitoring(subflow, subflow->monitoring_interval);
    goto done;
  }
  if(++subflow->monitoring_time > 2){
    g_print("S%d: subflow->monitoring_time > 2 %u\n", subflow->id, subflow->monitored_bytes);
    this->requested_bytes += subflow->monitored_bytes;
    subflow->taken_bytes += subflow->monitored_bytes;
    _disable_monitoring(subflow);
    _disable_checking(subflow, 1);
    _transit_to(subflow, STATE_STABLE);
  }
  //We are going up

done:
  return subflow->state;
}

State
_check_stable(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  g_print("S%d: STATE STABLE\n", subflow->id);
  if(_mt0(subflow)->discard && _mt0(subflow)->lost){
    g_print("S%d: Discard and Lost happened\n", subflow->id);
    subflow->supplied_bytes=_action_fall(this, subflow);
    _transit_to(subflow, STATE_OVERUSED);
    _disable_checking(subflow, 1);
    goto done;
  }
  if(_mt0(subflow)->corrh_owd > DT_){
      g_print("S%d: corrh_owd(%f) > %f\n",
              subflow->id,
              _mt0(subflow)->corrh_owd,
              DT_);

    _transit_to(subflow, STATE_OVERUSED);
    subflow->supplied_bytes=_action_fall(this, subflow);
    _disable_checking(subflow, 1);
    goto done;
  }
  if(_mt0(subflow)->corrh_owd > ST_){
    if(_mt1(subflow)->corrh_owd > ST_){
        g_print("S%d: corrh_owd(%f,%f) > %f\n",
                subflow->id,
                _mt0(subflow)->corrh_owd,
                _mt1(subflow)->corrh_owd,
                ST_);
      subflow->supplied_bytes = _action_fall(this, subflow);
      _disable_checking(subflow, 1);
      _transit_to(subflow, STATE_OVERUSED);
    }
    goto done;
  }
  //congestion avoidance
  if(_mt0(subflow)->corrPiT < .5){
      g_print("S%d: corrPiT: %f < .5\n",
              subflow->id,
              _mt0(subflow)->corrPiT);
    subflow->supplied_bytes = _action_fall(this, subflow);
    _disable_checking(subflow, 1);
    goto done;
  }
  if(_mt0(subflow)->state != STATE_STABLE)
    goto done;

  //calculating monitored bytes
  if(subflow->fallen_bytes > 0){
    guint32 target;
    g_print("S%d: fallen bytes > 0\n", subflow->id);
    target = _get_monitoring_bytes(subflow, subflow->fallen_bytes);
    if(subflow->monitoring_interval > 14){
      subflow->fallen_bytes = 0;
      _disable_monitoring(subflow);
      g_print("S%d: subflow->monitoring_interval > 14\n", subflow->id);
      goto done;
    }
    subflow->fallen_bytes-= target;
    _enable_monitoring(subflow);
    _transit_to(subflow, STATE_UNSTABLE);
    g_print("S%d: subflow->fallen_bytes-= target (%u)\n", subflow->id, target);
    goto done;
  }else if(_mt0(subflow)->corrPiT > 1.5){
    guint32 target;
    target = subflow->sending_rate * .1;
    g_print("S%d: _mt0(subflow)->corrPiT > 1.2 UP: %u\n", subflow->id, target);
    target = _get_monitoring_bytes(subflow, target);
    _enable_monitoring(subflow);
    _transit_to(subflow, STATE_UNSTABLE);
    goto done;
  }

done:
  return subflow->state;
}


//------------------------ACTIONS-----------------------------

void
_transit_to(
    Subflow *subflow,
    State target)
{
  switch(target){
    case STATE_OVERUSED:
      subflow->checking = _check_overused;
    break;
    case STATE_STABLE:
      subflow->checking = _check_stable;
    break;
    case STATE_UNSTABLE:
      subflow->checking = _check_unstable;
    break;
  }
  subflow->state = target;
}


void _disable_checking(Subflow* subflow, guint disable_ticks)
{
  if(!subflow->available) goto done;
  subflow->disable_check += disable_ticks;
done:
  return;
}


guint32 _get_monitoring_bytes(Subflow *this, guint32 limit)
{
  this->monitoring_interval = 1;
  do{
    ++this->monitoring_interval;
    this->monitored_bytes = (gdouble)this->sending_rate / (gdouble) this->monitoring_interval;
  }while(limit < this->monitored_bytes);
  return this->monitored_bytes;
}


void _disable_monitoring(Subflow* subflow)
{
  if(!subflow->available) goto done;
  subflow->monitored_bytes = 0;
  subflow->monitoring_interval = 0;
  subflow->monitoring_time = 0;
  mprtps_path_set_monitor_interval(subflow->path, 0);
  g_print("S%d Monitoring disabled\n", subflow->id);
done:
  return;
}

void _enable_monitoring(Subflow *subflow)
{
  if(!subflow->available) goto done;
  subflow->monitoring_time = 0;
  mprtps_path_set_monitor_interval(subflow->path, subflow->monitoring_interval);
  g_print("S%d Monitoring enabled with %u\n", subflow->id, subflow->monitoring_interval);
done:
  return;
}

void _change_monitoring(Subflow *subflow, guint interval)
{
  if(!subflow->available) goto done;
  subflow->monitoring_time = 0;
  g_print("S%d Monitoring changed with %u\n", subflow->id, subflow->monitoring_interval);
  mprtps_path_set_monitor_interval(subflow->path, interval);
done:
  return;
}


gint32 _action_request(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  gint32 requested_bytes = 0;
  if(_mt1(subflow)->goodput < subflow->sending_rate) goto done;
  requested_bytes = _mt1(subflow)->goodput - subflow->sending_rate;
  g_print("S%d: Bounce back, SR:%u GPt1: %u, requested bytes: %u, RRt0: %u\n",
            subflow->id, subflow->sending_rate,
            _mt1(subflow)->goodput,
            requested_bytes, _mt0(subflow)->receiver_rate);
  this->requested_bytes+=requested_bytes;
done:
  return requested_bytes;
}


gint32 _action_fall(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  gint32 fallen_bytes = 0;
  if(subflow->sending_rate < _mt0(subflow)->goodput)
    fallen_bytes = subflow->sending_rate * .70710678118; //sqrt(2)/2
  else
    fallen_bytes = subflow->sending_rate-_mt0(subflow)->goodput;
  g_print("S%d: Undershooting, SR:%u fallen bytes: %u GP: %u RR %u\n",
          subflow->id, subflow->sending_rate,
          fallen_bytes, _mt0(subflow)->goodput, _mt0(subflow)->receiver_rate );
  this->fallen_bytes+= subflow->fallen_bytes = fallen_bytes;
  return fallen_bytes;
}

