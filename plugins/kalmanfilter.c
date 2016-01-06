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
#include "kalmanfilter.h"
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


GST_DEBUG_CATEGORY_STATIC (kalmanfilter_debug_category);
#define GST_CAT_DEFAULT kalmanfilter_debug_category

G_DEFINE_TYPE (KalmanFilter, kalmanfilter, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void kalmanfilter_finalize (GObject * object);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
kalmanfilter_class_init (KalmanFilterClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = kalmanfilter_finalize;

  GST_DEBUG_CATEGORY_INIT (kalmanfilter_debug_category, "kalmanfilter", 0,
      "KalmanFilter");

}

void
kalmanfilter_finalize (GObject * object)
{
  KalmanFilter *this;
  this = KALMANFILTER(object);
  g_object_unref(this->measurement_variances);
//  g_object_unref(this->measurement_diffs_variances);
}

void
kalmanfilter_init (KalmanFilter * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->estimated_error = 1.;
  this->measurement_error = 1.;
  this->prior_error = 1.;
}

KalmanFilter *make_kalmanfilter(guint32 length, GstClockTime max_time)
{
  KalmanFilter *result;
    result = g_object_new (KALMANFILTER_TYPE, NULL);
    THIS_WRITELOCK (result);
    result->counter = 0;
    result->measurement_variances = make_variancetracker(length, max_time);
    THIS_WRITEUNLOCK (result);

    return result;
}



gdouble kalmanfilter_time_update(KalmanFilter *this,
                              gdouble control_factor,
                              gint64 control_value,
                              gdouble distrust)
{
  this->prior_value = control_factor * this->estimated_value + control_value;
  this->prior_error = control_factor * this->estimated_error + distrust * this->measurement_error;
  return this->prior_value;
}

void kalmanfilter_measurement_update(KalmanFilter *this, gint64 measured_value)
{
  gdouble measurement_diff = (gdouble)measured_value - this->estimated_value;
  variancetracker_add(this->measurement_variances, measured_value);
  this->measurement_error=variancetracker_get_stats(this->measurement_variances, NULL, NULL);
  this->kalman_gain = this->prior_error / (this->prior_error + this->measurement_error);
  this->estimated_value = this->prior_value + this->kalman_gain * measurement_diff;
  this->estimated_error = (1.-this->kalman_gain) * this->prior_error;
}


void kalmanfilter_reset(KalmanFilter *this)
{
  THIS_WRITELOCK (this);
  THIS_WRITEUNLOCK (this);
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
