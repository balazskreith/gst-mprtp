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


static guint* _set_uint_item(Correlator* this, guint value);
static guint _get_uint_item(Correlator* this, guint* value);
static gdouble* _set_double_item(Correlator* this, gdouble value);
static gdouble _get_double_item(Correlator* this, gdouble* value);
static void _add_to_channel(Correlator* this, CorrelatorChannel* channel, guint new_sample);

static void _reduce_channel(Correlator* this, CorrelatorChannel* channel);
static gboolean _delaypuffer_isfull(Correlator* this, datapuffer_t* puffer);
static gboolean _samplepuffer_isfull(Correlator* this, datapuffer_t* puffer);
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
  g_queue_free_full(this->uint_recycle, g_free);
  g_queue_free_full(this->double_recycle, g_free);
  datapuffer_dtor(this->y_ch.samples);
  datapuffer_dtor(this->y_ch.variances);
  datapuffer_dtor(this->x_ch.samples);
  datapuffer_dtor(this->x_ch.variances);
  datapuffer_dtor(this->delay_puffer);
  datapuffer_dtor(this->covariances);
  g_object_unref(this->sysclock);
  g_object_unref(this->on_correlation_calculated);
}


void
correlator_init (Correlator * this)
{
  this->sysclock       = gst_system_clock_obtain ();
  this->made           = _now(this);
  this->uint_recycle   = g_queue_new();
  this->double_recycle = g_queue_new();
  this->on_correlation_calculated  = make_notifier("correlator-on-calculated");
}

Correlator*
make_correlator(gint32 tau, gint32 max_length)
{
  Correlator *result;
  result = (Correlator *) g_object_new (CORRELATOR_TYPE, NULL);
  result->x_ch.samples   = datapuffer_ctor(max_length);
  result->x_ch.variances = datapuffer_ctor(max_length);
  result->y_ch.samples   = datapuffer_ctor(max_length);
  result->y_ch.variances = datapuffer_ctor(max_length);
  result->covariances    = datapuffer_ctor(max_length);
  result->delay_puffer   = 0 < tau ? datapuffer_ctor(max_length) : NULL;
  result->tau            = tau;
  result->max_length     = result->accumulation_length = max_length;

  return result;
}

gboolean _samplepuffer_isfull(Correlator* this, datapuffer_t* puffer){
  return datapuffer_isfull(puffer) || this->accumulation_length <= datapuffer_readcapacity(puffer);
}

gboolean _delaypuffer_isfull(Correlator* this, datapuffer_t* puffer){
  return datapuffer_isfull(puffer) || this->tau <= datapuffer_readcapacity(puffer);
}

void _reduce_channel(Correlator* this, CorrelatorChannel* channel){
  while(_samplepuffer_isfull(this, channel->samples)){
    guint sample;
//    g_print("%d count: %d\n", this->accumulation_length, channel->samples->count);
    sample = _get_uint_item(this, datapuffer_read(channel->samples));
    channel->samples_sum -= sample;
    --channel->counter;
  }

  while(_samplepuffer_isfull(this, channel->variances)){
    gdouble sample = _get_double_item(this, datapuffer_read(channel->variances));
    channel->variance_sum -= sample;
  }
}

void
correlator_set_tau(Correlator* this, gint32 tau){
  tau = MIN(tau, this->max_length);
  if(tau == this->tau){
    return;
  }
  if(!tau){
    datapuffer_clear(this->delay_puffer, g_free);
    datapuffer_dtor(this->delay_puffer);
    this->delay_puffer = NULL;
    this->tau = 0;
    return;
  }
  if(!this->delay_puffer){
    this->delay_puffer = datapuffer_ctor(this->max_length);
  }
  if(this->tau < tau){
    this->tau = tau;
    return;
  }
  this->tau = tau;
  while(_delaypuffer_isfull(this, this->delay_puffer)){
    guint sample = _get_uint_item(this, datapuffer_read(this->delay_puffer));
    _add_to_channel(this, &this->y_ch, sample);
  }
}

void
correlator_set_accumulation_length(Correlator* this, gint32 accumulation_length){

  if(this->accumulation_length <= accumulation_length){
    this->accumulation_length = CONSTRAIN(1, this->max_length, accumulation_length);
    return;
  }

  this->accumulation_length = CONSTRAIN(1, this->max_length, accumulation_length);
  _reduce_channel(this, &this->x_ch);
  _reduce_channel(this, &this->y_ch);

  while(_samplepuffer_isfull(this, this->covariances)){
    gdouble sample = _get_double_item(this, datapuffer_read(this->covariances));
    this->covariance_sum -= sample;

  }

}

