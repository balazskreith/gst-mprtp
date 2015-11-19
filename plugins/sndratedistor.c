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
typedef struct _Process Process;

typedef enum{
  STATE_OVERUSED = -1,
  STATE_STABLE   =  0,
  STATE_MONITORED = 1,
}SubflowState;

struct _Measurement{
  gdouble         variance;
  gdouble         corrl_owd;
  gdouble         corrh_owd;
  guint32         goodput;
  SubflowState    state;
};

struct _Process{
  gboolean active;
  guint8   ticknum;
  void    (*process)(SendingRateDistributor*,Subflow*);
};

struct _Subflow{
  guint8          id;
  MPRTPSPath*     path;
  gboolean        available;
  gdouble         fallen_rate;

  guint8          joint_subflow_ids[SNDRATEDISTOR_MAX_NUM];
  void          (*procedure)(SendingRateDistributor*,Subflow*);
  guint32         extra_bytes;
  guint           monitoring_interval;
  guint8          stability;
  gint32          delta_sr;
  guint32         sending_rate;
  Measurement     measurements[MEASUREMENT_LENGTH];
  guint8          measurements_index;
  Process         balancing;
  Process         pausing;
};

//----------------------------------------------------------------------
//-------- Private functions belongs to the object ----------
//----------------------------------------------------------------------

static const gdouble ST_ = 1.1; //Stable treshold
static const gdouble OT_ = 2.;  //Overused treshold
static const gdouble DT_ = 1.5; //Down Treshold
static const gdouble MT_ = 1.1; //Monitoring Overused treshold

static void
sndrate_distor_finalize (
    GObject * object);

static void
_supply_requested_bytes(
    SendingRateDistributor *this);

static void
_consume_fallen_bytes(
    SendingRateDistributor *this);

static void
_request(
    SendingRateDistributor *this);

static void
_assign(
    SendingRateDistributor *this);

static void
_disable_monitoring(
    Subflow* subflow);

static void
_enable_monitoring(
    Subflow *subflow);

static void
_recalc_monitoring(
    Subflow *subflow);

static Subflow*
_get_most_stable_highest_extra(
    SendingRateDistributor *this);

static Measurement*
_mt0(Subflow* this);

static Measurement*
_mt1(Subflow* this);

static Measurement*
_mt2(Subflow* this);

static Measurement*
_m_step(Subflow *this);

static void
_state_overused(
    SendingRateDistributor *this,
    Subflow *subflow);

static void
_state_stable(
    SendingRateDistributor *this,
    Subflow *subflow);

static void
_state_monitored(
    SendingRateDistributor *this,
    Subflow *subflow);

static void
_transit_to(
    Subflow *subflow,
    SubflowState target);

static void
_balancing(
    SendingRateDistributor *this,
    Subflow* subflow);

static void
_pause_rate_controling(
    SendingRateDistributor *this,
    Subflow* subflow,
    guint8 pausing);

static void
_process_undershoot(
    SendingRateDistributor *this,
    Subflow* subflow);

static void
_process_bounce_back(
    SendingRateDistributor *this,
    Subflow* subflow);

static gboolean
_process_is_pausing(
    SendingRateDistributor *this,
    Subflow* subflow);

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
#define _get_subflow(this, n) ((Subflow*)(this->subflows + n * sizeof(Subflow)))

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
                                       gdouble variance,
                                       gdouble corrh_owd,
                                       gdouble corrl_owd)
{
  Subflow *subflow;
  subflow = _get_subflow(this, id);
  if(!subflow->available) goto done;
  _m_step(subflow);
  _mt0(subflow)->corrh_owd = corrh_owd;
  _mt0(subflow)->corrl_owd = corrl_owd;
  _mt0(subflow)->goodput = goodput;
  _mt0(subflow)->variance = variance;
done:
  return;
}

void sndrate_distor_reduce(SendingRateDistributor *this, guint8 id)
{
  Subflow *subflow;
  subflow = _get_subflow(this, id);
  if(!subflow->available) goto done;
  if(_process_is_pausing(this, subflow)) goto done;
  _balancing(this, subflow);
  _pause_rate_controling(this, subflow, 2);
  _transit_to(subflow, STATE_OVERUSED);
done:
  return;
}

