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
#include "variancetracker.h"
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


GST_DEBUG_CATEGORY_STATIC (variancetracker_debug_category);
#define GST_CAT_DEFAULT covariancetracker_debug_category

G_DEFINE_TYPE (VarianceTracker, variancetracker, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void variancetracker_finalize (GObject * object);
static void
_add_value(VarianceTracker *this, gint64 value);
static void
_obsolate (VarianceTracker * this);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
variancetracker_class_init (VarianceTrackerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = variancetracker_finalize;

  GST_DEBUG_CATEGORY_INIT (variancetracker_debug_category, "variancetracker", 0,
      "VarianceTracker");

}

void
variancetracker_finalize (GObject * object)
{
  VarianceTracker *this;
  this = VARIANCETRACKER(object);
  g_object_unref(this->sysclock);
  g_free(this->items);
}

void
variancetracker_init (VarianceTracker * this)
{
  g_rw_lock_init (&this->rwmutex);
}

VarianceTracker *make_variancetracker(
    guint32 length,
    GstClockTime obsolation_treshold)
{
  VarianceTracker *result;
  result = g_object_new (VARIANCETRACKER_TYPE, NULL);
  THIS_WRITELOCK (result);
  result->items = g_malloc0(sizeof(VarianceTrackerItem)*length);
  result->value_sum = 0;
  result->length = length;
  result->sysclock = gst_system_clock_obtain();
  result->treshold = obsolation_treshold;
  result->counter = 0;
  THIS_WRITEUNLOCK (result);

  return result;
}

void variancetracker_reset(VarianceTracker *this)
{
  THIS_WRITELOCK (this);
  memset(this->items, 0, sizeof(VarianceTrackerItem) * this->length);
  this->counter = this->write_index = this->read_index = 0;
  this->value_sum = 0;
  this->squere_sum = 0;
  THIS_WRITEUNLOCK (this);
}

void variancetracker_add(VarianceTracker *this, gint64 value)
{
  THIS_WRITELOCK (this);
  _add_value(this, value);
  THIS_WRITEUNLOCK (this);
}

guint32 variancetracker_get_num(VarianceTracker *this)
{
  guint32 result;
  THIS_READLOCK(this);
  result = this->counter;
  THIS_READUNLOCK(this);
  return result;
}

guint64 variancetracker_get_last(VarianceTracker *this)
{
  guint64 result;
  THIS_READLOCK(this);
  if(this->read_index == this->write_index) result = 0;
  else if(this->write_index == 0) result = this->items[this->length-1].value;
  else result = this->items[this->write_index-1].value;
  THIS_READUNLOCK(this);
  return result;
}


gdouble
variancetracker_get_stats (VarianceTracker * this,
                         gint64 *sum,
                         gint64 *squere_sum)
{
  gdouble variance = 0.;
  gdouble counter;
  gdouble sum_squere;
  THIS_READLOCK (this);
  if(this->counter < 2) goto done;
  sum_squere = (gdouble)this->value_sum * (gdouble)this->value_sum;
  counter = this->counter;
  //V = (N * SX2 - (SX1 * SX1)) / (N * (N - 1))
  variance = (counter * (gdouble)this->squere_sum - sum_squere) / (counter * (counter - 1.));
  if(sum) *sum = this->value_sum;
  if(squere_sum) *squere_sum = this->squere_sum;
  THIS_READUNLOCK (this);
//  g_print("var: %d,%ld,%f,%f,%f\n",
//          this->counter,
//          this->squere_sum,
//          sum_squere,
//          variance, sqrt(variance));
done:
  return variance;
}



void
variancetracker_obsolate (VarianceTracker * this)
{
  THIS_READLOCK (this);
  _obsolate(this);
  THIS_READUNLOCK (this);
}




void _add_value(VarianceTracker *this, gint64 value)
{
  GstClockTime now;
  gint64 squere;
  squere = value * value;
  now = gst_clock_get_time(this->sysclock);
  //add new one
  ++this->counter;
  this->items[this->write_index].value = value;
  this->items[this->write_index].squere = squere;
  this->items[this->write_index].added = now;
  this->squere_sum += squere;
  this->value_sum += value;
  if(++this->write_index == this->length){
      this->write_index=0;
  }

  _obsolate(this);
}

void
_obsolate (VarianceTracker * this)
{
  GstClockTime treshold,now;
  guint64 value,squere;
  now = gst_clock_get_time(this->sysclock);
  treshold = now - this->treshold;
again:
  if(this->write_index == this->read_index) goto elliminate;
  else if(this->items[this->read_index].added < treshold) goto elliminate;
  else goto done;
elliminate:
  value = this->items[this->read_index].value;
  squere = this->items[this->read_index].squere;
  this->value_sum -= value;
  this->squere_sum -= squere;
  this->items[this->read_index].value = 0;
  this->items[this->read_index].squere = 0;
  this->items[this->read_index].added = 0;
  if(++this->read_index == this->length){
      this->read_index=0;
  }
  --this->counter;
  goto again;
done:
  return;
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
