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
#include "nlms.h"
#include <math.h>
#include <string.h>

//#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
//#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
//#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
//#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define THIS_READLOCK(this)
#define THIS_READUNLOCK(this)
#define THIS_WRITELOCK(this)
#define THIS_WRITEUNLOCK(this)


GST_DEBUG_CATEGORY_STATIC (nlms_debug_category);
#define GST_CAT_DEFAULT nlms_debug_category

G_DEFINE_TYPE (NormalizedLeastMeanSquere, nlms, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void nlms_finalize (GObject * object);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
nlms_class_init (NormalizedLeastMeanSquereClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = nlms_finalize;

  GST_DEBUG_CATEGORY_INIT (nlms_debug_category, "nlms", 0,
      "NormalizedLeastMeanSquere");

}

void
nlms_finalize (GObject * object)
{
//  NormalizedLeastMeanSquere *this;
//  this = NLMS(object);
//  if(this->inputs) g_free(this->inputs);
//  if(this->weights) g_free(this->weights);
}

void
nlms_init (NormalizedLeastMeanSquere * this)
{
  g_rw_lock_init (&this->rwmutex);
}

void
nlms_test (void)
{
  gdouble estimation, error;
  gint i;
  gint64 measurements[10] = {5,6,7,8,9,10,11,12,13,14};
  NormalizedLeastMeanSquere *nlms = make_nlms(3, 1., 1.);
  for(i = 0; i<10; ++i){
    estimation = nlms_measurement_update(nlms, measurements[i], &error);
    g_print("%d: measured: %ld estimation: %f error: %f\n",
            i,  measurements[i], estimation, error);
  }
  g_object_unref(nlms);
  nlms = NULL;
  g_free(nlms->weights);
}

NormalizedLeastMeanSquere *make_nlms(guint32 inputs_length,
                                     gdouble step_size,
                                     gdouble divider_constant)
{
  NormalizedLeastMeanSquere *result;
  result = g_object_new (NLMS_TYPE, NULL);
  THIS_WRITELOCK (result);
  result->step_size = step_size;
  result->divider_constant = divider_constant;
  result->length = inputs_length;
  result->inputs = g_malloc0(sizeof(gint64)*inputs_length);
  result->weights = g_malloc0(sizeof(gdouble)*inputs_length);
  result->counter = 0;
  THIS_WRITEUNLOCK (result);

  return result;
}



gdouble nlms_measurement_update(NormalizedLeastMeanSquere *this,
                                gint64 measured_value,
                                gdouble *error)
{

  gdouble estimated_value = 0.;
  gdouble divider = 0.;
  gdouble weight_ratio;
  gint i,c;

  //error estimation
  for(i=0; i<this->length; ++i){
    estimated_value += this->inputs[i] * this->weights[i];
//    g_print("%f = %ld * %f\n", estimated_value, this->inputs[i], this->weights[i]);
  }
  this->estimation_error = measured_value - estimated_value;
  if(error) *error = this->estimation_error;
  //weights estimation
  for(i=0; i<this->length; ++i){
//    g_print("Input: %ld\n", this->inputs[i]);
    divider += (gdouble)this->inputs[i] * (gdouble)this->inputs[i];
  }
  divider += this->divider_constant;
  weight_ratio = this->step_size / divider;
//  g_print("wr: %f\n", weight_ratio);
  for(i=0; i<this->length; ++i){
      this->weights[i] += weight_ratio * (gdouble)this->inputs[i] * this->estimation_error;
  }

  if(this->counter < this->length){
    this->inputs[this->counter] = measured_value;
  }else{
    for(i=0, c=this->length-1; i<c; ++i){
      this->inputs[i] = this->inputs[i+1];
      this->weights[i] = this->weights[i+1];
    }
    this->inputs[c] = measured_value;
    this->weights[c] = weight_ratio * this->inputs[c] * this->estimation_error;
  }
  ++this->counter;
  return estimated_value;
}

void nlms_reset(NormalizedLeastMeanSquere *this)
{
  THIS_WRITELOCK (this);
  THIS_WRITEUNLOCK (this);
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
