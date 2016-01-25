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
_add_value(NumsTracker *this, gint64 value, GstClockTime removal);
static void
_rem_value(NumsTracker *this);
static void
_obsolate (NumsTracker * this);

static gint
_cmp_for_max (guint64 x, guint64 y);

//----------------------------------------------------------------------
//--------- Private functions for MinMax Plugin --------
//----------------------------------------------------------------------
static void
_destroy_minmax_plugin(gpointer data);
static void
_minmax_add_activator(gpointer pdata, gint64 value);
static void
_minmax_rem_activator(gpointer pdata, gint64 value);

static void
_ewma_add_activator(gpointer pdata, gint64 value);

static void
_variance_add_activator(gpointer pdata, gint64 value);

static void
_variance_rem_activator(gpointer pdata, gint64 value);

static void
_sum_add_activator(gpointer pdata, gint64 value);

static void
_sum_rem_activator(gpointer pdata, gint64 value);


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
  GList *it;
  NumsTrackerPlugin *plugin;
  this = NUMSTRACKER(object);
  g_object_unref(this->sysclock);
  for(it = this->plugins; it != NULL; it = it->next){
    plugin = it->data;
    plugin->destroyer(plugin);
  }
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


void numstracker_reset(NumsTracker *this)
{
  THIS_WRITELOCK (this);
  while(0 < this->counter) _rem_value(this);
  THIS_WRITEUNLOCK (this);
}

gboolean numstracker_find(NumsTracker *this, gint64 value)
{
  gboolean result = FALSE;
  guint32 read_index;
  THIS_READLOCK(this);
  read_index = this->read_index;
  if(this->counter < 1) goto done;
again:
  if(this->items[read_index].value == value){
    result = TRUE;
    goto done;
  }
  if(read_index == this->write_index) goto done;
  if(++read_index == this->length) read_index = 0;
  goto again;
done:
  THIS_READUNLOCK(this);
  return result;
}

void numstracker_add_plugin(NumsTracker *this, NumsTrackerPlugin *plugin)
{
  THIS_WRITELOCK (this);
  this->plugins = g_list_prepend(this->plugins, plugin);
  THIS_WRITEUNLOCK (this);
}

void numstracker_rem_plugin(NumsTracker *this, NumsTrackerPlugin *plugin)
{
  THIS_WRITELOCK (this);
  plugin->destroyer(plugin);
  this->plugins = g_list_remove(this->plugins, plugin);
  THIS_WRITEUNLOCK (this);
}

void numstracker_add(NumsTracker *this, gint64 value)
{
  THIS_WRITELOCK (this);
  _add_value(this, value, 0);
  THIS_WRITEUNLOCK (this);
}

void numstracker_add_rem_pipe(NumsTracker *this,
                              void (*rem_pipe)(gpointer, gint64),
                              gpointer rem_pipe_data)
{
  THIS_WRITELOCK (this);
  this->rem_pipe = rem_pipe;
  this->rem_pipe_data = rem_pipe_data;
  THIS_WRITEUNLOCK (this);
}

