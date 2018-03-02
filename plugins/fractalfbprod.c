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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>     /* qsort */
#include "fractalfbprod.h"
#include "reportprod.h"

#define _now(this) gst_clock_get_time (this->sysclock)

GST_DEBUG_CATEGORY_STATIC (fractalfbproducer_debug_category);
#define GST_CAT_DEFAULT fractalfbproducer_debug_category

G_DEFINE_TYPE (FRACTaLFBProducer, fractalfbproducer, G_TYPE_OBJECT);

#define RLE_LENGTH (FRACTALPRODUCER_CHUNKS_MAX_LENGTH-2)

typedef struct{
  FRACTaLFBProducer* this;
  gboolean initialized;
}IteratorMinMaxHelper;

typedef struct {
  guint16 begin_seq;
  gboolean found;
}RLEFilterHelper;

static void fractalfbproducer_finalize (GObject * object);
static gboolean _do_fb(FRACTaLFBProducer* data);;
static gboolean _packet_subflow_filter(FRACTaLFBProducer *this, RcvPacket *packet);
static void _on_received_packet(FRACTaLFBProducer *this, RcvPacket *packet);
static void _setup_xr_cc_fb_rle(FRACTaLFBProducer * this,  ReportProducer* reportproducer);
static void _on_fb_update(FRACTaLFBProducer *this,  ReportProducer* reportproducer);
static void _rle_minmax_iterator(RcvPacket* packet, IteratorMinMaxHelper* helper);
static void _refresh_begin_end_seq(FRACTaLFBProducer * this);
static gboolean _rle_tail_filter(RLEFilterHelper *helper, RcvPacket *packet);


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
_delta_seq (guint16 start, guint16 end)
{
  if(start == end) return 0;
  if (start < end) {
    return end - start;
  }
  return 65536 - start + end;
}

static guint32 _delta_ts(guint32 last_ts, guint32 actual_ts) {
  if (last_ts <= actual_ts) {
    return actual_ts - last_ts;
  } else {
    return 4294967296 - last_ts + actual_ts;
  }
}

void
fractalfbproducer_class_init (FRACTaLFBProducerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fractalfbproducer_finalize;

  GST_DEBUG_CATEGORY_INIT (fractalfbproducer_debug_category, "fractalfbproducer", 0,
      "FRACTALFBProducer");

}

void
fractalfbproducer_finalize (GObject * object)
{
  FRACTaLFBProducer *this;
  this = FRACTALFBPRODUCER(object);

  rcvtracker_rem_on_received_packet_listener(this->tracker,  (ListenerFunc)_on_received_packet);
  rcvsubflow_rem_on_rtcp_fb_cb(this->subflow, (ListenerFunc) _on_fb_update);

  g_object_unref(this->sysclock);
  g_object_unref(this->ts_generator);
  g_object_unref(this->tracker);

}

void
fractalfbproducer_init (FRACTaLFBProducer * this)
{
  this->sysclock = gst_system_clock_obtain();
}

static void _packet_unrefer(FRACTaLFBProducer* this, RcvPacket* packet) {
  rcvpacket_unref(packet);
}

static void _packet_refer(FRACTaLFBProducer* this, RcvPacket* packet) {
  rcvpacket_ref(packet);
}

static void _packet_debug(RcvPacket* packet, gchar* string) {
  sprintf(string, "(%p, %hu, %hu, %d)", packet, packet->subflow_id, packet->subflow_seq, packet->ref);
}
FRACTaLFBProducer *make_fractalfbproducer(RcvSubflow* subflow, RcvTracker *tracker)
{
  FRACTaLFBProducer *this;
  this = g_object_new (FRACTALFBPRODUCER_TYPE, NULL);
  this->subflow         = subflow;
  this->tracker         = g_object_ref(tracker);
  this->rle_sw          = make_slidingwindow(RLE_LENGTH, .5 * GST_SECOND);
  this->ts_generator    = g_object_ref(rcvtracker_get_cc_ts_generator(tracker));

  slidingwindow_add_postprocessor(this->rle_sw, (ListenerFunc)_packet_unrefer, this);
  slidingwindow_add_preprocessor(this->rle_sw, (ListenerFunc)_packet_refer, this);

  rcvtracker_add_on_received_packet_listener_with_filter(this->tracker,
      (ListenerFunc) _on_received_packet,
      (ListenerFilterFunc) _packet_subflow_filter,
      this);

  rcvsubflow_add_on_rtcp_fb_cb(subflow, (ListenerFunc) _on_fb_update, this);
  DISABLE_LINE slidingwindow_setup_debug(this->rle_sw, (SlidingWindowItemSprintf)_packet_debug, g_print);
  return this;
}

void fractalfbproducer_reset(FRACTaLFBProducer *this)
{
  this->initialized = FALSE;
}

gboolean _packet_subflow_filter(FRACTaLFBProducer *this, RcvPacket *packet)
{
  return packet->subflow_id == this->subflow->id;
}

void _on_received_packet(FRACTaLFBProducer *this, RcvPacket *packet)
{
//  g_print("Received %hu-%hu packet subflow %d on fractalprod (%p) %d, subflow_seq: %hu\n",
//      this->begin_seq, this->end_seq, packet->subflow_id, this->rle_sw, this->subflow->id, packet->subflow_seq);

  if(!this->initialized){
    this->initialized = TRUE;
    this->begin_seq = this->end_seq = packet->subflow_seq;
    slidingwindow_add_data(this->rle_sw, packet);
    ++this->rcved_packets;
    goto done;
  }

  if (_cmp_seq(packet->subflow_seq, this->begin_seq) < 0 && RLE_LENGTH < _delta_seq(packet->subflow_seq, this->end_seq)) {
    // do not add the queue and report discarded
    goto done;
  }
  slidingwindow_add_data(this->rle_sw, packet);
  ++this->rcved_packets;

done:
  return;
}


