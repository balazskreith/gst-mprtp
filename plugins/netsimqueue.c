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
#include "netsimqueue.h"
#include <math.h>


GST_DEBUG_CATEGORY_STATIC (netsimqueue_debug_category);
#define GST_CAT_DEFAULT netsimqueue_debug_category

G_DEFINE_TYPE (NetsimQueue, netsimqueue, G_TYPE_OBJECT);

struct _NetsimQueueBuffer{
  GstBuffer*      buffer;
  GstClockTime    received;
};

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void netsimqueue_finalize (GObject * object);
static GstBuffer *_pop_buffer(NetsimQueue * this, GstClockTime *received);
static void _insert_into(NetsimQueueBuffer* location, GstBuffer *buffer);
static GstBuffer* _extract_from(NetsimQueueBuffer* location, GstClockTime *received);
static void _unref_location(NetsimQueueBuffer* location);
void _check_max_time_to_desired_time(NetsimQueue *this);
void _check_min_time_to_desired_time(NetsimQueue *this);
//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
netsimqueue_class_init (NetsimQueueClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = netsimqueue_finalize;

  GST_DEBUG_CATEGORY_INIT (netsimqueue_debug_category, "netsimqueue", 0,
      "MpRTP Manual Sending Controller");
}

void
netsimqueue_finalize (GObject * object)
{
  gint i;
  NetsimQueue * this;
  this = NETSIMQUEUE(object);
  for(i=0; i<MAX_NETSIMQUEUEBUFFERS_NUM; ++i){
    g_free(this->buffers[i]);
  }
  g_object_unref(this->sysclock);
}

void
netsimqueue_init (NetsimQueue * this)
{
  gint i;
  for(i=0; i<MAX_NETSIMQUEUEBUFFERS_NUM; ++i){
    this->buffers[i] = g_malloc0(sizeof(NetsimQueueBuffer));
  }
  this->buffers_allowed_max_num = MAX_NETSIMQUEUEBUFFERS_NUM;
  this->drop_policy = NETSIMQUEUE_DROP_POLICY_MILK;
  this->sysclock = gst_system_clock_obtain();

  this->min_time = 0;
  this->max_time = 0;
  this->desired_max_time = 0;
  this->desired_min_time = 0;
}

void netsimqueue_smooth_movement(NetsimQueue * this, gboolean smooth_movement)
{
  this->smooth_movement = smooth_movement;
}

void netsimqueue_set_min_time(NetsimQueue * this, gint min_time_in_ms)
{
  GstClockTime now;
  if(!this->smooth_movement){
      this->desired_min_time = this->min_time = min_time_in_ms * GST_MSECOND;
      goto done;
  }
  now = gst_clock_get_time(this->sysclock);
  this->desired_min_time = min_time_in_ms * GST_MSECOND;
  this->next_min_movement = now + g_random_int_range(1,100) * GST_MSECOND;

done:
  return;
}

void netsimqueue_set_max_time(NetsimQueue * this, gint max_time_in_ms)
{
  GstClockTime now;
  if(!this->smooth_movement){
      this->desired_max_time = this->max_time = max_time_in_ms * GST_MSECOND;
      goto done;
  }

  now = gst_clock_get_time(this->sysclock);
  this->desired_max_time = max_time_in_ms * GST_MSECOND;
  this->next_max_movement = now + g_random_int_range(1,100) * GST_MSECOND;

done:
  return;
}

void netsimqueue_set_drop_policy(NetsimQueue * this, NetsimQueueDropPolicy policy)
{
  this->drop_policy = policy;
}
void netsimqueue_set_max_packets(NetsimQueue * this, guint16 allowed_max_num)
{
  this->buffers_allowed_max_num = allowed_max_num;
}

