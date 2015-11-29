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
  STATE_MONITORED       = 0,
//  STATE_HALT           =  1,
  STATE_STABLE         =  1,
}State;

typedef void (*Controller)(SendingRateDistributor*,Subflow*);
typedef State (*Checker)(SendingRateDistributor*,Subflow*);

typedef struct _Utilization{
  gint     delta_sr;
  gboolean accepted;
  gdouble  changing_rate;
}Utilization;

struct _Measurement{
  guint16         PiT;
  gdouble         corrl_owd;
  gdouble         corrh_owd;
  guint32         jitter;
  guint32         goodput;
  gboolean        lost;
  gboolean        discard;
  guint32         receiver_rate;
  guint32         sending_rate;
  gdouble         CorrMR;
  gdouble         CorrJitter;
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
  guint8          retention;
  guint8          consecutive_stable;

  guint           disable_controlling;
  Checker         controlling;

  //need for state transitions
  State           state;

  //Need for measurements
  gboolean        measured;
  Measurement     measurements[MEASUREMENT_LENGTH];
  guint8          measurements_index;


  //Talon
  gint32          fallen_bytes;
  gint32          fallen_from;
  gint32          monitoring_target;

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

static Measurement*
_mt2(Subflow* this);

static Measurement*
_m_step(Subflow *this);

//----------------------STATES-------------------

static State
_check_overused(
    SendingRateDistributor *this,
    Subflow *subflow);

static State
_check_stable(
    SendingRateDistributor *this,
    Subflow *subflow);

static State
_check_monitored(
    SendingRateDistributor *this,
    Subflow *subflow);

//-----------------------ACTIONS------------------
static void
_transit_to(
    Subflow *subflow,
    State target);

static void
_disable_controlling(
    Subflow* subflow,
    guint disable_ticks);

#define _disable_monitoring(subflow) _setup_monitoring(subflow, 0);

static void
_setup_monitoring(
    Subflow *subflow,
    guint interval);


static void
_change_monitoring(
    Subflow *subflow);


static gint32
_action_undershoot(
    SendingRateDistributor *this,
    Subflow *subflow);
//
//static gint32
//_action_mitigate(
//    SendingRateDistributor *this,
//    Subflow *subflow);

static gint32
_action_bounce_back(
    SendingRateDistributor *this,
    Subflow *subflow);

static gint32
_action_bounce_up(
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
  _disable_controlling(_get_subflow(this, *id), 2);
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
                                       guint32 jitter,
                                       gdouble corrh_owd,
                                       gdouble corrl_owd,
                                       guint16 PiT,
                                       gboolean lost,
                                       gboolean discard,
                                       guint8 lost_history,
                                       guint8 discard_history)
{
  Subflow *subflow;
  gdouble MRCorr;
  gint32 jitter_sum;
  subflow = _get_subflow(this, id);
  MRCorr = (gdouble)subflow->sending_rate + (gdouble) _mt1(subflow)->sending_rate + (gdouble) _mt2(subflow)->sending_rate;
  MRCorr /= 3. * (gdouble)receiver_rate;
  if(!subflow->available) goto done;
  jitter_sum = _mt2(subflow)->jitter;
  if(!subflow->measured) _m_step(subflow);
  subflow->measured = TRUE;
  _mt0(subflow)->PiT = PiT;
  _mt0(subflow)->corrh_owd = corrh_owd;
  _mt0(subflow)->corrl_owd = corrl_owd;
  _mt0(subflow)->goodput = goodput;
  _mt0(subflow)->lost = lost;
  _mt0(subflow)->discard = discard;
  _mt0(subflow)->jitter = jitter;
  _mt0(subflow)->receiver_rate = receiver_rate;
  _mt0(subflow)->sending_rate = subflow->sending_rate;
  _mt0(subflow)->CorrMR = MRCorr;
  jitter_sum += _mt2(subflow)->jitter + _mt1(subflow)->jitter;
  _mt0(subflow)->CorrJitter = (gdouble)jitter_sum /  (3. * (gdouble) jitter);

  g_print("S%d update."
          "State: %d, CorrH: %f, CorrL: %f, GP: %u, J: %d "
          "PiT: %hu, L:%d, D: %d, RR: %u MrCorr: %f Jitter_J: %f\n",
          subflow->id,
          _mt0(subflow)->state,
          _mt0(subflow)->corrh_owd,
          _mt0(subflow)->corrl_owd,
          _mt0(subflow)->goodput,
          _mt0(subflow)->jitter,
          _mt0(subflow)->PiT,
          _mt0(subflow)->lost,
          _mt0(subflow)->discard,
          _mt0(subflow)->receiver_rate,
          MRCorr,
          _mt0(subflow)->CorrJitter);
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
  guint32 new_SR_sum = 0;
  Utilization utilization = {0,FALSE};
  guint32 RR_sum = 0;

  if(media_rate)
    this->media_rate = (this->media_rate>>1) + (media_rate>>1);
  else if(!this->media_rate)
    this->media_rate = 512000;
//  g_print("this->media_rate: %u\n", this->media_rate);
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
    if(this->pacing){
      mprtps_path_set_pacing(subflow->path, 0);
    }
    if(subflow->disable_controlling > 0){
      --subflow->disable_controlling;
      continue;
    }
    _mt0(subflow)->state = subflow->controlling(this, subflow);
  }
  this->pacing = FALSE;
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
//  if(utilization.delta_sr < 0) utilization.delta_sr*=1.1;
  g_print("NMR: %u+%d=%d\n",this->media_rate, utilization.delta_sr,this->media_rate + utilization.delta_sr);
  this->media_rate += utilization.delta_sr;
//  g_print("MR: %u Utilization DSR = %d - %d = %d\n",
//             this->media_rate,
//             this->requested_bytes,
//             this->fallen_bytes,
//             utilization.delta_sr);
   if(utilization.delta_sr != 0)
   {
     utilization.changing_rate = (gdouble)this->media_rate / (gdouble)media_rate;
     this->signal_request(this->signal_controller, &utilization);
     this->pacing = utilization.delta_sr < 0;
   }
  //5. Calculate next sending rates.
  for(i=0; i < subflow_ids_length; ++i){
    gint32 delta_sr;
    id = subflow_ids[i];
    subflow = _get_subflow(this, id);
    delta_sr = subflow->taken_bytes - subflow->supplied_bytes;
//    g_print("Before: S%d SR:%d+%d\n", subflow->id, subflow->sending_rate, delta_sr);
    subflow->sending_rate+=delta_sr;
    if(this->pacing && subflow->state == STATE_STABLE){
      mprtps_path_set_pacing(subflow->path, 1);
    }
//    g_print("After: S%d SR:%d MR: %u\n", subflow->id, subflow->sending_rate, this->media_rate);
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
    new_SR_sum += subflow->sending_rate;
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


Measurement* _mt2(Subflow* this)
{
  guint8 index;
  if(this->measurements_index == 1) index = MEASUREMENT_LENGTH-1;
  else if(this->measurements_index == 0) index = MEASUREMENT_LENGTH-2;
  else index = this->measurements_index-2;
  return &this->measurements[index];
}

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
    subflow->supplied_bytes = _action_undershoot(this, subflow);
    _disable_controlling(subflow, 1);
    goto done;
  }
  if(_mt0(subflow)->corrh_owd > OT_){
    g_print("S%d: _mt0(subflow)->corrh_owd > OT_)\n", subflow->id);
    _disable_controlling(subflow, 1);
    goto done;
  }
  _disable_controlling(subflow, 1);
  subflow->taken_bytes = _action_bounce_back(this, subflow);
  _transit_to(subflow, STATE_STABLE);
done:
  return subflow->state;
}


