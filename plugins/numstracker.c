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
#include "numstracker.h"
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


GST_DEBUG_CATEGORY_STATIC (numstracker_debug_category);
#define GST_CAT_DEFAULT conumstracker_debug_category

G_DEFINE_TYPE (NumsTracker, numstracker, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void numstracker_finalize (GObject * object);
static void
_add_value(NumsTracker *this, gint64 value);
static void
_obsolate (NumsTracker * this);

static gint
_cmp_for_max (guint64 x, guint64 y);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
numstracker_class_init (NumsTrackerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = numstracker_finalize;

  GST_DEBUG_CATEGORY_INIT (numstracker_debug_category, "numstracker", 0,
      "NumsTracker");

}

void
numstracker_finalize (GObject * object)
{
  NumsTracker *this;
  this = NUMSTRACKER(object);
  g_object_unref(this->sysclock);
  g_free(this->items);
}

void
numstracker_init (NumsTracker * this)
{
  g_rw_lock_init (&this->rwmutex);
}

NumsTracker *make_numstracker(
    guint32 length,
    GstClockTime obsolation_treshold)
{
  NumsTracker *result;
  result = g_object_new (NUMSTRACKER_TYPE, NULL);
  THIS_WRITELOCK (result);
  result->items = g_malloc0(sizeof(NumsTrackerItem)*length);
  result->value_sum = 0;
  result->length = length;
  result->sysclock = gst_system_clock_obtain();
  result->treshold = obsolation_treshold;
  result->counter = 0;
  THIS_WRITEUNLOCK (result);

  return result;
}

NumsTracker *make_numstracker_with_tree(
    guint32 length,
    GstClockTime obsolation_treshold)
{
  NumsTracker *result;
  result = g_object_new (NUMSTRACKER_TYPE, NULL);
  THIS_WRITELOCK (result);
  result->items = g_malloc0(sizeof(NumsTrackerItem)*length);
  result->value_sum = 0;
  result->length = length;
  result->sysclock = gst_system_clock_obtain();
  result->treshold = obsolation_treshold;
  result->counter = 0;
  result->tree = make_bintree(_cmp_for_max);
  THIS_WRITEUNLOCK (result);

  return result;
}

void numstracker_reset(NumsTracker *this)
{
  THIS_WRITELOCK (this);
  memset(this->items, 0, sizeof(NumsTrackerItem) * this->length);
  this->counter = this->write_index = this->read_index = 0;
  this->value_sum = 0;
  bintree_reset(this->tree);
  THIS_WRITEUNLOCK (this);
}

void numstracker_add(NumsTracker *this, gint64 value)
{
  THIS_WRITELOCK (this);
  _add_value(this, value);
  THIS_WRITEUNLOCK (this);
}

guint32 numstracker_get_num(NumsTracker *this)
{
  guint32 result;
  THIS_READLOCK(this);
  result = this->counter;
  THIS_READUNLOCK(this);
  return result;
}

guint64 numstracker_get_last(NumsTracker *this)
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
numstracker_get_stats (NumsTracker * this,
                         gint64 *sum,
                         guint64 *max,
                         guint64 *min)
{
  THIS_READLOCK (this);
  if(this->counter < 1) goto done;
  if(sum) *sum = this->value_sum;
  if(this->tree && max) *max = bintree_get_top_value(this->tree);
  if(this->tree && min) *min = bintree_get_top_value(this->tree);
  THIS_READUNLOCK (this);
done:
  return;
}



void
numstracker_obsolate (NumsTracker * this)
{
  THIS_READLOCK (this);
  _obsolate(this);
  THIS_READUNLOCK (this);
}




void _add_value(NumsTracker *this, gint64 value)
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
  this->value_sum += value;
  if(++this->write_index == this->length){
      this->write_index=0;
  }

  if(this->tree && value < 0)
    g_warning("numtracker with tree only allows positive values");
  else if(this->tree)
    bintree_insert_value(this->tree, value);

  _obsolate(this);
}

void
_obsolate (NumsTracker * this)
{
  GstClockTime treshold,now;
  guint64 value;
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
  this->items[this->read_index].squere = 0;
  this->items[this->read_index].added = 0;
  if(this->tree) bintree_delete_value(this->tree, value);
  if(++this->read_index == this->length){
      this->read_index=0;
  }
  --this->counter;
  goto again;
done:
  return;
}

gint
_cmp_for_max (guint64 x, guint64 y)
{
  return x == y ? 0 : x < y ? -1 : 1;
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
