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
#include "percentiletracker2.h"
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


GST_DEBUG_CATEGORY_STATIC (percentiletracker2_debug_category);
#define GST_CAT_DEFAULT percentiletracker2_debug_category

G_DEFINE_TYPE (PercentileTracker2, percentiletracker2, G_TYPE_OBJECT);

//typedef struct _CollectingState{
//  BinTree2 *collector;
//  BinTree2 *dispensor;
//  guint    requested;
//}CollectingState;
//
//typedef struct _BalancingState{
//  void (*balancer)(PercentileTracker2*);
//}BalancingState;
//
//struct _PercentileState{
//  void    (*processor)(PercentileTracker2*, gint64);
//  gint64 (*producer)(PercentileTracker2*);
//  CollectingState   collecting;
//  BalancingState    balancing;
//};

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void percentiletracker2_finalize (GObject * object);
static void
_add_value(PercentileTracker2 *this, gint64 value);
static void _pipe_stats(PercentileTracker2 * this);
static gint64
_get_stats (PercentileTracker2 * this,
                         gint64 *min,
                         gint64 *max,
                         gint64 *sum);

static void
_obsolate (PercentileTracker2 * this);
static void
_rem_value(PercentileTracker2 * this);
static gint
_cmp_for_max (gint64 x, gint64 y);
static gint
_cmp_for_min (gint64 x, gint64 y);

static void _median_balancer(PercentileTracker2 *this);
static void _percentile_balancer(PercentileTracker2 *this);
static gint64 _get_median(PercentileTracker2 * this);
static gint64 _get_percentile(PercentileTracker2 * this);
//#define _counter(this) (bintree2_get_num(this->maxtree) + bintree2_get_num(this->mintree))
#define _counter(this) (this->Mxc + this->Mnc)
#define _now(this) gst_clock_get_time(this->sysclock)
//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------


static gint _compare (const void* a, const void* b)
{
  return ( *(gint64*)a - *(gint64*)b );
}

void
percentiletracker2_class_init (PercentileTracker2Class * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = percentiletracker2_finalize;

  GST_DEBUG_CATEGORY_INIT (percentiletracker2_debug_category, "percentiletracker2", 0,
      "PercentileTracker2");

}

void
percentiletracker2_finalize (GObject * object)
{
  PercentileTracker2 *this;
  this = PERCENTILETRACKER2(object);
  g_object_unref(this->maxtree);
  g_object_unref(this->mintree);
  g_object_unref(this->sysclock);
  mprtp_free(this->items);
}

void
percentiletracker2_init (PercentileTracker2 * this)
{

  g_rw_lock_init (&this->rwmutex);
  this->sum = 0;
  this->sysclock = gst_system_clock_obtain();
  this->treshold = GST_SECOND;
}

PercentileTracker2 *make_percentiletracker2(
                                  guint32 length,
                                  guint percentile)
{
    return make_percentiletracker2_full(_cmp_for_min, _cmp_for_max, length, percentile);
}

PercentileTracker2 *make_percentiletracker2_debug(
                                  guint32 length,
                                  guint percentile)
{
  PercentileTracker2 *this = make_percentiletracker2_full(_cmp_for_min, _cmp_for_max, length, percentile);
  this->debug = TRUE;
  return this;
}

PercentileTracker2 *make_percentiletracker2_full(BinTree2CmpFunc cmp_min,
                                  BinTree2CmpFunc cmp_max,
                                  guint32 length,
                                  guint percentile)
{
  PercentileTracker2 *this;
  this = g_object_new (PERCENTILETRACKER2_TYPE, NULL);
  THIS_WRITELOCK (this);
  this->ratio = (gdouble)percentile / (gdouble)(100 - percentile);
  this->items = mprtp_malloc(sizeof(PercentileTracker2Item)*length);
  this->percentile = percentile;
  this->length = length;
  this->maxtree = make_bintree2(cmp_max);
  this->mintree = make_bintree2(cmp_min);

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
  this->collection = (gint64*) mprtp_malloc(sizeof(gint64) * this->required);
  THIS_WRITEUNLOCK (this);

  return this;
}