void correlator_add_on_correlation_calculated_listener(Correlator* this, ListenerFunc listener, gpointer udata)
{
  notifier_add_listener(this->on_correlation_calculated, listener, udata);
}

void correlator_add_extractors(Correlator* this, CorrelatorDataExtractor Ix_extractor, CorrelatorDataExtractor Iy_extractor){
  this->Ix_extractor = Ix_extractor;
  this->Iy_extractor = Iy_extractor;
}

void correlator_add_data(Correlator* this, gpointer data){
  correlator_add_samples(this, this->Ix_extractor(data), this->Iy_extractor(data));
}

guint* _set_uint_item(Correlator* this, guint value){
  guint* result;
  if(g_queue_is_empty(this->uint_recycle)){
    result = g_malloc(sizeof(guint));
  }else{
    result = g_queue_pop_tail(this->uint_recycle);
  }
  memcpy(result, &value, sizeof(guint));
  return result;
}

guint _get_uint_item(Correlator* this, guint* value){
  guint result = *value;
  g_queue_push_head(this->uint_recycle, value);
  return result;
}

gdouble* _set_double_item(Correlator* this, gdouble value){
  gdouble* result;
  if(g_queue_is_empty(this->double_recycle)){
    result = g_malloc(sizeof(gdouble));
  }else{
    result = g_queue_pop_tail(this->double_recycle);
  }
  memcpy(result, &value, sizeof(gdouble));
  return result;
}

gdouble _get_double_item(Correlator* this, gdouble* value){
  gdouble result = *value;
  g_queue_push_head(this->double_recycle, value);
  return result;
}

static void _add_to_channel(Correlator* this, CorrelatorChannel* channel, guint new_sample){
  guint old_sample = 0;
  gdouble new_variance = 0.;
  gdouble old_variance = 0.;

  if(_samplepuffer_isfull(this, channel->samples)){
    old_sample = _get_uint_item(this, datapuffer_read(channel->samples));
    --channel->counter;
  }
  ++channel->counter;
  datapuffer_write(channel->samples, _set_uint_item(this, new_sample));
  channel->samples_sum += new_sample;
  channel->samples_sum -= old_sample;
  channel->samples_avg  = (gdouble)channel->samples_sum / (gdouble) this->accumulation_length;

  new_variance = pow((gdouble)new_sample - channel->samples_avg, 2);
  if(_samplepuffer_isfull(this, channel->variances)){
    old_variance = _get_double_item(this, datapuffer_read(channel->variances));
  }
  datapuffer_write(channel->variances, _set_double_item(this, new_variance));
  channel->variance_sum += new_variance - old_variance;
//  channel->variance = (1./(gdouble) channel->counter) * channel->variance_sum;
  channel->variance = (1./(gdouble) this->accumulation_length) * channel->variance_sum;
  channel->std = sqrt(channel->variance);
}

void correlator_add_sample(Correlator* this, guint Ix){
  correlator_add_samples(this, Ix, Ix);
}

void correlator_add_samples(Correlator* this, guint x, guint y)
{
  gdouble correlation;
  guint delayed_y  = 0.;
  gdouble new_covariance = 0.;
  gdouble old_covariance = 0.;
  CorrelatorChannel* chX = &this->x_ch;
  CorrelatorChannel* chY = &this->y_ch;

  _add_to_channel(this, chX, x);
  if(!this->delay_puffer){
    delayed_y  = y;
    _add_to_channel(this, chY, y);
  }else {
    if(_delaypuffer_isfull(this, this->delay_puffer)){
      delayed_y = _get_uint_item(this, datapuffer_read(this->delay_puffer));
      _add_to_channel(this, chY, delayed_y);
    }
    datapuffer_write(this->delay_puffer, _set_uint_item(this, y));
  }

  new_covariance =  ((gdouble)x - chX->samples_avg) * ((gdouble)delayed_y - chY->samples_avg);
  if(_samplepuffer_isfull(this, this->covariances)){
    old_covariance = _get_double_item(this, datapuffer_read(this->covariances));
  }
  datapuffer_write(this->covariances, _set_double_item(this, new_covariance));
  this->covariance_sum += new_covariance - old_covariance;
  this->covariance = (1./ (gdouble)MIN(chX->counter, chY->counter)) * this->covariance_sum;
//  g_print("%f/%f*%f\n", this->covariance, chX->std, chY->std);
  //Pearson correlation coefficient
  correlation = this->covariance / (chX->std * chY->std);
  notifier_do(this->on_correlation_calculated, &correlation);
}

