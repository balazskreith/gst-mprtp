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
#include "numstracker.h"


GST_DEBUG_CATEGORY_STATIC (sndrate_distor_debug_category);
#define GST_CAT_DEFAULT sndrate_distor_debug_category
#define MOMENTS_LENGTH 8

G_DEFINE_TYPE (SendingRateDistributor, sndrate_distor, G_TYPE_OBJECT);


typedef struct _Subflow Subflow;
typedef struct _Moment Moment;

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

struct _Subflow{
  guint8                 id;
  MPRTPSPath*            path;
  gboolean               initialized;
  gboolean               controlled;
  SubflowRateController* controller;
};

//----------------------------------------------------------------------
//-------- Private functions belongs to the object ----------
//----------------------------------------------------------------------

static void
sndrate_distor_finalize (
    GObject * object);


//--------------------UTILITIES-----------------------
//static void
//_refresh_targets(
//    SendingRateDistributor* this,
//    Subflow *subflow);

static void
_refresh_available_ids(
    SendingRateDistributor* this);

static void
_time_update_preparation(
    SendingRateDistributor* this);

static void
_time_update_evaluation(
    SendingRateDistributor* this);

static void
_time_update_requestion(
    SendingRateDistributor* this);

static void
_time_update_application(
    SendingRateDistributor* this);

static void
_subflow_derivative_update(
    SendingRateDistributor* this,
    Subflow *subflow);

#define _get_subflow(this, n) ((Subflow*)(this->subflows + n * sizeof(Subflow)))


#define foreach_subflows(this, i, subflow) \
  for(i=0, subflow = _get_subflow(this, this->available_ids[0]); i < this->available_ids_length; subflow = _get_subflow(this,  this->available_ids[++i]))
#define _get_next_sending_target(subflow) \
  (subflow->target_rate + subflow->requested_bytes - subflow->supplied_bytes)

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
    _get_subflow(this, i)->controlled = FALSE;
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

SubflowRateController* sndrate_distor_add_controllable_path(SendingRateDistributor *this,
                                 MPRTPSPath *path,
                                 guint32 sending_target)
{
  Subflow *subflow;
  subflow =  _get_subflow(this, mprtps_path_get_id(path));
  if(!subflow->controller) subflow->controller = make_subratectrler();
  subratectrler_set(subflow->controller, path, sending_target);
  subflow->controlled = TRUE;
  subflow->path = g_object_ref(path);
  ++this->controlled_num;
  _refresh_available_ids(this);
  return subflow->controller;
}

void sndrate_distor_remove_id(SendingRateDistributor *this, guint8 id)
{
  Subflow *subflow;
  subflow = _get_subflow(this, id);
  if(!subflow->controlled) goto done;
  subratectrler_unset(subflow->controller);
  subflow->controlled = FALSE;
  g_object_unref(subflow->path);
  --this->controlled_num;
  _refresh_available_ids(this);
done:
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
    *next_target = subflow->estimated_gp_rate;
  if(media_target)
    *media_target = this->target_rate;

}

guint32 sndrate_distor_time_update(SendingRateDistributor *this)
{
  //1. Initialize and reset variable for updating
  _time_update_preparation(this);

  //2. mitigate the effect of byte requesting and supplying by
  //moving the bytes between the subflows
  _time_update_evaluation(this);

  //3. Requesting the media source for new media rate
  _time_update_requestion(this);

  //4. Applying the new rate on the rate distributor
  _time_update_application(this);

  return this->target_rate;
}



guint32 sndrate_distor_get_sending_rate(SendingRateDistributor *this, guint8 id)
{
  return _get_subflow(this, id)->target_rate;
}


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

void _time_update_preparation(SendingRateDistributor* this)
{
  gint i;
  Subflow *subflow;

  this->supplied_bytes = 0;
  this->requested_bytes = 0;
  this->movable_bytes = 0;

  foreach_subflows(this, i, subflow)
  {
    _subflow_derivative_update(this, subflow);
    subflow->requested_bytes = 0;
    subflow->supplied_bytes = 0;
    subflow->movable_bytes = 0;

    if(subflow->disable_controlling > 0){
      --subflow->disable_controlling;
      continue;
    }
    subflow->state = subflow->controlling(this, subflow);
    _mt0(subflow)->controlled = TRUE;
    this->supplied_bytes+=subflow->supplied_bytes;
    this->requested_bytes+=subflow->requested_bytes;

    if(subflow->state != STATE_OVERUSED &&
       0 < subflow->max_rate &&
       subflow->max_rate < subflow->target_rate){
      subflow->movable_bytes=subflow->target_rate-subflow->max_rate;
      this->movable_bytes+=subflow->movable_bytes;
    }
  }
}