static void _print_items(PercentileTracker2 *this)
{
  gint i,c;
  gint64 *items, perc, min, max, sum;
  items = mprtp_malloc(sizeof(gint64) * this->length);
  g_print("Ready: %d Items (%d = Mx: %d + Mn: %d (%d)): ", this->ready, _counter(this),  this->Mxc,  this->Mnc, this->counter);
  for(c = 0,i=this->read_index; c < _counter(this); ++c,i = (i + 1) % this->length){
    items[c] = this->items[i].value;
  }
  qsort (items, c, sizeof(gint64), _compare);
  for(i=0; i<c; ++i) g_print("%-5lu ", items[i]);
  perc = percentiletracker2_get_stats(this, &min, &max, &sum);
  g_print("Min: %lu, %dth percentile: %lu Max: %lu, Sum: %lu\n", min, this->percentile, perc, max, sum);
  mprtp_free(items);
}

void percentiletracker2_test(void)
{
  gint i;
  PercentileTracker2 *tracker2;
  tracker2 = make_percentiletracker2(10, 50);
  percentiletracker2_add(tracker2, 7);
  percentiletracker2_add(tracker2, 1);
  percentiletracker2_add(tracker2, 3);
  percentiletracker2_add(tracker2, 8);
  percentiletracker2_add(tracker2, 2);
  percentiletracker2_add(tracker2, 6);
  percentiletracker2_add(tracker2, 4);
  percentiletracker2_add(tracker2, 5);
  percentiletracker2_add(tracker2, 9);
  percentiletracker2_add(tracker2, 10);

  _print_items(tracker2);
  _rem_value(tracker2);_rem_value(tracker2);_rem_value(tracker2);_rem_value(tracker2);_rem_value(tracker2);
  _rem_value(tracker2);_rem_value(tracker2);_rem_value(tracker2);_rem_value(tracker2);_rem_value(tracker2);
  _print_items(tracker2);

  for(i=0; i<10; ++i){
    percentiletracker2_add(tracker2, (g_random_int() % 100) + 50);
    _print_items(tracker2);
  }

  g_object_unref(tracker2);
//
  tracker2 = make_percentiletracker2(10, 20);
  percentiletracker2_add(tracker2, 7);
  percentiletracker2_add(tracker2, 1);
  percentiletracker2_add(tracker2, 3);
  percentiletracker2_add(tracker2, 8);
  percentiletracker2_add(tracker2, 2);
  percentiletracker2_add(tracker2, 6);
  percentiletracker2_add(tracker2, 4);
  percentiletracker2_add(tracker2, 5);
  percentiletracker2_add(tracker2, 9);
  percentiletracker2_add(tracker2, 10);

  _print_items(tracker2);
  _rem_value(tracker2);_rem_value(tracker2);_rem_value(tracker2);_rem_value(tracker2);_rem_value(tracker2);
  _rem_value(tracker2);_rem_value(tracker2);_rem_value(tracker2);_rem_value(tracker2);_rem_value(tracker2);
  _print_items(tracker2);

  for(i=0; i<10; ++i){
    percentiletracker2_add(tracker2, (g_random_int() % 100) + 50);
    _print_items(tracker2);
  }

  g_object_unref(tracker2);

  tracker2 = make_percentiletracker2(10, 80);
  percentiletracker2_add(tracker2, 7);
  percentiletracker2_add(tracker2, 1);
  percentiletracker2_add(tracker2, 3);
  percentiletracker2_add(tracker2, 8);
  percentiletracker2_add(tracker2, 2);
  percentiletracker2_add(tracker2, 6);
  percentiletracker2_add(tracker2, 4);
  percentiletracker2_add(tracker2, 5);
  percentiletracker2_add(tracker2, 9);
  percentiletracker2_add(tracker2, 10);

  _print_items(tracker2);
  _rem_value(tracker2);_rem_value(tracker2);_rem_value(tracker2);_rem_value(tracker2);_rem_value(tracker2);
  _rem_value(tracker2);_rem_value(tracker2);_rem_value(tracker2);_rem_value(tracker2);_rem_value(tracker2);
  _print_items(tracker2);

  for(i=0; i<10; ++i){
    percentiletracker2_add(tracker2, (g_random_int() % 100) + 50);
    _print_items(tracker2);
  }

  g_object_unref(tracker2);

}

void percentiletracker2_reset(PercentileTracker2 *this)
{
  THIS_WRITELOCK (this);
  bintree2_reset(this->maxtree);
  bintree2_reset(this->mintree);
  this->ready = FALSE;
  memset(this->items, 0, sizeof(PercentileTracker2Item) * this->length);
  this->write_index = this->read_index = 0;
  this->Mxc = this->Mnc = 0;
  this->sum = 0;
  THIS_WRITEUNLOCK (this);
}

