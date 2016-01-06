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
#include "nlms.h"
#include "kalmanfilter.h"


GST_DEBUG_CATEGORY_STATIC (sndrate_distor_debug_category);
#define GST_CAT_DEFAULT sndrate_distor_debug_category
#define MEASUREMENT_LENGTH 3
#define INCREASEMENT_LENGTH 16
#define CHANGE_HISTORY_LENGTH 8
G_DEFINE_TYPE (SendingRateDistributor, sndrate_distor, G_TYPE_OBJECT);


typedef struct _Subflow Subflow;
typedef struct _Measurement Measurement;


typedef enum{
  STATE_OVERUSED       = -1,
  STATE_STABLE         =  0,
  STATE_MONITORED       = 1,
}State;

typedef enum{
  SHAREABILITY_CLOSED       = -1,
  SHAREABILITY_NORMAL       =  0,
  SHAREABILITY_OPENED       = 1,
}Shareability;


typedef State (*Checker)(SendingRateDistributor*,Subflow*);

typedef struct _UtilizationReport{
  guint32  target_rate;
  guint32  min_rate;
  guint32  max_rate;
  struct{
    gboolean available;
    gdouble  target_weight;
    gint32   max_rate;
    gint32   min_rate;
    gint32   lost_bytes;
    gint32   discarded_bytes;
    guint64  owd;
    gint8    shareability;
  }subflows[32];
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
  guint32         jitter;
  gdouble         goodput;
  guint32         lost;
  guint32         discard;
  gdouble         discard_rate;
  guint32         receiver_rate;
  guint32         sender_rate;

  gdouble         goodput_ratio;
  gdouble         corrGP;
  gdouble         delay_ratio;

  State           state;
};

typedef struct _Moment Moment;
struct _Moment{
  guint16         PiT;
  guint64         delay;
  guint32         jitter;
  guint32         lost;
  guint32         discard;
  guint32         receiver_rate;
  guint32         sender_rate;
  guint32         goodput;

  guint16         delta_PiT;
  guint64         delta_delay;
  guint32         delta_jitter;
  guint32         delta_discard;
  guint32         delta_receiver_rate;
  guint32         delta_sender_rate;
  guint32         delta_goodput;

  State           state;
};

#define _tpn(num) ((0 < num ? num : CHANGE_HISTORY_LENGTH)-1)
#define _th(subflow, index) (Moment*) (subflow->history[index])
#define _t0(subflow) _th(subflow, subflow->history_index)
#define _t1(subflow) _th(subflow, _tpn(subflow->history_index))
#define _t2(subflow) _th(subflow, _tpn(_tpn(subflow->history_index)))
#define _t3(subflow) _th(subflow, _tpn(_tpn(_tpn(subflow->history_index))))

struct _Subflow{
  guint8             id;
  MPRTPSPath*        path;
  gboolean           available;

  gint32             requested_bytes;
  gint32             supplied_bytes;
  gint32             movable_bytes;

  guint8             overused_history;

  gdouble            ewma_goodput;

  gdouble            weight;
  gdouble            target_weight;
  gdouble            bounce_point;

  gint32             target_rate;
  gint32             estimated_rate;
  gint32             next_target;
  gint32             max_rate;
  gint32             min_rate;

  Moment             history[CHANGE_HISTORY_LENGTH];
  gint               history_index;

  gint32             change_history[CHANGE_HISTORY_LENGTH];
  gint               change_index;
  gint32             change_sum;

  gdouble            goodput_sum;

  //Need for monitoring
  guint              monitoring_interval;
  guint              monitoring_time;

  guint              disable_controlling;
  Checker            controlling;
  guint              turning_point;
  guint              monitoring_disabled;

  Shareability       shareability;

  //need for state transitions
  State              state;
  PercentileTracker *delays;
  gint32             falling_point;

