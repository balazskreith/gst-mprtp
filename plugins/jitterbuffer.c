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

typedef struct{
  SlidingWindow* packet_skews;
  gdouble        path_skew;
  gint64         last_delay;
}Subflow;

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
//

#define _subflow(this, id) ((Subflow*)(((Subflow*)this->subflows) + id))

static void _on_skew_median_calculated(Subflow *subflow, swpercentilecandidates_t *candidates)
{
  gint64 skew_median,skew_min,skew_max;
  PercentileRawResult(gint64,   \
                   candidates,  \
                   skew_median, \
                   skew_min,    \
                   skew_max,    \
                   0            \
                   );
  if(subflow->path_skew == 0.){
    subflow->path_skew = skew_median;
  }else{
    subflow->path_skew = subflow->path_skew * .99 + .01 * skew_median;
  }

  if(0){
    subflow->path_skew += 0 * skew_min + 0 * skew_max;
  }
}

static void _on_path_minmax_skew_calculated(JitterBuffer *this, swminmaxstat_t *stat)
{
  gdouble max;
  max = *(gdouble*)stat->max;

  if(this->playout_delay == 0.){
    this->playout_delay = max;
  }else{
    this->playout_delay = (this->playout_delay * 255 + max) / 256;
  }
}

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
  g_queue_free(this->playoutq);
  g_object_unref(this->sysclock);

  g_free(this->subflows);
}


void
jitterbuffer_init (JitterBuffer * this)
{
  this->sysclock   = gst_system_clock_obtain ();
  this->made       = _now(this);
  this->playoutq   = g_queue_new();
  this->clock_rate = 90000;

  this->subflows   = g_malloc0(sizeof(Subflow) * 256);
  this->path_skews = make_slidingwindow_double(256, 0);

  slidingwindow_add_plugin(this->path_skews,
      make_swminmax(bintree3cmp_double, (ListenerFunc) _on_path_minmax_skew_calculated, this));

}

JitterBuffer*
make_jitterbuffer(void)
{
  JitterBuffer *result;
  result = (JitterBuffer *) g_object_new (JITTERBUFFER_TYPE, NULL);

  return result;
}

void jitterbuffer_set_clock_rate(JitterBuffer *this, gint32 clock_rate)
{
  this->clock_rate = clock_rate;
}


void jitterbuffer_push_packet(JitterBuffer *this, RcvPacket* packet)
{
  Subflow* subflow;
  gint64 skew, margin;
  g_queue_insert_sorted(this->playoutq, packet, (GCompareDataFunc) _cmp_rcvpacket_abs_seq, NULL);
  if(!packet->subflow_id){
    goto done;
  }
  subflow = _subflow(this, packet->subflow_id);

  if(!subflow->last_delay){
    subflow->last_delay = packet->delay;
    subflow->packet_skews = make_slidingwindow_int64(100, 2 * GST_SECOND);
    slidingwindow_add_plugin(subflow->packet_skews,
        make_swpercentile(500, bintree3cmp_int64, (ListenerFunc)_on_skew_median_calculated, subflow));
    goto done;
  }

  margin = 10 * GST_MSECOND;
  skew = CONSTRAIN(-1 * margin, margin, (gint64)packet->delay - subflow->last_delay);

  slidingwindow_add_data(subflow->packet_skews, &skew);
  subflow->last_delay = packet->delay;

  if(!this->last_ts){
    this->last_ts = packet->timestamp;
  }

  if(_cmp_ts(packet->timestamp, this->last_ts) <= 0){
    goto done;
  }
  this->last_ts = packet->timestamp;

  slidingwindow_add_data(this->path_skews, &subflow->path_skew);
  this->playout_time = _now(this) + this->playout_delay;

done:
  return;
}

gboolean jitterbuffer_has_repair_request(JitterBuffer* this, guint16 *gap_seq)
{
  if(this->gap_seq == -1){
    return FALSE;
  }
  *gap_seq = this->gap_seq;
  this->gap_seq = -1;
  return TRUE;
}

RcvPacket* jitterbuffer_pop_packet(JitterBuffer *this, GstClockTime *playout_time)
{
  RcvPacket* packet = NULL;

  if(g_queue_is_empty(this->playoutq)){
    *playout_time = 0;
    goto done;
  }

  packet = g_queue_peek_head(this->playoutq);
  if(!this->last_seq_init){
    this->last_seq = packet->abs_seq;
    this->last_seq_init = TRUE;
    packet = g_queue_pop_head(this->playoutq);
    goto done;
  }

  if(0 < _cmp_seq(this->last_seq, packet->abs_seq)){
    packet = g_queue_pop_head(this->playoutq);
    goto done;
  }

  if(_now(this) < this->playout_time){
    *playout_time = this->playout_time;
    packet = NULL;
    goto done;
  }

  packet = g_queue_pop_head(this->playoutq);
  if((guint16)(this->last_seq + 2) == packet->abs_seq){
    this->gap_seq = this->last_seq + 1;
  }
  this->last_seq = packet->abs_seq;
done:
  return packet;

}


