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
#include "percentiletracker.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>     /* qsort */

//#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
//#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
//#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
//#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define THIS_READLOCK(this)
#define THIS_READUNLOCK(this)
#define THIS_WRITELOCK(this)
#define THIS_WRITEUNLOCK(this)


GST_DEBUG_CATEGORY_STATIC (percentiletracker_debug_category);
#define GST_CAT_DEFAULT percentiletracker_debug_category

G_DEFINE_TYPE (PercentileTracker, percentiletracker, G_TYPE_OBJECT);

//typedef struct _CollectingState{
//  BinTree *collector;
//  BinTree *dispensor;
//  guint    requested;
//}CollectingState;
//
//typedef struct _BalancingState{
//  void (*balancer)(PercentileTracker*);
//}BalancingState;
//
//struct _PercentileState{
//  void    (*processor)(PercentileTracker*, guint64);
//  guint64 (*producer)(PercentileTracker*);
//  CollectingState   collecting;
//  BalancingState    balancing;
//};

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void percentiletracker_finalize (GObject * object);
static void
_add_value(PercentileTracker *this, guint64 value);
static void _pipe_stats(PercentileTracker * this);
static guint64
_get_stats (PercentileTracker * this,
                         guint64 *min,
                         guint64 *max,
                         guint64 *sum);

static void
_obsolate (PercentileTracker * this);
static void
_rem_value(PercentileTracker * this);
static gint
_cmp_for_max (guint64 x, guint64 y);
static gint
_cmp_for_min (guint64 x, guint64 y);

static void _median_balancer(PercentileTracker *this);
static void _percentile_balancer(PercentileTracker *this);
static guint64 _get_median(PercentileTracker * this);
static guint64 _get_percentile(PercentileTracker * this);
//#define _counter(this) (bintree_get_num(this->maxtree) + bintree_get_num(this->mintree))
#define _counter(this) (this->Mxc + this->Mnc)
#define _now(this) gst_clock_get_time(this->sysclock)
//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------


static gint _compare (const void* a, const void* b)
{
  return ( *(guint64*)a - *(guint64*)b );
}

void
percentiletracker_class_init (PercentileTrackerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = percentiletracker_finalize;

  GST_DEBUG_CATEGORY_INIT (percentiletracker_debug_category, "percentiletracker", 0,
      "PercentileTracker");

}

void
percentiletracker_finalize (GObject * object)
{
  PercentileTracker *this;
  this = PERCENTILETRACKER(object);
  g_object_unref(this->maxtree);
  g_object_unref(this->mintree);
  g_object_unref(this->sysclock);
  mprtp_free(this->items);
}

void
percentiletracker_init (PercentileTracker * this)
{

  g_rw_lock_init (&this->rwmutex);
  this->sum = 0;
  this->sysclock = gst_system_clock_obtain();
  this->treshold = GST_SECOND;
}

PercentileTracker *make_percentiletracker(
                                  guint32 length,
                                  guint percentile)
{
    return make_percentiletracker_full(_cmp_for_min, _cmp_for_max, length, percentile);
}

PercentileTracker *make_percentiletracker_debug(
                                  guint32 length,
                                  guint percentile)
{
  PercentileTracker *this = make_percentiletracker_full(_cmp_for_min, _cmp_for_max, length, percentile);
  this->debug = TRUE;
  return this;
}


static void _reserve_items(PercentileTracker *this, guint32 length)
{
  PercentileTrackerItem *items;
  if(this->items && length <= this->length){
    return;
  }

  items =  mprtp_malloc(sizeof(PercentileTrackerItem)*length);
  if(this->items){
    memcpy(items, this->items, sizeof(PercentileTrackerItem)*this->length);
    mprtp_free(this->items);
    this->items = NULL;
  }
  this->items = items;
  this->length = length;
}