void netsimqueue_push_buffer(NetsimQueue * this, GstBuffer *buffer)
{
  NetsimQueueBuffer *location;
  if(this->buffers_counter == this->buffers_allowed_max_num){
    if(this->drop_policy == NETSIMQUEUE_DROP_POLICY_MILK){
      GstBuffer *candidate;
      candidate = netsimqueue_pop_buffer(this);
      if(candidate) gst_buffer_unref(candidate);
    } else {
      gst_buffer_unref(buffer);
      goto done;
    }
  }
  if(++this->buffers_write_index == MAX_NETSIMQUEUEBUFFERS_NUM){
    this->buffers_write_index = 0;
  }
  location = this->buffers[this->buffers_write_index];
  _insert_into(location, buffer);
  location->received = gst_clock_get_time(this->sysclock);
  ++this->buffers_counter;
done:
  return;
}

GstBuffer *netsimqueue_pop_buffer(NetsimQueue * this)
{
  GstBuffer *result;
  GstClockTime received;
  GstClockTime now;
  NetsimQueueBuffer *location;
  _check_min_time_to_desired_time(this);
  _check_max_time_to_desired_time(this);
again:
  result = NULL;
  now = gst_clock_get_time(this->sysclock);
  if(this->buffers_counter == 0){
    goto done;
  }
  location = this->buffers[this->buffers_read_index];
  if(this->min_time > 0){
    if(now < location->received + this->min_time){
      goto done;
    }
  }

  result = _pop_buffer(this, &received);
//  g_print("Spent time: %lu-%d-%lu\n",
//          GST_TIME_AS_MSECONDS(now - received), this->buffers_counter, this->min_time);

  if(this->max_time > 0){
    if(received < now - this->max_time){
      gst_buffer_unref(result);
      goto again;
    }
  }
done:
  return result;
}


GstBuffer *_pop_buffer(NetsimQueue * this, GstClockTime *received)
{
  GstBuffer *result;
  NetsimQueueBuffer *location;
  result = NULL;
  location = this->buffers[this->buffers_read_index];
  result = _extract_from(location, received);
  if(++this->buffers_read_index == MAX_NETSIMQUEUEBUFFERS_NUM){
      this->buffers_read_index = 0;
  }
  --this->buffers_counter;
  return result;
}

void _insert_into(NetsimQueueBuffer* location, GstBuffer *buffer)
{
  if(location->buffer) _unref_location(location);
  location->buffer = buffer;
  gst_buffer_ref(buffer);
}

GstBuffer* _extract_from(NetsimQueueBuffer* location, GstClockTime *received)
{
  GstBuffer *result = NULL;
  result = location->buffer;
  location->buffer = NULL;
  if(received) *received = location->received;
  return result;
}

void _unref_location(NetsimQueueBuffer* location)
{
  GstBuffer *buffer;
  buffer = _extract_from(location, NULL);
  if(buffer) gst_buffer_unref(buffer);
}

void _check_min_time_to_desired_time(NetsimQueue *this)
{
  GstClockTime now;
  gint fluctuation;
  if(this->desired_min_time == this->min_time) goto done;
  now = gst_clock_get_time(this->sysclock);
  if(now < this->next_min_movement) goto done;
  this->next_min_movement = now + g_random_int_range(1,600) * GST_MSECOND;
  fluctuation = g_random_int_range(0,10) < 3 ? -1 : 1;
  if(this->min_time < this->desired_min_time)
    this->min_time +=  fluctuation * GST_MSECOND;
  else
    this->min_time -= fluctuation * GST_MSECOND;
  done:
  if(this->min_time < 0)
    this->min_time = 0;
  return;
}

void _check_max_time_to_desired_time(NetsimQueue *this)
{
  GstClockTime now;
  gint fluctuation;
  if(this->desired_max_time == this->max_time) goto done;
  now = gst_clock_get_time(this->sysclock);
  if(now < this->next_max_movement) goto done;
  this->next_max_movement = now + g_random_int_range(1,600) * GST_MSECOND;
  fluctuation = g_random_int_range(0,10) < 3 ? -1 : 1;
  if(this->max_time < this->desired_max_time)
    this->max_time +=  fluctuation * GST_MSECOND;
  else
    this->max_time -= fluctuation * GST_MSECOND;
  done:
  if(this->max_time < 0)
    this->max_time = 0;
  return;
}