void numstracker_add_with_removal(NumsTracker *this, gint64 value, GstClockTime removal)
{
  THIS_WRITELOCK (this);
  _add_value(this, value, removal);
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
                         gint64 *sum)
{
  THIS_READLOCK (this);
  if(this->counter < 1) goto done;
  if(sum){
      *sum = this->value_sum;
  }
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




void _add_value(NumsTracker *this, gint64 value, GstClockTime removal)
{
  GstClockTime now;
  now = gst_clock_get_time(this->sysclock);
  //add new one
  ++this->counter;
  this->items[this->write_index].value = value;
  this->items[this->write_index].added = now;
  this->items[this->write_index].remove = removal;
  this->value_sum += value;
  if(++this->write_index == this->length){
      this->write_index=0;
  }

  {
    GList *it;
    NumsTrackerPlugin *plugin;
    for(it = this->plugins; it != NULL; it = it->next){
      plugin = it->data;
      if(!plugin->add_activator) continue;
      plugin->add_activator(plugin, value);
    }
  }
  _obsolate(this);
}

void _rem_value(NumsTracker *this)
{
  gint64 value;
  value = this->items[this->read_index].value;
  this->value_sum -= value;
  this->items[this->read_index].value = 0;
  this->items[this->read_index].added = 0;
  this->items[this->read_index].remove = 0;
  if(++this->read_index == this->length){
      this->read_index=0;
  }
  --this->counter;

  if(this->rem_pipe){
    this->rem_pipe(this->rem_pipe_data, value);
  }

  {
    GList *it;
    NumsTrackerPlugin *plugin;
    for(it = this->plugins; it != NULL; it = it->next){
      plugin = it->data;
      if(!plugin->rem_activator) continue;
      plugin->rem_activator(plugin, value);
    }
  }
}

#define _removal(this, index) this->items[index].remove
void
_obsolate (NumsTracker * this)
{
  GstClockTime treshold,now, removal;
  now = gst_clock_get_time(this->sysclock);
  treshold = now - this->treshold;

again:
  if(this->counter < 1) goto done;

  removal = this->items[this->read_index].remove;
  if(this->write_index == this->read_index) goto elliminate;
  else if(this->items[this->read_index].added < treshold) goto elliminate;
  else if(0 < removal && removal < now) goto elliminate;
  else goto done;
elliminate:
  _rem_value(this);
  goto again;
done:
  return;
}

gint
_cmp_for_max (guint64 x, guint64 y)
{
  return x == y ? 0 : x < y ? -1 : 1;
}

//-----------------------------------------------------------------------------
//-------------------------------- P L U G I N S ------------------------------
//-----------------------------------------------------------------------------

NumsTrackerMinMaxPlugin *
make_numstracker_minmax_plugin(void (*max_pipe)(gpointer,guint64), gpointer max_data,
                               void (*min_pipe)(gpointer,guint64), gpointer min_data)
{
  NumsTrackerMinMaxPlugin *this = g_malloc0(sizeof(NumsTrackerMinMaxPlugin));
  this->base.add_activator = _minmax_add_activator;
  this->base.rem_activator = _minmax_rem_activator;
  this->max_pipe = max_pipe;
  this->max_pipe_data = max_data;
  this->min_pipe = min_pipe;
  this->min_pipe_data = min_data;
  this->tree = make_bintree(_cmp_for_max);
  this->base.destroyer = _destroy_minmax_plugin;
  return this;
}

void get_numstracker_minmax_plugin_stats(NumsTrackerMinMaxPlugin *this, gint64 *max, gint64 *min)
{
  if(max) *max = bintree_get_top_value(this->tree);
  if(min) *min = bintree_get_bottom_value(this->tree);
}

void
_destroy_minmax_plugin(gpointer data)
{
  NumsTrackerMinMaxPlugin *this = data;
  g_object_unref(this->tree);
}

static void _minmax_pipe(NumsTrackerMinMaxPlugin *this)
{
  if(this->max_pipe){
   guint64 top;
   top = bintree_get_top_value(this->tree);
   this->max_pipe(this->max_pipe_data, top);
 }

 if(this->min_pipe){
   guint64 bottom;
   bottom = bintree_get_bottom_value(this->tree);
   this->min_pipe(this->min_pipe_data, bottom);
 }
}

void
_minmax_add_activator(gpointer pdata, gint64 value)
{
  NumsTrackerMinMaxPlugin *this = pdata;
  if(this->tree && value < 0){
    g_warning("numtracker with tree only allows positive values");
    goto done;
  }
  bintree_insert_value(this->tree, value);
  _minmax_pipe(this);

done:
  return;

}
void
_minmax_rem_activator(gpointer pdata, gint64 value)
{
  NumsTrackerMinMaxPlugin *this = pdata;
  if(this->tree && value < 0){
    g_warning("numtracker with tree only allows positive values");
    goto done;
  }
  bintree_delete_value(this->tree, value);
  _minmax_pipe(this);

  done:
    return;
}


NumsTrackerEWMAPlugin *
make_numstracker_ewma_plugin(void (*avg_pipe)(gpointer,gdouble), gpointer avg_data,
                               gdouble factor)
{
  NumsTrackerEWMAPlugin *this = g_malloc0(sizeof(NumsTrackerEWMAPlugin));;
  this->base.add_activator = _ewma_add_activator;
  this->base.rem_activator = NULL;
  this->avg_pipe = avg_pipe;
  this->avg_pipe_data = avg_data;
  this->factor = factor;
  this->base.destroyer = g_free;
  return this;
}

void get_numstracker_ewma_plugin_stats(NumsTrackerEWMAPlugin *this, gdouble *avg, gdouble *dev)
{
  if(avg) *avg = this->avg;
}


void
_ewma_add_activator(gpointer pdata, gint64 value)
{
  NumsTrackerEWMAPlugin *this = pdata;
  this->avg = this->factor * (gdouble) value + (1.-this->factor) * this->avg;
  if(this->avg_pipe){
     this->avg_pipe(this->avg_pipe_data, this->avg);
   }
}


NumsTrackerVariancePlugin *
make_numstracker_variance_plugin(void (*var_pipe)(gpointer,gdouble), gpointer var_data)
{
  NumsTrackerVariancePlugin *this = g_malloc0(sizeof(NumsTrackerVariancePlugin));;
  this->base.add_activator = _variance_add_activator;
  this->base.rem_activator = _variance_rem_activator;
  this->var_pipe = var_pipe;
  this->var_pipe_data = var_data;
  this->base.destroyer = g_free;
  return this;
}

void get_numstracker_variance_plugin_stats(NumsTrackerVariancePlugin *this, gdouble *variance)
{
  gdouble counter, sum_squere;
  if(this->counter < 2) goto done;
  sum_squere = (gdouble)this->sum * (gdouble)this->sum;
  counter = this->counter;
  //V = (N * SX2 - (SX1 * SX1)) / (N * (N - 1))
  if(variance)
    *variance = (counter * (gdouble)this->squere_sum - sum_squere) / (counter * (counter - 1.));
done:
  return;
}

static void _variance_pipe(NumsTrackerVariancePlugin *this)
{
  gdouble variance;
  if(!this->var_pipe) return;
  get_numstracker_variance_plugin_stats(this, &variance);
  this->var_pipe(this->var_pipe_data, variance);
}

void
_variance_add_activator(gpointer pdata, gint64 value)
{
  NumsTrackerVariancePlugin *this = pdata;
  this->squere_sum += value * value;
  this->sum += value;
  ++this->counter;
  _variance_pipe(this);

}
void
_variance_rem_activator(gpointer pdata, gint64 value)
{
  NumsTrackerVariancePlugin *this = pdata;
  this->squere_sum -= value * value;
  this->sum -= value;
  --this->counter;
  _variance_pipe(this);
}



NumsTrackerSumPlugin *
make_numstracker_sum_plugin(void (*sum_pipe)(gpointer,gint64), gpointer sum_data)
{
  NumsTrackerSumPlugin *this = g_malloc0(sizeof(NumsTrackerSumPlugin));;
  this->base.add_activator = _sum_add_activator;
  this->base.rem_activator = _sum_rem_activator;
  this->sum_pipe = sum_pipe;
  this->sum_pipe_data = sum_data;
  this->base.destroyer = g_free;
  return this;
}


void get_numstracker_sum_plugin_stats(NumsTrackerSumPlugin *this, gint64 *sum)
{
  if(sum) *sum = this->sum;
}

static void _sum_pipe(NumsTrackerSumPlugin *this)
{
  if(!this->sum_pipe) return;
  this->sum_pipe(this->sum_pipe_data, this->sum);
}

void
_sum_add_activator(gpointer pdata, gint64 value)
{
  NumsTrackerSumPlugin *this = pdata;
  this->sum += value;
  _sum_pipe(this);

}
void
_sum_rem_activator(gpointer pdata, gint64 value)
{
  NumsTrackerSumPlugin *this = pdata;
  this->sum -= value;
  _sum_pipe(this);
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
