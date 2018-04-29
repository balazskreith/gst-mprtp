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


GST_DEBUG_CATEGORY_STATIC (jitterbuffer_debug_category);
#define GST_CAT_DEFAULT jitterbuffer_debug_category

#define _now(this) gst_clock_get_time (this->sysclock)

G_DEFINE_TYPE (JitterBuffer, jitterbuffer, G_TYPE_OBJECT);

typedef struct{
  SlidingWindow* packet_skews;
  gdouble        path_skew_in_ts;
  guint32        last_rcv_rtp_ts;
  guint32        last_snd_rtp_ts;
}Subflow;

static void _refresh_playout_delay(JitterBuffer* this, RcvPacket* packet);

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

static guint32 _delta_ts(guint32 last_ts, guint32 actual_ts) {
  if (last_ts <= actual_ts) {
    return actual_ts - last_ts;
  } else {
    return 4294967296 - last_ts + actual_ts;
  }
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

#define _subflow(this, id) ((Subflow*)(((Subflow*)this->subflows) + id))

static void _on_path_skew_calculated(Subflow *subflow, gint64* skew)
{
  gint64 path_skew_in_ts;
  gint64 min_skew_in_ts = -1 * (gint64) (DEFAULT_RTP_TIMESTAMP_GENERATOR_CLOCKRATE / 10);
  gint64 max_skew_in_ts = (gint64) (DEFAULT_RTP_TIMESTAMP_GENERATOR_CLOCKRATE / 10);
  path_skew_in_ts = CONSTRAIN(min_skew_in_ts, max_skew_in_ts, *skew);
//g_print("%ld\n", skew_median);
  if(subflow->path_skew_in_ts == 0.){
    subflow->path_skew_in_ts = path_skew_in_ts;
  }else{
    subflow->path_skew_in_ts = subflow->path_skew_in_ts * .99 + .01 * path_skew_in_ts;
  }
}

static void _on_path_minmax_skew_calculated(JitterBuffer *this, swminmaxstat_t *stat)
{
  gdouble max;
  max = MAX(0, *(gdouble*)stat->max);

  if(this->playout_delay_in_ts == 0.){
    this->playout_delay_in_ts = max;
  }else{
    this->playout_delay_in_ts = (this->playout_delay_in_ts * 255 + max) / 256;
  }
//  this->playout_delay_in_ts = CONSTRAIN(0, GST_SECOND, this->playout_delay_in_ts);
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

  this->subflows   = g_malloc0(sizeof(Subflow) * 256);
  this->path_skews = make_slidingwindow_double(256, 0);

  slidingwindow_add_plugin(this->path_skews,
      make_swminmax((GCompareFunc)bintree3cmp_double, (ListenerFunc) _on_path_minmax_skew_calculated, this));

}

JitterBuffer*
make_jitterbuffer(void)
{
  JitterBuffer *result;
  result = (JitterBuffer *) g_object_new (JITTERBUFFER_TYPE, NULL);

  return result;
}


gboolean jitterbuffer_is_packet_discarded(JitterBuffer* this, RcvPacket* packet)
{
  if(!this->last_seq_init){
    return FALSE;
  }
  return _cmp_seq(packet->abs_seq, this->last_seq) < 0;
}


void jitterbuffer_push_packet(JitterBuffer *this, RcvPacket* packet)
{
  packet = rcvpacket_ref(packet);
  g_queue_insert_sorted(this->playoutq, packet, (GCompareDataFunc) _cmp_rcvpacket_abs_seq, NULL);
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

guint32 jitterbuffer_get_playout_delay_in_ts(JitterBuffer* this) {
  return this->playout_delay_in_ts;
}

RcvPacket* jitterbuffer_pop_packet(JitterBuffer *this)
{
  RcvPacket* packet = NULL;

  if(g_queue_is_empty(this->playoutq)){
    goto done;
  }

  packet = g_queue_peek_head(this->playoutq);

  _refresh_playout_delay(this, packet);

  if(!this->last_seq_init){
    this->last_seq = packet->abs_seq;
    this->last_seq_init = TRUE;
    packet = g_queue_pop_head(this->playoutq);
    goto done;
  }

  packet = g_queue_pop_head(this->playoutq);
  if((guint16)(this->last_seq + 2) == packet->abs_seq) {
    this->gap_seq = this->last_seq + 1;
  }
  this->last_seq = packet->abs_seq;
done:
  if (packet) {
    rcvpacket_unref(packet);
  }
  return packet;
}

void _refresh_playout_delay(JitterBuffer* this, RcvPacket* packet) {
  Subflow* subflow;
  gint64 skew, margin;

  if(!packet->subflow_id){
    return;
  }
  subflow = _subflow(this, packet->subflow_id);

  if(!subflow->last_rcv_rtp_ts){
    subflow->last_rcv_rtp_ts = packet->rcv_rtp_ts;
    subflow->last_snd_rtp_ts = packet->snd_rtp_ts;
    subflow->packet_skews = make_slidingwindow_int64(100, 2 * GST_SECOND);
    slidingwindow_add_plugin(subflow->packet_skews,
        make_swpercentile2(50,
            (GCompareFunc)bintree3cmp_int64,
            (ListenerFunc)_on_path_skew_calculated,
            subflow,
            (SWExtractorFunc) swpercentile2_self_extractor,
            (SWMeanCalcer) swpercentile2_prefer_right_selector,
            (SWEstimator) swpercentile2_prefer_right_selector)
        );
    return;
  }

  margin = DEFAULT_RTP_TIMESTAMP_GENERATOR_CLOCKRATE / 100;
  skew = (gint64)_delta_ts(subflow->last_rcv_rtp_ts, packet->rcv_rtp_ts) - (gint64)_delta_ts(subflow->last_snd_rtp_ts, packet->snd_rtp_ts);
  skew = CONSTRAIN(-1 * margin, margin, skew);
//  bintree_print(((swpercentile2_t*)((SlidingWindowPlugin*)subflow->packet_skews->plugins->data)->priv)->maxtree);
//  bintree_print(((swpercentile2_t*)((SlidingWindowPlugin*)subflow->packet_skews->plugins->data)->priv)->mintree);
//  bintree_foreach(((swpercentile2_t*)((SlidingWindowPlugin*)subflow->packet_skews->plugins->data)->priv)->maxtree,
//      (GFunc) _node_foreach,
//      ((swpercentile2_t*)((SlidingWindowPlugin*)subflow->packet_skews->plugins->data)->priv)->mintree
//  );
  slidingwindow_add_data(subflow->packet_skews, &skew);
  subflow->last_rcv_rtp_ts = packet->rcv_rtp_ts;
  subflow->last_snd_rtp_ts = packet->snd_rtp_ts;

  slidingwindow_add_data(this->path_skews, &subflow->path_skew_in_ts);
}


