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
#include "packetforwarder.h"

GST_DEBUG_CATEGORY_STATIC (packetforwarder_debug_category);
#define GST_CAT_DEFAULT packetforwarder_debug_category

/* class initialization */
G_DEFINE_TYPE (PacketForwarder, packetforwarder, G_TYPE_OBJECT);

typedef struct{
  GstPad *rtppad;
  GstPad *rtcppad;
}PacketForwarderPrivate;

#define _now(this) gst_clock_get_time (this->sysclock)
#define _priv(this) ((PacketForwarderPrivate*)(this->priv))

static void packetforwarder_finalize (GObject * object);

static void _process(gpointer udata);
static void _start(PacketForwarder* this);
static void _stop(PacketForwarder* this);
//----------------------------------------------------------------------
//---- Private function implementations to Stream Dealer object --------
//----------------------------------------------------------------------


void
packetforwarder_class_init (PacketForwarderClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = packetforwarder_finalize;

  GST_DEBUG_CATEGORY_INIT (packetforwarder_debug_category, "packetforwarder", 0,
      "MPRTP Packet Forwarder Component");

}

void
packetforwarder_finalize (GObject * object)
{
  PacketForwarder *this;
  GstBuffer* buffer;
  this = PACKETFORWARDER(object);

  _stop(this);

  g_object_unref(this->sysclock);
  //Deleting
  while((buffer = (GstBuffer*)g_async_queue_try_pop(this->mprtpq)) != NULL){
    gst_buffer_unref(buffer);
  }

  while((buffer = (GstBuffer*)g_async_queue_try_pop(this->mprtcpq)) != NULL){
    gst_buffer_unref(buffer);
  }

  g_async_queue_unref(this->mprtcpq);
  g_async_queue_unref(this->mprtpq);
}


void
packetforwarder_init (PacketForwarder * this)
{
  this->sysclock = gst_system_clock_obtain();
  this->mprtpq   = g_async_queue_new();
  this->mprtcpq  = g_async_queue_new();

}

PacketForwarder* make_packetforwarder(GstPad* rtppad, GstPad* rtcppad, gboolean async)
{
  PacketForwarder *this;
  this = g_object_new (PACKETFORWARDER_TYPE, NULL);
  this->priv = g_malloc0(sizeof(PacketForwarderPrivate));
  _priv(this)->rtcppad = rtcppad;
  _priv(this)->rtppad  = rtppad;
  _start(this);
  return this;
}

void packetforwarder_add_rtppad_buffer(PacketForwarder* this, GstBuffer *buffer)
{
  g_async_queue_push(this->mprtpq, buffer);
}

void packetforwarder_add_rtcppad_buffer(PacketForwarder* this, GstBuffer *buffer)
{
  g_async_queue_push(this->mprtcpq, buffer);
}

void _start(PacketForwarder* this)
{
  this->thread = gst_task_new (_process, this, NULL);
  gst_task_set_lock (this->thread, &this->thread_mutex);
  gst_task_start (this->thread);
}

void _stop(PacketForwarder* this)
{
  gst_task_stop (this->thread);
  gst_task_join (this->thread);
  this->thread = NULL;
}

static void _process(gpointer udata)
{
  PacketForwarder* this = udata;
  GstBuffer* buffer;
  gboolean repeat = FALSE;
again:
  buffer = g_async_queue_timeout_pop(this->mprtpq, 100);
  if(buffer){
    gst_pad_push(_priv(this)->rtppad, buffer);
    repeat = TRUE;
  }
  buffer = g_async_queue_timeout_pop(this->mprtcpq, 100);
  if(buffer){
    gst_pad_push(_priv(this)->rtcppad, buffer);
    repeat = TRUE;
  }

  if(repeat){
    goto again;
  }

  return;
}
