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
typedef enum{
  STREAMJOINER_MESSAGE_RTPPACKET                 = 1,
  STREAMJOINER_MESSAGE_STAT                      = 2,
  STREAMJOINER_MESSAGE_JOINDELAY                 = 3,
}MessageTypes;

typedef struct{
  MessageTypes type;
}Message;

typedef struct{
  Message    base;
  GstBuffer *buffer;
  guint32    ts;
  guint16    abs_seq;

}RTPBufferMessage;

typedef struct{
  Message            base;
  RcvTrackerStat     stat;
}StatMessage;

typedef struct{
  Message            base;
  GstClockTime       join_delay;
}JoinDelayMessage;

static void
stream_joiner_finalize (
    GObject * object);

static gboolean
_has_packet_to_playout(
    StreamJoiner *this,
    GstClockTime elapsed_time);

static RTPPacket*
_pop_packet(
    StreamJoiner *this);

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

static gint
_cmp_ts (guint32 x, guint32 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 2147483648) return -1;
  if(x > y && x - y > 2147483648) return -1;
  if(x < y && y - x > 2147483648) return 1;
  if(x > y && x - y < 2147483648) return 1;
  return 0;
}

static gint _playoutq_sort_helper(gconstpointer a, gconstpointer b, gpointer user_data)
{
  const RTPPacket *ai = a;
  const RTPPacket *bi = b;
  return _cmp_seq(ai->abs_seq, bi->abs_seq);
}


static void _joinq_rem_pipe(gpointer udata, gpointer item)
{
  StreamJoiner *this = udata;
  RTPBufferMessage* msg = item;

  g_queue_insert_sorted(this->playoutq, msg, _playoutq_sort_helper, NULL);
}

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
  g_object_unref(this->repair_channel);
  g_object_unref(this->sysclock);
}


void
stream_joiner_init (StreamJoiner * this)
{
  this->sysclock           = gst_system_clock_obtain ();
  this->made               = _now(this);
  this->join_delay         = 0;

}

StreamJoiner*
make_stream_joiner(Mediator* repair_channel)
{
  StreamJoiner *result;
  result = (StreamJoiner *) g_object_new (STREAM_JOINER_TYPE, NULL);


  result->repair_channel          = g_object_ref(repair_channel);

  result->joinq                   = make_slidingwindow(100, result->join_delay);
  result->playoutq                = g_queue_new();

  slidingwindow_add_on_rem_item_cb(result->joinq, _joinq_rem_pipe, result);
  return result;
}

void stream_joiner_push_packet(StreamJoiner *this, RTPPacket* packet)
{
  g_queue_insert_sorted(this->playoutq, packet, _playoutq_sort_helper, NULL);
}

void stream_joiner_on_rcvtracker_stat_change(StreamJoiner *this, RcvTrackerStat* stat)
{
  GstClockTime now = _now(this);
  if(this->last_playout_time_refreshed + 20 * GST_MSECOND < now){
    return;
  }

  if(this->playout_delay == 0.){
    this->playout_delay = stat->max_skew;
  }else{
    this->playout_delay = (stat->max_skew + 255. * this->playout_delay) / 256.;
  }

  this->last_playout_time_refreshed = now;
}

void stream_joiner_set_join_delay(StreamJoiner *this, GstClockTime join_delay)
{
  this->join_delay = join_delay;
}

RTPPacket* stream_joiner_pop_packet(StreamJoiner *this)
{
  GstClockTime now = _now(this);
  RTPPacket* packet = NULL;

  if(now < this->playout_time){
    return NULL;
  }

  if(this->playout_time == 0){
    this->playout_time = now;
  }

  if(!_has_packet_to_playout(this, now)){
    this->playout_time += this->playout_delay;
    return NULL;
  }

  if((packet = _pop_packet(this)) == NULL){
    return NULL;
  }

  if(_cmp_ts(this->last_ts, packet->timestamp) < 0){
    this->last_ts = packet->timestamp;
    this->playout_time += this->playout_delay;
  }

  return packet;
}


RTPPacket* _pop_packet(StreamJoiner *this)
{
  RTPPacket* result = g_queue_pop_head(this->playoutq);

  if(this->last_seq_init == FALSE){
    this->last_seq_init = TRUE;
    this->last_seq = result->abs_seq;
    this->last_ts  = result->timestamp;
    return result;
  }

  if(_cmp_seq(++this->last_seq, result->abs_seq) < 0){
    DiscardedPacket *discarded_packet = g_slice_new0(DiscardedPacket);
    g_queue_push_head(this->playoutq, result);
    discarded_packet->abs_seq = this->last_seq;
    mediator_set_request(this->repair_channel, discarded_packet);
    return NULL;
  }

  return result;
}

gboolean _has_packet_to_playout(StreamJoiner *this, GstClockTime elapsed_time)
{
  if(g_queue_is_empty(this->playoutq)){
    return FALSE;
  }

  if(!this->join_delay){
    return TRUE;
  }

  if(g_queue_get_length(this->playoutq) < 2){
    elapsed_time -= ((RTPPacket*)g_queue_peek_head(this->playoutq))->created;
  }else{
    RTPPacket *first,*last;
    first = g_queue_peek_head(this->playoutq);
    last  = g_queue_peek_tail(this->playoutq);
    elapsed_time = first->created < last->created ? last->created - first->created : first->created - last->created;
  }

  return this->join_delay <= elapsed_time;
}



#undef MAX_TRESHOLD_TIME
#undef MIN_TRESHOLD_TIME
#undef BETHA_FACTOR

