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
#define INCREASEMENT_LENGTH 16
G_DEFINE_TYPE (SendingRateDistributor, sndrate_distor, G_TYPE_OBJECT);


typedef struct _Subflow Subflow;
typedef struct _Measurement Measurement;
//typedef struct _KalmanFilter KalmanFilter;


typedef enum{
  STATE_OVERUSED       = -1,
  STATE_STABLE         =  0,
  STATE_MONITORED       = 1,
}State;

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
  gdouble         max_corrh;
  guint32         jitter;
  guint32         goodput;
  gboolean        lost;
  gboolean        discard;
  guint32         receiver_rate;
  guint32         sending_rate;
  gdouble         CorrRate;
  gdouble         CorrGP;
  gdouble         CorrJitter;
  gdouble         CorrPit;
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
  guint           ping_pong_round;
  guint           monitoring_interval;
  guint           monitoring_time;

  guint           disable_controlling;
  Checker         controlling;

  //need for state transitions
  State           state;

  //Need for measurements
  gboolean        measured;
  Measurement     measurements[MEASUREMENT_LENGTH];
  guint8          measurements_index;


  guint32         increase_sequence[INCREASEMENT_LENGTH];
  guint8          increase_index;

  guint           cc_cue;
  //Talon
  gint32          jump_to;
  gint32          jump_from;
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
    SendingRateDistributor *this,
    Subflow *subflow,
    State target);

static void
_disable_controlling(
    Subflow* subflow,
    guint disable_ticks);

//#define _disable_monitoring(subflow) _setup_monitoring(subflow, 0);

static gboolean
_is_subflow_increasable(
    SendingRateDistributor *this,
    Subflow *subflow);

#define _disable_monitoring(subflow) _setup_monitoring(subflow, 0)
static void
_setup_monitoring(
    Subflow *subflow,
    guint interval);

static gboolean
_is_monitoring_done(
        SendingRateDistributor *this,
        Subflow *subflow);

static void
_mitigate_monitoring(
        SendingRateDistributor *this,
        Subflow *subflow);

static void
_reduce_monitoring(
      SendingRateDistributor *this,
      Subflow *subflow);

static void
_enable_monitoring(
        SendingRateDistributor *this,
        Subflow *subflow);

static gboolean
_is_monitoring_ping_pong_round_end(
    Subflow *subflow);

static void
_continue_monitoring(
        SendingRateDistributor *this,
        Subflow *subflow);

static void
_reset_monitoring(
        SendingRateDistributor *this,
        Subflow *subflow);

static gint32
_action_undershoot(
    SendingRateDistributor *this,
    Subflow *subflow);
//

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
  this->controlled_num = 0;
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
  if(this->controlled_num > SNDRATEDISTOR_MAX_NUM){
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
  _transit_to(this, _get_subflow(this, *id), STATE_STABLE);
  _disable_controlling(_get_subflow(this, *id), 2);
  ++this->controlled_num;
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
  _setup_monitoring(_get_subflow(this, id), 0);
  _get_subflow(this, id)->available = FALSE;
  g_queue_push_tail(this->free_ids, free_id);
  --this->controlled_num;
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
  gdouble CorrRate;
  gdouble CorrGP;
  gint32 jitter_sum;
  gint32 PiT_sum;

  subflow = _get_subflow(this, id);
  CorrRate = (gdouble)subflow->sending_rate + (gdouble) _mt1(subflow)->sending_rate + (gdouble) _mt2(subflow)->sending_rate;
  if(subflow->monitoring_interval > 0)
    CorrRate /= 3. * (gdouble)receiver_rate * (1. - 1./(gdouble)subflow->monitoring_interval);
  else
    CorrRate /= 3. * (gdouble)receiver_rate;
  CorrGP = (gdouble)goodput / (gdouble)receiver_rate;

  if(!subflow->available) goto done;
  jitter_sum = _mt2(subflow)->jitter;
  PiT_sum = _mt2(subflow)->PiT;
  if(!subflow->measured) _m_step(subflow);
  subflow->measured = TRUE;
  _mt0(subflow)->state = subflow->state;
  _mt0(subflow)->PiT = PiT;
  _mt0(subflow)->corrh_owd = corrh_owd;
  _mt0(subflow)->corrl_owd = corrl_owd;
  _mt0(subflow)->goodput = goodput;
  _mt0(subflow)->lost = lost;
  _mt0(subflow)->discard = discard;
  _mt0(subflow)->jitter = jitter;
  _mt0(subflow)->receiver_rate = receiver_rate;
  _mt0(subflow)->sending_rate = subflow->sending_rate;
  _mt0(subflow)->CorrRate = CorrRate;
  _mt0(subflow)->CorrGP = CorrGP;
  _mt0(subflow)->max_corrh = MAX(_mt0(subflow)->max_corrh, _mt0(subflow)->corrh_owd);
  jitter_sum += _mt2(subflow)->jitter + _mt1(subflow)->jitter;
  PiT_sum += _mt2(subflow)->PiT + _mt1(subflow)->PiT;
  _mt0(subflow)->CorrJitter = (gdouble)jitter_sum /  (3. * (gdouble) jitter);
  _mt0(subflow)->CorrPit = (gdouble)PiT_sum /  (3. * (gdouble) _mt0(subflow)->PiT);

  if(_mt0(subflow)->CorrPit < .9 && CorrRate > 1.1)
    ++subflow->cc_cue;
  else if(subflow->cc_cue > 0)
    --subflow->cc_cue;

  g_print("S%d update."
          "State: %d, CorrH: %f, CorrL: %f, GP: %u, J: %d "
          "PiT: %hu, L:%d, D: %d, RR: %u MrCorr: %f Jitter_J: %f CorrPit: %f CorrGP: %f CC_cue: %u\n",
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
          _mt0(subflow)->CorrRate,
          _mt0(subflow)->CorrJitter,
          _mt0(subflow)->CorrPit,
          _mt0(subflow)->CorrGP,
          subflow->cc_cue);

done:
  return;
}


