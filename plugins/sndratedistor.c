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
#include <math.h>
#include <gst/gst.h>
#include <stdlib.h>
#include "percentiletracker.h"
#include <string.h>
#include "streamsplitter.h"
#include "sefctrler.h"


GST_DEBUG_CATEGORY_STATIC (sndrate_distor_debug_category);
#define GST_CAT_DEFAULT sndrate_distor_debug_category
#define MEASUREMENT_LENGTH 3
#define INCREASEMENT_LENGTH 16
G_DEFINE_TYPE (SendingRateDistributor, sndrate_distor, G_TYPE_OBJECT);


typedef struct _Subflow Subflow;
typedef struct _Measurement Measurement;


typedef enum{
  STATE_OVERUSED       = -1,
  STATE_STABLE         =  0,
  STATE_MONITORED       = 1,
}State;

typedef State (*Checker)(SendingRateDistributor*,Subflow*);

typedef struct _UtilizationReport{
  guint32  target;
  guint32  actual_rate;
  guint32  desired_rate;
}UtilizationReport;


typedef struct _CorrValue{
  gint32  saved[4];
  guint   index;
  gint32  sum;
  gdouble value;
}CorrValue;

struct _Measurement{
  guint16         PiT;
  gdouble         corrh_owd;
  gdouble         corrd;
  guint64         delay;
  gint32          goodput;
  guint32         lost;
  guint32         discard;
  guint32         receiver_rate;

  State           state;
};



struct _Subflow{
  guint8             id;
  MPRTPSPath*        path;
  gboolean           available;

  gint32             requested_bytes;
  gint32             supplied_bytes;

  gint32             fallen_point;
  gdouble            weight;
  gint32             sending_rate;

  //Need for monitoring
  guint              monitoring_interval;
  guint              monitoring_time;

  guint              disable_controlling;
  Checker            controlling;

  //need for state transitions
  State              state;
  PercentileTracker *delays;
  //Need for measurements
  gboolean           measured;
  Measurement        measurements[MEASUREMENT_LENGTH];
  guint8             measurements_index;

};

//----------------------------------------------------------------------
//-------- Private functions belongs to the object ----------
//----------------------------------------------------------------------

static const gdouble ST_ = 1.1; //Stable treshold
static const gdouble OT_ = 2.;  //Overused treshold
static const gdouble DT_ = 1.5; //Down Treshold

static void
sndrate_distor_finalize (
    GObject * object);

//--------------------UTILITIES-----------------------

static void
_refresh_available_ids(
    SendingRateDistributor* this);

static void
_refresh_weights(
    SendingRateDistributor* this);

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
_enable_monitoring(
        SendingRateDistributor *this,
        Subflow *subflow);

//static void
//_reset_monitoring(
//        SendingRateDistributor *this,
//        Subflow *subflow);

static guint32
_action_undershoot(
    SendingRateDistributor *this,
    Subflow *subflow);
//

static guint32
_action_bounce_back(
    SendingRateDistributor *this,
    Subflow *subflow);

static guint32
_action_bounce_up(
    SendingRateDistributor *this,
    Subflow *subflow);

//
//inline static
//gdouble sqr(gdouble x) {
//    return x*x;
//}

//source: http://stackoverflow.com/questions/5083465/fast-efficient-least-squares-fit-algorithm-in-c
//
//static gint linreg(gint n,
//                   const gdouble x[],
//                   const gdouble y[],
//                   gdouble* m,
//                   gdouble* b,
//                   gdouble* r);
//
#define _get_subflow(this, n) ((Subflow*)(this->subflows + n * sizeof(Subflow)))


#define foreach_subflows(this, i, subflow) \
  for(i=0, subflow = _get_subflow(this, this->available_ids[0]); i < this->available_ids_length; subflow = _get_subflow(this,  this->available_ids[++i]))
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
  this->subflows = g_malloc0(sizeof(Subflow)*SNDRATEDISTOR_MAX_NUM);
  for(i=0; i<SNDRATEDISTOR_MAX_NUM; ++i){
    _get_subflow(this, i)->id = i;
    _get_subflow(this, i)->available = FALSE;
//    _get_subflow(this, i)->joint_subflow_ids[i] = 1;
  }
}


SendingRateDistributor *make_sndrate_distor(SignalRequestFunc signal_request,
                                            gpointer controller)
{
  SendingRateDistributor *result;
  result = g_object_new (SNDRATEDISTOR_TYPE, NULL);
  result->signal_request = signal_request;
  result->signal_controller = controller;
  result->target_media_rate = 1024;
  result->actual_media_rate = 1024;
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
  //Reset subflow
//  memset(_get_subflow(this, *id), 0, sizeof(Subflow));
  _get_subflow(this, *id)->id = *id;
  _get_subflow(this, *id)->available = TRUE;
  _get_subflow(this, *id)->sending_rate = sending_rate;
  _get_subflow(this, *id)->path = g_object_ref(path);
  if(!_get_subflow(this, *id)->delays){
    _get_subflow(this, *id)->delays = make_percentiletracker(256, 80);
    percentiletracker_set_treshold(_get_subflow(this, *id)->delays, 30 * GST_SECOND);
  }
  else{
    percentiletracker_reset(_get_subflow(this, *id)->delays);
  }
  _get_subflow(this, *id)->disable_controlling = 0;
  _get_subflow(this, *id)->measured = FALSE;
  memset(_get_subflow(this, *id)->measurements, 0, sizeof(Measurement) * 3);
  _get_subflow(this, *id)->measurements_index = 0;
  _get_subflow(this, *id)->monitoring_time = 0;
  _get_subflow(this, *id)->monitoring_interval = 0;
  _transit_to(this, _get_subflow(this, *id), STATE_STABLE);
  _disable_controlling(_get_subflow(this, *id), 2);
  ++this->controlled_num;
  _refresh_available_ids(this);
  _refresh_weights(this);
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
  _refresh_available_ids(this);
  _refresh_weights(this);
done:
  return;
}