void sndrate_distor_mitigate(SendingRateDistributor *this, guint8 id)
{
  Subflow *subflow;
  subflow = _get_subflow(this, id);
  if(!subflow->available) goto done;
  if(_process_is_pausing(this, subflow)) goto done;
  _balancing(this, subflow);
done:
  return;
}

void sndrate_distor_keep(SendingRateDistributor *this, guint8 id)
{
  Subflow *subflow;
  subflow = _get_subflow(this, id);
  if(!subflow->available){
    g_warning("NOT ADDED SUBFLOW!!!!");
    goto done;
  }
  if(subflow->balancing.active){
    subflow->balancing.process(this, subflow);
  }
  if(_process_is_pausing(this, subflow)) goto done;
  subflow->procedure(this, subflow);
done:
  return;
}

void sndrate_distor_time_update(SendingRateDistributor *this, guint32 media_rate)
{
  this->media_rate = media_rate;
  _supply_requested_bytes(this);
  _consume_fallen_bytes(this);
  _request(this);
  _assign(this);
  return;
}

guint32 sndrate_distor_get_rate(SendingRateDistributor *this, guint8 id)
{
  return _get_subflow(this, id)->sending_rate;
}


void _supply_requested_bytes(SendingRateDistributor *this)
{
  Subflow*          subflow;
  guint32           taken_bytes;
  gdouble           rate;
  guint8            id;
  gdouble           mr,tr;
  if(!this->requested_bytes) goto done;
  for(id=0; id < SNDRATEDISTOR_MAX_NUM; ++id){
    if(!this->changeable[id]) continue;
    subflow = _get_subflow(this, id);
    rate = (gdouble) subflow->sending_rate  * (gdouble) subflow->stability;
    rate/= (gdouble)this->changeable_sr_sum * (gdouble)this->changeable_stability_sum;
    taken_bytes = (gdouble)this->requested_bytes * rate;
    if(!subflow->monitoring_interval || subflow->monitoring_interval < 3)
      goto assign;

    tr = (gdouble) taken_bytes / (gdouble) subflow->sending_rate;
    do{
        mr = 1./(gdouble)subflow->monitoring_interval * (gdouble)subflow->sending_rate;
        if(tr < mr) break;
        --subflow->monitoring_interval;
    }while(subflow->monitoring_interval>2);
  assign:
    subflow->extra_bytes += taken_bytes;
    subflow->delta_sr -= taken_bytes;
  }
done:
  return;
}

void _consume_fallen_bytes(SendingRateDistributor *this)
{
  Subflow*          subflow;
  guint32           allocated_bytes;
  gdouble           rate;
  guint8            id;
  if(!this->fallen_bytes) goto done;
  while((subflow = _get_most_stable_highest_extra(this))){
    if(subflow->extra_bytes < this->fallen_bytes)
      allocated_bytes = subflow->extra_bytes;
    else
      allocated_bytes = subflow->extra_bytes - this->fallen_bytes;
    subflow->stability = 0;
    subflow->extra_bytes -= allocated_bytes;
    subflow->delta_sr += allocated_bytes;
    this->fallen_bytes-=allocated_bytes;
    _recalc_monitoring(subflow);
    if(!this->fallen_bytes) goto done;
  }

  for(id=0; id < SNDRATEDISTOR_MAX_NUM; ++id){
    if(!this->changeable[id]) continue;
    subflow = _get_subflow(this, id);
    rate = (gdouble) subflow->sending_rate;
    rate/= (gdouble) this->changeable_sr_sum;
    allocated_bytes = rate * this->fallen_bytes;
    subflow->delta_sr += rate * (gdouble)this->fallen_bytes;
  }
  this->fallen_bytes = 0;
done:
  return;
}

void _request(SendingRateDistributor *this)
{

}

