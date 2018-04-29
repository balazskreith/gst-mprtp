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

#include "stdcalcer.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <gst/gst.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

typedef struct {
  GstClockTime added;
  gdouble value;
}StdCalcerItem;

GST_DEBUG_CATEGORY_STATIC (std_calcer_debug_category);
#define GST_CAT_DEFAULT std_calcer_debug_category

#define _now(this) gst_clock_get_time (this->sysclock)

G_DEFINE_TYPE (StdCalcer, std_calcer, G_TYPE_OBJECT);

static void
std_calcer_finalize (
    GObject * object);


static void
_check_stds(
    StdCalcer* this
);

static void
_do_welford(
    StdCalcer* this,
    gdouble value
);

static void
_do_ewma(
    StdCalcer* this,
    gdouble value
);

static gdouble
_get_p_value(
    gdouble confidence_level,
    guint df
);

void
std_calcer_class_init (StdCalcerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = std_calcer_finalize;

  GST_DEBUG_CATEGORY_INIT (std_calcer_debug_category, "std_calcer", 0,
      "MkFIFO Representation");
}

void
std_calcer_finalize (GObject * object)
{
  StdCalcer *this = STDCALCER (object);
  g_queue_free_full(this->recycle, g_free);
  g_queue_free_full(this->pushed_items, g_free);
  g_object_unref(this->sysclock);
}


void
std_calcer_init (StdCalcer * this)
{

}

StdCalcer* make_std_calcer(GstClockTime window_size, GstClockTime checking_window, StdCalcerMode mode) {
  StdCalcer *this;
  this = (StdCalcer *) g_object_new (STDCALCER_TYPE, NULL);
  this->sysclock = gst_system_clock_obtain ();
  this->recycle = g_queue_new();
  this->pushed_items = g_queue_new();
  this->last_checked = _now(this);
  this->checking_window = checking_window;
  this->window_size = window_size;
  this->confidence_level = 0.05;
  this->mode = mode;

  return this;
}


void std_calcer_set_window_size(StdCalcer* this, GstClockTime value) {
  this->window_size = value;
}

void std_calcer_print(StdCalcer* this, const gchar* name) {

}

void std_calcer_clear(StdCalcer* this) {
  StdCalcerItem* item;
  this->samples_num = 0;
again:
  if (g_queue_is_empty(this->pushed_items)) {
    return;
  }
  item = g_queue_pop_head(this->pushed_items);
  g_queue_push_tail(this->recycle, item);
  --this->count;
  goto again;
}

void std_calcer_reset(StdCalcer* this) {
  this->samples_num = 0;
  this->oldS = this->newS = 0.0;
  this->previousMean = this->newMean = 0.0;
}

void std_calcer_do_check(StdCalcer* this) {
  _check_stds(this);
}

gdouble std_calcer_get_std(StdCalcer* this) {
  return this->std;
}

gdouble std_calcer_get_mean(StdCalcer* this) {
  return this->newMean;
}

void std_calcer_set_confidence_level(StdCalcer* this, gdouble value) {
  this->confidence_level = value;
}

gboolean std_calcer_do_t_probe(StdCalcer* this, gdouble assumed_mu, gdouble mu) {
  gdouble p;
  gdouble t;
  if (this->samples_num < 10) {
    return FALSE;
  }
  p = _get_p_value(this->confidence_level, this->count - 1);
  t = mu - assumed_mu;
  t /= this->std / sqrt(this->count);
  this->t = t;
  return abs(t) < p;
}

void std_calcer_add_value(StdCalcer* this, gdouble value) {
  StdCalcerItem* item;
  GstClockTime now = _now(this);

  ++this->count;


  if (!g_queue_is_empty(this->recycle)) {
    item = g_queue_pop_head(this->recycle);
  } else {
    item = g_malloc0(sizeof(StdCalcerItem));
  }
  item->value = value;
  item->added = now;


  if (this->mode == STDCALCER_WELFORD_MODE) {
    _do_welford(this, value);
  } else if (this->mode == STDCALCER_EWMA_MODE) {
    _do_ewma(this, value);
  }


  if (0 < this->window_size) {
    g_queue_push_tail(this->pushed_items, item);
  } else {
    g_queue_push_tail(this->recycle, item);
  }

  // check if we need to check
  if (0 < this->checking_window && this->last_checked < _now(this) - this->checking_window) {
    _check_stds(this);
    this->last_checked = _now(this);
  }

again:
  if (g_queue_is_empty(this->pushed_items)) {
    return;
  }
  item = g_queue_peek_head(this->pushed_items);
  if (now - this->window_size < item->added) {
    return;
  }
  item = g_queue_pop_head(this->pushed_items);
  g_queue_push_tail(this->recycle, item);
  --this->count;
  goto again;
}

static void _set_sum_item(StdCalcerItem* item, gdouble* sum) {
  *sum += item->value;
}

typedef struct {
  gdouble mean;
  gdouble variance;
}StdCalcHelper;

static void _set_variance_item(StdCalcerItem* item, StdCalcHelper* helper) {
  helper->variance += pow(item->value - helper->mean, 2);
}

