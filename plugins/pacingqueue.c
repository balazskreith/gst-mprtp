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
#include "pacingqueue.h"
#include "mprtplogger.h"


GST_DEBUG_CATEGORY_STATIC (pacing_queue_debug_category);
#define GST_CAT_DEFAULT pacing_queue_debug_category

#define _now(this) gst_clock_get_time (this->sysclock)

G_DEFINE_TYPE (PacingQueue, pacing_queue, G_TYPE_OBJECT);


//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
pacing_queue_finalize (
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

static gint _cmp_sndpacket_abs_seq(SndPacket *a, SndPacket* b, gpointer udata)
{
  return _cmp_seq(a->pacing_time, b->pacing_time);
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
pacing_queue_class_init (PacingQueueClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = pacing_queue_finalize;

  GST_DEBUG_CATEGORY_INIT (pacing_queue_debug_category, "pacing_queue", 0,
      "MpRTP Manual Sending Controller");
}

void
pacing_queue_finalize (GObject * object)
{
  PacingQueue *this = PACINGQUEUE (object);
  SndPacket* packet;
  while((packet = g_queue_pop_head(this->sendingq)) != NULL){
    sndpacket_unref(packet);
  }
  g_object_unref(this->sendingq);
  g_object_unref(this->sysclock);
}


void
pacing_queue_init (PacingQueue * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->made     = _now(this);
  this->sendingq    = g_queue_new();

}

PacingQueue*
make_pacing_queue(Mediator* repair_channel)
{
  PacingQueue *result;
  result = (PacingQueue *) g_object_new (PACINGQUEUE_TYPE, NULL);

  result->repair_channel = g_object_ref(repair_channel);

  return result;
}

void pacing_queue_push_packet(PacingQueue *this, SndPacket* packet)
{
  g_queue_insert_sorted(this->sendingq, packet, (GCompareDataFunc) _cmp_sndpacket_abs_seq, NULL);
}

SndPacket* pacing_queue_pop_packet(PacingQueue *this, GstClockTime *pacing_time)
{
  SndPacket* packet = NULL;
  gint cmp;
  GstClockTime pace_time;

  if(g_queue_is_empty(this->sendingq)){
    goto done;
  }
  packet = g_queue_peek_head(this->sendingq);
  pace_time = this->pacing_times[packet->subflow_id];

  if(_now(this) < pace_time){
    goto done;
  }
  packet = g_queue_pop_head(this->sendingq);
  
  if(g_queue_is_empty(this->sendingq)){
    if(pacing_time) {
      *pacing_time = 0;
    }
    goto done;
  }

done:
  return packet;

}