static gboolean _do_fb(FRACTaLFBProducer *this)
{
  GstClockTime now = _now(this);
  slidingwindow_refresh(this->rle_sw);

  if(now - 20 * GST_MSECOND < this->last_fb){
    return FALSE;
  }
  if(this->last_fb < _now(this) - 100 * GST_MSECOND){
    return TRUE;
  }
  return 3 < this->rcved_packets;
//  return 0 < this->rcved_packets;
}

void _on_fb_update(FRACTaLFBProducer *this, ReportProducer* reportproducer)
{
  if(!_do_fb(this)){
    goto done;
  }
//PROFILING("report_producer_begin",
  report_producer_begin(reportproducer, this->subflow->id);
//);
  _setup_xr_cc_fb_rle(this, reportproducer);

  this->last_fb = _now(this);
  this->rcved_packets = 0;
done:
  return;
}

typedef struct{
  FRACTaLFBProducer* this;
  guint32 report_timestamp;
}IteratorChunkHelper;

static void _rle_chunk_iterator(RcvPacket* packet, IteratorChunkHelper* helper) {
  FRACTaLFBProducer* this = helper->this;
  gint index;
  GstRTCPXRChunk* chunk;
//  g_print("Iterate on %hu->%hu fractalprod (%p)%d, subflow_seq: (%p)%hu\n",
//      this->begin_seq, this->end_seq, this, this->subflow->id, packet, packet->subflow_seq);
  if (this->begin_seq <= packet->subflow_seq) {
    index = packet->subflow_seq - this->begin_seq;
  } else {
//    g_print("begin seq: %hu, subflow_seq: %hu\n", this->begin_seq, packet->subflow_seq);
    index = 65536 - this->begin_seq + packet->subflow_seq;
  }
  if (64 < index) {
//      g_print("Problem on %hu->%hu fractalprod (%p)%d, subflow_seq: (%p)%hu\n",
//          this->begin_seq, this->end_seq, this, this->subflow->id, packet, packet->subflow_seq);
//    g_print("There is a problem with the index calculatioin at FRACTaLProducer %d\n", index);
    return;
  }
  chunk = this->chunks + index;
  chunk->CCFeedback.lost = 1;
  chunk->CCFeedback.ecn = 1;
  chunk->CCFeedback.ato = _delta_ts(packet->cc_ts, helper->report_timestamp);
}


void _setup_xr_cc_fb_rle(FRACTaLFBProducer * this,  ReportProducer* reportproducer) {
  guint32 report_count = 1;
  gint chunks_num;
  IteratorChunkHelper chunk_helper;

  if (!this->initialized) {
    goto done;
  }

  _refresh_begin_end_seq(this);

  while(RLE_LENGTH < _delta_seq(this->begin_seq, this->end_seq)) {
    RLEFilterHelper helper = {this->begin_seq, FALSE};
    slidingwindow_filter_out_from_tail(this->rle_sw, (SlidingWindowPredicator) _rle_tail_filter, &helper);
    _refresh_begin_end_seq(this);
  }

  memset(this->chunks, 0, sizeof(guint16) * FRACTALPRODUCER_CHUNKS_MAX_LENGTH);
  chunk_helper.this = this;
  chunk_helper.report_timestamp = timestamp_generator_get_ts(this->ts_generator);
  slidingwindow_iterate(this->rle_sw, (SlidingWindowIterator) _rle_chunk_iterator, &chunk_helper);
  chunks_num = _delta_seq(this->begin_seq, this->end_seq) + 1;

//  g_print("Sent chunks: %d (%d->%d): ", this->subflow->id, this->begin_seq, this->end_seq);
//  for (int i = 0; i < chunks_num; ++i) {
//    GstRTCPXRChunk* chunk = this->chunks + i;
//    g_print("(%d, %d) ", (guint16)(this->begin_seq + i), chunk->CCFeedback.lost);
//  }
//  g_print("\n");

  report_producer_add_xr_cc_rle_fb(reportproducer,
      report_count,
      chunk_helper.report_timestamp,
      this->begin_seq,
      this->end_seq,
      this->chunks,
      chunks_num
      );
done:
  return;
}


void _rle_minmax_iterator(RcvPacket* packet, IteratorMinMaxHelper* helper) {
  FRACTaLFBProducer* this = helper->this;
  if (!helper->initialized) {
    this->begin_seq = this->end_seq = packet->subflow_seq;
    helper->initialized = TRUE;
    return;
  }
  if (_cmp_seq(this->end_seq, packet->subflow_seq) < 0) {
    this->end_seq = packet->subflow_seq;
  }
  if (_cmp_seq(packet->subflow_seq, this->begin_seq) < 0) {
    this->begin_seq = packet->subflow_seq;
  }

//  g_print("(%hu, %hu) ", packet->subflow_seq, packet->subflow_id);

  if (packet->subflow_id != this->subflow->id) {
      g_print("(%p, %hu, %hu, %d) ", packet, packet->subflow_seq, packet->subflow_id, packet->ref);
  }
}

void _refresh_begin_end_seq(FRACTaLFBProducer * this) {
  IteratorMinMaxHelper minmax_helper;
  minmax_helper.this = this;
  minmax_helper.initialized = FALSE;
  slidingwindow_iterate(this->rle_sw, (SlidingWindowIterator) _rle_minmax_iterator, &minmax_helper);
}

gboolean _rle_tail_filter(RLEFilterHelper *helper, RcvPacket *packet) {
  if (helper->found){
    return FALSE;
  }
  helper->found = helper->begin_seq == packet->subflow_seq;
  return TRUE;
}


#undef RLE_LENGTH