State
_check_stable(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  g_print("S%d: STATE STABLE (%d)\n", subflow->id, subflow->retention);
  if(_mt0(subflow)->discard && _mt0(subflow)->lost){
    g_print("S%d: Discard and Lost happened\n", subflow->id);
    subflow->supplied_bytes = _action_undershoot(this, subflow);
    _transit_to(subflow, STATE_OVERUSED);
    _disable_controlling(subflow, 1);
    goto done;
  }
  if(_mt0(subflow)->corrh_owd > DT_){
    g_print("S%d: corrh_owd(%f) > %f\n", subflow->id, _mt0(subflow)->corrh_owd, DT_);
    subflow->supplied_bytes = _action_undershoot(this, subflow);
    _transit_to(subflow, STATE_OVERUSED);
    _disable_controlling(subflow, 1);
    goto done;
  }
  if(_mt0(subflow)->corrh_owd > ST_){
    if(_mt1(subflow)->corrh_owd > ST_){
      g_print("S%d: corrh_owd(%f,%f) > %f\n", subflow->id, _mt0(subflow)->corrh_owd, _mt1(subflow)->corrh_owd, ST_);
      subflow->supplied_bytes = _action_undershoot(this, subflow);
      _transit_to(subflow, STATE_OVERUSED);
      goto done;
    }
  }

  if(_mt0(subflow)->corrh_owd > 1.){
    subflow->retention += 2;
    goto done;
  }
//  if(_mt0(subflow)->CorrMR < .9){
//    if(_mt1(subflow)->CorrMR < .9){
//      subflow->retention += 2;
//      g_print("_mt0(subflow)->CorrMR < min_treshold\n");
//    }
//    goto done;
//  }
//  if(1.1 < _mt0(subflow)->CorrMR){
//    if(1.1 < _mt1(subflow)->CorrMR){
//      subflow->retention += 2;
//      g_print("max_treshold < _mt0(subflow)->CorrMR\n");
//    }
//    goto done;
//  }
  if(_mt0(subflow)->CorrJitter < .5){
    g_print("max_treshold < _mt0(subflow)->CorrJitter .5\n");
    subflow->retention += 2;
    goto done;
  }
  if(_mt0(subflow)->CorrJitter < .9 && _mt1(subflow)->CorrJitter < .9){
    g_print("max_treshold < _mt0(subflow)->CorrJitter .9\n");
    subflow->retention += 2;
    goto done;
  }
  if(--subflow->retention == 0){
    g_print("READY FOR MONITORING (%u)\n", subflow->consecutive_stable);
    if(13 < subflow->consecutive_stable)
      _setup_monitoring(subflow, 14);
    else if(subflow->consecutive_stable < 8){
      if((subflow->sending_rate<<2) < subflow->fallen_from)
        _setup_monitoring(subflow, 2);
      else if((subflow->sending_rate<<1) < subflow->fallen_from)
        _setup_monitoring(subflow, 4);
      else
        _setup_monitoring(subflow, 6);
    }else{
        _setup_monitoring(subflow, subflow->consecutive_stable + 1);
    }
    _transit_to(subflow, STATE_MONITORED);
    //monitoring
    goto done;
  }
if(0)  _change_monitoring(subflow);
//  _transit_to(subflow, STATE_MONITORED);
done:
  if(++subflow->consecutive_stable > 20){
      subflow->consecutive_stable = 14;
  }
  subflow->retention = MIN(subflow->retention, 10);
  return subflow->state;
}


