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

typedef struct _CollectingState{
  BinTree2 *collector;
  BinTree2 *dispensor;
  guint    requested;
}CollectingState;

typedef struct _BalancingState{
  void (*balancer)(PercentileTracker2*);
}BalancingState;

struct _PercentileState{
  void    (*processor)(PercentileTracker2*, gint64);
  gint64 (*producer)(PercentileTracker2*);
  CollectingState   collecting;
  BalancingState    balancing;
};

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
static gint
_cmp_for_max (gint64 x, gint64 y);
static gint
_cmp_for_min (gint64 x, gint64 y);

static void _balance_value(PercentileTracker2 *this, gint64 value);
static void _collect_value(PercentileTracker2* this, gint64 value);
static void _transit_to_collecting(PercentileTracker2 *this, guint requested);
static void _transit_to_balancing(PercentileTracker2 *this);
static void _median_balancer(PercentileTracker2 *this);
static void _nmedian_balancer(PercentileTracker2 *this);
static gint64 _get_median(PercentileTracker2 * this);
static gint64 _get_nmedian(PercentileTracker2 * this);
#define _counter(this) (bintree2_get_num(this->maxtree) + bintree2_get_num(this->mintree))

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

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
  g_free(this->items);
}

void
percentiletracker2_init (PercentileTracker2 * this)
{

  g_rw_lock_init (&this->rwmutex);
  this->sum = 0;
  this->sysclock = gst_system_clock_obtain();
  this->treshold = GST_SECOND;
  this->state = g_malloc0(sizeof(PercentileState));
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
  CollectingState *collecting;
  BalancingState *balancing;
  PercentileTracker2 *this;
  this = g_object_new (PERCENTILETRACKER2_TYPE, NULL);
  THIS_WRITELOCK (this);
  this->ratio = (gdouble)percentile / (gdouble)(100 - percentile);
  this->items = g_malloc0(sizeof(PercentileTracker2Item)*length);
  this->percentile = percentile;
  this->length = length;
  this->maxtree = make_bintree2(cmp_max);
  this->mintree = make_bintree2(cmp_min);

  collecting = &this->state->collecting;
  balancing = &this->state->balancing;
  if(this->ratio < 1.){

    this->required = (1./this->ratio) + 1;
    this->state->producer = _get_nmedian;
    collecting->collector = this->mintree;
    collecting->dispensor = this->maxtree;
    balancing->balancer = _nmedian_balancer;
  }else if(1. < this->ratio){
    this->required = this->ratio + 1;
    this->state->producer = _get_nmedian;
    collecting->collector = this->maxtree;
    collecting->dispensor = this->mintree;
    balancing->balancer = _nmedian_balancer;
  }else{
    this->required = 2;
    this->state->producer = _get_median;
    collecting->collector = this->maxtree;
    collecting->dispensor = this->mintree;
    balancing->balancer = _median_balancer;
  }
//  g_print("required num: %d\n", this->required);
  _transit_to_collecting(this, this->required);
  THIS_WRITEUNLOCK (this);

  return this;
}

void percentiletracker2_test(void)
{
  PercentileTracker2 *tracker;
  tracker = make_percentiletracker2(20, 50);
  percentiletracker2_add(tracker, 7);
  percentiletracker2_add(tracker, 1);
  percentiletracker2_add(tracker, 3);
  percentiletracker2_add(tracker, 8);
  percentiletracker2_add(tracker, 2);
  percentiletracker2_add(tracker, 6);
  percentiletracker2_add(tracker, 4);
  percentiletracker2_add(tracker, 5);
  percentiletracker2_add(tracker, 9);
  percentiletracker2_add(tracker, 10);
  percentiletracker2_add(tracker, 11);

  {
    gint64 min,max,perc;
    perc = percentiletracker2_get_stats(tracker, &min, &max, NULL);
    g_print("PercentileTracker2 test for 50th percentile\n"
              "Min: %lu, 50th percentile: %lu Max: %lu\n", min, perc, max);
  }
  {
    gint64 min,max,perc,i;
    for(i=0; i<100; ++i){
      percentiletracker2_add(tracker, (g_random_int() % 100) + 50);
      perc = percentiletracker2_get_stats(tracker, &min, &max, NULL);
      g_print("PercentileTracker2 test for 50th percentile\n"
              "Min: %lu, 50th percentile: %lu Max: %lu\n", min, perc, max);
    }

  }

  g_object_unref(tracker);

  tracker = make_percentiletracker2(20, 10);
  percentiletracker2_add(tracker, 7);
  percentiletracker2_add(tracker, 1);
  percentiletracker2_add(tracker, 3);
  percentiletracker2_add(tracker, 8);
  percentiletracker2_add(tracker, 2);
  percentiletracker2_add(tracker, 6);
  percentiletracker2_add(tracker, 4);
  percentiletracker2_add(tracker, 5);
  percentiletracker2_add(tracker, 9);
  percentiletracker2_add(tracker, 10);

  {
    gint64 min,max,perc;
    perc = percentiletracker2_get_stats(tracker, &min, &max, NULL);
    g_print("PercentileTracker2 test for 10th percentile\n"
            "Min: %lu, 10th percentile: %lu Max: %lu\n", min, perc, max);
  }
  g_object_unref(tracker);

}