void _assign(SendingRateDistributor *this)
{
  guint8         id;
  Subflow*       subflow;
  for(id=0; id < SNDRATEDISTOR_MAX_NUM; ++id){
    subflow = _get_subflow(this, id);
    if(!subflow->available) continue;
    subflow->sending_rate += subflow->delta_sr;
    subflow->delta_sr=0;
  }
}


void _disable_monitoring(Subflow* subflow)
{
  if(!subflow->available || !subflow->path) goto done;
  mprtps_path_set_monitor_interval(subflow->path, 0);
done:
  return;
}

void _enable_monitoring(Subflow *subflow)
{
  guint8 monitoring_interval = 8;
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
  guint32 MB,NMB,TSR,SR,MI;
  gdouble D1,D2;
  if(!subflow->monitoring_interval) goto exit;
  MI = subflow->monitoring_interval;
  if(MI < 3 || 13 < MI) goto done;
  SR = subflow->sending_rate;
  TSR = subflow->sending_rate + subflow->delta_sr;
  MB = 1./(gdouble)MI * (gdouble)SR;
  if(SR + MB < TSR){
    _disable_monitoring(subflow);
    goto exit;
  }
  NMB = 1./(gdouble)MI * (gdouble)TSR;
  if(NMB < MB) goto increase;
  else         goto decrease;
increase:
  D1 = abs(MB - NMB);
  D2 = abs(1./(gdouble)(MI-1)*(gdouble)TSR-(gdouble)MB);
  if(D1 < D2 || MI < 3) goto done;
  --MI;
  NMB = 1./(gdouble)(MI)*(gdouble)TSR;
  goto increase;
decrease:
  D1 = abs(NMB - MB);
  D2 = abs(1./(gdouble)(MI+1)*(gdouble)TSR-(gdouble)MB);
  if(D1 < D2 || 13 < MI) goto done;
  ++MI;
  NMB = 1./(gdouble)(MI)*(gdouble)TSR;
  goto decrease;
done:
  subflow->monitoring_interval = MI;
  mprtps_path_set_monitor_interval(subflow->path, MI);
exit:
  return;
}