void _check_stds(StdCalcer* this) {
  StdCalcHelper helper = {0., 0.};
  guint length = g_queue_get_length(this->pushed_items);
  if (length < 10) {
    // there is no reason to calculate if the sample num is small
    return;
  }
  g_queue_foreach(this->pushed_items, (GFunc)_set_sum_item, &helper.mean);
  helper.mean /= (gdouble) this->count;
  g_queue_foreach(this->pushed_items, (GFunc)_set_variance_item, &helper);
  helper.variance /= (gdouble) this->count;

  // check whether the welford is close or not
  // the null hypothesis is that the welman ford is coming from
  // the same distribution
  {
    gdouble std = sqrt(helper.variance);
    gdouble t = this->newMean - helper.mean;
    gdouble p;
    guint df = this->count - 1; // degrees of freedom
    t /= std / sqrt(this->count);
    p = _get_p_value(this->confidence_level, df);
    if (abs(t) < p) { // accept the hypothesis, everithing is fine
      return;
    }
    // the calculated mean is different then the welford method, we refresh
    this->previousMean = helper.mean;
    this->std = std;
    this->samples_num = MAX(length / 4, 10);
//    this->oldS = helper.variance * (this->samples_num - 1);
  }
}

void _do_welford(StdCalcer* this, gdouble sample) {
  ++this->samples_num;
  if (this->samples_num < 2) {
    this->previousMean = this->newMean = sample;
    this->oldS = 0.0;
  }
  // See Knuth TAOCP vol 2, 3rd edition, page 232
  this->newMean = this->previousMean + (sample - this->previousMean) / (gdouble)this->samples_num;
  this->newS = this->oldS + (sample - this->previousMean)*(sample - this->newMean);

  // set up for next iteration
  this->previousMean = this->newMean;
  this->oldS = this->newS;
  this->std = sqrt(this->newS / (gdouble)(this->samples_num - 1));
}

void _do_ewma(StdCalcer* this, gdouble sample) {
  guint dev = sample < this->last_sample ? this->last_sample - sample : sample - this->last_sample;
  this->last_sample = sample;
  this->std = (31.0 * this->std + dev) / 32.0;
  this->samples_num = 30;
  this->count = 30;
}

gint32 std_calcer_get_total_number(StdCalcer* this) {
  return this->samples_num;
}

static const gdouble p_values[] = {

                        // 0.1
                        3.078, 1.886, 1.638, 1.533, 1.476, 1.44,  1.415, 1.397,
                        1.383, 1.372, 1.363, 1.356, 1.35,  1.345, 1.341, 1.337,
                        1.333, 1.33,  1.328, 1.325, 1.323, 1.321, 1.319, 1.318,
                        1.316, 1.315, 1.314, 1.313, 1.311, 1.31,
                        1.296, 1.289, 1.282,

                        // 0.05
                        6.314, 2.92,  2.353, 2.132, 2.015, 1.943, 1.895, 1.86,
                        1.833, 1.812, 1.796, 1.782, 1.771, 1.761, 1.753, 1.746,
                        1.74,  1.734, 1.729, 1.725, 1.721, 1.717, 1.714, 1.711,
                        1.708, 1.706, 1.703, 1.701, 1.699, 1.697,
                        1.671, 1.658, 1.646,

                        // 0.025
                        12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306,
                        2.262,  2.228, 2.201, 2.179, 2.16,  2.145, 2.131, 2.12,
                        2.11,   2.101, 2.093, 2.086, 2.08,  2.074, 2.069, 2.064,
                        2.06,   2.056, 2.052, 2.048, 2.045, 2.042,
                        2,      1.98,         1.962,

                        // 0.01
                        31.821, 6.965, 4.541, 3.747, 3.365, 3.143, 2.998, 2.896,
                        2.821,  2.764, 2.718, 2.681, 2.65,  2.624, 2.602, 2.583,
                        2.567,  2.552, 2.539, 2.528, 2.518, 2.508, 2.5,   2.492,
                        2.485,  2.479, 2.473, 2.467, 2.462, 2.457,
                        2.39, 2.358, 2.33,

                        // 0.0005
                        636.578, 31.6, 12.924, 8.61,  6.869, 5.959, 5.408, 5.041,
                        4.781,   4.587, 4.437, 4.318, 4.221, 4.14,  4.073, 4.015,
                        3.965,   3.922, 3.883, 3.85,  3.819, 3.792, 3.768, 3.745,
                        3.725,   3.707, 3.689, 3.674, 3.66,  3.646,
                        3.46,    3.373, 3.3
                    };

//static gdouble p_values_[] = { 1.282, 1.646, 1.962, 2.330, 2.581, 3.098, 3.300 };


gdouble _get_p_value(gdouble confidence_level, guint df) {
  guint base;
  if (confidence_level <= 0.0005) {
    base = 132;
  } else if (confidence_level <= 0.01) {
    base = 99;
  } else if (confidence_level <= 0.025) {
    base = 66;
  } else if (confidence_level <= 0.05) {
    base = 33;
  } else {
    base = 0;
  }

  if (df < 31) {
    return p_values[base + df-1];
  } else if(df < 61) {
    return p_values[base + 30];
  } else if(df < 121) {
    return p_values[base + 31];
  } else {
    return p_values[base + 32];
  }
}