PercentileTracker *make_percentiletracker_full(BinTreeCmpFunc cmp_min,
                                  BinTreeCmpFunc cmp_max,
                                  guint32 length,
                                  guint percentile)
{
  PercentileTracker *this;
  this = g_object_new (PERCENTILETRACKER_TYPE, NULL);
  THIS_WRITELOCK (this);
  this->ratio = (gdouble)percentile / (gdouble)(100 - percentile);
  this->percentile = percentile;
  this->expandfnc = NULL;
  _reserve_items(this, length);
  this->maxtree = make_bintree(this->maxtree_cmp = cmp_max);
  this->mintree = make_bintree(this->mintree_cmp = cmp_min);

  if(this->ratio < 1.){
    this->required = (1./this->ratio) + 1;
    this->median = FALSE;
  }else if(1. < this->ratio){
    this->required = this->ratio + 1;
    this->median = FALSE;
  }else{
    this->required = 2;
    this->median = TRUE;
  }
  this->ready = FALSE;
  this->collection = (guint64*) mprtp_malloc(sizeof(guint64) * this->required);
  THIS_WRITEUNLOCK (this);

  return this;
}

static void _print_items(PercentileTracker *this)
{
  gint i,c;
  guint64 *items, perc, min, max, sum;
  items = mprtp_malloc(sizeof(guint64) * this->length);
  g_print("Ready: %d Items (%d = Mx: %d + Mn: %d (%d)): ", this->ready, _counter(this),  this->Mxc,  this->Mnc, this->counter);
  for(c = 0,i=this->read_index; c < _counter(this); ++c,i = (i + 1) % this->length){
    items[c] = this->items[i].value;
  }
  qsort (items, c, sizeof(guint64), _compare);
  for(i=0; i<c; ++i) g_print("%-5lu ", items[i]);
  perc = percentiletracker_get_stats(this, &min, &max, &sum);
  g_print("Min: %lu, %dth percentile: %lu Max: %lu, Sum: %lu\n", min, this->percentile, perc, max, sum);
  mprtp_free(items);
}

#define PROFILING(func) \
{  \
  GstClockTime start, elapsed; \
  start = _now(this); \
  func; \
  elapsed = GST_TIME_AS_MSECONDS(_now(this) - start); \
  if(0 < elapsed) g_print("elapsed time in ms: %lu\n", elapsed); \
} \


void percentiletracker_test(void)
{
  gint i;
  PercentileTracker *tracker;
  tracker = make_percentiletracker(10, 50);
  percentiletracker_add(tracker, 7);
  percentiletracker_add(tracker, 1);
  percentiletracker_add(tracker, 3);
  percentiletracker_add(tracker, 8);
  percentiletracker_add(tracker, 2);
  percentiletracker_add(tracker, 6);
  percentiletracker_add(tracker, 4);
  percentiletracker_add(tracker, 5);
  percentiletracker_add(tracker, 9);
  percentiletracker_add(tracker, 10);

  _print_items(tracker);
  _rem_value(tracker);_rem_value(tracker);_rem_value(tracker);_rem_value(tracker);_rem_value(tracker);
  _rem_value(tracker);_rem_value(tracker);_rem_value(tracker);_rem_value(tracker);_rem_value(tracker);
  _print_items(tracker);

  for(i=0; i<10; ++i){
    percentiletracker_add(tracker, (g_random_int() % 100) + 50);
    _print_items(tracker);
  }

  g_object_unref(tracker);
//
  tracker = make_percentiletracker(10, 20);
  percentiletracker_add(tracker, 7);
  percentiletracker_add(tracker, 1);
  percentiletracker_add(tracker, 3);
  percentiletracker_add(tracker, 8);
  percentiletracker_add(tracker, 2);
  percentiletracker_add(tracker, 6);
  percentiletracker_add(tracker, 4);
  percentiletracker_add(tracker, 5);
  percentiletracker_add(tracker, 9);
  percentiletracker_add(tracker, 10);

  _print_items(tracker);
  _rem_value(tracker);_rem_value(tracker);_rem_value(tracker);_rem_value(tracker);_rem_value(tracker);
  _rem_value(tracker);_rem_value(tracker);_rem_value(tracker);_rem_value(tracker);_rem_value(tracker);
  _print_items(tracker);

  for(i=0; i<(1<<30); ++i){
    percentiletracker_add(tracker, (g_random_int() % 100) + 50);
    //_print_items(tracker);
  }

  g_object_unref(tracker);

  tracker = make_percentiletracker(10, 80);
  percentiletracker_add(tracker, 7);
  percentiletracker_add(tracker, 1);
  percentiletracker_add(tracker, 3);
  percentiletracker_add(tracker, 8);
  percentiletracker_add(tracker, 2);
  percentiletracker_add(tracker, 6);
  percentiletracker_add(tracker, 4);
  percentiletracker_add(tracker, 5);
  percentiletracker_add(tracker, 9);
  percentiletracker_add(tracker, 10);

  _print_items(tracker);
  _rem_value(tracker);_rem_value(tracker);_rem_value(tracker);_rem_value(tracker);_rem_value(tracker);
  _rem_value(tracker);_rem_value(tracker);_rem_value(tracker);_rem_value(tracker);_rem_value(tracker);
  _print_items(tracker);

  for(i=0; i<10; ++i){
    percentiletracker_add(tracker, (g_random_int() % 100) + 50);
    _print_items(tracker);
  }

  g_object_unref(tracker);

}

