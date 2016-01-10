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
#include "floatnumstracker.h"
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


GST_DEBUG_CATEGORY_STATIC (floatnumstracker_debug_category);
#define GST_CAT_DEFAULT cofloatnumstracker_debug_category

G_DEFINE_TYPE (FloatNumsTracker, floatnumstracker, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void floatnumstracker_finalize (GObject * object);
static void
_add_value(FloatNumsTracker *this, gdouble value);
static void
_obsolate (FloatNumsTracker * this);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
floatnumstracker_class_init (FloatNumsTrackerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = floatnumstracker_finalize;

  GST_DEBUG_CATEGORY_INIT (floatnumstracker_debug_category, "floatnumstracker", 0,
      "FloatNumsTracker");

}

void
floatnumstracker_finalize (GObject * object)
{
  FloatNumsTracker *this;
  this = FLOATNUMSTRACKER(object);
  g_object_unref(this->sysclock);
  g_free(this->items);
}

void
floatnumstracker_init (FloatNumsTracker * this)
{
  g_rw_lock_init (&this->rwmutex);
}

FloatNumsTracker *make_floatnumstracker(
    guint32 length,
    GstClockTime obsolation_treshold)
{
  FloatNumsTracker *result;
  result = g_object_new (FLOATNUMSTRACKER_TYPE, NULL);
  THIS_WRITELOCK (result);
  result->items = g_malloc0(sizeof(FloatNumsTrackerItem)*length);
  result->value_sum = 0;
  result->length = length;
  result->sysclock = gst_system_clock_obtain();
  result->treshold = obsolation_treshold;
  result->counter = 0;
  THIS_WRITEUNLOCK (result);

  return result;
}

void floatnumstracker_reset(FloatNumsTracker *this)
{
  THIS_WRITELOCK (this);
  memset(this->items, 0, sizeof(FloatNumsTrackerItem) * this->length);
  this->counter = this->write_index = this->read_index = 0;
  this->value_sum = 0;
  THIS_WRITEUNLOCK (this);
}

void floatnumstracker_add(FloatNumsTracker *this, gdouble value)
{
  THIS_WRITELOCK (this);
  _add_value(this, value);
  THIS_WRITEUNLOCK (this);
}

guint32 floatnumstracker_get_num(FloatNumsTracker *this)
{
  guint32 result;
  THIS_READLOCK(this);
  result = this->counter;
  THIS_READUNLOCK(this);
  return result;
}

guint64 floatnumstracker_get_last(FloatNumsTracker *this)
{
  guint64 result;
  THIS_READLOCK(this);
  if(this->read_index == this->write_index) result = 0;
  else if(this->write_index == 0) result = this->items[this->length-1].value;
  else result = this->items[this->write_index-1].value;
  THIS_READUNLOCK(this);
  return result;
}


void
floatnumstracker_get_stats (FloatNumsTracker * this,
                            gdouble *sum,
                            gdouble *avg)
{
  THIS_READLOCK (this);
  if(this->counter < 1) goto done;
  if(sum) *sum = this->value_sum;
  if(avg) *avg = this->counter < 1 ? 0. : this->value_sum / (gdouble)this->counter;
  THIS_READUNLOCK (this);
done:
  return;
}

void
floatnumstracker_iterate (FloatNumsTracker * this,
                            void(*process)(gpointer,gdouble),
                            gpointer data)
{
  gint32 c,i;
  THIS_READLOCK (this);
  for(c = 0, i = this->read_index; c < this->counter; ++c){
    process(data, this->items[i]);
    if(++i == this->length) i = 0;
  }
  THIS_READUNLOCK (this);
done:
  return;
}



void
floatnumstracker_obsolate (FloatNumsTracker * this)
{
  THIS_READLOCK (this);
  _obsolate(this);
  THIS_READUNLOCK (this);
}




void _add_value(FloatNumsTracker *this, gdouble value)
{
  GstClockTime now;
  now = gst_clock_get_time(this->sysclock);
  //add new one
  ++this->counter;
  this->items[this->write_index].value = value;
  this->items[this->write_index].added = now;
  this->value_sum += value;
  if(++this->write_index == this->length){
      this->write_index=0;
  }
  _obsolate(this);
}

void
_obsolate (FloatNumsTracker * this)
{
  GstClockTime treshold,now;
  gdouble value;
  now = gst_clock_get_time(this->sysclock);
  treshold = now - this->treshold;
again:
  if(this->counter < 1) goto done;
  if(this->write_index == this->read_index) goto elliminate;
  else if(this->items[this->read_index].added < treshold) goto elliminate;
  else goto done;
elliminate:
  value = this->items[this->read_index].value;
  this->value_sum -= value;
  this->items[this->read_index].value = 0;
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
