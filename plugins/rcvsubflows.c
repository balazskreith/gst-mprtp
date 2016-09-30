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


#define CHANGE_SUBFLOW_PROPERTY_VALUE(subflows_list, subflow_id, property, property_value, changed_subflows) \
{                                                     \
  if(changed_subflows){                               \
    g_queue_clear(changed_subflows);                  \
  }                                                   \
  GSList* it;                                         \
  for(it = subflows_list; it; it = it->next)          \
  {                                                   \
    RcvSubflow* subflow = it->data;                   \
    if(subflow_id == 0 || subflow_id == 255){         \
      subflow->property = property_value;             \
      if(changed_subflows){                           \
        g_queue_push_head(changed_subflows, subflow); \
      }                                               \
      continue;                                       \
    }                                                 \
    if(subflow->id == subflow_id){                    \
      subflow->property = property_value;             \
      if(changed_subflows){                           \
        g_queue_push_head(changed_subflows, subflow); \
      }                                               \
      break;                                          \
    }                                                 \
  }                                                   \
}                                                     \

#define NOTIFY_CHANGED_SUBFLOWS(changed_subflows, observer)        \
{                                                                  \
  while(!g_queue_is_empty(changed_subflows)){                      \
    observer_notify(observer, g_queue_pop_head(changed_subflows)); \
  }                                                                \
}                                                                  \


//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------
//Functions realted to rcvsubflows
static void
rcvsubflows_finalize (
    GObject * object);


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
  g_queue_clear(this->changed_subflows);
  g_object_unref (this->sysclock);

  g_object_unref(this->on_subflow_detached);
  g_object_unref(this->on_subflow_joined);
  g_object_unref(this->on_congestion_controlling_type_changed);

  g_object_unref(this->changed_subflows);

  g_free(this->subflows);
}


void
rcvsubflows_init (RcvSubflows * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->made                   = _now(this);

  this->on_subflow_detached                     = make_observer();
  this->on_subflow_joined                       = make_observer();
  this->on_congestion_controlling_type_changed  = make_observer();

  this->changed_subflows                        = g_queue_new();
}

void rcvsubflows_join(RcvSubflows* this, guint8 id)
{
  RcvSubflow *subflow;
  subflow = _make_subflow(this, id);

  this->subflows[id] = subflow;
  ++this->subflows_num;

  this->joined = g_slist_prepend(this->joined, subflow);

  observer_notify(this->on_subflow_joined, subflow);

}

void rcvsubflows_detach(RcvSubflows* this, guint8 id)
{
  RcvSubflow* subflow;
  subflow = this->subflows[id];

  observer_notify(this->on_subflow_detached, subflow);

  this->joined = g_slist_remove(this->joined, subflow);

  --this->subflows_num;
  this->subflows[id] = NULL;

  _dispose_subflow(subflow);

}

void rcvsubflows_iterate(RcvSubflows* this, GFunc process, gpointer udata)
{
  if(!this->joined){
    return;
  }
  g_slist_foreach(this->joined, process, udata);
}

void rcvsubflows_add_on_subflow_joined_cb(RcvSubflows* this, NotifierFunc callback, gpointer udata)
{
  observer_add_listener(this->on_subflow_joined, callback, udata);
}

void rcvsubflows_add_on_subflow_detached_cb(RcvSubflows* this, NotifierFunc callback, gpointer udata)
{
  observer_add_listener(this->on_subflow_detached, callback, udata);
}

void rcvsubflows_add_on_congestion_controlling_type_changed_cb(RcvSubflows* this, NotifierFunc callback, gpointer udata)
{
  observer_add_listener(this->on_congestion_controlling_type_changed, callback, udata);
}

void rcvsubflow_notify_rtcp_fb_cbs(RcvSubflow* subflow, gpointer udata)
{
  observer_notify(subflow->on_rtcp_time_update, udata);
}

void rcvsubflow_add_on_rtcp_fb_cb(RcvSubflow* subflow, NotifierFunc callback, gpointer udata)
{
  observer_add_listener(subflow->on_rtcp_time_update, callback, udata);
}

void rcvsubflow_rem_on_rtcp_fb_cb(RcvSubflow* subflow, NotifierFunc callback)
{
  observer_rem_listener(subflow->on_rtcp_time_update, callback);
}

RcvSubflow* rcvsubflows_get_subflow(RcvSubflows* this, guint8 subflow_id)
{
  return this->subflows + subflow_id;
}

gint32 rcvsubflows_get_subflows_num(RcvSubflows* this)
{
  return this->subflows_num;
}

void rcvsubflows_set_congestion_controlling_type(RcvSubflows* this, guint8 subflow_id, CongestionControllingType new_type)
{
  CHANGE_SUBFLOW_PROPERTY_VALUE(this->subflows, subflow_id, congestion_controlling_type, new_type, this->changed_subflows);
  NOTIFY_CHANGED_SUBFLOWS(this->changed_subflows, this->on_congestion_controlling_type_changed);
}

void rcvsubflows_set_rtcp_interval_type(RcvSubflows* this, guint8 subflow_id, RTCPIntervalType new_type)
{
  CHANGE_SUBFLOW_PROPERTY_VALUE(this->subflows, subflow_id, rtcp_interval_type, new_type, NULL);
}


RcvSubflow* _make_subflow(RcvSubflows* base_db, guint8 subflow_id)
{
  RcvSubflow* result = g_malloc0(sizeof(RcvSubflow));
  result->base_db                  = base_db;
  result->on_rtcp_time_update  = make_observer();
  return result;
}

void _dispose_subflow(RcvSubflow *subflow)
{
  g_object_unref(subflow->on_rtcp_time_update);
  g_free(subflow);
}



#undef CHANGE_SUBFLOW_PROPERTY_VALUE
