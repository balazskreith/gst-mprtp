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
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "streamjoiner.h"
#include "mprtplogger.h"


GST_DEBUG_CATEGORY_STATIC (stream_joiner_debug_category);
#define GST_CAT_DEFAULT stream_joiner_debug_category

#define _now(this) gst_clock_get_time (this->sysclock)

G_DEFINE_TYPE (StreamJoiner, stream_joiner, G_TYPE_OBJECT);


//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
stream_joiner_finalize (
    GObject * object);

//
//static gint
//_cmp_seq (guint16 x, guint16 y)
//{
//  if(x == y) return 0;
//  if(x < y && y - x < 32768) return -1;
//  if(x > y && x - y > 32768) return -1;
//  if(x < y && y - x > 32768) return 1;
//  if(x > y && x - y < 32768) return 1;
//  return 0;
//}
//
//static gint
//_cmp_ts (guint32 x, guint32 y)
//{
//  if(x == y) return 0;
//  if(x < y && y - x < 2147483648) return -1;
//  if(x > y && x - y > 2147483648) return -1;
//  if(x < y && y - x > 2147483648) return 1;
//  if(x > y && x - y < 2147483648) return 1;
//  return 0;
//}
//

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
stream_joiner_class_init (StreamJoinerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = stream_joiner_finalize;

  GST_DEBUG_CATEGORY_INIT (stream_joiner_debug_category, "stream_joiner", 0,
      "MpRTP Manual Sending Controller");
}

void
stream_joiner_finalize (GObject * object)
{
  StreamJoiner *this = STREAM_JOINER (object);
  RcvPacket* packet;
  while((packet = g_queue_pop_head(this->joinq)) != NULL){
    rcvpacket_unref(packet);
  }
  g_object_unref(this->joinq);
  g_object_unref(this->sysclock);
}


void
stream_joiner_init (StreamJoiner * this)
{
  this->sysclock           = gst_system_clock_obtain ();
  this->made               = _now(this);

}

StreamJoiner*
make_stream_joiner(void)
{
  StreamJoiner *result;
  result = (StreamJoiner *) g_object_new (STREAM_JOINER_TYPE, NULL);

  result->joinq = g_queue_new();


  return result;
}

void stream_joiner_push_packet(StreamJoiner *this, RcvPacket* packet)
{
  g_queue_push_tail(this->joinq, packet);
}

RcvPacket* stream_joiner_pop_packet(StreamJoiner *this)
{
  RcvPacket* packet = NULL;

  if(g_queue_is_empty(this->joinq)){
    goto done;
  }

  packet = g_queue_pop_head(this->joinq);

done:
  return packet;

}