void percentiletracker_reset(PercentileTracker *this)
{
  THIS_WRITELOCK (this);
  g_object_unref(this->maxtree);
  g_object_unref(this->mintree);
  this->maxtree = make_bintree(this->maxtree_cmp);
  this->mintree = make_bintree(this->mintree_cmp);
  this->ready = FALSE;
  memset(this->items, 0, sizeof(PercentileTrackerItem) * this->length);
  this->write_index = this->read_index = 0;
  this->Mxc = this->Mnc = 0;
  this->sum = 0;
  THIS_WRITEUNLOCK (this);
}

void percentiletracker_add(PercentileTracker *this, guint64 value)
{
  THIS_WRITELOCK (this);
  //add new one
  _add_value(this, value);
  _obsolate(this);

  if(this->debug) _print_items(this);

  if(this->median)
    _median_balancer(this);
  else
    _percentile_balancer(this);

  _pipe_stats(this);
  THIS_WRITEUNLOCK (this);
}

void percentiletracker_set_treshold(PercentileTracker *this, GstClockTime treshold)
{
  THIS_WRITELOCK (this);
  this->treshold = treshold;
  if(this->expandfnc){
      _reserve_items(this, this->expandfnc(treshold));
  }
  THIS_WRITEUNLOCK (this);
}

void percentiletracker_set_expandfnc(PercentileTracker *this, guint32 (*expandfnc)(GstClockTime))
{
  THIS_WRITELOCK (this);
  this->expandfnc = expandfnc;
  THIS_WRITEUNLOCK (this);
}


void percentiletracker_set_stats_pipe(PercentileTracker *this, void(*stats_pipe)(gpointer, PercentileTrackerPipeData*),gpointer stats_pipe_data)
{
  THIS_WRITELOCK (this);
  this->stats_pipe = stats_pipe;
  this->stats_pipe_data = stats_pipe_data;
  THIS_WRITEUNLOCK (this);
}

guint32 percentiletracker_get_num(PercentileTracker *this)
{
  guint32 result;
  THIS_READLOCK(this);
  if(this->read_index <= this->write_index)
    result = this->write_index - this->read_index;
  else
    result = this->length - this->read_index + this->write_index;
//  result = bintree_get_num(this->maxtree) + bintree_get_num(this->mintree);
  THIS_READUNLOCK(this);
  return result;
}

guint64 percentiletracker_get_last(PercentileTracker *this)
{
  guint64 result;
  THIS_READLOCK(this);
  if(this->read_index == this->write_index) result = 0;
  else if(this->write_index == 0) result = this->items[this->length-1].value;
  else result = this->items[this->write_index-1].value;
  THIS_READUNLOCK(this);
  return result;
}


guint64
percentiletracker_get_stats (PercentileTracker * this,
                             guint64 *min,
                             guint64 *max,
                             guint64 *sum)
{
  guint64 result = 0;
  THIS_READLOCK (this);
  result = _get_stats(this, min, max, sum);
  THIS_READUNLOCK (this);
  return result;
}


