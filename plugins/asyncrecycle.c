/* GStreamer Scheduling tree
 * Copyright (C) 2015 Balázs Kreith (contact: balazs.kreith@gmail.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be ureful,
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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "asyncrecycle.h"


#define _now(this) (gst_clock_get_time (this->sysclock))

GST_DEBUG_CATEGORY_STATIC (async_recycle_debug_category);
#define GST_CAT_DEFAULT async_recycle_debug_category

G_DEFINE_TYPE (AsyncRecycle, async_recycle, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
async_recycle_finalize (GObject * object);

void
async_recycle_class_init (AsyncRecycleClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = async_recycle_finalize;

  GST_DEBUG_CATEGORY_INIT (async_recycle_debug_category, "rndctrler", 0,
      "MpRTP Receiving Controller");

}


void
async_recycle_finalize (GObject * object)
{
  AsyncRecycle *this = ASYNCRECYCLE (object);
  gpointer item;
  while((item = g_async_queue_try_pop(this->items)) != NULL){
    this->dtor(item);
  }
  g_async_queue_unref(this->items);
}

void
async_recycle_init (AsyncRecycle * this)
{
  this->items  = g_async_queue_new();
}

AsyncRecycle *make_async_recycle(AsyncRecycleItemCtor ctor, AsyncRecycleItemDtor dtor, AsyncRecycleItemShaper shaper)
{
  AsyncRecycle *result = g_object_new(ASYNCRECYCLE_TYPE, NULL);
  result->ctor   = ctor;
  result->dtor   = dtor;
  result->shaper = shaper;
  return result;
}


gpointer async_recycle_retrieve(AsyncRecycle* this)
{
  gpointer item = g_async_queue_try_pop(this->items);
  return item != NULL ? item : this->ctor();
}

gpointer async_recycle_retrieve_and_shape(AsyncRecycle *this, gpointer udata)
{
  gpointer result = async_recycle_retrieve(this);
  if(this->shaper){
    this->shaper(result, udata);
  }
  return result;
}

void async_recycle_add(AsyncRecycle* this, gpointer item)
{
  g_async_queue_push(this->items, item);
}



//------------------------------------------------------------



