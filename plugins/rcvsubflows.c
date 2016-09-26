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
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "rcvsubflows.h"


GST_DEBUG_CATEGORY_STATIC (rcvsubflows_debug_category);
#define GST_CAT_DEFAULT rcvsubflows_debug_category

G_DEFINE_TYPE (RcvSubflows, rcvsubflows, G_TYPE_OBJECT);

#define _now(this) gst_clock_get_time (this->sysclock)
typedef struct{
  void    (*callback)(gpointer udata, RcvSubflow* subflow);
  gpointer  udata;
}Notifier;
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------
//Functions realted to rcvsubflows
static void
rcvsubflows_finalize (
    GObject * object);

void _add_notifier(
    GSList **list,
    void (*callback)(gpointer udata, RcvSubflow* subflow),
    gpointer udata);

static Notifier*
_make_notifier(
    void (*callback)(gpointer udata, RcvSubflow* subflow),
    gpointer udata);

static void
_dispose_notifier(
    gpointer data);

static void
_call_notifiers(
    GSList *notifiers,
    RcvSubflow *subflow);

static RcvSubflow*
rcvsubflows_get_subflow(
    RcvSubflows* this,
    guint8 subflow_id);

//----------------------------------------------------------------------
//---- Private function implementations to Stream Dealer object --------
//----------------------------------------------------------------------

void
rcvsubflows_class_init (RcvSubflowsClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = rcvsubflows_finalize;

  GST_DEBUG_CATEGORY_INIT (rcvsubflows_debug_category, "rcvsubflows", 0,
      "Stream Splitter");

}

RcvSubflows* make_rcvsubflows(void)
{
  RcvSubflows *this;
  this = g_object_new (RCVSUBFLOWS_TYPE, NULL);
  return this;
}

void
rcvsubflows_finalize (GObject * object)
{
  RcvSubflows *this = RCVSUBFLOWS (object);
  g_hash_table_destroy (this->subflows);
  g_object_unref (this->sysclock);
  g_free(this->subflows);
}


void
rcvsubflows_init (RcvSubflows * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->subflows               = g_malloc0(sizeof(RcvSubflow) * 256);
  this->made                   = _now(this);
}

void rcvsubflows_on_add_notifications(RcvSubflows* this,void (*callback)(gpointer udata, RcvSubflow* subflow), gpointer udata)
{
  _add_notifier(&this->on_add_notifications, callback, udata);
}

RcvSubflow* rcvsubflows_add(RcvSubflows* this, guint8 id)
{
  RcvSubflow *result;
  if(this->subflows[id] != NULL){
    GST_LOG_OBJECT(this, "The subflow is already exists");
    return this->subflows[id];
  }
  ++this->subflows_num;
  this->added = g_slist_prepend(this->added, this->subflows[id]);

  this->subflows[id] = result = g_malloc0(sizeof(RcvSubflow));

  _call_notifiers(this->on_add_notifications, result);

  return result;

}

void rcvsubflows_rem(RcvSubflows* this, guint8 id)
{
  RcvSubflow* subflow;
  if(this->subflows[id] == NULL){
    GST_LOG_OBJECT(this, "The subflow is not exists");
  }
  subflow = this->subflows[id];
  this->added = g_slist_remove(this->added, subflow);

  _call_notifiers(subflow->notifiers.on_removing);

  g_free(this->subflows[id]);
  this->subflows[id] = NULL;
  --this->subflows_num;
}

void rcvsubflows_iterate(RcvSubflows* this, GFunc process, gpointer udata)
{
  if(!this->added){
    return;
  }
  g_slist_foreach(this->added, process, udata);
}


RcvSubflow* rcvsubflows_get_subflow(RcvSubflows* this, guint8 subflow_id)
{
  return this->subflows + subflow_id;
}

void rcvsubflow_add_removal_notification(RcvSubflow* subflow, void (*callback)(gpointer udata, RcvSubflow* subflow), gpointer udata)
{
  _add_notifier(&subflow->notifiers.on_removing, callback, udata);
}



void _add_notifier(GSList **list, void (*callback)(gpointer udata, RcvSubflow* subflow), gpointer udata)
{
  *list = g_slist_prepend(*list, _make_notifier(callback, udata));
}

Notifier* _make_notifier(void (*callback)(gpointer udata, RcvSubflow* subflow), gpointer udata)
{
  Notifier* result =  g_slice_new0(Notifier);
  result->udata = udata;
  result->callback = callback;
  return result;
}

void _dispose_notifier(gpointer data)
{
  if(!data){
    return;
  }
  Notifier* notifier = data;
  g_slice_free(Notifier, notifier);
}

static void _call_notifiers(GSList *notifiers, RcvSubflow *subflow)
{
  GSList *it;
  if(!notifiers){
    return;
  }

  for(it = notifiers; it; it = it->next){
    Notifier* notifier = it->data;
    notifier->callback(notifier->udata, subflow);
  }
}


