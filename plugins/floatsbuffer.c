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
#include "floatsbuffer.h"
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


GST_DEBUG_CATEGORY_STATIC (floatsbuffer_debug_category);
#define GST_CAT_DEFAULT cofloatsbuffer_debug_category

G_DEFINE_TYPE (FloatsBuffer, floatsbuffer, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void floatsbuffer_finalize (GObject * object);
static void _pipe(FloatsBuffer * this);
static void
_iterate (FloatsBuffer * this,
          void (*process)(gpointer,gdouble),
          gpointer data);
static void
_add_value(FloatsBuffer *this, gdouble value, GstClockTime remove);
static void _rem_value(FloatsBuffer *this);
static void
_obsolate (FloatsBuffer * this);
static void _fire_evaluator(FloatsBuffer *this, FloatsBufferEvaluator *evaluator);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
floatsbuffer_class_init (FloatsBufferClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = floatsbuffer_finalize;

  GST_DEBUG_CATEGORY_INIT (floatsbuffer_debug_category, "floatsbuffer", 0,
      "FloatsBuffer");

}

void
floatsbuffer_finalize (GObject * object)
{
  FloatsBuffer *this;
  GList *it;
  FloatsBufferPlugin *plugin;
  FloatsBufferEvaluator *evaluator;
  this = FLOATSBUFFER(object);
  g_object_unref(this->sysclock);
  g_free(this->items);

  for(it = this->plugins; it != NULL; it = it->next){
    plugin = it->data;
    plugin->destroyer(plugin);
  }

  for(it = this->evaluators; it != NULL; it = it->next){
      evaluator = it->data;
      evaluator->destroyer(evaluator);
  }
}

void
floatsbuffer_init (FloatsBuffer * this)
{
  g_rw_lock_init (&this->rwmutex);
}

FloatsBuffer *make_floatsbuffer(
    guint32 length,
    GstClockTime obsolation_treshold)
{
  FloatsBuffer *result;
  result = g_object_new (FLOATSBUFFER_TYPE, NULL);
  THIS_WRITELOCK (result);
  result->items = g_malloc0(sizeof(FloatsBufferItem)*length);
  result->sum = 0;
  result->length = length;
  result->sysclock = gst_system_clock_obtain();
  result->treshold = obsolation_treshold;
  result->counter = 0;
  THIS_WRITEUNLOCK (result);

  return result;
}

void floatsbuffer_reset(FloatsBuffer *this)
{
  THIS_WRITELOCK (this);
  memset(this->items, 0, sizeof(FloatsBufferItem) * this->length);
  this->counter = this->write_index = this->read_index = 0;
  this->sum = 0;
  THIS_WRITEUNLOCK (this);
}

void floatsbuffer_set_stats_pipe(FloatsBuffer *this, void (*pipe)(gpointer,FloatsBufferStatData*), gpointer pipe_data)
{
  THIS_WRITELOCK (this);
  this->stat_pipe = pipe;
  this->stat_pipe_data = pipe_data;
  THIS_WRITEUNLOCK (this);
}

void floatsbuffer_add_plugin(FloatsBuffer *this, FloatsBufferPlugin *plugin)
{
  THIS_WRITELOCK (this);
  this->plugins = g_list_prepend(this->plugins, plugin);
  THIS_WRITEUNLOCK (this);
}

void floatsbuffer_rem_plugin(FloatsBuffer *this, FloatsBufferPlugin *plugin)
{
  THIS_WRITELOCK (this);
  plugin->destroyer(plugin);
  this->plugins = g_list_remove(this->plugins, plugin);
  THIS_WRITEUNLOCK (this);
}


FloatsBufferEvaluator *
floatsbuffer_add_evaluator(FloatsBuffer *this,
    guint activator_filter,
    void (*iterator)(gpointer, gdouble), gpointer iterator_data)
{
  FloatsBufferEvaluator *evaluator;
  THIS_WRITELOCK (this);
  evaluator = g_malloc0(sizeof(FloatsBufferEvaluator));
  evaluator->activator_filter = activator_filter;
  evaluator->iterator = iterator;
  evaluator->iterator_data = iterator_data;
  evaluator->destroyer = g_free;
  this->evaluators = g_list_prepend(this->evaluators, evaluator);
  THIS_WRITEUNLOCK (this);
  return evaluator;
}


void floatsbuffer_rem_evaluator(FloatsBuffer *this, FloatsBufferEvaluator *evaluator)
{
  THIS_WRITELOCK (this);
  if(evaluator->destroyer) evaluator->destroyer(evaluator);
  else g_free(evaluator);
  this->evaluators = g_list_remove(this->evaluators, evaluator);
  THIS_WRITEUNLOCK (this);
}


void floatsbuffer_add_full(FloatsBuffer *this, gdouble value, GstClockTime removal)
{
  THIS_WRITELOCK (this);
  _add_value(this, value, removal);
  THIS_WRITEUNLOCK (this);
}




guint32 floatsbuffer_get_num(FloatsBuffer *this)
{
  guint32 result;
  THIS_READLOCK(this);
  result = this->counter;
  THIS_READUNLOCK(this);
  return result;
}

guint64 floatsbuffer_get_last(FloatsBuffer *this)
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
floatsbuffer_get_stats (FloatsBuffer * this,
                            gdouble *sum,
                            gdouble *avg)
{
  THIS_READLOCK (this);
  if(this->counter < 1) goto done;
  if(sum) *sum = this->sum;
  if(avg) *avg = this->avg;
  THIS_READUNLOCK (this);
done:
  return;
}

void
floatsbuffer_iterate (FloatsBuffer * this,
                            void (*process)(gpointer,gdouble),
                            gpointer data)
{
  THIS_READLOCK (this);
  _iterate(this, process, data);
  THIS_READUNLOCK (this);
}



void
floatsbuffer_obsolate (FloatsBuffer * this)
{
  THIS_READLOCK (this);
  _obsolate(this);
  THIS_READUNLOCK (this);
}

void _pipe(FloatsBuffer * this)
{
  FloatsBufferStatData stat;
  if(!this->stat_pipe) return;
  if(1 < this->counter){
    this->var = (gdouble)this->counter * this->sq_sum;
    this->var -= (gdouble)(this->sum * this->sum);
    this->var /= (gdouble) (this->counter * (this->counter - 1));
    this->dev = sqrt(this->var);
  }else{
    this->var = this->dev = 0;
  }
  if(++this->write_index == this->length){
      this->write_index=0;
  }
  stat.avg = this->avg;
  stat.dev = this->dev;
  stat.sum = this->sum;
  stat.var = this->var;
  this->stat_pipe(this->stat_pipe_data, &stat);
}

void
_iterate (FloatsBuffer * this,
                            void (*process)(gpointer,gdouble),
                            gpointer data)
{
  gint32 c,i;
  for(c = 0, i = this->read_index; c < this->counter; ++c){
    process(data, this->items[i].value);
    if(++i == this->length) i = 0;
  }
}


void _add_value(FloatsBuffer *this, gdouble value, GstClockTime removal)
{
  GstClockTime now;
  now = gst_clock_get_time(this->sysclock);
  //add new one
  ++this->counter;
  this->items[this->write_index].value = value;
  this->items[this->write_index].added = now;
  this->items[this->write_index].remove = removal;
  this->sum += INFINITY <= value? 1 : value;
  this->sq_sum += INFINITY <= value ? 1 : value * value;
  this->avg = this->counter < 1 ? 0. : this->sum / (gdouble)this->counter;
  {
    GList *it;
    FloatsBufferPlugin *plugin;
    for(it = this->plugins; it != NULL; it = it->next){
      plugin = it->data;
      if(!plugin->add_activator) continue;
      plugin->add_activator(plugin, value);
    }
  }

  {
    GList *it;
    FloatsBufferEvaluator *evaluator;
    gboolean activate;
    for(it = this->evaluators; it != NULL; it = it->next){
      evaluator = it->data;
      activate = evaluator->activator_filter & FLOATSBUFFER_FIRE_AT_ADD;
      if(!activate) continue;
      _fire_evaluator(this, evaluator);
    }
  }
  _pipe(this);
  _obsolate(this);
}


void _rem_value(FloatsBuffer *this)
{
  gdouble value;
  value = this->items[this->read_index].value;
  this->sum-= INFINITY <= value? 1 : value;
  this->sq_sum -= INFINITY <= value ? 1 : value * value;
  this->items[this->read_index].value = 0;
  this->items[this->read_index].added = 0;
  this->items[this->read_index].remove = 0;
  if(++this->read_index == this->length){
      this->read_index=0;
  }
  --this->counter;

  {
    GList *it;
    FloatsBufferPlugin *plugin;
    for(it = this->plugins; it != NULL; it = it->next){
      plugin = it->data;
      if(!plugin->rem_activator) continue;
      plugin->rem_activator(plugin, value);
    }
  }

  {
    GList *it;
    FloatsBufferEvaluator *evaluator;
    gboolean activate;
    for(it = this->evaluators; it != NULL; it = it->next){
      evaluator = it->data;
      activate = evaluator->activator_filter & FLOATSBUFFER_FIRE_AT_REM;
      if(!activate) continue;
      _fire_evaluator(this, evaluator);
    }
  }
  _pipe(this);
}

#define _removal(this, index) this->items[index].remove
void
_obsolate (FloatsBuffer * this)
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

void _fire_evaluator(FloatsBuffer *this, FloatsBufferEvaluator *evaluator)
{
  if(evaluator->iterator){
    _iterate(this, evaluator->iterator, evaluator->iterator_data);
  }
}

//-----------------------------------------------------------------------------
//-------------------------------- P L U G I N S ------------------------------
//-----------------------------------------------------------------------------

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