void percentiletracker2_add(PercentileTracker2 *this, gint64 value)
{
  THIS_WRITELOCK (this);
  //add new one
  _add_value(this, value);
  _obsolate(this);

  if(this->median)
    _median_balancer(this);
  else
    _percentile_balancer(this);

  _pipe_stats(this);
  THIS_WRITEUNLOCK (this);
}

void percentiletracker2_set_treshold(PercentileTracker2 *this, GstClockTime treshold)
{
  THIS_WRITELOCK (this);
  this->treshold = treshold;
  THIS_WRITEUNLOCK (this);
}

void percentiletracker2_set_stats_pipe(PercentileTracker2 *this, void(*stats_pipe)(gpointer, PercentileTracker2PipeData*),gpointer stats_pipe_data)
{
  THIS_WRITELOCK (this);
  this->stats_pipe = stats_pipe;
  this->stats_pipe_data = stats_pipe_data;
  THIS_WRITEUNLOCK (this);
}

guint32 percentiletracker2_get_num(PercentileTracker2 *this)
{
  guint32 result;
  THIS_READLOCK(this);
  if(this->read_index <= this->write_index)
    result = this->write_index - this->read_index;
  else
    result = this->length - this->read_index + this->write_index;
//  result = bintree2_get_num(this->maxtree) + bintree2_get_num(this->mintree);
  THIS_READUNLOCK(this);
  return result;
}

gint64 percentiletracker2_get_last(PercentileTracker2 *this)
{
  gint64 result;
  THIS_READLOCK(this);
  if(this->read_index == this->write_index) result = 0;
  else if(this->write_index == 0) result = this->items[this->length-1].value;
  else result = this->items[this->write_index-1].value;
  THIS_READUNLOCK(this);
  return result;
}


gint64
percentiletracker2_get_stats (PercentileTracker2 * this,
                             gint64 *min,
                             gint64 *max,
                             gint64 *sum)
{
  gint64 result = 0;
  THIS_READLOCK (this);
  result = _get_stats(this, min, max, sum);
  THIS_READUNLOCK (this);
  return result;
}


void
percentiletracker2_obsolate (PercentileTracker2 * this)
{
  THIS_READLOCK (this);
  _obsolate(this);
  _pipe_stats(this);
  THIS_READUNLOCK (this);
}

void _add_value(PercentileTracker2 *this, gint64 value)
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
  qsort (this->collection, this->required, sizeof(gint64), _compare);
  //distribute
  if(this->ratio == 1.){
    bintree2_insert_value(this->maxtree, this->collection[0]);
    bintree2_insert_value(this->mintree, this->collection[1]);
    this->Mxc = this->Mnc = 1;
  }else if(this->ratio < 1.){
    gint i,c = this->required - 1;
    for(i = 0; i < c; ++i){
      bintree2_insert_value(this->maxtree, this->collection[i]);
      ++this->Mxc;
    }
    bintree2_insert_value(this->mintree, this->collection[c]);
    ++this->Mnc;
  }else if(1. < this->ratio){
    gint i;
    for(i = this->required - 1; 0 < i; --i) {
      bintree2_insert_value(this->mintree, this->collection[i]);
      ++this->Mnc;
    }
    bintree2_insert_value(this->maxtree, this->collection[0]);
    ++this->Mxc;
  }
  goto done;

add2tree:
  if(value <= bintree2_get_top_value(this->maxtree)){
    bintree2_insert_value(this->maxtree, value);
    ++this->Mxc;
  }else{
    bintree2_insert_value(this->mintree, value);
    ++this->Mnc;
  }
done:
  return;
}

void _pipe_stats(PercentileTracker2 * this)
{
  PercentileTracker2PipeData pdata;
  if(!this->stats_pipe) return;
  pdata.percentile = _get_stats(this, &pdata.min, &pdata.max, &pdata.sum);
  this->stats_pipe(this->stats_pipe_data, &pdata);
}

