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

#include "bucket.h"
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
  gint index;
}BucketItem;

GST_DEBUG_CATEGORY_STATIC (bucket_debug_category);
#define GST_CAT_DEFAULT bucket_debug_category

#define _now(this) gst_clock_get_time (this->sysclock)

G_DEFINE_TYPE (Bucket, bucket, G_TYPE_OBJECT);

static void
bucket_finalize (
    GObject * object);


void
bucket_class_init (BucketClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = bucket_finalize;

  GST_DEBUG_CATEGORY_INIT (bucket_debug_category, "bucket", 0,
      "MkFIFO Representation");
}

void
bucket_finalize (GObject * object)
{
  Bucket *this = BUCKET (object);
  g_queue_free_full(this->recycle, g_free);
  g_queue_free_full(this->pushed_items, g_free);
  g_free(this->positive_reference);
  g_free(this->negative_reference);
  g_free(this->transform_reference);
  g_free(this->bucket_sizes);
  g_free(this->actual);
  g_object_unref(this->sysclock);
}


void
bucket_init (Bucket * this)
{

}

Bucket* make_bucket(const guint length, gdouble first_bucket_size) {
  gint i;
  Bucket *this;
  this = (Bucket *) g_object_new (BUCKET_TYPE, NULL);
  this->length = length;
  this->sysclock = gst_system_clock_obtain ();
  this->actual = g_malloc0(sizeof(guint) * this->length);
  this->bucket_sizes = g_malloc0(sizeof(gdouble) * this->length);
  this->positive_reference = g_malloc0(sizeof(guint) * this->length);
  this->negative_reference = g_malloc0(sizeof(guint) * this->length);
  this->transform_reference = g_malloc0(sizeof(guint) * this->length);
  this->recycle = g_queue_new();
  this->pushed_items = g_queue_new();

  for (i = 0; i < length; ++i) {
    this->transform_reference[i] = 1;
    this->negative_reference[i] = i < 1 ? 0 : pow(2, i - 1);
    this->positive_reference[i] = i < 1 ? 1 : 0;
  }
  return this;
}


void bucket_set_positive_reference(Bucket* this, guint* vector) {
  memcpy(this->positive_reference, vector, sizeof(guint) * this->length);
}

void bucket_set_negative_reference(Bucket* this, guint* vector) {
  memcpy(this->negative_reference, vector, sizeof(guint) * this->length);
}

void bucket_set_transform_reference(Bucket* this, guint* vector) {
  memcpy(this->transform_reference, vector, sizeof(guint) * this->length);
}

void bucket_set_bucket_sizes(Bucket* this, gdouble* vector) {
  memcpy(this->bucket_sizes, vector, sizeof(gdouble) * this->length);
}

void bucket_set_window_size(Bucket* this, GstClockTime value) {
  this->window_size = value;
}

void bucket_print(Bucket* this, const gchar* name) {
  gint index;
  gchar bucket_buffer[255];
  gchar positive_buffer[255];
  gchar negative_buffer[255];
  gchar transform_buffer[255];
  gchar actual_buffer[255];
  memset(positive_buffer, 0, 255);
  memset(negative_buffer, 0, 255);
  memset(transform_buffer, 0, 255);
  memset(actual_buffer, 0, 255);
  memset(bucket_buffer, 0, 255);
  for (index = 0; index < this->length; ++index) {
    gchar buf[255];
    sprintf(buf, "%-5f | ", this->bucket_sizes[index]); strcat(bucket_buffer, buf); memset(buf, 0, 255);
    sprintf(buf, "%-3d | ", this->positive_reference[index]); strcat(positive_buffer, buf); memset(buf, 0, 255);
    sprintf(buf, "%-3d | ", this->negative_reference[index]); strcat(negative_buffer, buf); memset(buf, 0, 255);
    sprintf(buf, "%-3d | ", this->transform_reference[index]); strcat(transform_buffer, buf); memset(buf, 0, 255);
    sprintf(buf, "%-3d | ", this->actual[index]); strcat(actual_buffer, buf); memset(buf, 0, 255);

  }
  g_print(" - - - - - %s - - - - \n", name);
  g_print("bucket vector:    %s\n", bucket_buffer);
  g_print("positive vector:  %s\n", positive_buffer);
  g_print("negative vector:  %s\n", negative_buffer);
  g_print("transform vector: %s\n", transform_buffer);
  g_print("actual vector:    %s\n", actual_buffer);
}

void bucket_set_bucket_chain(Bucket* this, gdouble value, gdouble growth_factor) {
  gdouble bucket_size = value;
  gint index = 0;
  for(; index < this->length; ++index) {
    this->bucket_sizes[index] = bucket_size;
    bucket_size *= growth_factor;
  }
}

static guint _get_bucket_index(Bucket* this, gdouble new_value) {
  guint bucket_index;
  gdouble bucket_size;
  for (bucket_index = 0; bucket_index < this->length; ++bucket_index) {
    bucket_size = this->bucket_sizes[bucket_index];
    if (bucket_size < new_value) {
      continue;
    }
    return bucket_index;
  }
  return this->length - 1;
}

void bucket_clear(Bucket* this) {
  BucketItem* item;
again:
  if (g_queue_is_empty(this->pushed_items)) {
    this->total_number = 0;
    return;
  }
  item = g_queue_pop_head(this->pushed_items);
  this->actual[item->index] -= this->transform_reference[item->index];
  g_queue_push_tail(this->recycle, item);
  goto again;
}

void bucket_add_value(Bucket* this, gdouble value) {
  guint index = _get_bucket_index(this, value);
  bucket_add_value_at(this, index);
}

void bucket_add_value_at(Bucket* this, guint index) {
  BucketItem* item;
  GstClockTime now = _now(this);
  if (!g_queue_is_empty(this->recycle)) {
    item = g_queue_pop_head(this->recycle);
  } else {
    item = g_malloc0(sizeof(BucketItem));
  }
  item->index = index;
  item->added = now;
//  g_print("added value: %d\n", item->index);
  this->actual[item->index] += this->transform_reference[item->index];
  ++this->total_number;
  g_queue_push_tail(this->pushed_items, item);

again:
  if (g_queue_is_empty(this->pushed_items)) {
    return;
  }
  item = g_queue_peek_head(this->pushed_items);
  if (now - this->window_size < item->added) {
    return;
  }
  item = g_queue_pop_head(this->pushed_items);
  this->actual[item->index] -= this->transform_reference[item->index];
  g_queue_push_tail(this->recycle, item);
  --this->total_number;
  goto again;
}

static gdouble _get_cosine_similarity(guint* a, guint* b, guint length) {
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

gdouble bucket_get_positive_cosine_similarity(Bucket* this) {
  return _get_cosine_similarity(this->actual, this->positive_reference, this->length);
}

gdouble bucket_get_negative_cosine_similarity(Bucket* this) {
  return _get_cosine_similarity(this->actual, this->negative_reference, this->length);
}

guint bucket_get_counter_at(Bucket* this, gint index) {
  return this->actual[index];
}

gint32 bucket_get_total_number(Bucket* this) {
  return this->total_number;
}

gdouble bucket_get_stability(Bucket* this) {
  gdouble positive_similarity = _get_cosine_similarity(this->actual, this->positive_reference, this->length);
  gdouble negative_similarity = _get_cosine_similarity(this->actual, this->negative_reference, this->length);
  return positive_similarity - negative_similarity;
}

