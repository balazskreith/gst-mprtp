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
#include "pointerpool.h"
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


GST_DEBUG_CATEGORY_STATIC (pointerpool_debug_category);
#define GST_CAT_DEFAULT pointerpool_debug_category

G_DEFINE_TYPE (PointerPool, pointerpool, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void pointerpool_finalize (GObject * object);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
pointerpool_class_init (PointerPoolClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = pointerpool_finalize;

  GST_DEBUG_CATEGORY_INIT (pointerpool_debug_category, "pointerpool", 0,
      "PointerPool");

}

void
pointerpool_finalize (GObject * object)
{
  PointerPool *this;
  int i;
  this = POINTERPOOL(object);
  for(i=0; i<this->length; ++i){
    if(!this->items[i]) continue;
    this->item_dtor(this->items[i]);
  }
  //g_free(this->items);
}

void
pointerpool_init (PointerPool * this)
{
  g_rw_lock_init (&this->rwmutex);
}

PointerPool *make_pointerpool(guint32 length,
                              gpointer (*item_ctor)(void),
                              void (*item_dtor)(gpointer),
                              void (*item_reset)(gpointer))
{
  PointerPool *result;
  result = g_object_new (POINTERPOOL_TYPE, NULL);
  THIS_WRITELOCK (result);
  result->items = g_malloc0(sizeof(gpointer)*length);
  result->item_ctor = item_ctor;
  result->item_dtor = item_dtor;
  result->item_reset = item_reset;
  result->length = length;
  result->counter = 0;
  THIS_WRITEUNLOCK (result);

  return result;
}

void pointerpool_add(PointerPool *this, gpointer item)
{
  THIS_WRITELOCK (this);
  if(this->counter == this->length){
    this->item_dtor(item);
    goto done;
  }
  this->items[this->write_index] = item;
  if(++this->write_index == this->length){
      this->write_index=0;
  }
  ++this->counter;
done:
  THIS_WRITEUNLOCK (this);
}

gpointer pointerpool_get(PointerPool* this)
{
  gpointer result;
  THIS_WRITELOCK (this);
  if(this->counter < 1){
    result = this->item_ctor();
    goto done;
  }
  result = this->items[this->read_index];
  if(++this->read_index == this->length){
      this->read_index=0;
  }
  --this->counter;

  if(this->item_reset) this->item_reset(result);

done:
  THIS_WRITEUNLOCK (this);
  return result;
}

void pointerpool_reset(PointerPool *this)
{
  THIS_WRITELOCK (this);
  memset(this->items, 0, sizeof(gpointer) * this->length);
  this->counter = this->write_index = this->read_index = 0;
  THIS_WRITEUNLOCK (this);
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
