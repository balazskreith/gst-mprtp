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
#include "streamjoiner.h"
#include "gstmprtpbuffer.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
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

static void
_process(gpointer udata);

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
  const RTPBufferMessage *ai = a;
  const RTPBufferMessage *bi = b;
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
  g_object_unref (this->sysclock);
}


void
stream_joiner_init (StreamJoiner * this)
{
  this->sysclock           = gst_system_clock_obtain ();
  this->made               = _now(this);
  this->join_delay         = 10 * GST_MSECOND;


}

StreamJoiner*
make_stream_joiner(GAsyncQueue* rtppackets_out, GAsyncQueue *discarded_packets_out)
{
  StreamJoiner *result;
  result = (StreamJoiner *) g_object_new (STREAM_JOINER_TYPE, NULL);
  result->rtppackets_out         = g_async_queue_ref(rtppackets_out);
  result->messages_in            = g_async_queue_new();
  result->discarded_packets_out  = g_async_queue_ref(discarded_packets_out);
  result->joinq                   = make_slidingwindow(100, result->join_delay);
  result->playoutq                = g_queue_new();

  slidingwindow_add_pipes(result->joinq, _joinq_rem_pipe, result, NULL, NULL);
  return result;
}

void stream_joiner_add_stat(StreamJoiner *this, RcvTrackerStat* stat)
{
  StatMessage *msg;
  msg = g_slice_new(StatMessage);
  msg->base.type = STREAMJOINER_MESSAGE_STAT;
  memcpy(&msg->stat, stat, sizeof(RcvTrackerStat));
  g_async_queue_push(this->messages_in, msg);
}

void stream_joiner_add_packet(StreamJoiner *this, RTPPacket* packet)
{
  RTPBufferMessage *msg;
  rtppackets_packet_ref(packet);
  msg = g_slice_new(RTPBufferMessage);
  msg->base.type   = STREAMJOINER_MESSAGE_RTPPACKET;
  msg->ts          = packet->timestamp;
  msg->buffer      = packet->buffer;
  msg->abs_seq     = packet->abs_seq;
  g_async_queue_push(this->messages_in, msg);
}

static void _process_message(StreamJoiner *this, Message *msg)
{
  if(msg->type == STREAMJOINER_MESSAGE_STAT){
    StatMessage* stat_msg = (StatMessage*)msg;
    if(this->playout_delay == 0.){
      this->playout_delay = stat_msg->stat.max_skew;
    }else{
      this->playout_delay = (stat_msg->stat.max_skew + 255. * this->playout_delay) / 256.;
    }
    g_slice_free(StatMessage, msg);
    return;
  }

  if(msg->type == STREAMJOINER_MESSAGE_JOINDELAY){
    JoinDelayMessage join_msg = (JoinDelayMessage*)msg;
    this->join_delay = join_msg->join_delay;
    slidingwindow_set_treshold(this->joinq, this->join_delay);
    g_slice_free(JoinDelayMessage, msg);
    return;
  }

  {
    RTPBufferMessage *rtp_message = (RTPBufferMessage*) msg;
    //Message is RTPBuffer
    if(0 < this->join_delay){
      slidingwindow_add_data(this->joinq, rtp_message);
    }else{
      g_queue_insert_sorted(this->playoutq, rtp_message, _playoutq_sort_helper, NULL);
    }
  }
}

static void _forward(StreamJoiner *this, RTPBufferMessage *rtpbuf_msg)
{
  if(this->last_seq_init == FALSE){
    this->last_seq_init = TRUE;
    this->last_seq = rtpbuf_msg->abs_seq;
    goto send;
  }

  if(_cmp_seq(++this->last_seq, rtpbuf_msg->abs_seq) < 0){
    DiscardedPacket discarded_packet = g_slice_new0(DiscardedPacket);
    discarded_packet->abs_seq    = this->last_seq;
    g_queue_push_head(this->playoutq, rtpbuf_msg);
    g_async_queue_push(this->discarded_packets_out, discarded_packet);
    goto done;
  }

  if(_cmp_ts(this->last_ts, rtpbuf_msg->ts) < 0){
    this->last_ts = rtpbuf_msg->ts;
    this->pacing_time += this->playout_delay;
  }

send:
  g_async_queue_push(this->rtppackets_out, rtpbuf_msg->buffer);
  g_slice_free(RTPBufferMessage, rtpbuf_msg);
done:
  return;
}

void _process(gpointer udata)
{
  StreamJoiner *this;
  RTPBufferMessage* rtpbuf_msg;
  Message *msg;

  slidingwindow_refresh(this->joinq);

  while((msg = g_async_queue_try_pop(this->messages_in))){
    _process_message(this, msg);
  }

  if(_now(this) < this->pacing_time){
    goto done;
  }

  if(this->pacing_time == 0){
    this->pacing_time = _now(this);
  }

  if(g_queue_is_empty(this->playoutq)){
    this->pacing_time += this->playout_delay;
    goto done;
  }

  _forward(this, g_queue_pop_head(this->joinq));


done:
  return;

}

void
stream_joiner_set_join_delay (StreamJoiner * this, GstClockTime join_delay)
{
  JoinDelayMessage *msg;
  msg = g_slice_new(JoinDelayMessage);
  msg->join_delay  = join_delay;
  msg->base.type = STREAMJOINER_MESSAGE_JOINDELAY;
  g_async_queue_push(this->messages_in, msg);
}




#undef MAX_TRESHOLD_TIME
#undef MIN_TRESHOLD_TIME
#undef BETHA_FACTOR

