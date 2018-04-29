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
#include "thresholdfinder.h"


GST_DEBUG_CATEGORY_STATIC (threshold_finder_debug_category);
#define GST_CAT_DEFAULT threshold_finder_debug_category

#define _now(this) gst_clock_get_time (this->sysclock)

#define MIN_TH 10e-7
#define PIECES 3
#define TERMINAL_DIVISION 20

#define DEBUG TRUE

G_DEFINE_TYPE (ThresholdFinder, threshold_finder, G_TYPE_OBJECT);

//----------------------------------------------------------------------

typedef struct{
  gdouble x;
  gdouble y;
}Vector;

typedef struct {
  gdouble x;
  gdouble y;
}Point;

typedef struct {
  gdouble a;
  gdouble b;
}Function;

static void
threshold_finder_finalize (
    GObject * object);


//----------------------------------------------------------------------


static int cmpdoubles (const void * a, const void * b) {
   return ( *(gdouble*)a - *(gdouble*)b );
}

static Function
_get_f(
    Point* p1,
    Point* p2);

static gint
_find_threshold(
    ThresholdFinder* this,
    gdouble* values,
    guint length,
    guint terminal_size);

//----------------------------------------------------------------------

void
threshold_finder_class_init (ThresholdFinderClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = threshold_finder_finalize;

  GST_DEBUG_CATEGORY_INIT (threshold_finder_debug_category, "threshold_finder", 0,
      "Threshold Finder");
}

void
threshold_finder_finalize (GObject * object)
{
  ThresholdFinder *this = THRESHOLD_FINDER (object);
  g_object_unref(this->sysclock);
}


void
threshold_finder_init (ThresholdFinder * this)
{
  this->sysclock       = gst_system_clock_obtain ();
  this->made           = _now(this);
}

ThresholdFinder*
make_threshold_finder(void)
{
  ThresholdFinder *result;
  result = (ThresholdFinder *) g_object_new (THRESHOLD_FINDER_TYPE, NULL);
  return result;
}

static void _print_values(gdouble* values, gint length) {
  gint i;
  g_print("Threshold values: ");
  for (i = 0; i < length; ++i){
    g_print("%f,", values[i]);
  }
  g_print("\n");
}

gint
threshold_finder_do(ThresholdFinder* this, gdouble* values, guint length) {
  gint result = 0;
  if (DEBUG) {
    _print_values(values, length);
  }
  qsort(values, length, sizeof(gdouble), cmpdoubles);
  result = _find_threshold(this, values, length, length / TERMINAL_DIVISION);
  return result;
}

typedef struct{
  gint index;
  gint positions[2];
  Function f;
  gdouble y;
  gdouble y_hat;
  gdouble cost;
}Subject;

gint
_find_threshold(ThresholdFinder* this, gdouble* values, guint length, guint terminal_size) {
  Point samples[3];
  gint sampled = 0;
  guint size = length - 1;
  Subject selected;
  Subject actual;
  gboolean selected_init = FALSE;
  gint piece;
  memset(&selected, 0, sizeof(Subject));

  for (piece = 0; piece < PIECES + 1; ++piece) {
    gint position = size / PIECES * piece;
    Point point = {position, values[position]};
    gint index = sampled++ % 3;
    samples[index] = point;
    if (sampled < 3) {
      continue;
    }
    if (index == 2) {
      actual.f = _get_f(samples, samples + 1);
    } else if (index == 0) {
      actual.f = _get_f(samples + 1, samples + 2);
    } else{
      actual.f = _get_f(samples + 2, samples);
    }
    actual.y_hat = actual.f.a * point.x + actual.f.b;
    actual.y = point.y;
    actual.cost = abs(actual.y - actual.y_hat);
    actual.positions[0] =  size / PIECES * (piece - 1);
    actual.positions[1] = position;
    if (!selected_init || selected.cost < actual.cost) {
      selected = actual;
      selected_init = TRUE;
    }
  }

  if (length < terminal_size) {
    return selected.positions[0];
  }
  return selected.positions[0] + _find_threshold(this, values + selected.positions[0], selected.positions[1] - selected.positions[0] + 1, terminal_size);
}


Function _get_f(Point* p1, Point* p2) {
  Function result = {0., 0.};
  if (MIN_TH < p2->x - p1->x) {
    result.a = (p2->y - p1->y) / (p2->x - p1->x);
  } else {
    result.a = 999999999.9; // Inf
  }
  result.b = p1->y - p1->x * result.a;
  return result;
}