State
_check_monitored(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  g_print("S%d: STATE MONITORED\n", subflow->id);
  if(_mt0(subflow)->corrh_owd > DT_){
    g_print("S%d: _mt0(subflow)->corrh_owd > DT_\n", subflow->id);
    _disable_monitoring(subflow);
    subflow->supplied_bytes += _action_undershoot(this, subflow);
    _transit_to(subflow, STATE_OVERUSED);
    _disable_controlling(subflow, 1);
    goto done;
  }
  if(_mt0(subflow)->corrh_owd > ST_){
    g_print("S%d: _mt0(subflow)->corrh_owd > ST_\n", subflow->id);
    _disable_monitoring(subflow);
    _transit_to(subflow, STATE_STABLE);
    _disable_controlling(subflow, 1);
    goto done;
  }
  if(_mt0(subflow)->CorrJitter < .9 && _mt1(subflow)->CorrJitter < .9){
    _disable_monitoring(subflow);
    _transit_to(subflow, STATE_STABLE);
    goto done;
  }
//  if(1.1 < _mt0(subflow)->CorrMR){
//    g_print("1.1 < _mt0(subflow)->CorrMR\n");
//    _disable_monitoring(subflow);
//    _transit_to(subflow, STATE_STABLE);
//    goto done;
//  }
  if(_mt0(subflow)->corrl_owd > MT_){
    g_print("S%d: subflow->monitoring_interval > 0 && _mt0(subflow)->corrl_owd > MT_\n", subflow->id);
    if(++subflow->monitoring_interval == 15){
      _disable_monitoring(subflow);
      _transit_to(subflow, STATE_STABLE);
      goto done;
    }
    _setup_monitoring(subflow, subflow->monitoring_interval);
    goto done;
  }

  if(++subflow->monitoring_time > 2){
    g_print("S%d: subflow->monitoring_time > 2 %u\n", subflow->id, subflow->monitored_bytes);
    subflow->taken_bytes += _action_bounce_up(this, subflow);
    _disable_monitoring(subflow);
    _transit_to(subflow, STATE_STABLE);
  }
  //We are going up
done:
//g_print("S%d subflow->monitoring_time: %d\n", subflow->id, subflow->monitoring_time);
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
      subflow->controlling = _check_overused;
    break;
    case STATE_STABLE:
      subflow->controlling = _check_stable;
      subflow->retention = 2;
      subflow->consecutive_stable = 0;
    break;
    case STATE_MONITORED:
      subflow->controlling = _check_monitored;
      subflow->monitoring_time = 0;
    break;
  }
  subflow->state = target;
}


