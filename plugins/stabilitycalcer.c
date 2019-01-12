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
#include "stabilitycalcer.h"


GST_DEBUG_CATEGORY_STATIC (stability_calcer_debug_category);
#define GST_CAT_DEFAULT stability_calcer_debug_category

#define _now(this) gst_clock_get_time (this->sysclock)

#define STD_VECTOR_LENGTH 512
#define STABILITY_VECTOR_LENGTH 1024

G_DEFINE_TYPE (StabilityCalcer, stability_calcer, G_TYPE_OBJECT);

//----------------------------------------------------------------------

typedef struct{
  gint value;
  GstClockTime added;
}Item;

static void
stability_calcer_finalize (
    GObject * object);


//----------------------------------------------------------------------

void
stability_calcer_class_init (StabilityCalcerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = stability_calcer_finalize;

  GST_DEBUG_CATEGORY_INIT (stability_calcer_debug_category, "stability_calcer", 0,
      "Threshold Finder");
}

void
stability_calcer_finalize (GObject * object)
{
  StabilityCalcer *this = STABILITY_CALCER (object);
  g_object_unref(this->sysclock);
  g_free(this->items);
  g_free(this->recycle);
}


void
stability_calcer_init (StabilityCalcer * this)
{
  this->sysclock       = gst_system_clock_obtain ();
  this->made           = _now(this);
  this->recycle = g_queue_new();
  this->items = g_queue_new();
  this->time_threshold = GST_SECOND;

}

StabilityCalcer*
make_stability_calcer(void)
{
  StabilityCalcer *result;
  result = (StabilityCalcer *) g_object_new (STABILITY_CALCER_TYPE, NULL);
  return result;
}

void
stability_calcer_set_time_threshold(StabilityCalcer* this, GstClockTime time_threshold) {
  this->time_threshold = time_threshold;
}


gdouble
stability_calcer_get_score(StabilityCalcer* this) {
  gdouble result;

  return result;
}

void
stability_calcer_add_sample(StabilityCalcer* this, gint value) {
  Item* item;
  if (g_queue_is_empty(this->recycle)) {
    item = g_malloc(sizeof(Item));
  } else {
    item = g_queue_pop_head(this->recycle);
  }
//  item->value = qts;
  item->value = value;
  item->added = _now(this);

  g_queue_push_tail(this->items, item);

  while (!g_queue_is_empty(this->items)) {
    Item* head = g_queue_peek_head(this->items);
    if (item->added - head->added <= this->time_threshold) {
      break;
    }
    head = g_queue_pop_head(this->items);
    g_queue_push_tail(this->recycle, head);
  }
}