void _time_update_evaluation(SendingRateDistributor* this)
{
  Subflow *subflow, *target, *source;
  gint i,j;
  gint32 total = 0, moved_bytes = 0;
  gdouble weight;

  if(!this->supplied_bytes) goto movability;

  foreach_subflows(this, i, subflow)
  {
    if(subflow->state == STATE_OVERUSED) continue;
    if(subflow->shareability != SHAREABILITY_OPENED) continue;
    total += subflow->target_rate;
  }

  if(0 == total) goto movability;

  foreach_subflows(this, i, subflow){
    if(subflow->state == STATE_OVERUSED) continue;
    if(subflow->shareability != SHAREABILITY_OPENED) continue;
    weight = (gdouble)subflow->target_rate / (gdouble)total;
    subflow->requested_bytes += weight * (gdouble) this->supplied_bytes;
  }
  this->supplied_bytes = 0;

movability:
  g_print("movable bytes: %u, requested bytes: %u\n", this->movable_bytes, this->requested_bytes);
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
  foreach_subflows(this, i, subflow){
    gint32 next_sending_target;
    gint32 requested_bytes;
    next_sending_target = _get_next_sending_target(subflow);
    if(subflow->min_rate < next_sending_target) continue;
g_print("S%d Violating minability. next sending rate is %d, min rate is %d\n",
        subflow->id, next_sending_target, subflow->min_rate);
    requested_bytes = subflow->min_rate - next_sending_target;
    subflow->requested_bytes += requested_bytes;
    this->requested_bytes+= requested_bytes;
  }

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

  //extract informations from subflows
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

  //apply requestion to subflows
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

void _time_update_application(SendingRateDistributor* this)
{
  gint i;
  Subflow *subflow;
  gint32 delta;

  foreach_subflows(this, i, subflow)
  {
    delta = subflow->requested_bytes - subflow->supplied_bytes;
    subflow->target_rate += delta;
    subflow->weight = (gdouble) subflow->target_rate / this->target_rate;

    _mt0(subflow)->delta = delta;
    _mt0(subflow)->target_rate = subflow->target_rate;
    g_print("S%d delta is %d target is %d \n", subflow->id, delta, subflow->target_rate);
    _mt0(subflow)->state = subflow->state;
    _mt0(subflow)->completed=TRUE;
    if(_mt0(subflow)->target_rate * 2 < _mt1(subflow)->target_rate){
      //mprtps_path_set_pacing(subflow->path, subflow->target_rate);
    }else{
      mprtps_path_setup_cwnd(subflow->path, 0);
    }
    if(0. < subflow->target_weight){
      if(0 < this->max_rate)
        subflow->max_rate = subflow->target_weight * this->max_rate;
      else if(subflow->target_weight < subflow->weight * .95 || subflow->target_weight < subflow->weight  * 1.05)
        subflow->max_rate = subflow->target_weight * this->target_rate;
      else
        subflow->target_weight = 0.;
    }

    if(subflow->state == STATE_STABLE){
      numstracker_add(subflow->targets, subflow->target_rate);
    }
  }
}


void _subflow_derivative_update(SendingRateDistributor* this, Subflow *subflow)
{

  _mt0(subflow)->delay_ratio = (gdouble)_mt1(subflow)->delay / (gdouble)_mt0(subflow)->delay;
  _mt0(subflow)->goodput_ratio = (gdouble)_mt0(subflow)->goodput / _mt1(subflow)->goodput;
  _mt0(subflow)->discard_rate = (gdouble)_mt0(subflow)->discard / _mt0(subflow)->receiver_rate;
  _mt0(subflow)->corrh_owd = (gdouble)_mt0(subflow)->delay / (gdouble) percentiletracker_get_stats(subflow->delays, NULL, NULL, NULL);

  subflow->estimated_gp_rate = subflow->estimated_gp_rate * .05 +
    (_mt0(subflow)->sender_rate - _mt0(subflow)->discard) * .95;

  {
    guint32 x0,x1, diff, diff2;
    x0 = _mt0(subflow)->sender_rate - _mt0(subflow)->discard;
    x1 = _mt1(subflow)->sender_rate - _mt1(subflow)->discard;
    if(x0 < x1) diff = x1 - x0;
    else diff = x0 - x1;
    if(diff < subflow->estimated_gp_dev) diff2 = subflow->estimated_gp_dev - diff;
    else diff2 = diff - subflow->estimated_gp_dev;

    subflow->estimated_gp_dev += diff2 * .25;
  }

  {
    gint32 rr,sr;
    gdouble r1, r2, d1, d2;
    rr = _mt0(subflow)->receiver_rate;
    sr = _mt0(subflow)->sender_rate;
    if(rr == 0.) rr = 1.;
    r1 = (gdouble)sr / (gdouble)rr;
    _mt0(subflow)->corr_rate = _mt1(subflow)->corr_rate * .5;
    _mt0(subflow)->corr_rate += r1 * .5;
    rr = _mt1(subflow)->receiver_rate;
    sr = _mt1(subflow)->sender_rate;
    if(rr == 0.) rr = 1.;
    r2 = (gdouble)sr / (gdouble)rr;
    if(r1 < r2) d1 = r2 - r1;
    else d1 = r1 - r2;
    if(d1 < _mt0(subflow)->corr_rate_dev) d2 = _mt0(subflow)->corr_rate_dev - d1;
    else d2 = d1 - _mt0(subflow)->corr_rate_dev;
    _mt0(subflow)->corr_rate_dev += d2 * .5;
  }


}
