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
#include "streamtracker.h"
#include <math.h>
#include <string.h>

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)


GST_DEBUG_CATEGORY_STATIC (streamtracker_debug_category);
#define GST_CAT_DEFAULT streamtracker_debug_category

G_DEFINE_TYPE (StreamTracker, streamtracker, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void streamtracker_finalize (GObject * object);
static void
_add_value(StreamTracker *this, guint64 value);
static void
_balancing_trees (StreamTracker * this);
static gint
_cmp_for_max (guint64 x, guint64 y);
static gint
_cmp_for_min (guint64 x, guint64 y);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
streamtracker_class_init (StreamTrackerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = streamtracker_finalize;

  GST_DEBUG_CATEGORY_INIT (streamtracker_debug_category, "streamtracker", 0,
      "StreamTracker");

}

void
streamtracker_finalize (GObject * object)
{
  StreamTracker *this;
  this = STREAMTRACKER(object);
  g_object_unref(this->maxtree);
  g_object_unref(this->mintree);
  g_object_unref(this->sysclock);
  g_free(this->items);
}

void
streamtracker_init (StreamTracker * this)
{
  g_rw_lock_init (&this->rwmutex);
}

StreamTracker *make_streamtracker(BinTreeCmpFunc cmp_min,
                                  BinTreeCmpFunc cmp_max,
                                  guint32 length,
                                  guint percentile)
{
  StreamTracker *result;
  result = g_object_new (STREAMTRACKER_TYPE, NULL);
  THIS_WRITELOCK (result);
  result->maxtree = make_bintree(cmp_max);
  result->mintree = make_bintree(cmp_min);
  result->max_multiplier = (gdouble)(100 - percentile) / (gdouble)percentile;
  result->items = g_malloc0(sizeof(StreamTrackerItem)*length);
  result->sum = 0;
  result->length = length;
  result->sysclock = gst_system_clock_obtain();
  result->treshold = GST_SECOND;
  THIS_WRITEUNLOCK (result);

  return result;
}

void streamtracker_test(void)
{
  StreamTracker *tracker;
  tracker = make_streamtracker(_cmp_for_min, _cmp_for_max, 10, 40);
  streamtracker_add(tracker, 7);
  streamtracker_add(tracker, 1);
  streamtracker_add(tracker, 3);
  streamtracker_add(tracker, 8);
  streamtracker_add(tracker, 2);
  streamtracker_add(tracker, 6);
  streamtracker_add(tracker, 4);
  streamtracker_add(tracker, 5);
  streamtracker_add(tracker, 9);
  streamtracker_add(tracker, 10);

  {
    guint64 min,max,perc;
    perc = streamtracker_get_stats(tracker, &min, &max, NULL);
    g_print("StreamTracker test for 40th percentile\n"
            "Min: %lu, 40th percentile: %lu Max: %lu\n", min, perc, max);
  }

}

void streamtracker_reset(StreamTracker *this)
{
  THIS_WRITELOCK (this);
  bintree_reset(this->maxtree);
  bintree_reset(this->mintree);
  memset(this->items, 0, sizeof(StreamTrackerItem) * this->length);
  this->write_index = this->read_index = 0;
  this->sum = 0;
  THIS_WRITEUNLOCK (this);
}

void streamtracker_add(StreamTracker *this, guint64 value)
{
  THIS_WRITELOCK (this);
  _add_value(this, value);
  _balancing_trees(this);
  THIS_WRITEUNLOCK (this);
}

void streamtracker_set_treshold(StreamTracker *this, GstClockTime treshold)
{
  THIS_WRITELOCK (this);
  this->treshold = treshold;
  THIS_WRITEUNLOCK (this);
}

guint32 streamtracker_get_num(StreamTracker *this)
{
  guint32 result;
  THIS_READLOCK(this);
  result = bintree_get_num(this->maxtree) + bintree_get_num(this->mintree);
  THIS_READUNLOCK(this);
  return result;
}


guint64
streamtracker_get_stats (StreamTracker * this,
                         guint64 *min,
                         guint64 *max,
                         guint64 *sum)
{
  guint64 result = 0;
  gint32 max_count, min_count, diff;
//  g_print("mprtpr_path_get_delay_median begin\n");
  THIS_READLOCK (this);
  if(sum) *sum = this->sum;
  if(min) *min = 0;
  if(max) *max = 0;
  min_count = bintree_get_num(this->mintree);
  max_count = (gdouble)bintree_get_num(this->maxtree) * this->max_multiplier + .5;
  g_print("%f: %u-%u\n", this->max_multiplier, min_count, max_count);
  diff = max_count - min_count;

  if(min_count + max_count < 1)
    goto done;
  if(0 < diff)
    result = bintree_get_top_value(this->maxtree);
  else if(diff < 0)
    result = bintree_get_top_value(this->mintree);
  else{
    result = (bintree_get_top_value(this->maxtree) +
              bintree_get_top_value(this->mintree))>>1;
  }
  if(min) *min = bintree_get_bottom_value(this->maxtree);
  if(max) *max = bintree_get_bottom_value(this->mintree);
//  g_print("%d-%d\n", min_count, max_count);
done:
THIS_READUNLOCK (this);
//  g_print("mprtpr_path_get_delay_median end\n");
  return result;
}

void _add_value(StreamTracker *this, guint64 value)
{
  GstClockTime treshold,now;
  now = gst_clock_get_time(this->sysclock);
  treshold = now - this->treshold;
  again:
  //elliminate the old ones
  if((this->items[this->read_index].value > 0 &&
      this->items[this->read_index].added < treshold) ||
      this->write_index == this->read_index)
  {
    if(this->items[this->read_index].value <= bintree_get_top_value(this->maxtree))
      bintree_delete_value(this->maxtree, this->items[this->read_index].value);
    else
      bintree_delete_value(this->mintree, this->items[this->read_index].value);
    this->sum -= this->items[this->read_index].value;
    this->items[this->read_index].value = 0;
    this->items[this->read_index].added = 0;
    if(++this->read_index == this->length){
        this->read_index=0;
    }
    goto again;
  }


  //add new one
  this->sum += value;
  this->items[this->write_index].value = value;
  this->items[this->write_index].added = now;
  if(this->items[this->write_index].value <= bintree_get_top_value(this->maxtree))
    bintree_insert_value(this->maxtree, this->items[this->write_index].value);
  else
    bintree_insert_value(this->mintree, this->items[this->write_index].value);

  if(++this->write_index == this->length){
      this->write_index=0;
  }
}

void
_balancing_trees (StreamTracker * this)
{
  gint32 max_count, min_count;
  gint32 diff;
  BinTreeNode *top;


balancing:
  min_count = bintree_get_num(this->mintree);
  max_count = (gdouble)bintree_get_num(this->maxtree) * this->max_multiplier + .5;
  diff = max_count - min_count;
//  g_print("max_tree_num: %d, min_tree_num: %d\n", max_tree_num, min_tree_num);
  if (-2 < diff && diff < 2) {
    goto done;
  }
  if (diff < -1) {
    top = bintree_pop_top_node(this->mintree);
    bintree_insert_node(this->maxtree, top);
  } else if (1 < diff) {
      top = bintree_pop_top_node(this->maxtree);
      bintree_insert_node(this->mintree, top);
  }
  goto balancing;

done:
  return;
}

gint
_cmp_for_max (guint64 x, guint64 y)
{
  return x == y ? 0 : x < y ? -1 : 1;
}

gint
_cmp_for_min (guint64 x, guint64 y)
{
  return x == y ? 0 : x < y ? 1 : -1;
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
