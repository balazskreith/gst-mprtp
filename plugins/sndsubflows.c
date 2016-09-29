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
#include "sndsubflows.h"


GST_DEBUG_CATEGORY_STATIC (sndsubflows_debug_category);
#define GST_CAT_DEFAULT sndsubflows_debug_category

G_DEFINE_TYPE (SndSubflows, sndsubflows, G_TYPE_OBJECT);

#define CHANGE_SUBFLOW_PROPERTY_VALUE(subflows_list, subflow_id, property, property_value, changed_subflows) \
{                                                     \
  if(changed_subflows){                               \
    g_queue_clear(changed_subflows);                  \
  }                                                   \
  GSList* it;                                         \
  for(it = subflows_list; it; it = it->next)          \
  {                                                   \
    SndSubflow* subflow = it->data;                   \
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

#define _now(this) gst_clock_get_time (this->sysclock)
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------
//Functions realted to sndsubflows
static void
sndsubflows_finalize (
    GObject * object);


static SndSubflow*
_make_subflow(SndSubflows* base_db, guint8 subflow_id);

static void
_dispose_subflow(SndSubflow *subflow);


//----------------------------------------------------------------------
//---- Private function implementations to Stream Dealer object --------
//----------------------------------------------------------------------

void
sndsubflows_class_init (SndSubflowsClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = sndsubflows_finalize;

  GST_DEBUG_CATEGORY_INIT (sndsubflows_debug_category, "sndsubflows", 0,
      "Stream Splitter");

}

SndSubflows* make_sndsubflows(void)
{
  SndSubflows *this;
  this = g_object_new (SNDSUBFLOWS_TYPE, NULL);
  return this;
}

void
sndsubflows_finalize (GObject * object)
{
  SndSubflows *this = SNDSUBFLOWS (object);
  g_queue_clear(this->changed_subflows);
  g_object_unref (this->sysclock);

  g_object_unref(this->on_subflow_detached);
  g_object_unref(this->on_subflow_joined);
  g_object_unref(this->on_congestion_controlling_type_changed);
  g_object_unref(this->on_path_active_changed);

  g_object_unref(this->changed_subflows);

  g_free(this->subflows);
}


void
sndsubflows_init (SndSubflows * this)
{
  this->sysclock            = gst_system_clock_obtain ();
  this->made                = _now(this);

  this->on_subflow_detached                     = make_observer();
  this->on_subflow_joined                       = make_observer();
  this->on_congestion_controlling_type_changed  = make_observer();
  this->on_path_active_changed                  = make_observer();

  this->changed_subflows                        = g_queue_new();

}

void sndsubflows_join(SndSubflows* this, guint8 id)
{
  SndSubflow *subflow;
  subflow = _make_subflow(this, id);

  this->subflows[id] = subflow;
  ++this->subflows_num;

  this->joined = g_slist_prepend(this->joined, subflow);

  observer_notify(this->on_subflow_joined, subflow);

}

void sndsubflows_detach(SndSubflows* this, guint8 id)
{
  SndSubflow* subflow;
  subflow = this->subflows[id];

  observer_notify(this->on_subflow_detached, subflow);

  this->joined = g_slist_remove(this->joined, subflow);

  --this->subflows_num;
  this->subflows[id] = NULL;

  _dispose_subflow(subflow);

}

void sndsubflows_iterate(SndSubflows* this, GFunc process, gpointer udata)
{
  if(!this->joined){
    return;
  }
  g_slist_foreach(this->joined, process, udata);
}

void sndsubflows_add_on_subflow_joined_cb(SndSubflows* this, NotifierFunc callback, gpointer udata)
{
  observer_add_listener(this->on_subflow_joined, callback, udata);
}

void sndsubflows_add_on_subflow_detached_cb(SndSubflows* this, NotifierFunc callback, gpointer udata)
{
  observer_add_listener(this->on_subflow_detached, callback, udata);
}

void sndsubflows_add_on_congestion_controlling_type_changed_cb(SndSubflows* this, NotifierFunc callback, gpointer udata)
{
  observer_add_listener(this->on_congestion_controlling_type_changed, callback, udata);
}

void sndsubflows_add_on_path_active_changed_cb(SndSubflows* this, NotifierFunc callback, gpointer udata)
{
  observer_add_listener(this->on_path_active_changed, callback, udata);
}

void sndsubflows_set_target_rate(SndSubflows* this, SndSubflow* subflow, gint32 target_rate)
{
  this->target_rate -= subflow->target_bitrate;
  this->target_rate += subflow->target_bitrate = target_rate;
}

gint32 sndsubflows_get_total_target(SndSubflows* this)
{
  return this->target_rate;
}

gint32 sndsubflows_get_subflows_num(SndSubflows* this)
{
  return this->subflows_num;
}

SndSubflow* sndsubflows_get_subflow(SndSubflows* this, guint8 subflow_id)
{
  return this->subflows + subflow_id;
}


void sndsubflows_set_congestion_controlling_type(SndSubflows* this, guint8 subflow_id, CongestionControllingType new_type)
{
  CHANGE_SUBFLOW_PROPERTY_VALUE(this->subflows, subflow_id, congestion_controlling_type, new_type, this->changed_subflows);
  NOTIFY_CHANGED_SUBFLOWS(this->changed_subflows, this->on_congestion_controlling_type_changed);
}

void sndsubflows_set_path_active(SndSubflows* this, guint8 subflow_id, gboolean value)
{
  CHANGE_SUBFLOW_PROPERTY_VALUE(this->subflows, subflow_id, active, value, this->changed_subflows);
  NOTIFY_CHANGED_SUBFLOWS(this->changed_subflows, this->on_path_active_changed);
}

void sndsubflows_set_rtcp_interval_type(SndSubflows* this, guint8 subflow_id, RTCPIntervalType new_type)
{
  CHANGE_SUBFLOW_PROPERTY_VALUE(this->subflows, subflow_id, rtcp_interval_type, new_type, NULL);
}

void sndsubflows_set_path_lossy(SndSubflows* this, guint8 subflow_id, gboolean value)
{
  CHANGE_SUBFLOW_PROPERTY_VALUE(this->subflows, subflow_id, lossy, value, NULL);
}

void sndsubflows_set_path_congested(SndSubflows* this, guint8 subflow_id, gboolean value)
{
  CHANGE_SUBFLOW_PROPERTY_VALUE(this->subflows, subflow_id, congested, value, NULL);
}

//-----------------------------------------------------------------------------------------------------------

void sndsubflow_on_stat_changed_cb(SndSubflow* subflow, NotifierFunc callback, gpointer udata)
{
  observer_add_listener(subflow->on_stat_changed, callback, udata);
}

void sndsubflow_set_state(SndSubflow* subflow, SndSubflowState state)
{
  subflow->state = state;
}

SndSubflowState sndsubflow_get_state(SndSubflow* subflow)
{
  return subflow->state;
}

void sndsubflow_add_on_packet_sent_cb(SndSubflow* subflow, NotifierFunc callback, gpointer udata)
{
  observer_add_listener(subflow->on_packet_sent, callback, udata);
}

guint16 sndsubflow_get_next_subflow_seq(SndSubflow* subflow)
{
  return subflowseqtracker_increase(&subflow->seqtracker);
}

guint8 sndsubflow_get_flags_abs_value(SndSubflow* subflow)
{
  return (subflow->active ? 4 : 0) + (subflow->lossy ? 0 : 2) + (subflow->congested ? 0 : 1);
}

gboolean sndsubflow_fec_requested(SndSubflow* subflow)
{
  if(!subflow->fec_interval){
    return FALSE;
  }
  return subflow->seqtracker.seqence_num % subflow->fec_interval == 0;
}




SndSubflow* _make_subflow(SndSubflows* base_db, guint8 subflow_id)
{
  SndSubflow* result = g_malloc0(sizeof(SndSubflow));
  result->base_db         = base_db;
  result->on_packet_sent  = make_observer();
  result->on_stat_changed = make_observer();
  return result;
}

void _dispose_subflow(SndSubflow *subflow)
{
  g_object_unref(subflow->on_packet_sent);
  g_object_unref(subflow->on_stat_changed);
  g_free(subflow);
}



#undef CHANGE_SUBFLOW_PROPERTY_VALUE
