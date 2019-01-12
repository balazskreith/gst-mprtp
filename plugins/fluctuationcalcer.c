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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>     /* qsort */
#include "fluctuationcalcer.h"
#include "reportprod.h"

#define _now(this) gst_clock_get_time (this->sysclock)

typedef struct {
	GstClockTime added;
	gboolean is_good;
	double value;
}Item;

GST_DEBUG_CATEGORY_STATIC (fluctuationcalcer_debug_category);
#define GST_CAT_DEFAULT fluctuationcalcer_debug_category

G_DEFINE_TYPE (FluctuationCalcer, fluctuationcalcer, G_TYPE_OBJECT);

static void fluctuationcalcer_finalize(GObject * object);
static void _refresh(FluctuationCalcer *this);
static Item* _make_item(FluctuationCalcer *this);
static void _free_item(Item* item);
static Item* _get_item(FluctuationCalcer *this);
static void _throw_item(FluctuationCalcer* this, Item* item);


void
fluctuationcalcer_class_init (FluctuationCalcerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fluctuationcalcer_finalize;

  GST_DEBUG_CATEGORY_INIT (fluctuationcalcer_debug_category, "fluctuationcalcer", 0,
      "FRACTALFBProducer");

}

void
fluctuationcalcer_finalize (GObject * object)
{
  FluctuationCalcer *this;
  this = FLUCTUATIONCALCER(object);
  g_queue_free_full(this->items, (GDestroyNotify)_free_item);
  g_object_unref(this->items);
  g_queue_free_full(this->recycle, (GDestroyNotify)_free_item);
  g_object_unref(this->recycle);
  
}

void
fluctuationcalcer_init (FluctuationCalcer * this)
{
  this->sysclock = gst_system_clock_obtain();
}

FluctuationCalcer *make_fluctuationcalcer(void)
{
	FluctuationCalcer *this;
	this = g_object_new(FLUCTUATIONCALCER_TYPE, NULL);
	this->items = g_queue_new();
	this->recycle = g_queue_new();

	return this;
}

void fluctuationcalcer_add_good_measurement(FluctuationCalcer *this, double value)
{
	Item* item = _get_item(this);
	item->is_good = TRUE;
	item->value = value;
	item->added = _now(this);
	g_queue_push_tail(this->items, item);
	this->good += value;
	_refresh(this);
}

void fluctuationcalcer_add_bad_measurement(FluctuationCalcer *this, double value)
{
	Item* item = _get_item(this);
	item->is_good = FALSE;
	item->value = value;
	item->added = _now(this);
	this->bad += value;
	g_queue_push_tail(this->items, item);
	_refresh(this);
}

void fluctuationcalcer_setup_time_threshold_provider(FluctuationCalcer *this, FluctuationCalcerTimeValidityProvider time_validity_provider, gpointer udata) {
	this->time_validity_provider = time_validity_provider;
	this->time_validity_provider_udata = udata;
}

static gdouble _get_cosine_similarity(gdouble* a, gdouble* b, guint length) {
	gdouble sumxx = 0., sumxy = 0., sumyy = 0.;
	gint index;
	for (index = 0; index < length; ++index) {
		gdouble x = a[index];
		gdouble y = b[index];
		sumxx += x * x;
		sumyy += y * y;
		sumxy += x * y;
	}
	if (sumxy == 0.) {
		return 0.;
	}
	else if (sumxx == 0. || sumyy == 0.) {
		return 1.;
	}
	return sumxy / sqrt(sumxx * sumyy);
}

gdouble fluctuationcalcer_get_stability_score(FluctuationCalcer *this) {
	gdouble result;
	gdouble good_reference[] = { 1., 0. };
	gdouble bad_reference[] = { 0., 1. };
	gdouble actual[] = { this->good, this->bad };
	gdouble good = _get_cosine_similarity(actual, good_reference, 2);
	gdouble bad = _get_cosine_similarity(actual, bad_reference, 2);
	result = MAX(0., good - bad);
	return result;
}

void _refresh(FluctuationCalcer *this) {
	GstClockTime time_threshold;
	Item* item;
	if (this->time_validity_provider == NULL) {
		return;
	}
	time_threshold = _now(this) - this->time_validity_provider(this->time_validity_provider_udata);
again:
	if (g_queue_is_empty(this->items)) {
		return;
	}
	item = g_queue_peek_head(this->items);
	if (time_threshold < item->added) {
		return;
	}
	item = g_queue_pop_head(this->items);
	if (item->is_good) {
		this->good -= item->value;
	} else {
		this->bad -= item->value;
	}
	_throw_item(this, item);
	goto again;
}

Item* _get_item(FluctuationCalcer *this) {
	Item* result;
	if (g_queue_is_empty(this->recycle)) {
		return _make_item(this);
	}
	result = g_queue_pop_head(this->recycle);
	result->is_good = FALSE;
	return result;
}

void _throw_item(FluctuationCalcer* this, Item* item) {
	g_queue_push_tail(this->recycle, item);
}

Item* _make_item(FluctuationCalcer *this) {
	Item* result = malloc(sizeof(Item));
	return result;
}

void _free_item(Item* item) {
	free(item);
}