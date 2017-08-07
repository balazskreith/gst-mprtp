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

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "recycle.h"


#define _now(this) (gst_clock_get_time (this->sysclock))

GST_DEBUG_CATEGORY_STATIC (recycle_debug_category);
#define GST_CAT_DEFAULT recycle_debug_category

G_DEFINE_TYPE (Recycle, recycle, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
recycle_finalize (GObject * object);

void
recycle_class_init (RecycleClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = recycle_finalize;

  GST_DEBUG_CATEGORY_INIT (recycle_debug_category, "recycle", 0,
      "Recycle Controller");

}


void
recycle_finalize (GObject * object)
{
  Recycle *this = RECYCLE (object);
  datapuffer_clear(this->items, this->dtor);
  datapuffer_dtor(this->items);
}

void
recycle_init (Recycle * this)
{

}

Recycle *make_recycle(gint32 size, RecycleItemCtor ctor, RecycleItemDtor dtor, RecycleItemShaper shaper)
{
  Recycle *result = g_object_new(RECYCLE_TYPE, NULL);
  result->items  = datapuffer_ctor(size);
  result->ctor   = ctor;
  result->dtor   = dtor;
  result->shaper = shaper;
  return result;
}

gpointer recycle_retrieve(Recycle* this)
{
  gpointer result = NULL;
  if(datapuffer_isempty(this->items)){
    result = this->ctor();
    goto done;
  }
  result = datapuffer_read(this->items);
  if (!result) {
    result = this->ctor();
  }
done:
  return result;
}

gpointer recycle_retrieve_and_shape(Recycle *this, gpointer udata)
{
  gpointer result = recycle_retrieve(this);
  if(this->shaper){
    this->shaper(result, udata);
  }
  return result;
}

void recycle_add(Recycle* this, gpointer item)
{

  if(datapuffer_isfull(this->items)){
    this->dtor(item);
    return;
  }

  datapuffer_write(this->items, item);
}



//------------------------------------------------------------

DEFINE_RECYCLE_TYPE(/*global scope*/, uint16, guint16)
DEFINE_RECYCLE_TYPE(/*global scope*/, int32, gint32)
DEFINE_RECYCLE_TYPE(/*global scope*/, int64, gint64)
DEFINE_RECYCLE_TYPE(/*global scope*/, uint32, guint32)
DEFINE_RECYCLE_TYPE(/*global scope*/, uint64, guint64)
DEFINE_RECYCLE_TYPE(/*global scope*/, double, gdouble)



