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
#include "streamsplitter.h"
#include "mprtpspath.h"
#include <string.h>
#include <stdio.h>
#include <math.h>


GST_DEBUG_CATEGORY_STATIC (packetsender_debug_category);
#define GST_CAT_DEFAULT packetsender_debug_category

/* class initialization */
G_DEFINE_TYPE (PacketSender, packetsender, G_TYPE_OBJECT);

typedef struct{
  GstPad *rtppad;
  GstPad *rtcppad;
}PacketSenderPrivate;

#define _now(this) gst_clock_get_time (this->sysclock)
#define _priv(this) ((PacketSenderPrivate*)(this->priv))

static void _process(gpointer udata);
static void _start(PacketSender* this);
static void _stop(PacketSender* this);
//----------------------------------------------------------------------
//---- Private function implementations to Stream Dealer object --------
//----------------------------------------------------------------------


void
packetsender_class_init (PacketSenderClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = packetsender_finalize;

  GST_DEBUG_CATEGORY_INIT (packetsender_debug_category, "packetsender", 0,
      "MPRTP Packet Sender Component");

}

void
packetsender_finalize (GObject * object)
{
  PacketSender *this;
  GstBuffer* buffer;
  this = PACKETSRCVQUEUE(object);

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
packetsender_init (PacketSender * this)
{
  this->sysclock = gst_system_clock_obtain();
  this->mprtpq   = g_async_queue_new();
  this->mprtcpq  = g_async_queue_new();

}

PacketSender* make_packetsender(GstPad* rtppad, GstPad* rtcppad)
{
  PacketSender *this;
  this = g_object_new (PACKETSRCVQUEUE_TYPE, NULL);
  this->priv = g_malloc0(sizeof(PacketSenderPrivate));
  _priv(this)->rtcppad = rtcppad;
  _priv(this)->rtppad  = rtppad;
  _start(this);
  return this;
}

void packetsender_add_rtppad_buffer(PacketSender* this, GstBuffer *buffer)
{
  g_async_queue_push(this->mprtpq, buffer);
}

void packetsender_add_rtcppad_buffer(PacketSender* this, GstBuffer *buffer)
{
  g_async_queue_push(this->mprtcpq, buffer);
}

void _start(PacketSender* this)
{
  this->thread = gst_task_new (_process, this, NULL);
  gst_task_set_lock (this->thread, &this->thread_mutex);
  gst_task_start (this->thread);
}

void _stop(PacketSender* this)
{
  gst_task_stop (this->thread);
  gst_task_join (this->thread);
  this->thread = NULL;
}

void _process(gpointer udata)
{
  PacketSender* this = udata;
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
done:
  return;
}
