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
#include "notifier.h"


#define _now(this) (gst_clock_get_time (this->sysclock))

GST_DEBUG_CATEGORY_STATIC (notifier_debug_category);
#define GST_CAT_DEFAULT notifier_debug_category

G_DEFINE_TYPE (Notifier, notifier, G_TYPE_OBJECT);

typedef struct{
  gchar               name[256];
  ListenerFunc        callback;
  ListenerFilterFunc  filter;
  gpointer            udata;
}Listener;


static Listener* _make_listener(ListenerFunc callback, ListenerFilterFunc filter, gpointer udata);
static void _dispose_listener(gpointer item);


//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
notifier_finalize (GObject * object);


void
notifier_class_init (NotifierClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = notifier_finalize;

  GST_DEBUG_CATEGORY_INIT (notifier_debug_category, "rndctrler", 0,
      "MpRTP Receiving Controller");

}


void
notifier_finalize (GObject * object)
{
  Notifier *this = NOTIFIER (object);
  if(this->listeners){
    g_slist_free_full(this->listeners, _dispose_listener);
  }
}

void
notifier_init (Notifier * this)
{

}

Notifier *make_notifier(const gchar* name)
{
  Notifier *result = g_object_new(NOTIFIER_TYPE, NULL);
  strcpy(result->name, name);
  return result;
}

void notifier_add_listener(Notifier *this, ListenerFunc callback, gpointer udata)
{
  Listener *listener = _make_listener(callback, NULL, udata);
  //Debug purpose:
  strcpy(listener->name, this->name);
  this->listeners = g_slist_prepend(this->listeners, listener);
}

void notifier_add_listener_with_filter(Notifier *this, ListenerFunc callback, ListenerFilterFunc filter, gpointer udata)
{
  Listener *listener = _make_listener(callback, filter, udata);
  //Debug purpose:
  strcpy(listener->name, this->name);
  this->listeners = g_slist_prepend(this->listeners, listener);
}

static gint _find_listener_by_func(gconstpointer itemp, gconstpointer searchedp)
{
  const ListenerFunc searched_callback = searchedp;
  const Listener* listener = itemp;
  return listener->callback == searched_callback ? 0 : -1;
}

void notifier_rem_listener(Notifier *this, ListenerFunc callback)
{
  GSList* it;
  it = g_slist_find_custom(this->listeners, callback, _find_listener_by_func);
  if(!it){
    return;
  }
  this->listeners = g_slist_remove(this->listeners, it->data);
  _dispose_listener(it->data);
}


static void _listener_helper(gpointer item, gpointer udata)
{
  Listener* listener = item;
  if(listener->filter && !listener->filter(listener->udata, udata)){
    goto done;
  }
  if(listener->callback){
    listener->callback(listener->udata, udata);
  }else{
    g_print("Listener callback is undefined");
  }
done:
  return;
}

void notifier_do(Notifier *this, gpointer subject)
{
  if(!this){
    return;
  }
//PROFILING2("notifier_do",
  g_slist_foreach(this->listeners, _listener_helper, subject);
//);
}


Listener* _make_listener(ListenerFunc callback, ListenerFilterFunc filter, gpointer udata)
{
  Listener* result = g_slice_new0(Listener);
//  result->callback = callback;
  result->callback = GST_DEBUG_FUNCPTR(callback);
  result->filter = filter;
  result->udata = udata;

  return result;
}

void _dispose_listener(gpointer item)
{
  g_slice_free(Listener, item);
}


