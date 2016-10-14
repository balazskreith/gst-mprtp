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
#include "messenger.h"


#define _now(this) (gst_clock_get_time (this->sysclock))

GST_DEBUG_CATEGORY_STATIC (messenger_debug_category);
#define GST_CAT_DEFAULT messenger_debug_category

G_DEFINE_TYPE (Messenger, messenger, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
messenger_finalize (GObject * object);

void
messenger_class_init (MessengerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = messenger_finalize;

  GST_DEBUG_CATEGORY_INIT (messenger_debug_category, "rndctrler", 0,
      "MpRTP Receiving Controller");

}


void
messenger_finalize (GObject * object)
{
  Messenger *this = MESSENGER (object);
  while(!g_queue_is_empty(this->messages)){
    g_slice_free1(this->block_size, g_queue_pop_head(this->messages));
  }
  g_object_unref(this->messages);
  while(!g_queue_is_empty(this->recycle)){
    g_slice_free1(this->block_size, g_queue_pop_head(this->recycle));
  }
  g_object_unref(this->recycle);
}

void
messenger_init (Messenger * this)
{
  g_mutex_init(&this->mutex);
  g_cond_init(&this->cond);
  this->messages = g_queue_new();
  this->recycle  = g_queue_new();
}

Messenger *make_messenger(gsize block_size)
{
  Messenger *result = g_object_new(MESSENGER_TYPE, NULL);
  result->block_size = block_size;
  return result;
}


gpointer messenger_pop (Messenger *this)
{
  gpointer result = NULL;

  g_mutex_lock (&this->mutex);
  while (!result){
    g_cond_wait (&this->cond, &this->mutex);
    if(g_queue_is_empty(this->messages)){
      continue;
    }
    result = g_queue_pop_head(this->messages);
  }
  g_mutex_unlock (&this->mutex);
  return result;
}


gpointer messenger_try_pop(Messenger *this)
{
  gpointer result = NULL;
  g_mutex_lock(&this->mutex);
  if(!g_queue_is_empty(this->messages)){
    result = g_queue_pop_head(this->messages);
  }
  g_mutex_unlock(&this->mutex);
  return result;
}


gpointer messenger_pop_with_timeout (Messenger *this, gint64 microseconds)
{
  gint64 end_time;
  gpointer result = NULL;

  g_mutex_lock (&this->mutex);
  end_time = g_get_monotonic_time () + microseconds;
  while (g_queue_is_empty(this->messages)){
    if (!g_cond_wait_until (&this->cond, &this->mutex, end_time)){
      // timeout has passed.
      goto done;
    }
  }
  result = g_queue_pop_head(this->messages);
done:
  g_mutex_unlock (&this->mutex);
  return result;
}


void messenger_push(Messenger* this, gpointer message)
{
  gboolean was_empty;
  g_mutex_lock(&this->mutex);
  was_empty = g_queue_is_empty(this->messages);
  g_queue_push_tail(this->messages, message);
  if(was_empty){
    g_cond_signal(&this->cond);
  }
  g_mutex_unlock(&this->mutex);
}


void messenger_throw_block(Messenger* this, gpointer message)
{
  g_mutex_lock(&this->mutex);
  memset(message, 0, this->block_size);
  g_queue_push_tail(this->recycle, message);
  g_mutex_unlock(&this->mutex);
}


gpointer messenger_retrieve_block(Messenger *this)
{
  gpointer result;
  g_mutex_lock(&this->mutex);
  if(g_queue_is_empty(this->recycle)){
    result = g_slice_alloc0(this->block_size);
  }else{
    result = g_queue_pop_head(this->recycle);
  }
  //shape
  g_mutex_unlock(&this->mutex);
  return result;
}


//------------------------------------------------------------




