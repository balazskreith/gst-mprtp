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
#include "jitterbuffer.h"
#include "mprtplogger.h"


GST_DEBUG_CATEGORY_STATIC (jitterbuffer_debug_category);
#define GST_CAT_DEFAULT jitterbuffer_debug_category

#define _now(this) gst_clock_get_time (this->sysclock)

G_DEFINE_TYPE (JitterBuffer, jitterbuffer, G_TYPE_OBJECT);


//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
jitterbuffer_finalize (
    GObject * object);

//
static gint
_cmp_seq (guint16 x, guint16 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 32768) return -1;
  if(x > y && x - y > 32768) return -1;
  if(x < y && y - x > 32768) return 1;
  if(x > y && x - y < 32768) return 1;
  return 0;
}

static gint _cmp_rcvpacket_abs_seq(RcvPacket *a, RcvPacket* b, gpointer udata)
{
  return _cmp_seq(a->abs_seq, b->abs_seq);
}

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
jitterbuffer_class_init (JitterBufferClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = jitterbuffer_finalize;

  GST_DEBUG_CATEGORY_INIT (jitterbuffer_debug_category, "jitterbuffer", 0,
      "MpRTP Manual Sending Controller");
}

void
jitterbuffer_finalize (GObject * object)
{
  JitterBuffer *this = JITTERBUFFER (object);
  RcvPacket* packet;
  while((packet = g_queue_pop_head(this->playoutq)) != NULL){
    rcvpacket_unref(packet);
  }
  g_object_unref(this->playoutq);
  g_object_unref(this->sysclock);
}


void
jitterbuffer_init (JitterBuffer * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->made     = _now(this);
  this->playoutq    = g_queue_new();

}

JitterBuffer*
make_jitterbuffer(Mediator* repair_channel)
{
  JitterBuffer *result;
  result = (JitterBuffer *) g_object_new (JITTERBUFFER_TYPE, NULL);

  result->repair_channel = g_object_ref(repair_channel);

  return result;
}

void jitterbuffer_push_packet(JitterBuffer *this, RcvPacket* packet)
{
  g_queue_insert_sorted(this->playoutq, packet, (GCompareDataFunc) _cmp_rcvpacket_abs_seq, NULL);
}

RcvPacket* jitterbuffer_pop_packet(JitterBuffer *this, gboolean *repair_request)
{
  RcvPacket* packet = NULL;
  gint cmp;

  if(g_queue_is_empty(this->playoutq)){
    goto done;
  }

  packet = g_queue_pop_head(this->playoutq);
  if(!this->last_seq_init){
    this->last_seq = packet->abs_seq;
    this->last_seq_init = TRUE;
    goto done;
  }
  cmp = _cmp_seq(this->last_seq + 1, packet->abs_seq);
  if(0 < cmp){
    goto done;
  }
  while(cmp < 0){
    DiscardedPacket* discarded_packet;
    discarded_packet = g_slice_new0(DiscardedPacket);
    discarded_packet->abs_seq = ++this->last_seq;
    mediator_set_request(this->repair_channel, discarded_packet);
    if(repair_request){
      *repair_request = TRUE;
    }
    cmp = _cmp_seq(this->last_seq, packet->abs_seq);
  }
  this->last_seq = packet->abs_seq;
done:
  return packet;

}