void sndrate_distor_measurement_update(SendingRateDistributor *this,
                                       guint8 id,
                                       RRMeasurement *measurement)
{
  Subflow *subflow;

  subflow = _get_subflow(this, id);
  _mt0(subflow)->state = subflow->state;
  _m_step(subflow);
  _mt0(subflow)->state = _mt1(subflow)->state;
  subflow->sending_rate = measurement->sending_rate;
  percentiletracker_add(subflow->delays, measurement->min_delay);
  percentiletracker_add(subflow->delays, measurement->max_delay);
  percentiletracker_add(subflow->delays, measurement->median_delay);
  _mt0(subflow)->corrh_owd = (gdouble)measurement->median_delay / (gdouble) percentiletracker_get_stats(subflow->delays, NULL, NULL, NULL);
  _mt0(subflow)->delay = measurement->median_delay;
  _mt0(subflow)->corrd = (gdouble)_mt1(subflow)->delay / (gdouble)_mt0(subflow)->delay;
  _mt0(subflow)->discard = measurement->late_discarded_bytes;
  _mt0(subflow)->lost = measurement->lost;
  _mt0(subflow)->goodput = measurement->goodput;
  _mt0(subflow)->receiver_rate = measurement->receiver_rate;


//  g_print("S%d update."
//          "State: %d, CorrH: %f, CorrL: %f, GP: %u, J: %d "
//          "PiT: %hu, L:%d, D: %d, RR: %u MrCorr: %f Jitter_J: %f "
//          "CorrPit: %f CorrGP: %f CC_cue: %u Stable GP: %f SR: %d, MR: %u\n",
//          subflow->id,
//          _mt0(subflow)->state,
//          _mt0(subflow)->corrh_owd,
//          _mt0(subflow)->corrl_owd,
//          _mt0(subflow)->goodput,
//          _mt0(subflow)->jitter,
//          _mt0(subflow)->PiT,
//          _mt0(subflow)->lost,
//          _mt0(subflow)->discard,
//          _mt0(subflow)->receiver_rate,
//          _mt0(subflow)->CorrRate,
//          _mt0(subflow)->CorrJitter,
//          _mt0(subflow)->CorrPit,
//          _mt0(subflow)->CorrGP,
//          subflow->cc_cue,
//          subflow->stable_goodput,
//          subflow->sending_rate,
//          this->media_rate);

  return;
}


void sndrate_distor_time_update(SendingRateDistributor *this)
{
  gint id,i;
  Subflow *subflow;
  gint32 delta_sr;
  UtilizationReport ur;

  //1. Init SendingRateDistributor
  this->supplied_bytes = 0;
  this->requested_bytes = 0;

  //2. Init subflows
  foreach_subflows(this, i, subflow)
  {
    subflow->requested_bytes = 0;
    subflow->supplied_bytes = 0;
    if(subflow->disable_controlling > 0){
      --subflow->disable_controlling;
      continue;
    }
    subflow->controlling(this, subflow);
  }

  foreach_subflows(this, i, subflow){
    delta_sr = subflow->requested_bytes - subflow->supplied_bytes;
    subflow->sending_rate+=delta_sr;
  }
  _refresh_weights(this);
  ur.target = this->target_media_rate;
  ur.desired_rate = this->actual_media_rate;
  this->signal_request(this->signal_controller, &ur);
  this->actual_media_rate = ur.actual_rate;
  this->target_media_rate = ur.target;

  foreach_subflows(this, i, subflow){
    subflow->measured=FALSE;
  }

  return;
}

