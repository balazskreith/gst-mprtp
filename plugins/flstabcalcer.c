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
#include "flstabcalcer.h"


GST_DEBUG_CATEGORY_STATIC (fl_stability_calcer_debug_category);
#define GST_CAT_DEFAULT fl_stability_calcer_debug_category

#define _now(this) gst_clock_get_time (this->sysclock)

#define STD_VECTOR_LENGTH 512
#define STABILITY_VECTOR_LENGTH 1024

G_DEFINE_TYPE (FLStabilityCalcer, fl_stability_calcer, G_TYPE_OBJECT);

//----------------------------------------------------------------------

typedef struct{
  gdouble value;
  guint bucket;
  GstClockTime added;
}Item;

static void
fl_stability_calcer_finalize (
    GObject * object);


//----------------------------------------------------------------------

static void _calculate_std(FLStabilityCalcer* this);

//----------------------------------------------------------------------

void
fl_stability_calcer_class_init (FLStabilityCalcerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fl_stability_calcer_finalize;

  GST_DEBUG_CATEGORY_INIT (fl_stability_calcer_debug_category, "fl_stability_calcer", 0,
      "Threshold Finder");
}

void
fl_stability_calcer_finalize (GObject * object)
{
  FLStabilityCalcer *this = FL_STABILITY_CALCER (object);
  g_object_unref(this->sysclock);
  g_free(this->std_vector);
}


void
fl_stability_calcer_init (FLStabilityCalcer * this)
{
  this->sysclock       = gst_system_clock_obtain ();
  this->made           = _now(this);
  this->recycle = g_queue_new();
  this->items = g_queue_new();
  this->time_threshold = GST_SECOND;
  this->last_std_vector_calced = _now(this);

  this->std = .05;
  {
    gdouble vector[] = {0.0, 1.0, 4.0};
    memcpy(this->bad_ref_vector, vector, sizeof(gdouble) * FL_STABILITY_VECTOR_LENGTH);
  }

  this->std_vector = g_malloc(STD_VECTOR_LENGTH * sizeof(gdouble));
}

FLStabilityCalcer*
make_fl_stability_calcer(void)
{
  FLStabilityCalcer *result;
  result = (FLStabilityCalcer *) g_object_new (FL_STABILITY_CALCER_TYPE, NULL);
  return result;
}

void
fl_stability_calcer_set_time_threshold(FLStabilityCalcer* this, GstClockTime time_threshold) {
  this->time_threshold = time_threshold;
}

static gdouble _get_cosine_similarity(gdouble* a, gdouble* b, guint length) {
  gdouble sumxx = 0., sumxy = 0., sumyy = 0.;
  gint index;
  for (index = 0; index < length; ++index) {
    gdouble x = a[index];
    gdouble y = b[index];
    sumxx += x*x;
    sumyy += y*y;
    sumxy += x*y;
  }
  if (sumxy == 0.) {
    return 0.;
  } else if (sumxx == 0. || sumyy == 0.) {
    return 1.;
  }
  return sumxy / sqrt(sumxx * sumyy);
}

gdouble
fl_stability_calcer_do(FLStabilityCalcer* this) {
  gdouble result;
  if (this->actual_count < 2) {
    return 1.;
  }
  result = 1.- _get_cosine_similarity(this->actual_vector, this->bad_ref_vector, FL_STABILITY_VECTOR_LENGTH);
  return result;
}

void
fl_stability_calcer_add_sample(FLStabilityCalcer* this, gdouble fraction_lost) {
  Item* item;
  if (g_queue_is_empty(this->recycle)) {
    item = g_malloc(sizeof(Item));
  } else {
    item = g_queue_pop_head(this->recycle);
  }
  item->value = fraction_lost;
  item->added = _now(this);

  // Add value to the std vector
  {
    this->std_vector[this->std_vector_index] = fraction_lost;
    if (++this->std_vector_index == STD_VECTOR_LENGTH) {
      this->std_vector_index = 0;
      this->std_vector_turned = TRUE;
    }
  }

  if (1.7 * GST_SECOND < _now(this) - this->last_std_vector_calced) {
    _calculate_std(this);
    this->last_std_vector_calced = _now(this);
  }

  if (fraction_lost <= this->std * 2) {
    item->bucket = 0;
  } else if (fraction_lost <= this->std * 4) {
    item->bucket = 1;
  } else {
    item->bucket = 2;
  }

  ++this->actual_count;
  this->actual_vector[item->bucket] += item->value;
  g_queue_push_tail(this->items, item);

  while (!g_queue_is_empty(this->items)) {
    Item* head = g_queue_peek_head(this->items);
    if (item->added - head->added <= this->time_threshold) {
      break;
    }
    head = g_queue_pop_head(this->items);
    this->actual_vector[head->bucket] -= head->value;
    --this->actual_count;
    g_queue_push_tail(this->recycle, head);
  }
}

void _calculate_std(FLStabilityCalcer* this) {
  gint length = this->std_vector_turned ? STD_VECTOR_LENGTH : this->std_vector_index;
  gint i;
  gdouble avg = 0.;
  if (length < 30) {
    return;
  }
  for (i = 0; i < length; ++i) {
    avg += this->std_vector[i];
  }
  avg /= (gdouble) length;

  this->std = 0.;
  for (i = 0; i < length; ++i) {
    this->std += pow(this->std_vector[i] - avg, 2);
  }
  this->std /= (gdouble) (length - 1);
//  this->std = CONSTRAIN(.01, .1, sqrt(this->std));
}

