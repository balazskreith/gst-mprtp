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
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "correlator.h"
#include "mprtplogger.h"


GST_DEBUG_CATEGORY_STATIC (correlator_debug_category);
#define GST_CAT_DEFAULT correlator_debug_category

#define _now(this) gst_clock_get_time (this->sysclock)

G_DEFINE_TYPE (Correlator, correlator, G_TYPE_OBJECT);


//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
correlator_finalize (
    GObject * object);


//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
correlator_class_init (CorrelatorClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = correlator_finalize;

  GST_DEBUG_CATEGORY_INIT (correlator_debug_category, "correlator", 0,
      "MpRTP Manual Sending Controller");
}

void
correlator_finalize (GObject * object)
{
  Correlator *this = CORRELATOR (object);
  g_queue_clear(this->recycle);
  g_object_unref(this->recycle);
  datapuffer_dtor(this->Ix_ch.puffer);
  datapuffer_dtor(this->Iy_ch.puffer);
  datapuffer_dtor(this->delay_puffer);
  g_object_unref(this->sysclock);
  g_object_unref(this->on_calculated);
}


void
correlator_init (Correlator * this)
{
  this->sysclock   = gst_system_clock_obtain ();
  this->made       = _now(this);
  this->recycle    = g_queue_new();
  this->on_calculated = make_notifier("correlator-on-calculated");
}

Correlator*
make_correlator(gint32 tau, gint32 length)
{
  Correlator *result;
  result = (Correlator *) g_object_new (CORRELATOR_TYPE, NULL);
  result->Ix_ch.puffer   = datapuffer_ctor(length);
  result->Iy_ch.puffer   = datapuffer_ctor(length);
  result->delay_puffer   = 0 < tau ? datapuffer_ctor(tau) : NULL;
  result->tau            = tau;

  return result;
}


void correlator_add_on_calculated_listener(Correlator* this, ListenerFunc listener, gpointer udata)
{
  notifier_add_listener(this->on_calculated, listener, udata);
}

void correlator_add_extractors(Correlator* this, CorrelatorDataExtractor Ix_extractor, CorrelatorDataExtractor Iy_extractor){
  this->Ix_extractor = Ix_extractor;
  this->Iy_extractor = Iy_extractor;
}

void correlator_add_data(Correlator* this, gpointer data){
  correlator_add_intensities(this, this->Ix_extractor(data), this->Iy_extractor(data));
}

static gdouble* _set_item(Correlator* this, gdouble value){
  gdouble* result;
  if(g_queue_is_empty(this->recycle)){
    result = g_malloc(sizeof(gdouble));
  }else{
    result = g_queue_pop_tail(this->recycle);
  }
  memcpy(result, &value, sizeof(gdouble));
  return result;
}

static gdouble _get_item(Correlator* this, gdouble* value){
  gdouble result = *value;
  g_queue_push_head(this->recycle, value);
  return result;
}

static gdouble _add_to_channel(Correlator* this, CorrelatorChannel* channel, gdouble value){
  gdouble result = 0.;
  if(datapuffer_isfull(channel->puffer)){
    result = _get_item(this, datapuffer_read(channel->puffer));
    --channel->counter;
  }

  channel->sum += value - result;
  datapuffer_write(channel->puffer, _set_item(this, value));
  ++channel->counter;
  return result;
}

void correlator_add_intensity(Correlator* this, gdouble Ix){
  correlator_add_intensities(this, Ix, Ix);
}

void correlator_add_intensities(Correlator* this, gdouble Ix_0, gdouble Iy_0)
{
  gdouble Ix_T;
  gdouble Iy_Ttau = 0.;
  gdouble Iy_tau  = 0.;
  CorrelatorChannel* chX = &this->Ix_ch;
  CorrelatorChannel* chY = &this->Iy_ch;

  Ix_T = _add_to_channel(this, chX, Ix_0);
  if(!this->delay_puffer){
    Iy_tau  = Iy_0;
    Iy_Ttau = _add_to_channel(this, chY, Iy_0);
  }else {
    if(datapuffer_isfull(this->delay_puffer)){
      Iy_tau  = _get_item(this, datapuffer_read(this->delay_puffer));
      Iy_Ttau = _add_to_channel(this, chY, Iy_tau);
    }
    datapuffer_write(this->delay_puffer, _set_item(this, Iy_0));
  }


  this->Gxy += (Ix_0 * Iy_tau) - (Ix_T * Iy_Ttau);
  if(!chX->counter || !chY->counter){
    this->g = 0.;
  }else{
    gdouble cx = (gdouble) chX->counter;
    gdouble cy = (gdouble) (chX->counter - this->tau);
    this->g = (gdouble)(this->Gxy/cy) / (gdouble)((chX->sum/cx) * (chY->sum/cy)) - 1.;
  }
  notifier_do(this->on_calculated, &this->g);
}