void sndrate_distor_time_update(SendingRateDistributor *this, guint32 media_rate)
{
  gint id,i;
  Subflow *subflow;
  guint32 SR_sum = 0;
  guint32 new_SR_sum = 0;
  Utilization utilization = {0,FALSE};
  guint32 RR_sum = 0;

  if(media_rate)
    this->media_rate = (this->media_rate>>1) + (media_rate>>1);
  else if(!this->media_rate)
    this->media_rate = 512000;

  //1. Init
  this->stable_sr_sum = 0;
  this->taken_bytes = 0;
  this->supplied_bytes = 0;
  this->fallen_bytes = 0;
  this->requested_bytes = 0;
  this->available_ids_length = 0;

  if(this->wait_before_monitoring > 0){
    --this->wait_before_monitoring;
  }

  //2. Init subflows, Check Stability and perform Overused and Unstable procedures
  for(id=0; id < SNDRATEDISTOR_MAX_NUM; ++id){
    subflow = _get_subflow(this, id);
    if(!subflow->available) continue;
    this->available_ids[this->available_ids_length++] = subflow->id;
    subflow->taken_bytes = 0;
    subflow->supplied_bytes = 0;
    RR_sum+=_mt0(subflow)->receiver_rate;
    if(subflow->disable_controlling > 0){
      --subflow->disable_controlling;
      continue;
    }
    subflow->controlling(this, subflow);
  }
  if(this->overused > 0){
    if(this->monitored > 0){
      for(i=0; i < this->available_ids_length; ++i){
        subflow = _get_subflow(this, this->available_ids[i]);
        if(subflow->state != STATE_MONITORED) continue;
        _transit_to(this, subflow, STATE_STABLE);
        _disable_monitoring(subflow);
      }
    }else{
      ++this->wait_before_monitoring;
    }
  }
  //signaling
  utilization.delta_sr = this->requested_bytes - this->fallen_bytes;
  if(utilization.delta_sr > 0){
    g_print("Plus delta: %d\n", utilization.delta_sr);
    if(this->controlled_num > 1)
      utilization.delta_sr>>=1;
  }
  this->media_rate += utilization.delta_sr;
  if(utilization.delta_sr != 0)
  {
   utilization.changing_rate = (gdouble)this->media_rate / (gdouble)media_rate;
   this->signal_request(this->signal_controller, &utilization);
  }

  //5. Calculate next sending rates.
  for(i=0; i < this->available_ids_length; ++i){
    gint32 delta_sr;
    id = this->available_ids[i];
    subflow = _get_subflow(this, id);
    delta_sr = subflow->taken_bytes - subflow->supplied_bytes;
//    g_print("Before: S%d SR:%d+%d\n", subflow->id, subflow->sending_rate, delta_sr);
    subflow->sending_rate+=delta_sr;
//    g_print("After: S%d SR:%d MR: %u\n", subflow->id, subflow->sending_rate, this->media_rate);
    SR_sum += subflow->sending_rate;
    subflow->measured = FALSE;
  }

  //6. calculate weights
  for(i=0; i < this->available_ids_length; ++i){
    gdouble weight;
    id = this->available_ids[i];
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
    _disable_monitoring(subflow);
    goto done;
  }
  if(_mt0(subflow)->max_corrh > OT_){
    g_print("S%d: _mt0(subflow)->corrh_owd > OT_)\n", subflow->id);
    _disable_controlling(subflow, 1);
    _disable_monitoring(subflow);
    goto done;
  }
  _disable_controlling(subflow, 1);
  subflow->taken_bytes = _action_bounce_back(this, subflow);
  _transit_to(this, subflow, STATE_STABLE);
done:
  return subflow->state;
}


