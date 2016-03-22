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
#include "sndctrler.h"
#include "numstracker.h"


GST_DEBUG_CATEGORY_STATIC (sndrate_distor_debug_category);
#define GST_CAT_DEFAULT sndrate_distor_debug_category
#define MOMENTS_LENGTH 8

G_DEFINE_TYPE (SendingRateDistributor, sndrate_distor, G_TYPE_OBJECT);


typedef struct _Subflow Subflow;

struct _Subflow{
  guint8                 id;
  MPRTPSPath*            path;
  gboolean               initialized;
  gboolean               ready;
  gboolean               controlled;

  gint32                 extra_rate;
  gint32                 delta_rate;
  gint32                 sending_target;
  gint32                 supplied_bitrate;
  gint32                 requested_bitrate;
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


//#define _get_subflow(this, n) ((Subflow*)(this->subflows + n * sizeof(Subflow)))
static Subflow* _get_subflow(SendingRateDistributor* this, gint n)
{
  Subflow* subflows = this->subflows;
  return subflows+n;
}

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
  mprtp_free(this->subflows);
}


void
sndrate_distor_init (SendingRateDistributor * this)
{
  gint i;
  this->sysclock = gst_system_clock_obtain();
  this->controlled_num = 0;
  this->subflows = mprtp_malloc(sizeof(Subflow)*MPRTP_PLUGIN_MAX_SUBFLOW_NUM);
  this->ur.control.max_mtakover = .25;
  this->ur.control.max_stakover = .125;
  for(i=0; i<MPRTP_PLUGIN_MAX_SUBFLOW_NUM; ++i){
    _get_subflow(this, i)->id = i;
    _get_subflow(this, i)->controlled = FALSE;
//    _get_subflow(this, i)->joint_subflow_ids[i] = 1;
  }
}


SendingRateDistributor *make_sndrate_distor(void)
{
  SendingRateDistributor *result;
  result = g_object_new (SNDRATEDISTOR_TYPE, NULL);
  result->splitter = NULL;
  result->pacer    = NULL;
  return result;
}

void sndrate_distor_setup(SendingRateDistributor *this, StreamSplitter *splitter, PacketsSndQueue *pacer)
{
  this->splitter = splitter;
  this->pacer    = pacer;
}


void sndrate_setup_report(
    SendingRateDistributor *this,
    guint8 id,
    struct _SubflowUtilizationReport *report)
{
  Subflow *subflow;
  gint i;
  subflow =  _get_subflow(this, id);
  memcpy(&this->ur.subflows[id].report, report, sizeof(struct _SubflowUtilizationReport));
  this->ready = subflow->ready = TRUE;
  foreach_subflows(this, i, subflow)
  {
    this->ready &= subflow->ready;
  }
}


void sndrate_distor_add_controlled_subflow(SendingRateDistributor *this, guint8 id)
{
  Subflow *subflow;
  subflow =  _get_subflow(this, id);
  this->ur.subflows[id].controlled = subflow->controlled = TRUE;
  _refresh_available_ids(this);
}

void sndrate_distor_rem_controlled_subflow(SendingRateDistributor *this, guint8 id)
{
  Subflow *subflow;
  subflow =  _get_subflow(this, id);
  this->ur.subflows[id].controlled = subflow->controlled = FALSE;
  _refresh_available_ids(this);
}

MPRTPPluginUtilization* sndrate_distor_time_update(SendingRateDistributor *this)
{
  gint i;
  Subflow *subflow;
  MPRTPPluginUtilization* result = NULL;
  SubflowUtilization *su;
  gdouble monitored_sr = 0., stable_sr = 0.;
  gboolean pacing = FALSE;

  this->delta_rate = this->target_bitrate = 0;
  if(!this->splitter || !this->pacer || !this->ready) goto done;
  this->ready = FALSE;

  foreach_subflows(this, i, subflow)
  {
    su = &this->ur.subflows[subflow->id];
    if(!su->controlled)
      continue;

    subflow->delta_rate      = su->report.target_rate - subflow->sending_target;
    this->delta_rate        += subflow->delta_rate;
    subflow->sending_target  = su->report.target_rate;
    this->target_bitrate    += subflow->sending_target;
    subflow->ready           = FALSE;

    if(su->report.state == 0)       stable_sr    += su->report.sending_rate;
    else if(su->report.state == 1)  monitored_sr += su->report.sending_rate;
  }

  result = &this->ur;
  result->report.target_rate = this->target_bitrate;

  if(0 <= this->delta_rate) goto distribute;
//  g_print("negative delta rate is %d, stable sr is %f monitored sr is %f\n", this->delta_rate, stable_sr, monitored_sr);
  //we try to distribute the reminaing bitrate amongst the stable subflows
  foreach_subflows(this, i, subflow)
  {
    gint32 extra_bitrate;
    gdouble takover, divider;
    su = &this->ur.subflows[subflow->id];
    if(su->report.state < 0  || subflow->delta_rate < 0) continue;
    takover = su->report.state == 0 ? this->ur.control.max_stakover : this->ur.control.max_mtakover;
    divider = su->report.state == 0 ? stable_sr : monitored_sr;
    extra_bitrate = MIN(subflow->sending_target * takover,
                         -1. * this->delta_rate * ((gdouble) su->report.sending_rate / divider));

    subflow->delta_rate     += this->delta_rate += extra_bitrate;
    subflow->sending_target += extra_bitrate;
//    g_print("subflow %d take over %d bits, remining: %d\n", subflow->id, extra_bitrate,subflow->delta_rate);
  }

  //if remaining bitrate still available we apply pacing if it exceeds the 10% of the sending target;
  pacing =this->last_target * .1 < -1* this->delta_rate;
distribute:
  this->last_target = this->target_bitrate;
  packetssndqueue_setup(this->pacer, this->target_bitrate, pacing);
  foreach_subflows(this, i, subflow)
  {
    stream_splitter_setup_sending_target(this->splitter, subflow->id, subflow->sending_target);
  }
  stream_splitter_commit_changes (this->splitter);
done:
  return result;
}


void sndrate_set_initial_disabling_time(SendingRateDistributor *this, guint64 initial_disabling_time)
{
  this->initial_disabling_time = initial_disabling_time;
}

guint32 sndrate_distor_get_sending_rate(SendingRateDistributor *this, guint8 id)
{
  return _get_subflow(this, id)->sending_target;
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


