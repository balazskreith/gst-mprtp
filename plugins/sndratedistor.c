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
#include "numstracker.h"


GST_DEBUG_CATEGORY_STATIC (sndrate_distor_debug_category);
#define GST_CAT_DEFAULT sndrate_distor_debug_category
#define MOMENTS_LENGTH 8

G_DEFINE_TYPE (SendingRateDistributor, sndrate_distor, G_TYPE_OBJECT);


typedef struct _Subflow Subflow;
typedef struct _Moment Moment;

struct _Subflow{
  guint8                 id;
  MPRTPSPath*            path;
  gboolean               initialized;
  gboolean               controlled;
  SubflowRateController* controller;

  gint32                 extra_rate;
  gint32                 delta_rate;
  gint32                 sending_rate;
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




void sndrate_distor_time_update(SendingRateDistributor *this)
{
  //1. Initialize and reset variable for updating
  _time_update_preparation(this);

  //2. mitigate the effect of byte requesting and supplying by
  //moving the bytes between the subflows
  _time_update_evaluation(this);

  //3. Requesting the media source for new media rate
  _time_update_requestion(this);

  return;
}



guint32 sndrate_distor_get_sending_rate(SendingRateDistributor *this, guint8 id)
{
  return _get_subflow(this, id)->sending_rate;
}


void _refresh_available_ids(SendingRateDistributor* this)
{
  gint id;
  Subflow *subflow;
  this->available_ids_length = 0;
  for(id=0; id < MPRTP_PLUGIN_MAX_SUBFLOW_NUM; ++id){
    subflow = _get_subflow(this, id);
    if(!subflow->controlled) continue;
    this->available_ids[this->available_ids_length++] = subflow->id;
  }
}

void _time_update_preparation(SendingRateDistributor* this)
{
  gint i;
  Subflow *subflow;
  gint32 prev_sending_target;
  UtilizationReport *ur = &this->ur;

  this->delta_rate = 0;
  this->extra_rate = 0;

  foreach_subflows(this, i, subflow)
  {
    ur->subflows[subflow->id].controlled = TRUE;
    prev_sending_target = subflow->sending_rate;
    subratectrler_time_update(subflow->controller,
                              &subflow->sending_rate,
                              &subflow->extra_rate,
                              &this->ur.subflows[subflow->id]);

    subflow->delta_rate =subflow->sending_rate - prev_sending_target;
    this->delta_rate += subflow->delta_rate;
    this->extra_rate += subflow->extra_rate;

  }
}

void _time_update_evaluation(SendingRateDistributor* this)
{
  Subflow *target, *source;
  gint i,j;
goto done;
  if(0 < this->delta_rate || this->extra_rate < 1) goto done;

  //moving bytes
  foreach_subflows(this, i, target){
    if(0 < target->delta_rate) continue;
    foreach_subflows(this, j, source){
      if(source->extra_rate < 1) continue;
      if(target->delta_rate * -1 <= source->extra_rate){
        subratectrler_add_extra_rate(source->controller, target->delta_rate * -1);
        this->delta_rate+=target->delta_rate * -1;
        break;
      }
      subratectrler_add_extra_rate(source->controller, target->extra_rate);
      this->delta_rate+=source->extra_rate;
    }
  }
  this->extra_rate = 0;
done:
  return;
}

void _time_update_requestion(SendingRateDistributor* this)
{
  UtilizationReport *ur = &this->ur;
  gint32 target_rate = 0;

  this->target_bitrate+=this->delta_rate;
  ur->target_rate = this->target_bitrate;

  if(this->signal_request && this->signal_controller)
    this->signal_request(this->signal_controller, ur);

  if(ur->target_rate == 0) {
    g_warning("The actual media rate must be greater than 0");
    ur->target_rate = target_rate;
  }

  this->target_bitrate = ur->target_rate;

}