Subflow* _get_most_stable_highest_extra(SendingRateDistributor *this)
{
  Subflow *subflow,*result = NULL;
  gint i;
  guint32 max = 0;
  for(i=0; i<SNDRATEDISTOR_MAX_NUM; ++i){
    subflow = _get_subflow(this, i);
    if(!subflow->available || !this->changeable[i]) continue;
    if(!subflow->extra_bytes || !subflow->stability) continue;
    if(subflow->extra_bytes * subflow->stability < max) continue;
    max = subflow->extra_bytes * subflow->stability;
    result = subflow;
  }
  return result;
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


void
_state_overused(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  subflow->stability = -1;
  if(_mt0(subflow)->corrh_owd > OT_){
    _balancing(this, subflow);
    _pause_rate_controling(this, subflow, 2);
    goto done;
  }
  _transit_to(subflow, STATE_STABLE);
done:
  return;
}

void
_state_stable(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  if(subflow->stability < 0){
    subflow->stability = 0;
  }
  if(!subflow->balancing.active){
    this->changeable[subflow->id] = 1;
  }
  if(_mt0(subflow)->corrh_owd > ST_){
    if(_mt1(subflow)->corrh_owd > ST_){
      _balancing(this, subflow);
      _pause_rate_controling(this, subflow, 2);
      _transit_to(subflow, STATE_OVERUSED);
    }
    _disable_monitoring(subflow);
    goto done;
  }
  _enable_monitoring(subflow);
  if(subflow->sending_rate <_mt0(subflow)->goodput)
    subflow->delta_sr += _mt0(subflow)->goodput - subflow->sending_rate;
  _transit_to(subflow, STATE_MONITORED);
done:
  return;
}

void
_state_monitored(
    SendingRateDistributor *this,
    Subflow *subflow)
{
  if(_mt0(subflow)->corrh_owd > DT_){
    _balancing(this, subflow);
    _disable_monitoring(subflow);
    _pause_rate_controling(this, subflow, 2);
    _transit_to(subflow, STATE_OVERUSED);
    goto done;
  }
  if(_mt0(subflow)->corrh_owd > ST_){
      _disable_monitoring(subflow);
    _transit_to(subflow, STATE_STABLE);
    goto done;
  }
  if(_mt0(subflow)->corrl_owd > MT_ && subflow->monitoring_interval < 14){
    ++subflow->monitoring_interval;
  }
  if(_mt1(subflow)->state != STATE_MONITORED){
    goto done;
  }
  if(subflow->stability < 1) subflow->stability = 1;
  else if(_mt2(subflow)->state == STATE_MONITORED) subflow->stability = 2;
  subflow->extra_bytes += 1./(gdouble) subflow->monitoring_interval * (gdouble)subflow->sending_rate;
done:
  return;
}

void
_transit_to(
    Subflow *subflow,
    SubflowState target)
{
  _mt0(subflow)->state = target;
  switch(target){
    case STATE_MONITORED:
      subflow->procedure = _state_monitored;
    break;
    case STATE_OVERUSED:
      subflow->procedure = _state_overused;
    break;
    case STATE_STABLE:
      subflow->procedure = _state_stable;
    break;
  }
}

void _balancing(SendingRateDistributor *this, Subflow* subflow)
{
  if(subflow->balancing.active) goto done;
  subflow->balancing.active = TRUE;
  subflow->balancing.ticknum = 0;
  subflow->balancing.process = _process_undershoot;
done:
  return;
}

void _pause_rate_controling(SendingRateDistributor *this, Subflow* subflow, guint8 pausing)
{
  subflow->pausing.active = TRUE;
  subflow->pausing.ticknum += pausing;
  subflow->pausing.process = NULL;
}


void _process_undershoot(SendingRateDistributor *this, Subflow* subflow)
{
  guint32 fallen_bytes;
  gint i;
  if(!subflow->available) goto exit;
  if(subflow->balancing.ticknum) goto done;
  subflow->monitoring_interval = 0;
  fallen_bytes = (subflow->sending_rate-_mt0(subflow)->goodput)*1.2;
  subflow->fallen_rate = (gdouble)fallen_bytes / (gdouble) subflow->sending_rate;
  subflow->delta_sr = -1*fallen_bytes;
  subflow->extra_bytes = 0;
  subflow->stability = -1;
  this->fallen_bytes += fallen_bytes;
  for(i=0; i<SNDRATEDISTOR_MAX_NUM;++i){
    if(!subflow->joint_subflow_ids[i]) continue;
    this->changeable[i] = 0;
  }
done:
  if(++subflow->balancing.ticknum < 2) goto exit;
  subflow->balancing.process = _process_bounce_back;
  subflow->balancing.ticknum = 0;
exit:
  return;
}

void _process_bounce_back(SendingRateDistributor *this, Subflow* subflow)
{
  gint i;
  if(!subflow) goto exit;
  if(subflow->balancing.ticknum) goto done;
  subflow->delta_sr = _mt1(subflow)->goodput-subflow->sending_rate;
  subflow->extra_bytes = 0;
  if(subflow->delta_sr < 0)
    this->requested_bytes += subflow->delta_sr;
  else
    this->fallen_bytes += subflow->delta_sr;
done:
  if(++subflow->balancing.ticknum < 2) goto exit;
  subflow->balancing.active = FALSE;
  subflow->balancing.ticknum = 0;
  for(i=0; i<SNDRATEDISTOR_MAX_NUM;++i){
    if(!subflow->joint_subflow_ids[i]) continue;
    if(i == subflow->id) goto changeable;
    if(!_get_subflow(this, i)->available) continue;
    if(_get_subflow(this, i)->balancing.active) continue;
  changeable:
    this->changeable[i] = 1;
  }
exit:
  return;
}

gboolean _process_is_pausing(SendingRateDistributor *this, Subflow* subflow)
{
  if(!subflow->pausing.active) return FALSE;
  if(subflow->pausing.ticknum > 0){
    --subflow->pausing.ticknum;
    return TRUE;
  }
  subflow->pausing.active = FALSE;
  return FALSE;
}
