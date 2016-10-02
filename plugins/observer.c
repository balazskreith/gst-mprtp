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
#include "observer.h"
#include "streamsplitter.h"
#include "gstmprtcpbuffer.h"
#include "streamjoiner.h"
#include "ricalcer.h"
#include "mprtplogger.h"
#include "fbrafbprod.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


#define _now(this) (gst_clock_get_time (this->sysclock))

GST_DEBUG_CATEGORY_STATIC (observer_debug_category);
#define GST_CAT_DEFAULT observer_debug_category

G_DEFINE_TYPE (Observer, observer, G_TYPE_OBJECT);

typedef struct{
  void     (*callback)(gpointer udata, gpointer notifyData);
  gpointer   udata;
}Notifier;

typedef struct{
  gpointer (*callback)(gpointer udata);
  gpointer   udata;
}Collector;

static Notifier* _make_notifier(NotifierFunc callback, gpointer udata);
static void _dispose_notifier(gpointer item);

static Notifier* _make_collector(NotifierFunc callback, gpointer udata);
static void _dispose_collector(gpointer item);


//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
observer_finalize (GObject * object);


void
observer_class_init (ObserverClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = observer_finalize;

  GST_DEBUG_CATEGORY_INIT (observer_debug_category, "rndctrler", 0,
      "MpRTP Receiving Controller");

}


void
observer_finalize (GObject * object)
{
  Observer *this = OBSERVER (object);
  if(this->notifiers){
    g_slist_free_full(this->notifiers, _dispose_notifier);
  }
  if(this->collectors){
    g_slist_free_full(this->collectors, _dispose_collector);
  }
}

void
observer_init (Observer * this)
{

}

Observer *make_observer(void)
{
  Observer *result = g_object_new(OBSERVER_TYPE, NULL);
  return result;
}

void observer_add_listener(Observer *this, NotifierFunc callback, gpointer udata)
{
  Notifier *notifier = _make_notifier(callback, udata);
  this->notifiers = g_slist_prepend(this->notifiers, notifier);
}

static gint _find_notifier_by_func(gpointer itemp, gpointer searchedp)
{
  NotifierFunc *searched_callback = searchedp;
  Notifier* notifier = itemp;
  return notifier->callback == searched_callback ? 0 : -1;
}

void observer_rem_listener(Observer *this, NotifierFunc callback)
{
  GSList* it;
  it = g_slist_find_custom(this->notifiers, callback, _find_notifier_by_func);
  if(!it){
    return;
  }
  this->notifiers = g_slist_remove(this->notifiers, it->data);
  _dispose_notifier(it->data);
}


static void _notifiy_helper(gpointer item, gpointer udata)
{
  Notifier* notifier = item;
  notifier->callback(notifier->udata, udata);
}

void observer_notify(Observer *this, gpointer subject)
{
  if(!this){
    return;
  }
  g_slist_foreach(this->notifiers, _notifiy_helper, subject);
}




void observer_add_collector(Observer *this, CollectorFunc callback, gpointer udata)
{
  Collector *collector = _make_collector(callback, udata);
  this->collectors = g_slist_append(this->collectors, collector);
}

static gint _find_collector_by_func(gpointer itemp, gpointer searchedp)
{
  CollectorFunc *searched_callback = searchedp;
  Collector* collector = itemp;
  return collector->callback == searched_callback ? 0 : -1;
}

void observer_rem_collector(Observer *this, CollectorFunc callback)
{
  GSList* it;
  it = g_slist_find_custom(this->collectors, callback, _find_collector_by_func);
  if(!it){
    return;
  }
  this->collectors = g_slist_remove(this->collectors, it->data);
  _dispose_collector(it->data);
}

void observer_collect(Observer *this, ...)
{
  va_list arguments;
  GSList *it;
  gpointer *res_ptr = NULL;
  if(!this){
    return;
  }
  va_start ( arguments, this );
  for(res_ptr = va_arg( arguments, gpointer*), it = this->collectors;
      res_ptr && it;
      res_ptr = va_arg(arguments, gpointer*), it = it->next)
  {
    Collector* collector = it->data;
    *res_ptr = collector->callback(collector->udata);
  }
  va_end ( arguments );
}




Notifier* _make_notifier(NotifierFunc callback, gpointer udata)
{
  Notifier* result = g_slice_new0(Notifier);
  result->callback = callback;
  result->udata = udata;
  return result;
}

void _dispose_notifier(gpointer item)
{
  g_slice_free(Notifier, item);
}




Collector* _make_collector(CollectorFunc callback, gpointer udata)
{
  Collector* result = g_slice_new0(Collector);
  result->callback = callback;
  result->udata = udata;
  return result;
}

void _dispose_collector(gpointer item)
{
  g_slice_free(Collector, item);
}
