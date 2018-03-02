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
#include "linreger.h"


GST_DEBUG_CATEGORY_STATIC (linear_regressor_debug_category);
#define GST_CAT_DEFAULT linear_regressor_debug_category

#define _now(this) gst_clock_get_time (this->sysclock)

G_DEFINE_TYPE (LinearRegressor, linear_regressor, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
linear_regressor_finalize (
    GObject * object);

static int
_process(
    int n,
    const gdouble x[],
    const gdouble y[],
    gdouble* m,
    gdouble* b,
    gdouble* r);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
linear_regressor_class_init (LinearRegressorClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = linear_regressor_finalize;

  GST_DEBUG_CATEGORY_INIT (linear_regressor_debug_category, "linear_regressor", 0,
      "MpRTP Manual Sending Controller");
}

void
linear_regressor_finalize (GObject * object)
{
  LinearRegressor *this = LINEAR_REGRESSOR (object);
  g_object_unref(this->sysclock);
  g_free(this->x);
  g_free(this->y);
}


void
linear_regressor_init (LinearRegressor * this)
{
  this->sysclock       = gst_system_clock_obtain ();
  this->made           = _now(this);
  this->m = this->r = this->b = 0;
}

LinearRegressor*
make_linear_regressor(guint length, guint refresh_period)
{
  LinearRegressor *result;
  result = (LinearRegressor *) g_object_new (LINEAR_REGRESSOR_TYPE, NULL);
  result->length = length;
  result->refresh_period = refresh_period;
  result->x = g_malloc0(sizeof(gdouble) * length);
  result->y = g_malloc0(sizeof(gdouble) * length);

  return result;
}

void linear_regressor_add_samples(LinearRegressor* this, gdouble x, gdouble y)
{
  this->x[this->index] = x;
  this->y[this->index] = y;
  this->index = (this->index + 1) % this->length;
  ++this->added;

  if (!this->refresh_period || this->added % this->refresh_period == 0) {
    _process(MIN(this->added, this->length), this->x, this->y, &this->m, &this->b, &this->r);
  }
}

gdouble
linear_regressor_predict(LinearRegressor* this, gdouble x) {
  return this->m * x + this->b;
}


inline static gdouble sqr(gdouble x) {
    return x*x;
}


int _process(int n, const gdouble x[], const gdouble y[], gdouble* m, gdouble* b, gdouble* r) {
  gdouble   sumx = 0.0;                      /* sum of x     */
  gdouble   sumx2 = 0.0;                     /* sum of x**2  */
  gdouble   sumxy = 0.0;                     /* sum of x * y */
  gdouble   sumy = 0.0;                      /* sum of y     */
  gdouble   sumy2 = 0.0;                     /* sum of y**2  */
  gdouble denom;

  for (int i=0; i<n; ++i){
      sumx  += x[i];
      sumx2 += sqr(x[i]);
      sumxy += x[i] * y[i];
      sumy  += y[i];
      sumy2 += sqr(y[i]);
  }

  denom = (n * sumx2 - sqr(sumx));
  if (denom == 0) {
      // singular matrix. can't solve the problem.
      *m = 0;
      *b = 0;
      if (r) *r = 0;
          return 1;
  }

  *m = (n * sumxy  -  sumx * sumy) / denom;
  *b = (sumy * sumx2  -  sumx * sumxy) / denom;
  if (r!=NULL) {
      *r = (sumxy - sumx * sumy / n) /    /* compute correlation coeff */
            sqrt((sumx2 - sqr(sumx)/n) *
            (sumy2 - sqr(sumy)/n));
  }

  return 0;
}