gdouble sndrate_distor_get_sending_weight(SendingRateDistributor *this, guint8 id)
{
  return _get_subflow(this, id)->weight;
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

void _refresh_available_ids(SendingRateDistributor* this)
{
  gint id;
  Subflow *subflow;
  this->available_ids_length = 0;
  for(id=0; id < SNDRATEDISTOR_MAX_NUM; ++id){
    subflow = _get_subflow(this, id);
    if(!subflow->available) continue;
    this->available_ids[this->available_ids_length++] = subflow->id;
  }
}

void _refresh_weights(SendingRateDistributor* this)
{
  gint i;
  Subflow *subflow;
  guint32 sum_sr=0;
  guint32 diff_mr = 0;

  foreach_subflows(this, i, subflow)
  {
    sum_sr+=subflow->sending_rate;
  }
  if(this->target_media_rate < sum_sr){
    diff_mr = sum_sr - this->target_media_rate;
  }

  foreach_subflows(this, i, subflow)
  {
    subflow->weight = (gdouble) subflow->sending_rate / (gdouble) sum_sr;
    if(diff_mr == 0) continue;
    subflow->sending_rate-=subflow->weight * diff_mr + 1;
  }

  this->actual_media_rate = sum_sr;
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

//
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

State
_check_overused(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  g_print("S%d: STATE OVERUSED\n", subflow->id);
  if(_mt0(subflow)->discard && _mt0(subflow)->lost){
    g_print("S%d: Discard and Lost happened\n", subflow->id);
    subflow->supplied_bytes += _action_undershoot(this, subflow);
    _disable_controlling(subflow, 1);
    _disable_monitoring(subflow);
    goto done;
  }
  if(_mt0(subflow)->corrh_owd > OT_){
    g_print("S%d: _mt0(subflow)->corrh_owd > OT_)\n", subflow->id);
    _disable_controlling(subflow, 1);
    _disable_monitoring(subflow);
    goto done;
  }
  _disable_controlling(subflow, 1);
  subflow->requested_bytes += _action_bounce_back(this, subflow);
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
  if(_mt0(subflow)->discard){
    if(_mt0(subflow)->corrh_owd > ST_ ||
       _mt0(subflow)->lost > 0 ||
       _mt1(subflow)->discard > 0)
    {
        g_print("_mt0(subflow)->discard  _mt0(subflow)->corrh_owd > ST_\n");
       subflow->supplied_bytes += _action_undershoot(this, subflow);
       _transit_to(this, subflow, STATE_OVERUSED);
       _disable_controlling(subflow, 1);
    }
    goto done;
  }

  if(_mt1(subflow)->state != STATE_STABLE ||
     _mt0(subflow)->corrh_owd > ST_ ||
     _mt1(subflow)->corrh_owd > ST_)
    goto done;

  g_print("READY FOR MONITORING\n");
  if(1. < _mt0(subflow)->corrh_owd) {
    goto done;
  }

//  if(this->monitored > 0 || this->overused > 0){
//    goto done;
//  }
  //központi monitoring és növekedés zárás figyelembe vétele
  _enable_monitoring(this, subflow);
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

  if(_mt0(subflow)->corrh_owd > ST_){
    if(_mt0(subflow)->corrh_owd > DT_){
      g_print("_mt0(subflow)->corrh_owd > DT_\n");
      subflow->supplied_bytes +=_action_undershoot(this, subflow);
      _transit_to(this, subflow, STATE_OVERUSED);
      _disable_controlling(subflow, 1);
      _disable_monitoring(subflow);
    }else{
      _transit_to(this, subflow, STATE_STABLE);
      _disable_monitoring(subflow);
    }
    goto done;
  }

  if(_mt0(subflow)->discard){
    _disable_monitoring(subflow);
    _transit_to(this, subflow, STATE_STABLE);
    goto done;
  }

  if(_is_monitoring_done(this, subflow)){
    //increase
    subflow->requested_bytes +=_action_bounce_up(this, subflow);
    _transit_to(this, subflow, STATE_STABLE);
    _disable_monitoring(subflow);
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
  }

  if(subflow->state == STATE_OVERUSED && target != subflow->state){
    --this->overused;
  }


  switch(target){
    case STATE_OVERUSED:
      //mprtps_path_set_congested(subflow->path);
      ++this->overused;
      subflow->controlling = _check_overused;
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
  if(8 < subflow->monitoring_interval) goto yes;
  if(1 < subflow->monitoring_interval){
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

//void _reset_monitoring(
//        SendingRateDistributor *this,
//        Subflow *subflow)
//{
//  if(!subflow->available) goto done;
//  subflow->monitoring_interval = 14;
//done:
//  _setup_monitoring(subflow, subflow->monitoring_interval);
//  return;
//}


guint32 _action_undershoot(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  guint32 supplied_bytes;
  subflow->fallen_point = subflow->sending_rate;
  supplied_bytes += subflow->sending_rate * MIN(1./_mt0(subflow)->corrh_owd, _mt0(subflow)->corrd);
  this->supplied_bytes += supplied_bytes;
  g_print("S%d changed weight: %f\n", subflow->id, subflow->weight);
  return supplied_bytes;
}




guint32 _action_bounce_back(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  gint32 goodput;
  guint32 requested_bytes = 0;

//  subflow->weight *= 1.1;
  goodput = MAX(_mt0(subflow)->goodput, _mt1(subflow)->goodput);
  goodput = MIN(goodput, subflow->fallen_point) * .9;
  if(subflow->sending_rate < goodput)
    requested_bytes = goodput - subflow->sending_rate;
  this->requested_bytes += requested_bytes;
  return requested_bytes;
}


guint32 _action_bounce_up(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  guint32 requested_bytes;
  requested_bytes = subflow->sending_rate * (1./(gdouble) subflow->monitoring_interval);
  this->requested_bytes += requested_bytes;
  return requested_bytes;
}