State
_check_stable(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  g_print("S%d: STATE STABLE\n", subflow->id);
  if(_mt0(subflow)->CorrGP < .6 || _mt0(subflow)->lost){
    g_print("S%d: Discard or Lost happened\n", subflow->id);
    subflow->supplied_bytes = _action_undershoot(this, subflow);
    _transit_to(this, subflow, STATE_OVERUSED);
    _disable_controlling(subflow, 1);
    _disable_monitoring(subflow);
    goto done;
  }
  if(_mt0(subflow)->corrh_owd > DT_){
    g_print("S%d: corrh_owd(%f) > %f\n", subflow->id, _mt0(subflow)->corrh_owd, DT_);
    subflow->supplied_bytes = _action_undershoot(this, subflow);
    _transit_to(this, subflow, STATE_OVERUSED);
    _reset_monitoring(this, subflow);
    _disable_controlling(subflow, 1);
    _disable_monitoring(subflow);
    goto done;
  }

  if(_mt0(subflow)->corrh_owd > ST_){
    if(_mt1(subflow)->state == STATE_STABLE)
    {
      subflow->supplied_bytes = _action_undershoot(this, subflow);
      _transit_to(this, subflow, STATE_OVERUSED);
      _disable_controlling(subflow, 1);
      _disable_monitoring(subflow);
    }
    goto done;
  }

  g_print("READY FOR MONITORING\n");
  //központi monitoring és növekedés zárás figyelembe vétele


  if(subflow->monitoring_interval == 0){
    _enable_monitoring(this, subflow);
  }else if(!_is_monitoring_ping_pong_round_end(subflow)){
    _continue_monitoring(this, subflow);
  }else {
    ++subflow->ping_pong_round;
    if(_is_subflow_increasable(this, subflow)){
      subflow->taken_bytes += _action_bounce_up(this, subflow);
      _reset_monitoring(this, subflow);
    }
  }
  _transit_to(this, subflow, STATE_MONITORED);
done:
  return subflow->state;
}

State
_check_monitored(
    SendingRateDistributor *this,
    Subflow *subflow)
{

  g_print("S%d: STATE MONITORED\n", subflow->id);
  if(_mt0(subflow)->CorrGP < .9 || _mt0(subflow)->lost){
    _transit_to(this, subflow, STATE_STABLE);
    if(subflow->monitoring_interval < 6)
      _reduce_monitoring(this, subflow);
    else
      _mitigate_monitoring(this, subflow);
    _disable_controlling(subflow, 1);
    goto done;
  }
  if(_mt0(subflow)->corrh_owd > DT_){
    g_print("S%d: _mt0(subflow)->corrh_owd > DT_\n", subflow->id);
    subflow->supplied_bytes += _action_undershoot(this, subflow);
    _transit_to(this, subflow, STATE_OVERUSED);
    _reset_monitoring(this, subflow);
    _disable_controlling(subflow, 1);
    _disable_monitoring(subflow);
    goto done;
  }
  if(_mt0(subflow)->max_corrh > ST_){
    g_print("S%d: _mt0(subflow)->max_corrh > ST_\n", subflow->id);
    _transit_to(this, subflow, STATE_STABLE);
    _reduce_monitoring(this, subflow);
//    _disable_monitoring(subflow);
    goto done;
  }

  if(_mt0(subflow)->corrl_owd > MT_){
    _mitigate_monitoring(this, subflow);
  }

  if(_is_monitoring_done(this, subflow)){
    _transit_to(this, subflow, STATE_STABLE);
  }
done:
  return subflow->state;
}


