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
#include "skalmanfilter.h"
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


GST_DEBUG_CATEGORY_STATIC (skalmanfilter_debug_category);
#define GST_CAT_DEFAULT skalmanfilter_debug_category

G_DEFINE_TYPE (SKalmanFilter, skalmanfilter, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void skalmanfilter_finalize (GObject * object);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
skalmanfilter_class_init (SKalmanFilterClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = skalmanfilter_finalize;

  GST_DEBUG_CATEGORY_INIT (skalmanfilter_debug_category, "skalmanfilter", 0,
      "SKalmanFilter");

}

void
skalmanfilter_finalize (GObject * object)
{
  SKalmanFilter *this;
  this = SKALMANFILTER(object);
  g_object_unref(this->measurement_variances);
//  g_object_unref(this->measurement_diffs_variances);
}

void
skalmanfilter_init (SKalmanFilter * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->estimate_error = 1.;
  this->measurement_error = 1.;
}

SKalmanFilter *make_skalmanfilter(guint32 length, GstClockTime max_time)
{
  return make_skalmanfilter_full(length, max_time, .125);
}

SKalmanFilter *make_skalmanfilter_full(guint32 length,
                                       GstClockTime max_time,
                                       gdouble alpha)
{
  SKalmanFilter *result;
  result = g_object_new (SKALMANFILTER_TYPE, NULL);
  THIS_WRITELOCK (result);
  result->counter = 0;
  result->measurement_variances = make_variancetracker(1024, max_time);
  result->ewma_variances = make_variancetracker(1024, max_time);
  result->alpha = alpha;
  THIS_WRITEUNLOCK (result);

  return result;
}

void skalmanfilter_setup_notifiers(SKalmanFilter *this)
{

}


gdouble skalmanfilter_measurement_update(SKalmanFilter *this, gint64 measured_value)
{
  gdouble measurement_diff = (gdouble)measured_value - this->estimated_value;
  variancetracker_add(this->measurement_variances, measured_value);
  this->measurement_error=variancetracker_get_stats(this->measurement_variances, NULL, NULL);
  this->kalman_gain = this->estimate_error / (this->estimate_error + this->measurement_error);
  this->estimated_value += this->kalman_gain * measurement_diff;
  this->estimate_error *= (1.-this->kalman_gain);
  if(this->measurement_error < measurement_diff * this->measurement_diff &&
     this->estimate_error < this->measurement_error){
    this->estimate_error += this->measurement_error;
  }
  this->measurement_diff = measurement_diff * this->alpha;

//  g_print("P: %f, R:%f, K:%f X^:%f\n",
//          this->estimate_error,
//          this->measurement_error,
//          this->kalman_gain,
//          this->estimated_value);

  return this->estimated_value;
}

gdouble skalmanfilter_get_measurement_std(SKalmanFilter *this)
{
  return sqrt(this->measurement_error);
}

void skalmanfilter_reset(SKalmanFilter *this)
{
  THIS_WRITELOCK (this);
  THIS_WRITEUNLOCK (this);
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