  gdouble            bandwidth_estimation;
  gdouble            bandwidth_estimation_error;

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
//static void
//_refresh_targets(
//    SendingRateDistributor* this,
//    Subflow *subflow);


static void
_time_update_preparation(
    SendingRateDistributor* this);

static void
_time_update_correction(
    SendingRateDistributor* this);

static void
_time_update_requestion(
    SendingRateDistributor* this);

static void
_time_update_compilation(
    SendingRateDistributor* this);

static void
_refresh_available_ids(
    SendingRateDistributor* this);


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

#define _disable_monitoring(subflow) _setup_monitoring(subflow, 0)
static void
_setup_monitoring(
    Subflow *subflow,
    guint interval);

static gboolean
_is_monitoring_done(
        SendingRateDistributor *this,
        Subflow *subflow);

static gboolean
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
#define _get_next_sending_target(subflow) \
  (subflow->target_rate + subflow->requested_bytes - subflow->supplied_bytes)
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
}


void
sndrate_distor_init (SendingRateDistributor * this)
{
  gint i;
  this->sysclock = gst_system_clock_obtain();
  this->controlled_num = 0;
  this->subflows = g_malloc0(sizeof(Subflow)*MPRTP_PLUGIN_MAX_SUBFLOW_NUM);
  for(i=0; i<MPRTP_PLUGIN_MAX_SUBFLOW_NUM; ++i){
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
//  result->max_rate = 128000;
//  result->target_media_rate = 64000;
//  result->target_rate = result->max_rate;
  return result;
}

void sndrate_distor_add_controllable_path(SendingRateDistributor *this,
                                 MPRTPSPath *path,
                                 guint32 sending_target)
{
  Subflow *subflow;
  subflow =  _get_subflow(this, mprtps_path_get_id(path));

  subflow->available = TRUE;
  subflow->target_rate = sending_target;
  subflow->max_rate = 0;
  subflow->min_rate = sending_target * .1;
  subflow->path = g_object_ref(path);
  if(!subflow->delays){
    subflow->delays = make_percentiletracker(256, 80);
    percentiletracker_set_treshold(subflow->delays, 30 * GST_SECOND);
  }
  else{
    percentiletracker_reset(subflow->delays);
  }
  subflow->disable_controlling = 0;
  subflow->measured = FALSE;
  memset(subflow->measurements, 0, sizeof(Measurement) * 3);
  subflow->measurements_index = 0;
  subflow->monitoring_time = 0;
  subflow->monitoring_interval = 0;
  _transit_to(this, subflow, STATE_STABLE);
  _disable_controlling(subflow, 2);

  ++this->controlled_num;
  _refresh_available_ids(this);
}

void sndrate_distor_remove_id(SendingRateDistributor *this, guint8 id)
{
  Subflow *subflow;
  subflow = _get_subflow(this, id);
  if(!subflow->available) goto done;
  _setup_monitoring(_get_subflow(this, id), 0);
  g_object_unref(_get_subflow(this, id)->path);
  subflow->available = FALSE;
  --this->controlled_num;
  _refresh_available_ids(this);
done:
  return;
}



void sndrate_distor_measurement_update(SendingRateDistributor *this,
                                       guint8 id,
                                       RRMeasurement *measurement)
{
  Subflow *subflow;
  gdouble old_goodput;
  subflow = _get_subflow(this, id);
  _mt0(subflow)->state = subflow->state;
  old_goodput = _mt2(subflow)->goodput;
  _m_step(subflow);
  _mt0(subflow)->state = _mt1(subflow)->state;

  if(subflow->state != STATE_OVERUSED){
      percentiletracker_add(subflow->delays, measurement->min_delay);
      percentiletracker_add(subflow->delays, measurement->max_delay);
    percentiletracker_add(subflow->delays, measurement->median_delay);
  }
  _mt0(subflow)->corrh_owd = (gdouble)measurement->median_delay / (gdouble) percentiletracker_get_stats(subflow->delays, NULL, NULL, NULL);
  _mt0(subflow)->delay = measurement->median_delay;
  _mt0(subflow)->corrd = (gdouble)_mt1(subflow)->delay / (gdouble)_mt0(subflow)->delay;
  _mt0(subflow)->discard = measurement->late_discarded_bytes;
  _mt0(subflow)->discard_rate = (gdouble)measurement->late_discarded_bytes / measurement->goodput;
  _mt0(subflow)->lost = measurement->lost;
  _mt0(subflow)->goodput = measurement->goodput;
  _mt0(subflow)->receiver_rate = measurement->receiver_rate;
  _mt0(subflow)->sender_rate = measurement->sender_rate;
  _mt0(subflow)->jitter = measurement->jitter;
  _mt0(subflow)->delay_ratio = (gdouble)_mt1(subflow)->delay / (gdouble)_mt0(subflow)->delay;
  _mt0(subflow)->goodput_ratio = (gdouble)_mt0(subflow)->goodput / _mt1(subflow)->goodput;
  _mt0(subflow)->corrGP = 3 * measurement->goodput / subflow->goodput_sum;

  subflow->goodput_sum+=measurement->goodput;
  subflow->goodput_sum-=old_goodput;
  subflow->estimated_rate = subflow->estimated_rate * .5 + (measurement->sender_rate - measurement->late_discarded_bytes) * .5;


//  if(subflow->state == STATE_OVERUSED)
//   percentiletracker_add(subflow->goodputs, subflow->bandwidth_estimation);
//  else
//   percentiletracker_add(subflow->goodputs, MAX(measurement->goodput,0.));

//  g_print("S%d update."
//          "State: %d, CorrH: %f, GP: %f, J: %d "
//          "PiT: %hu, L:%d, D: %d, RR: %u "
//          "SR: %d DR: %f GR: %f MI: %d EGP: %f\n",
//          subflow->id,
//          _mt0(subflow)->state,
//          _mt0(subflow)->corrh_owd,
//          _mt0(subflow)->goodput,
//          _mt0(subflow)->jitter,
//          _mt0(subflow)->PiT,
//          _mt0(subflow)->lost,
//          _mt0(subflow)->discard,
//          _mt0(subflow)->receiver_rate,
//          subflow->sending_rate,
//          _mt0(subflow)->delay_ratio,
//          _mt0(subflow)->goodput_ratio,
//          subflow->monitoring_interval,
//          subflow->ewma_goodput);

  return;
}

void sndrate_distor_extract_stats(SendingRateDistributor *this,
                                  guint8 id,
                                  guint64 *median_delay,
                                  gint32  *sender_rate,
                                  gdouble *target_rate,
                                  gdouble *goodput,
                                  gdouble *next_target,
                                  gdouble *media_target)
{
  Subflow *subflow;
  subflow = _get_subflow(this, id);
  if(!subflow->available) return;
  if(median_delay)
    *median_delay = percentiletracker_get_stats(subflow->delays, NULL, NULL, NULL);
  if(sender_rate)
    *sender_rate = _mt0(subflow)->sender_rate;
  if(target_rate)
    *target_rate = subflow->target_rate;
  if(goodput)
    *goodput = _mt0(subflow)->goodput;
  if(next_target)
    *next_target = subflow->next_target;
  if(media_target)
    *media_target = this->max_rate;

}

void sndrate_distor_time_update(SendingRateDistributor *this)
{
  //1. Initialize and reset variable for updating
  _time_update_preparation(this);

  //2. mitigate the effect of byte requesting and supplying by
  //moving the bytes between the subflows
  _time_update_correction(this);

  //3. Requesting the media source for new media rate
  _time_update_requestion(this);

  //4. Applying the new rate on the rate distributor
  _time_update_compilation(this);

  return;
}


void _time_update_preparation(SendingRateDistributor* this)
{
  gint i;
  Subflow *subflow;

  this->supplied_bytes = 0;
  this->requested_bytes = 0;
  this->movable_bytes = 0;

  foreach_subflows(this, i, subflow)
  {
    subflow->requested_bytes = 0;
    subflow->supplied_bytes = 0;
    subflow->movable_bytes = 0;

    subflow->overused_history<<=1;
    if(++subflow->change_index == CHANGE_HISTORY_LENGTH)
      subflow->change_index = 0;
    subflow->change_sum-=subflow->change_history[subflow->change_index];

    if(subflow->disable_controlling > 0){
      --subflow->disable_controlling;
      continue;
    }
    subflow->state = subflow->controlling(this, subflow);
    if(subflow->state == STATE_OVERUSED) subflow->overused_history+=1;
//    if(subflow->state == STATE_MONITORED) subflow->monitored_history+=1;

    this->supplied_bytes+=subflow->supplied_bytes;
    this->requested_bytes+=subflow->requested_bytes;

    if(0 < subflow->max_rate && subflow->max_rate < subflow->estimated_rate){
      subflow->movable_bytes=subflow->estimated_rate-subflow->max_rate;
      this->movable_bytes+=subflow->movable_bytes;
    }
  }
}

void _time_update_correction(SendingRateDistributor* this)
{
  Subflow *subflow, *target, *source;
  gint i,j;
  gint32 total = 0, moved_bytes = 0;
  gdouble weight;

  if(!this->supplied_bytes) goto movability;

  foreach_subflows(this, i, subflow)
  {
    if(subflow->overused_history > 0) continue;
    if(subflow->shareability != SHAREABILITY_OPENED) continue;
    total += subflow->target_rate;
  }

  if(0 == total) goto movability;

  foreach_subflows(this, i, subflow){
    if(subflow->overused_history > 0) continue;
    weight = (gdouble)subflow->target_rate / (gdouble)total;
    subflow->requested_bytes += weight * (gdouble) this->supplied_bytes;
  }
  this->supplied_bytes = 0;

movability:
  if(!this->movable_bytes || !this->requested_bytes) goto minability;

  foreach_subflows(this, i, source){
    if(source->movable_bytes == 0) continue;
    foreach_subflows(this, j, target){
      if(target->requested_bytes == 0) continue;
      if(target->requested_bytes <= source->movable_bytes){
        source->movable_bytes -= moved_bytes = target->requested_bytes;
        source->supplied_bytes+=target->requested_bytes;
      }else{
        source->supplied_bytes+= moved_bytes = source->movable_bytes;
        source->movable_bytes = 0;
      }
    }
  }

minability:
  if(!this->supplied_bytes) goto estimatibility;

  foreach_subflows(this, i, subflow){
    gint32 next_sending_target;
    gint32 requested_bytes;
    if(!subflow->supplied_bytes) continue;
    next_sending_target = _get_next_sending_target(subflow);
    if(subflow->min_rate < next_sending_target) continue;

    requested_bytes = subflow->min_rate - next_sending_target;
    subflow->requested_bytes += requested_bytes;
    this->requested_bytes+= requested_bytes;
  }

estimatibility:
  goto exit;
//  foreach_subflows(this, i, subflow){
//    if(subflow->target_rate < _mt0(subflow)->goodput){
//      subflow->correction = _mt0(subflow)->goodput - subflow->target_rate;
//      this->correction =
//    }
//  }

exit:
  return;
}

void _time_update_requestion(SendingRateDistributor* this)
{
  UtilizationReport ur;
  gint32 target_rate = 0;
  gint32 delta_rate;
  gint i;
  Subflow *subflow;
  gint32 check_requested_bytes = 0,check_supplied_bytes = 0;

  foreach_subflows(this, i, subflow){
    ur.subflows[subflow->id].available = TRUE;
    ur.subflows[subflow->id].lost_bytes = _mt0(subflow)->lost;
    ur.subflows[subflow->id].discarded_bytes = _mt0(subflow)->discard;
    ur.subflows[subflow->id].owd = _mt0(subflow)->delay;
    ur.subflows[subflow->id].shareability = subflow->shareability;
    ur.subflows[subflow->id].max_rate = subflow->max_rate;
    ur.subflows[subflow->id].min_rate = subflow->min_rate;
    target_rate += subflow->target_rate;
    check_requested_bytes+=subflow->requested_bytes;
    check_supplied_bytes+=subflow->supplied_bytes;
//    g_print("S%d target rate :%d, delta: %d\n", subflow->id,
//            subflow->target_rate, subflow->requested_bytes-subflow->supplied_bytes);
  }
  if(check_requested_bytes != this->requested_bytes){
    g_print("RB: %d CRB: %d\n", this->requested_bytes, check_requested_bytes);
    g_print("SB: %d CSB: %d\n", this->supplied_bytes, check_supplied_bytes);
  }
  delta_rate = this->requested_bytes - this->supplied_bytes;
//  g_print("target rate :%d, delta: %d\n", target_rate, delta_rate);
  target_rate += delta_rate;

  ur.target_rate = target_rate;
  ur.max_rate = this->max_rate;
  ur.min_rate = this->min_rate;

  if(this->signal_request && this->signal_controller)
    this->signal_request(this->signal_controller, &ur);

  if(ur.target_rate == 0) {
    g_warning("The actual media rate must be greater than 0");
    ur.target_rate = target_rate;
  }

  this->max_rate = ur.max_rate;
  this->min_rate = ur.min_rate;
  this->target_rate = ur.target_rate;

  foreach_subflows(this, i, subflow){
    subflow->target_weight = ur.subflows[subflow->id].target_weight;
    subflow->max_rate      = ur.subflows[subflow->id].max_rate;
    subflow->min_rate      = ur.subflows[subflow->id].min_rate;

    if(0 < ur.subflows[subflow->id].shareability)
      subflow->shareability  = SHAREABILITY_OPENED;
    else if(ur.subflows[subflow->id].shareability < 0)
      subflow->shareability  = SHAREABILITY_CLOSED;
    else
      subflow->shareability  = SHAREABILITY_NORMAL;
  }

}

void _time_update_compilation(SendingRateDistributor* this)
{
  gint i;
  Subflow *subflow;
  gint32 delta;

  foreach_subflows(this, i, subflow)
  {
//    subflow->actual_rate = kalmanfilter_time_update(subflow->estimator, 1., subflow->requested_bytes - subflow->supplied_bytes, 1.);
    delta = subflow->requested_bytes - subflow->supplied_bytes;
    subflow->change_sum+=delta;
    subflow->change_history[subflow->change_index] = delta;
    subflow->target_rate += delta;
    subflow->weight = (gdouble) subflow->target_rate / this->target_rate;
    subflow->measured=FALSE;

//    mprtps_path_set_pacing(subflow->path, subflow->target_rate);

    if(0. < subflow->target_weight){
      if(0 < this->max_rate)
        subflow->max_rate = subflow->target_weight * this->max_rate;
      else if(subflow->target_weight < subflow->weight * .95 || subflow->target_weight < subflow->weight  * 1.05)
        subflow->max_rate = subflow->target_weight * this->target_rate;
      else
        subflow->target_weight = 0.;
    }
  }

}


guint32 sndrate_distor_get_sending_rate(SendingRateDistributor *this, guint8 id)
{
  return _get_subflow(this, id)->target_rate;
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
//
//void _refresh_targets(SendingRateDistributor* this, Subflow *subflow)
//{
//  gdouble rate;
//  subflow->step_rate = 0;
//  if(0 == subflow->target_rate) return;
//  subflow->step_rate = subflow->target_rate;
//  for(rate = (gdouble) subflow->target_rate / (gdouble)subflow->sending_rate;
//      2. < rate;
//      rate = (gdouble) subflow->step_rate / (gdouble)subflow->sending_rate)
//  {
//    subflow->step_rate*=.5;
//  }
//  subflow->step_rate = subflow->target_rate;
//}

void _refresh_available_ids(SendingRateDistributor* this)
{
  gint id;
  Subflow *subflow;
  this->available_ids_length = 0;
  for(id=0; id < MPRTP_PLUGIN_MAX_SUBFLOW_NUM; ++id){
    subflow = _get_subflow(this, id);
    if(!subflow->available) continue;
    this->available_ids[this->available_ids_length++] = subflow->id;
  }
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

  if(_mt0(subflow)->corrh_owd > OT_){
    if(_mt1(subflow)->state == STATE_STABLE) goto done;
    subflow->supplied_bytes += _action_undershoot(this, subflow);
    _disable_controlling(subflow, 2);
    g_print("S%d Undershoot at OVERUSED by OT_0\n", subflow->id);
    goto done;
  }

//  if(_mt0(subflow)->discard_rate > .05){
//    subflow->supplied_bytes += _action_undershoot(this, subflow);
//    g_print("S%d Undershoot at OVERUSED by DS_0\n", subflow->id);
//    goto done;
//  }

  if(_mt1(subflow)->state != STATE_OVERUSED) goto done;

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
//   g_print("S%d: STATE_STABLE\n", subflow->id);
  if(_mt0(subflow)->discard_rate > .1 || _mt0(subflow)->corrh_owd > DT_){
      g_print("S%d Undershoot at STABLE by DS_DT\n", subflow->id);
       subflow->supplied_bytes += _action_undershoot(this, subflow);
       _transit_to(this, subflow, STATE_OVERUSED);
       _disable_controlling(subflow, 2);
       goto done;
  }

  if(_mt0(subflow)->corrh_owd > ST_)
  {
     if(_mt1(subflow)->state != STATE_STABLE)
       goto done;
     g_print("S%d Undershoot at STABLE by ST_0\n", subflow->id);
     subflow->supplied_bytes += _action_undershoot(this, subflow);
     _transit_to(this, subflow, STATE_OVERUSED);
     _disable_controlling(subflow, 2);
     goto done;
  }

  if(0 < _mt0(subflow)->discard){
    ++subflow->turning_point;
    subflow->supplied_bytes+=subflow->target_rate * .05;
    goto done;
  }

  if(_mt0(subflow)->corrh_owd > 1.){
    ++subflow->turning_point;
    goto done;
  }

  if(0 < subflow->monitoring_disabled){
    --subflow->monitoring_disabled;
    goto done;
  }

  if(_enable_monitoring(this, subflow)){
    _transit_to(this, subflow, STATE_MONITORED);
  }

done:
  return subflow->state;
}

State
_check_monitored(
    SendingRateDistributor *this,
    Subflow *subflow)
{

//  g_print("S%d: STATE MONITORED\n", subflow->id);
  if(_mt0(subflow)->corrh_owd > ST_ || _mt0(subflow)->discard_rate > .05){
      g_print("S%d Undershoot at MONITORED by ST_DS\n", subflow->id);
      subflow->supplied_bytes +=_action_undershoot(this, subflow);
      _transit_to(this, subflow, STATE_OVERUSED);
      _disable_controlling(subflow, 2);
      _disable_monitoring(subflow);
    goto done;
  }

  if(_mt0(subflow)->discard){
    ++subflow->turning_point;
    subflow->supplied_bytes +=_action_undershoot(this, subflow);
    _transit_to(this, subflow, STATE_STABLE);
    _disable_monitoring(subflow);
    _disable_controlling(subflow, 1);
    goto done;
  }

  if( 1.05 < _mt0(subflow)->corrh_owd){
    ++subflow->turning_point;
    _disable_monitoring(subflow);
    _transit_to(this, subflow, STATE_STABLE);
    goto done;
  }

  if(_is_monitoring_done(this, subflow)){
    //increase
    _disable_monitoring(subflow);
    subflow->requested_bytes +=_action_bounce_up(this, subflow);
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
//  if(subflow->state == STATE_MONITORED && target != subflow->state){
//    --this->monitored;
//  }
//
//  if(subflow->state == STATE_OVERUSED && target != subflow->state){
//    --this->overused;
//  }


  switch(target){
    case STATE_OVERUSED:
      mprtps_path_set_congested(subflow->path);
//      ++this->overused;
      subflow->controlling = _check_overused;
      subflow->monitoring_interval = 0;
    break;
    case STATE_STABLE:
      mprtps_path_set_non_congested(subflow->path);
      subflow->controlling = _check_stable;

    break;
    case STATE_MONITORED:
//      ++this->monitored;
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
  subflow->monitoring_time = 0;
  mprtps_path_set_monitor_interval(subflow->path, interval);
done:
  return;
}

gboolean _is_monitoring_done(
        SendingRateDistributor *this,
        Subflow *subflow)
{
  if(!subflow->available) goto yes;
  if(1 < subflow->monitoring_interval) goto yes;
  if(1 < ++subflow->monitoring_time) goto yes;

  return FALSE;
yes:
  return TRUE;
}



gboolean _enable_monitoring(
        SendingRateDistributor *this,
        Subflow *subflow)
{
  gdouble rate;
  gdouble target;
  gdouble actual;
  subflow->monitoring_interval = 0;
  if(!subflow->available) goto exit;
  if(subflow->estimated_rate < subflow->target_rate * .95){
    goto exit;
  }
  if(subflow->target_rate * 1.05 < subflow->estimated_rate){
    goto exit;
  }

  if(subflow->estimated_rate < subflow->falling_point * .9){
    target = subflow->falling_point;
    actual = subflow->target_rate;
//    g_print("Falling point\n");
    goto determine;
  }
  subflow->falling_point = 0;


  if(0 < subflow->max_rate){
    if(subflow->max_rate <= subflow->target_rate) {
      goto exit;
    }
//    g_print("target_rate point %d\n", subflow->target_rate);
    target = subflow->max_rate;
    actual = subflow->target_rate;
    goto determine;
  }

  if(0 < this->max_rate){
    if(this->max_rate <= this->target_rate){
      goto exit;
    }
    target = this->max_rate - subflow->target_rate;
    actual = this->target_rate;
    goto determine;
  }

  if(_mt0(subflow)->corrh_owd > 1. && _mt1(subflow)->corrh_owd > 1.){
    subflow->monitoring_interval = 14;
  }else if(_mt0(subflow)->corrh_owd > 1. || _mt1(subflow)->corrh_owd > 1.){
    subflow->monitoring_interval = 10;
  }else{
    subflow->monitoring_interval = 5;
  }


  goto determined;

determine:
  rate = target / actual;
  if(rate > 2.) subflow->monitoring_interval = 2;
  else if(rate > 1.5) subflow->monitoring_interval = 3;
  else if(rate > 1.25) subflow->monitoring_interval = 4;
  else if(rate > 1.2) subflow->monitoring_interval = 5;
  else if(rate > 1.16) subflow->monitoring_interval = 6;
  else if(rate > 1.14) subflow->monitoring_interval = 7;
  else if(rate > 1.12) subflow->monitoring_interval = 8;
  else if(rate > 1.11) subflow->monitoring_interval = 9;
  else if(rate > 1.10) subflow->monitoring_interval = 10;
  else if(rate > 1.09) subflow->monitoring_interval = 11;
  else if(rate > 1.06) subflow->monitoring_interval = 12;
  else if(rate > 1.03) subflow->monitoring_interval = 13;
  else subflow->monitoring_interval = 14;

determined:
  subflow->turning_point = MIN(subflow->turning_point, 3);
  if(0 < subflow->turning_point){
    if(subflow->monitoring_interval == 2)
      subflow->monitoring_interval*=pow(2., subflow->turning_point);
    else if(subflow->monitoring_interval < 5)
      subflow->monitoring_interval*=subflow->turning_point + 1;
    else if(subflow->monitoring_interval < 10)
      subflow->monitoring_interval+=subflow->turning_point * 2;
    else
      subflow->monitoring_interval=14;
  }
  subflow->monitoring_interval = MIN(14, subflow->monitoring_interval);
exit:
  _setup_monitoring(subflow, subflow->monitoring_interval);
  return subflow->monitoring_interval > 0;
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
  gint32 supplied_bytes = 0;
  gdouble r,gr;
  gint32 recent_monitored = 0;

  if(subflow->target_rate <= subflow->min_rate){
    goto done;
  }
  if(subflow->target_rate < subflow->estimated_rate * .9){
    goto done;
  }

  if(0 < subflow->monitoring_interval){
    recent_monitored = (gdouble)subflow->target_rate / (gdouble)subflow->monitoring_interval;
  }else if(_mt1(subflow)->state == STATE_MONITORED ||
     _mt2(subflow)->state == STATE_MONITORED)
  {
    gint32 i;
    i = subflow->change_index;
    if(--i == 0) i = CHANGE_HISTORY_LENGTH - 1;
    recent_monitored = subflow->change_history[i];
    if(--i == 0) i = CHANGE_HISTORY_LENGTH - 1;
    recent_monitored += subflow->change_history[i];
  }

  //mitigate
  if(0 < recent_monitored && recent_monitored < subflow->target_rate * .5){
    ++subflow->turning_point;
    if(recent_monitored < subflow->target_rate  *.8)
      supplied_bytes = recent_monitored * 1.5;
    else
      supplied_bytes = recent_monitored * 1.1;
    goto done;
  }

  if(0 < subflow->change_sum){
    g_print("S%d Mitigate: %d\n", subflow->id, subflow->change_sum);
    if(subflow->change_sum < subflow->target_rate * .33)
      supplied_bytes = subflow->change_sum * 1.5;
    else if(subflow->change_sum < subflow->target_rate * .75)
      supplied_bytes = subflow->change_sum * 1.1;
    else
      supplied_bytes = subflow->target_rate * .505;
    goto done;
  }

  if(_mt0(subflow)->corrh_owd < OT_)
  {
    if(_mt0(subflow)->discard_rate < .25)
      supplied_bytes = _mt0(subflow)->discard * 1.5;
    else if(_mt0(subflow)->discard_rate < .75)
      supplied_bytes = subflow->target_rate * .505;
    goto done;
  }

  if(_mt0(subflow)->sender_rate == 0){
    supplied_bytes = subflow->target_rate * .101;
    goto done;
  }

  r = _mt0(subflow)->receiver_rate / (gdouble)_mt0(subflow)->sender_rate;
  if(1. < r){
    supplied_bytes = .101 * subflow->target_rate;
    goto done;
  }

  gr = _mt0(subflow)->goodput / (gdouble)_mt0(subflow)->sender_rate;
  if(gr < 0.){
    supplied_bytes = .707 * subflow->target_rate;
    goto done;
  }
  if(r < gr * 1.5) r = gr;
  //neglect
  g_print("S%d Reduction %f-%f\n", subflow->id, r, gr);
  if(0. < r && r < 1.) supplied_bytes = (1.-r) * subflow->target_rate;
  else if(1. < r) supplied_bytes = .101 * subflow->target_rate;
  else supplied_bytes = .707 * subflow->target_rate;
  if(gr < r && .75 < r) supplied_bytes*=1.5;
  goto done;

done:
  subflow->falling_point = subflow->target_rate;
  g_print("S%d Undershoot by %d target rate was: %d\n", subflow->id, supplied_bytes, subflow->target_rate);
  return supplied_bytes;
}

guint32 _action_bounce_back(
    SendingRateDistributor *this,
    Subflow *subflow)
{
//  gdouble bounce_point;
  guint32 requested_bytes = 0;

//  bounce_point = MAX(_mt0(subflow)->goodput_ratio, _mt1(subflow)->goodput_ratio);
//  bounce_point = MIN(bounce_point, 2.) - 1.;
//  if(bounce_point < 0.) goto done;
//  requested_bytes = bounce_point * subflow->estimated_rate * .9;
//  g_print("BounceBack on S%d. SR: %d RB:%u\n", subflow->id, subflow->sending_rate, requested_bytes);
//done:
  return requested_bytes;
}


guint32 _action_bounce_up(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  guint32 requested_bytes = 0;
  requested_bytes = subflow->estimated_rate /(gdouble) subflow->monitoring_interval;
  if(0 < subflow->turning_point && 0 == subflow->falling_point)
    --subflow->turning_point;
//  g_print("BounceUp on S%d. SR: %d RB:%u\n", subflow->id, subflow->sending_rate, requested_bytes);
  return requested_bytes;
}