//------------------------ACTIONS-----------------------------

void
_transit_to(
    SendingRateDistributor *this,
    Subflow *subflow,
    State target)
{
  if(subflow->state == STATE_MONITORED && target != subflow->state){
    --this->monitored;
    this->wait_before_monitoring = 3;
  }

  if(subflow->state == STATE_OVERUSED && target != subflow->state){
    --this->overused;
  }


  switch(target){
    case STATE_OVERUSED:
      //mprtps_path_set_congested(subflow->path);
      ++this->overused;
      subflow->controlling = _check_overused;
      subflow->ping_pong_round = 0;
      subflow->monitoring_interval = 0;
    break;
    case STATE_STABLE:
      //mprtps_path_set_non_congested(subflow->path);
      subflow->controlling = _check_stable;

    break;
    case STATE_MONITORED:
      ++this->monitored;
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

gboolean _is_subflow_increasable(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  gdouble alpha = .25;
  gdouble limit = 1 - (1. / (gdouble)this->controlled_num) * alpha;
  gdouble ratio = (gdouble)subflow->sending_rate / (gdouble)this->media_rate;
  if(1.1 < _mt0(subflow)->CorrRate && 1.1 < _mt1(subflow)->CorrRate) return FALSE;
  if(1.5 < _mt0(subflow)->CorrRate) return FALSE;
  if(this->controlled_num < 2) return TRUE;
  g_print("S%d ratio (%f) < limit (%f)\n", subflow->id, ratio, limit);
  if(ratio < limit) return TRUE;
  return FALSE;
}

void _setup_monitoring(Subflow *subflow, guint interval)
{
  if(!subflow->available) goto done;
//  subflow->monitoring_interval = interval;
  subflow->monitoring_time = 0;
  g_print("S%d Monitoring changed with %u\n", subflow->id, subflow->monitoring_interval);
  mprtps_path_set_monitor_interval(subflow->path, interval);
done:
  return;
}

gboolean _is_monitoring_done(
        SendingRateDistributor *this,
        Subflow *subflow)
{
  if(!subflow->available) goto yes;
  if(10 < subflow->monitoring_interval) goto yes;
  if(5 < subflow->monitoring_interval){
    if(++subflow->monitoring_time < 2) goto no;
    else goto yes;
  }
  if(1 < subflow->monitoring_interval){
    if(++subflow->monitoring_time < 3) goto no;
    else goto yes;
  }
  goto no;
yes:
  return TRUE;
no:
  return FALSE;
}


void _mitigate_monitoring(
        SendingRateDistributor *this,
        Subflow *subflow)
{
  if(!subflow->available) goto done;
  if(14 <= subflow->monitoring_interval) goto done;
  if(8 < subflow->monitoring_interval){
    subflow->monitoring_interval+=1;
  }else{
    subflow->monitoring_interval+=2;
  }
  _setup_monitoring(subflow, subflow->monitoring_interval);
done:
  return;
}

void _reduce_monitoring(
        SendingRateDistributor *this,
        Subflow *subflow)
{
  if(!subflow->available) goto done;
  if(14 <= subflow->monitoring_interval) goto done;
  if(subflow->monitoring_interval < 8){
    subflow->monitoring_interval<<=1;
  }else if(subflow->monitoring_interval < 12){
    subflow->monitoring_interval+=4;
  }else{
    subflow->monitoring_interval = 14;
  }
  _setup_monitoring(subflow, subflow->monitoring_interval);
done:
  return;
}

void _enable_monitoring(
        SendingRateDistributor *this,
        Subflow *subflow)
{
  if(!subflow->available) goto done;
  subflow->monitoring_interval = 14;
done:
  _setup_monitoring(subflow, subflow->monitoring_interval);
  return;
}

gboolean _is_monitoring_ping_pong_round_end(Subflow *subflow)
{
  if(subflow->ping_pong_round < 2){
    return subflow->monitoring_interval < 5;
  }
  if(subflow->ping_pong_round < 4){
    return subflow->monitoring_interval < 6;
  }
  if(subflow->ping_pong_round < 6){
    return subflow->monitoring_interval < 7;
  }
  if(subflow->ping_pong_round < 9){
    return subflow->monitoring_interval < 8;
  }

  return subflow->monitoring_interval < 9;
}

void _continue_monitoring(
        SendingRateDistributor *this,
        Subflow *subflow)
{
  if(!subflow->available) goto done;
  if(subflow->monitoring_interval < 3) goto done;
  if(subflow->cc_cue > 0 || _mt0(subflow)->CorrGP < .9) goto done;
  if(_mt0(subflow)->CorrRate < .5 ||
     _mt1(subflow)->CorrRate < .5 ||
     _mt2(subflow)->CorrRate < .5) goto done;
  --subflow->monitoring_interval;
done:
  _setup_monitoring(subflow, subflow->monitoring_interval);
  return;
}

void _reset_monitoring(
        SendingRateDistributor *this,
        Subflow *subflow)
{
  if(!subflow->available) goto done;
  subflow->monitoring_interval = 14;
done:
  _setup_monitoring(subflow, subflow->monitoring_interval);
  return;
}


gint32 _action_undershoot(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  gint32 fallen_bytes = 0;
  //mitigating
  if(_mt0(subflow)->corrh_owd < 2.){
    gdouble plus_ratio = 1./(gdouble) subflow->monitoring_interval;
    if(plus_ratio < .303){
      fallen_bytes = subflow->sending_rate * (.303-plus_ratio);
    }
    if(subflow->sending_rate  < _mt0(subflow)->goodput){
      subflow->jump_from = subflow->sending_rate;
    }else if(subflow->sending_rate < _mt0(subflow)->goodput * 2){
      subflow->jump_from = (subflow->sending_rate + _mt0(subflow)->goodput) / 2;
    }else{
      subflow->jump_from = 0;
    }
    g_print("S%d: Undershooting type: Mitigation| ", subflow->id);
  }
  //reducing
  else if(_mt0(subflow)->corrh_owd < 10.){
    subflow->jump_to = MAX(subflow->sending_rate, _mt0(subflow)->goodput) * .505;
    if(subflow->sending_rate  < _mt0(subflow)->goodput){
      fallen_bytes = subflow->sending_rate * .303;
    }else{
      fallen_bytes = subflow->sending_rate * .505;
    }
    g_print("S%d: Undershooting type: Reduction| ", subflow->id);
  }
  //falling
  else{
     subflow->jump_to = MAX(subflow->sending_rate, _mt0(subflow)->goodput) * .707;
     if(subflow->sending_rate  < _mt0(subflow)->goodput){
       fallen_bytes = subflow->sending_rate * .505;
     }else{
       fallen_bytes = subflow->sending_rate * .707;
     }
     g_print("S%d: Undershooting type: Falling| ", subflow->id);
  }


  g_print("SR:%u fallen bytes: %u GP: %u RR %u\n",
          subflow->sending_rate,
          fallen_bytes, _mt0(subflow)->goodput, _mt0(subflow)->receiver_rate );
  subflow->jump_to = 0;
  this->fallen_bytes += fallen_bytes;
  return fallen_bytes;
}




gint32 _action_bounce_back(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  gint32 requested_bytes = 0;
//  gint32 target;
//  target = MAX(_mt0(subflow)->goodput, _mt1(subflow)->goodput);
//  target = MIN(target, subflow->jump_from);
//  if(subflow->sending_rate < target)
//    requested_bytes = (target - subflow->sending_rate) * .9;
//  else if(subflow->sending_rate < subflow->jump_from)
//    requested_bytes = (subflow->jump_from - subflow->sending_rate) * .5;
  if(subflow->jump_to > 0){
    if(subflow->sending_rate < subflow->jump_to)
      requested_bytes = (subflow->jump_to - subflow->sending_rate) * .9;
    else
      requested_bytes = subflow->sending_rate * .101;
  }else{
      requested_bytes = 0;
  }

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
  requested_bytes = ((gdouble)subflow->sending_rate / (gdouble)this->media_rate) *
                    (1./(gdouble) subflow->monitoring_interval) *
                    (gdouble)this->media_rate;
//  requested_bytes*= .8/(gdouble) subflow->monitoring_interval;
  g_print("S%d requested byte: %u\n", subflow->id, requested_bytes);
  this->requested_bytes+=requested_bytes;
  return requested_bytes;
}