void percentiletracker2_reset(PercentileTracker2 *this)
{
  THIS_WRITELOCK (this);
  bintree2_reset(this->maxtree);
  bintree2_reset(this->mintree);
  memset(this->items, 0, sizeof(PercentileTracker2Item) * this->length);
  this->write_index = this->read_index = 0;
  this->sum = 0;
  _transit_to_collecting(this, this->required);
  THIS_WRITEUNLOCK (this);
}

void percentiletracker2_add(PercentileTracker2 *this, gint64 value)
{
  THIS_WRITELOCK (this);
  this->state->processor(this, value);
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
  THIS_READUNLOCK (this);
}


void _add_value(PercentileTracker2 *this, gint64 value)
{
  this->sum += value;
  this->items[this->write_index].value = value;
  this->items[this->write_index].added = gst_clock_get_time(this->sysclock);
  if(++this->write_index == this->length){
    this->write_index=0;
  }
  this->counter = MIN(this->counter + 1, this->length);
  _pipe_stats(this);
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
  gint32 max_count, min_count;
  if(sum) *sum = this->sum;
  result = this->state->producer(this);
  if(!min && !max) goto done;
  if(min) *min = 0;
  if(max) *max = 0;

  min_count = bintree2_get_num(this->mintree);
  max_count = bintree2_get_num(this->maxtree);
  if(!min_count && !max_count) goto done;
  if(!max_count){
    if(min) *min = bintree2_get_top_value(this->mintree);
    if(max) *max = bintree2_get_bottom_value(this->mintree);
  }else if(!min_count){
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
  gint32 max_count,min_count;
  gint64 result;
  max_count = bintree2_get_num(this->maxtree);
  min_count = bintree2_get_num(this->mintree);

  if(min_count == max_count){
    result = (bintree2_get_top_value(this->maxtree) + bintree2_get_top_value(this->mintree))>>1;
  } else if(min_count < max_count)
    result = bintree2_get_top_value(this->maxtree);
  else
    result = bintree2_get_top_value(this->mintree);

  if(this->debug){
      g_print("_get_median debug, item num: %u <-> R:%d-W:%d\n",
              percentiletracker2_get_num(this),
              this->read_index,
              this->write_index);
      g_print("maxtree top: %lu (%u), mintree top: %lu (%u) result is: %lu\n",
              bintree2_get_top_value(this->maxtree),
              bintree2_get_num(this->maxtree),
              bintree2_get_top_value(this->mintree),
              bintree2_get_num(this->mintree),
              result);
    }
  return result;
}

gint64 _get_nmedian(PercentileTracker2 * this)
{
  gint32 max_count,min_count;
  gdouble ratio;
  max_count = bintree2_get_num(this->maxtree);
  min_count = bintree2_get_num(this->mintree);
  if(!min_count)
    return bintree2_get_top_value(this->maxtree);
  if(!max_count)
    return bintree2_get_top_value(this->mintree);

  ratio = (gdouble) max_count / (gdouble) min_count;
  if(ratio == this->ratio)
    return (bintree2_get_top_value(this->maxtree) + bintree2_get_top_value(this->mintree))>>1;
  if(this->ratio < ratio)
    return bintree2_get_top_value(this->maxtree);
  else
    return bintree2_get_top_value(this->mintree);
}


void _balance_value(PercentileTracker2 *this, gint64 value)
{
  BalancingState *state;
  state = &this->state->balancing;
  //add new one
  _add_value(this, value);

  if(value <= bintree2_get_top_value(this->maxtree))
    bintree2_insert_value(this->maxtree, value);
  else
    bintree2_insert_value(this->mintree, value);

  _obsolate(this);
  if(_counter(this) < this->required) goto collecting;
  else goto balancing;
balancing:
  state->balancer(this);
  return;
collecting:
  _transit_to_collecting(this, this->required - _counter(this));
  return;
}


void _collect_value(PercentileTracker2* this, gint64 value)
{
  CollectingState *state;
  BinTree2Node *node;
  gint32 c_was, c_is;
  state = &this->state->collecting;
  c_was = _counter(this);
  _add_value(this, value);
  bintree2_insert_value(state->collector, value);
  c_is = _counter(this);
  if(c_was == c_is) goto done;
  if(--state->requested > 0) goto done;

  node = bintree2_pop_top_node(state->collector);
  bintree2_insert_node(state->dispensor, node);

  c_was = c_is;
  _obsolate(this);
  c_is = _counter(this);
//  g_print("after insert c is %d (c was: %d) \n", c_is, c_was);
  if(c_is == c_was) goto transit;
  state->requested += c_was - c_is;
done:
//  g_print("requested num: %d\n", state->requested);
  return;
transit:
  _transit_to_balancing(this);
  return;
}

void _transit_to_collecting(PercentileTracker2 *this, guint requested)
{
  CollectingState *state;
  state = &this->state->collecting;
  this->state->processor = _collect_value;
  state->requested = requested;

//  while(bintree2_get_num(state->dispensor)){
//    BinTree2Node* node;
//    node = bintree2_pop_top_node(state->dispensor);
//    bintree2_insert_node(state->dispensor, node);
//  }
  return;
}


void _transit_to_balancing(PercentileTracker2 *this)
{
  this->state->processor = _balance_value;
}


void _median_balancer(PercentileTracker2 *this)
{
  gint32 diff;
  gint32 max_count, min_count;
  BinTree2Node *top;
  max_count = bintree2_get_num(this->maxtree);
  min_count = bintree2_get_num(this->mintree);
again:
  if(!max_count || !min_count) goto collecting;
  diff = max_count - min_count;
  if(-2 < diff && diff < 2) goto done;
  if (diff < -1) {
    top = bintree2_pop_top_node(this->mintree);
    bintree2_insert_node(this->maxtree, top);
    --min_count;++max_count;
  } else if (1 < diff) {
    top = bintree2_pop_top_node(this->maxtree);
    bintree2_insert_node(this->mintree, top);
    --max_count;++min_count;
  }
  goto again;
done:
  return;
collecting:
  _transit_to_collecting(this, 1);
}


void _nmedian_balancer(PercentileTracker2 *this)
{
  gint32 max_count, min_count;
  gdouble ratio;
  BinTree2Node *top;
  max_count = bintree2_get_num(this->maxtree);
  min_count = bintree2_get_num(this->mintree);
  if(!min_count) ratio = .001;
  else ratio = (gdouble) max_count / (gdouble) min_count;
  if(ratio < this->ratio)
    goto balancing_mintree;
  else
    goto balancing_maxtree;

balancing_mintree:
  if(min_count == 0) goto collecting;
  ratio = (gdouble) (max_count + 1) / (gdouble) (min_count - 1);
  if(this->ratio < ratio) goto done;
  top = bintree2_pop_top_node(this->mintree);
  bintree2_insert_node(this->maxtree, top);
  --min_count;++max_count;
  goto balancing_mintree;

balancing_maxtree:
  if(max_count == 0) goto collecting;
  ratio = (gdouble) (max_count - 1) / (gdouble) (min_count + 1);
  if(ratio < this->ratio) goto done;
  top = bintree2_pop_top_node(this->maxtree);
  bintree2_insert_node(this->mintree, top);
  ++min_count;--max_count;
  goto balancing_maxtree;

done:
  return;
collecting:
  _transit_to_collecting(this, 1);
}

void
_obsolate (PercentileTracker2 * this)
{
  GstClockTime treshold,now;
  now = gst_clock_get_time(this->sysclock);
  treshold = now - this->treshold;
again:
  if(this->counter < 1) goto done;
  if(this->write_index == this->read_index) goto elliminate;
  else if(this->items[this->read_index].added < treshold) goto elliminate;
  else goto done;
elliminate:
  if(this->items[this->read_index].value <= bintree2_get_top_value(this->maxtree))
    bintree2_delete_value(this->maxtree, this->items[this->read_index].value);
  else
    bintree2_delete_value(this->mintree, this->items[this->read_index].value);
  this->sum -= this->items[this->read_index].value;
  this->items[this->read_index].value = 0;
  this->items[this->read_index].added = 0;
  if(++this->read_index == this->length){
      this->read_index=0;
  }
  this->counter = MAX(0, this->counter - 1);
  goto again;
done:
  _pipe_stats(this);
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