gint64
_get_stats (PercentileTracker2 * this,
            gint64 *min,
            gint64 *max,
            gint64 *sum)
{
  gint64 result = 0;
  result = (this->median) ? _get_median(this) : _get_percentile(this);
  if(sum) *sum = this->sum;
  if(!min && !max) goto done;
  if(min) *min = 0;
  if(max) *max = 0;

  if(!this->Mnc && !this->Mxc) goto done;
  if(!this->Mxc){
    if(min) *min = bintree2_get_top_value(this->mintree);
    if(max) *max = bintree2_get_bottom_value(this->mintree);
  }else if(!this->Mnc){
    if(min) *min = bintree2_get_bottom_value(this->maxtree);
    if(max) *max = bintree2_get_top_value(this->maxtree);
  }else{
    if(min) *min = bintree2_get_bottom_value(this->maxtree);
    if(max) *max = bintree2_get_bottom_value(this->mintree);
  }
  done:
  return result;
}

gint64 _get_median(PercentileTracker2 * this)
{
  gint64 result;

  if(this->Mnc == this->Mxc){
    result = (bintree2_get_top_value(this->maxtree) + bintree2_get_top_value(this->mintree))>>1;
  } else if(this->Mnc < this->Mxc)
    result = bintree2_get_top_value(this->maxtree);
  else
    result = bintree2_get_top_value(this->mintree);

  return result;
}

gint64 _get_percentile(PercentileTracker2 * this)
{
  gdouble ratio;
  if(!this->Mnc)
    return bintree2_get_top_value(this->maxtree);
  if(!this->Mxc)
    return bintree2_get_top_value(this->mintree);

  ratio = (gdouble) this->Mxc / (gdouble) this->Mnc;
  if(ratio == this->ratio)
    return (bintree2_get_top_value(this->maxtree) + bintree2_get_top_value(this->mintree))>>1;
  if(this->ratio < ratio)
    return bintree2_get_top_value(this->maxtree);
  else
    return bintree2_get_top_value(this->mintree);
}

void _median_balancer(PercentileTracker2 *this)
{
  gint32 diff;
  gint64 value;
  if(!this->ready) goto done;
again:
  diff = this->Mxc - this->Mnc;
  if(-2 < diff && diff < 2) goto done;
  if (diff < -1) {
    value = bintree2_get_top_value(this->mintree);
    bintree2_delete_value(this->mintree, value);
    bintree2_insert_value(this->maxtree, value);
    --this->Mnc; ++this->Mxc;
  } else if (1 < diff) {
    value = bintree2_get_top_value(this->maxtree);
    bintree2_delete_value(this->maxtree, value);
    bintree2_insert_value(this->mintree, value);
    --this->Mxc; ++this->Mnc;
  }
  goto again;
done:
  return;
}


void _percentile_balancer(PercentileTracker2 *this)
{
  gdouble ratio;
  gint64 value;
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
  value = bintree2_get_top_value(this->mintree);
  bintree2_delete_value(this->mintree, value);
  bintree2_insert_value(this->maxtree, value);
  --this->Mnc; ++this->Mxc;
  goto balancing_mintree;

balancing_maxtree:
  ratio = (gdouble) (this->Mxc - 1) / (gdouble) (this->Mnc + 1);
  if(ratio < this->ratio || this->Mxc < 2) goto done;
  value = bintree2_get_top_value(this->maxtree);
  bintree2_delete_value(this->maxtree, value);
  bintree2_insert_value(this->mintree, value);
  --this->Mxc; ++this->Mnc;
  goto balancing_maxtree;

done:
  return;
}

void
_obsolate (PercentileTracker2 * this)
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

void _rem_value(PercentileTracker2 * this)
{
  gint64 value;
  this->sum -= value = this->items[this->read_index].value;
  this->items[this->read_index].value = 0;
  this->items[this->read_index].added = 0;
  if(++this->read_index == this->length){
    this->read_index=0;
  }
  if(!this->ready){
    goto collect;
  }
  if(value <= bintree2_get_top_value(this->maxtree)){
    bintree2_delete_value(this->maxtree, value);
    --this->Mxc;
  }else{
    bintree2_delete_value(this->mintree, value);
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
    bintree2_reset(this->maxtree);
    bintree2_reset(this->mintree);
    if(c < 1) goto done;
    for(j = 0, i = this->read_index; j < c; i = (i + 1) % this->length, ++j){
      this->collection[j] = this->items[i].value;
    }
  }
done:
  return;
}

gint
_cmp_for_max (gint64 x, gint64 y)
{
  return x == y ? 0 : x < y ? -1 : 1;
}

gint
_cmp_for_min (gint64 x, gint64 y)
{
  return x == y ? 0 : x < y ? 1 : -1;
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