void
percentiletracker_obsolate (PercentileTracker * this)
{
  THIS_READLOCK (this);
  _obsolate(this);
  _pipe_stats(this);
  THIS_READUNLOCK (this);
}

void _add_value(PercentileTracker *this, guint64 value)
{
  this->sum += value;

  if(0 < this->items[this->write_index].added)
    _rem_value(this);

  this->items[this->write_index].value = value;
  this->items[this->write_index].added = _now(this);
  if(++this->write_index == this->length){
    this->write_index=0;
  }

  if(this->ready){
    goto add2tree;
  }

  this->collection[this->counter] = value;
  this->counter = MIN(this->counter + 1, this->length);
  if(this->counter < this->required){
    goto done;
  }

  this->ready = TRUE;
  //sort the array
  qsort (this->collection, this->required, sizeof(guint64), _compare);
  //distribute
  if(this->ratio == 1.){
    bintree_insert_data(this->maxtree, this->collection[0]);
    bintree_insert_data(this->mintree, this->collection[1]);
    this->Mxc = this->Mnc = 1;
  }else if(this->ratio < 1.){
    gint i,c = this->required - 1;
    for(i = 0; i < c; ++i){
      bintree_insert_data(this->maxtree, this->collection[i]);
      ++this->Mxc;
    }
    bintree_insert_data(this->mintree, this->collection[c]);
    ++this->Mnc;
  }else if(1. < this->ratio){
    gint i;
    for(i = this->required - 1; 0 < i; --i) {
      bintree_insert_data(this->mintree, this->collection[i]);
      ++this->Mnc;
    }
    bintree_insert_data(this->maxtree, this->collection[0]);
    ++this->Mxc;
  }
  goto done;

add2tree:
  if(value <= bintree_get_top_data(this->maxtree)){
    bintree_insert_data(this->maxtree, value);
    ++this->Mxc;
  }else{
    bintree_insert_data(this->mintree, value);
    ++this->Mnc;
  }
done:
  return;
}

void _pipe_stats(PercentileTracker * this)
{
  PercentileTrackerPipeData pdata;
  if(!this->stats_pipe) return;
  pdata.percentile = _get_stats(this, &pdata.min, &pdata.max, &pdata.sum);
  pdata.num = this->Mxc + this->Mnc;
  this->stats_pipe(this->stats_pipe_data, &pdata);
}

guint64
_get_stats (PercentileTracker * this,
            guint64 *min,
            guint64 *max,
            guint64 *sum)
{
  guint64 result = 0;
  result = (this->median) ? _get_median(this) : _get_percentile(this);
  if(sum) *sum = this->sum;
  if(!min && !max) goto done;
  if(min) *min = 0;
  if(max) *max = 0;

  if(!this->Mnc && !this->Mxc) goto done;
  if(!this->Mxc){
    if(min) *min = bintree_get_top_data(this->mintree);
    if(max) *max = bintree_get_bottom_data(this->mintree);
  }else if(!this->Mnc){
    if(min) *min = bintree_get_bottom_data(this->maxtree);
    if(max) *max = bintree_get_top_data(this->maxtree);
  }else{
    if(min) *min = bintree_get_bottom_data(this->maxtree);
    if(max) *max = bintree_get_bottom_data(this->mintree);
  }
  done:
  return result;
}

guint64 _get_median(PercentileTracker * this)
{
  guint64 result;

  if(this->Mnc == this->Mxc){
//    g_print("mnc eq to mxc tops: %lu,%lu\n", bintree_get_top_value(this->maxtree), bintree_get_top_value(this->mintree));
    result = (bintree_get_top_data(this->maxtree) + bintree_get_top_data(this->mintree))>>1;
  } else if(this->Mnc < this->Mxc){
//      g_print("mnc st to mxc top is: %lu\n", bintree_get_top_value(this->maxtree));
    result = bintree_get_top_data(this->maxtree);
  }else{
//    g_print("mnc gt to mxc top is: %lu\n", bintree_get_top_value(this->mintree));
    result = bintree_get_top_data(this->mintree);
  }

  return result;
}