void _disable_controlling(Subflow* subflow, guint disable_ticks)
{
  if(!subflow->available) goto done;
  subflow->disable_controlling += disable_ticks;
done:
  return;
}



void _setup_monitoring(Subflow *subflow, guint interval)
{
  if(!subflow->available) goto done;
  subflow->monitoring_interval = interval;
  subflow->monitoring_time = 0;
  subflow->monitored_bytes = 0;
  g_print("S%d Monitoring changed with %u\n", subflow->id, subflow->monitoring_interval);
  mprtps_path_set_monitor_interval(subflow->path, interval);
done:
  return;
}

void _change_monitoring(Subflow *subflow)
{
  if(subflow->monitoring_target < subflow->sending_rate * 1.1){
    subflow->monitoring_target = 0;
  }

  if(subflow->sending_rate<<1 < subflow->monitoring_target){
    _setup_monitoring(subflow, 2);
    goto done;
  }else if(0 < subflow->monitoring_target){
    guint MI = 15;
    guint32 NMB;
    do{
      NMB = (gdouble)subflow->sending_rate / (gdouble)(--MI);
    }while(2 < MI && subflow->sending_rate + NMB < subflow->monitoring_target);
    _setup_monitoring(subflow, MI);
    goto done;
  }
  _setup_monitoring(subflow, 10);
done:
  return;
}

//static gdouble _min_corrMR(Subflow *subflow)
//{
//  return MIN(.9,
//             MIN(_mt0(subflow)->CorrMR_min,
//             MIN(_mt1(subflow)->CorrMR_min, _mt2(subflow)->CorrMR_min)));
//}



gint32 _action_undershoot(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  gint32 fallen_bytes = 0;
  if(_mt0(subflow)->goodput < (subflow->sending_rate>>1)){
    fallen_bytes = subflow->sending_rate - (subflow->sending_rate>>2);
  }else{
    fallen_bytes = subflow->sending_rate - (subflow->sending_rate>>1);
  }
  _setup_monitoring(subflow, 2);
  g_print("S%d: Undershooting, SR:%u fallen bytes: %u GP: %u RR %u\n",
          subflow->id, subflow->sending_rate,
          fallen_bytes, _mt0(subflow)->goodput, _mt0(subflow)->receiver_rate );
  subflow->fallen_bytes = fallen_bytes;
  this->fallen_bytes += fallen_bytes;
  subflow->fallen_from = subflow->sending_rate;
  return fallen_bytes;
}

//gint32 _action_mitigate(
//    SendingRateDistributor *this,
//    Subflow *subflow)
//{
//  gint32 congested_bytes = 0;
//  congested_bytes = (_mt0(subflow)->receiver_rate - subflow->sending_rate);
//  if(congested_bytes<<1 < subflow->sending_rate) congested_bytes<<=1;
//  g_print("S%d: Mitigate, SR:%u fallen bytes: %u GP: %u RR %u\n",
//          subflow->id, subflow->sending_rate,
//          congested_bytes, _mt0(subflow)->goodput, _mt0(subflow)->receiver_rate );
//  this->fallen_bytes += congested_bytes;
//  return congested_bytes;
//}


gint32 _action_bounce_back(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  gint32 requested_bytes = 0;
  requested_bytes = subflow->sending_rate>>1;
  _setup_monitoring(subflow, 0);
  g_print("S%d: Bounce back, SR:%u GPt1: %u, requested bytes: %u, RRt0: %u\n",
            subflow->id, subflow->sending_rate,
            _mt1(subflow)->goodput,
            requested_bytes, _mt0(subflow)->receiver_rate);
  this->requested_bytes+=requested_bytes;
  return requested_bytes;
}



gint32 _action_bounce_up(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  gint32 requested_bytes = 0;
  if(subflow->monitoring_interval > 14 || subflow->monitoring_interval == 0){
    requested_bytes = subflow->sending_rate * .1;
  }else{
    requested_bytes = (gdouble) subflow->sending_rate / (gdouble) subflow->monitoring_interval;
  }
  g_print("S%d: Bounce Up, SR:%u GPt1: %u, requested bytes: %u, RRt0: %u\n",
            subflow->id, subflow->sending_rate,
            _mt1(subflow)->goodput,
            requested_bytes, _mt0(subflow)->receiver_rate);
  this->requested_bytes+=requested_bytes;
  return requested_bytes;
}
