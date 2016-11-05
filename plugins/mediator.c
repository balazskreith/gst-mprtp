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
#include "mediator.h"


#define _now(this) (gst_clock_get_time (this->sysclock))

GST_DEBUG_CATEGORY_STATIC (mediator_debug_category);
#define GST_CAT_DEFAULT mediator_debug_category

G_DEFINE_TYPE (Mediator, mediator, G_TYPE_OBJECT);

#define REGULAR_REPORT_PERIOD_TIME (5*GST_SECOND)

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
mediator_finalize (GObject * object);


void
mediator_class_init (MediatorClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = mediator_finalize;

  GST_DEBUG_CATEGORY_INIT (mediator_debug_category, "rndctrler", 0,
      "MpRTP Receiving Controller");

}


void
mediator_finalize (GObject * object)
{
  Mediator *this = MEDIATOR (object);

  g_object_unref(this->on_request);
  g_object_unref(this->on_response);
}

void
mediator_init (Mediator * this)
{
  this->on_request  = make_notifier();
  this->on_response = make_notifier();
}

Mediator *make_mediator(void)
{
  Mediator *result = g_object_new(MEDIATOR_TYPE, NULL);
  return result;
}

void mediator_set_request(Mediator* this, gpointer request)
{
  notifier_do(this->on_request, request);
}


void mediator_set_response(Mediator* this, gpointer response)
{
  notifier_do(this->on_response, response);
}

void mediator_set_request_handler(Mediator *this, ListenerFunc response_cb, gpointer udata)
{
  notifier_add_listener_full(this->on_request, response_cb, udata);
}

void mediator_set_response_handler(Mediator *this, ListenerFunc request_cb, gpointer udata)
{
  notifier_add_listener_full(this->on_response, request_cb, udata);
}