guint64 _get_percentile(PercentileTracker * this)
{
  gdouble ratio;
  if(!this->Mnc)
    return bintree_get_top_data(this->maxtree);
  if(!this->Mxc)
    return bintree_get_top_data(this->mintree);

  ratio = (gdouble) this->Mxc / (gdouble) this->Mnc;
  if(ratio == this->ratio)
    return (bintree_get_top_data(this->maxtree) + bintree_get_top_data(this->mintree))>>1;
  if(this->ratio < ratio)
    return bintree_get_top_data(this->maxtree);
  else
    return bintree_get_top_data(this->mintree);
}

void _median_balancer(PercentileTracker *this)
{
  gint32 diff;
  guint64 value;
  if(!this->ready) goto done;
again:
  diff = this->Mxc - this->Mnc;
  if(-2 < diff && diff < 2) goto done;
  if (diff < -1) {
    value = bintree_get_top_data(this->mintree);
    bintree_delete_value(this->mintree, value);
    bintree_insert_data(this->maxtree, value);
    --this->Mnc; ++this->Mxc;
  } else if (1 < diff) {
    value = bintree_get_top_data(this->maxtree);
    bintree_delete_value(this->maxtree, value);
    bintree_insert_data(this->mintree, value);
    --this->Mxc; ++this->Mnc;
  }
  goto again;
done:
  return;
}


void _percentile_balancer(PercentileTracker *this)
{
  gdouble ratio;
  guint64 value;
  if(!this->ready)
    goto done;
  if(!this->Mnc)
    ratio = .001;
  else
    ratio = (gdouble) this->Mxc / (gdouble) this->Mnc;

  if(this->Mxc == 0 || this->Mnc == 0){
    goto done;
  }

  if(ratio < this->ratio)
    goto balancing_mintree;
  else
    goto balancing_maxtree;

balancing_mintree:
  ratio = (gdouble) (this->Mxc + 1) / (gdouble) (this->Mnc - 1);
  if(this->ratio < ratio || this->Mnc < 2) goto done;
  value = bintree_get_top_data(this->mintree);
  bintree_delete_value(this->mintree, value);
  bintree_insert_data(this->maxtree, value);
  --this->Mnc; ++this->Mxc;
  goto balancing_mintree;

balancing_maxtree:
  ratio = (gdouble) (this->Mxc - 1) / (gdouble) (this->Mnc + 1);
  if(ratio < this->ratio || this->Mxc < 2) goto done;
  value = bintree_get_top_data(this->maxtree);
  bintree_delete_value(this->maxtree, value);
  bintree_insert_data(this->mintree, value);
  --this->Mxc; ++this->Mnc;
  goto balancing_maxtree;

done:
  return;
}

void
_obsolate (PercentileTracker * this)
{
  GstClockTime treshold,now;
  now = gst_clock_get_time(this->sysclock);
  treshold = now - this->treshold;
again:
  if(this->counter < 1) goto done;
  if(this->items[this->read_index].added < treshold) goto elliminate;
  else goto done;
elliminate:
  _rem_value(this);
  goto again;
done:
  return;
}

void _rem_value(PercentileTracker * this)
{
  guint64 value;
  this->sum -= value = this->items[this->read_index].value;
  this->items[this->read_index].value = 0;
  this->items[this->read_index].added = 0;
  if(++this->read_index == this->length){
    this->read_index=0;
  }
  if(!this->ready){
    goto collect;
  }
  if(value <= bintree_get_top_data(this->maxtree)){
    bintree_delete_value(this->maxtree, value);
    --this->Mxc;
  }else{
    bintree_delete_value(this->mintree, value);
    --this->Mnc;
  }

  if(this->required <= _counter(this)){
    goto done;
  }
collect:
  {
    gint i,j,c = _counter(this);
    this->ready = FALSE;
    this->counter = c;
    this->Mxc = this->Mnc = 0;
    g_object_unref(this->maxtree);
    g_object_unref(this->mintree);
    this->maxtree = make_bintree(this->maxtree_cmp);
    this->mintree = make_bintree(this->mintree_cmp);
    if(c < 1) goto done;
    for(j = 0, i = this->read_index; j < c; i = (i + 1) % this->length, ++j){
      this->collection[j] = this->items[i].value;
    }
  }
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
