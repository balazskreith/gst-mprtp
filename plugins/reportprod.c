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
#include "reportprod.h"
#include "gstmprtcpbuffer.h"
#include "streamjoiner.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define DATABED_LENGTH 1400

GST_DEBUG_CATEGORY_STATIC (report_producer_debug_category);
#define GST_CAT_DEFAULT report_producer_debug_category

G_DEFINE_TYPE (ReportProducer, report_producer, G_TYPE_OBJECT);

#define _now(this) (gst_clock_get_time (this->sysclock))

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
report_producer_finalize (GObject * object);


//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

static void
_add_xr(ReportProducer *this);

static void
_add_xrblock(
    ReportProducer *this,
    GstRTCPXRBlock *block);

static void
_add_length(
    ReportProducer *this,
    guint16 additional_length);

static void
_add_afb(ReportProducer *this,
         guint32 media_source_ssrc,
         guint32  fci_id,
         gpointer fci_dat,
         guint fci_dat_len);

void
report_producer_class_init (ReportProducerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = report_producer_finalize;

  GST_DEBUG_CATEGORY_INIT (report_producer_debug_category, "report_producer", 0,
      "MpRTP Receiving Controller");

}

void
report_producer_finalize (GObject * object)
{
  ReportProducer *this = REPORTPRODUCER (object);
  g_object_unref (this->sysclock);
  mprtp_free(this->databed);
  mprtp_free(this->xr.databed);
}

void
report_producer_init (ReportProducer * this)
{
  this->sysclock        = gst_system_clock_obtain ();
  this->ssrc            = g_random_int();
  this->report          = this->databed = mprtp_malloc(DATABED_LENGTH);
  this->made            = _now(this);
  this->xr.actual_block = this->xr.databed = mprtp_malloc(DATABED_LENGTH);
  this->in_progress         = FALSE;
}

void report_producer_set_sender_ssrc(ReportProducer *this, guint32 sender_ssrc)
{
  this->sender_ssrc = sender_ssrc;
}

void report_producer_set_logfile(ReportProducer *this, const gchar *logfile)
{
  strcpy(this->logfile, logfile);
}

void report_producer_begin(ReportProducer *this, guint8 subflow_id)
{
  if(this->in_progress){
    return;
  }

  this->in_progress = TRUE;

  memset(this->databed, 0, DATABED_LENGTH);
  gst_mprtcp_report_init(this->report);
  this->block = gst_mprtcp_riport_add_block_begin(this->report, (guint16) subflow_id);
  this->actual = &this->block->block_header;
  this->length = 0;

  memset(this->xr.databed, 0, DATABED_LENGTH);
  this->xr.head_block   = this->xr.databed;
  this->xr.actual_block = this->xr.databed;
  this->xr.length       = 0;

}

void report_producer_add_rr(ReportProducer *this,
                            guint8 fraction_lost,
                            guint32 total_lost,
                            guint32 ext_hsn,
                            guint32 jitter,
                            guint32 LSR,
                            guint32 DLSR)
{
  guint16 length;//, main_length;
//  guint8 block_length;
  GstRTCPRR *rr;
  rr = gst_mprtcp_riport_block_add_rr(this->block);
  gst_rtcp_rr_add_rrb (rr,
                           this->sender_ssrc ,
                           fraction_lost,
                           total_lost,
                           ext_hsn,
                           jitter,
                           LSR,
                           DLSR);
  gst_rtcp_header_getdown (&rr->header, NULL, NULL, NULL, NULL, &length, NULL);
  gst_rtcp_header_change(&rr->header, NULL, NULL, NULL, NULL, NULL, &this->ssrc);
  _add_length(this, length);
}

void report_producer_add_xr_discarded_bytes(ReportProducer *this,
                                    guint8 interval_metric_flag,
                                    gboolean early_bit,
                                    guint32 payload_bytes_discarded)
{
  GstRTCPXRDiscardedBlock block;
  gst_rtcp_xr_discarded_bytes_setup(&block,
                                    interval_metric_flag,
                                    early_bit,
                                    this->ssrc,
                                    payload_bytes_discarded);

  _add_xrblock(this, (GstRTCPXRBlock*) &block);
}

void report_producer_add_xr_discarded_packets(ReportProducer *this,
                                    guint8 interval_metric_flag,
                                    gboolean early_bit,
                                    guint32 discarded_packets_num)
{
  GstRTCPXRDiscardedBlock block;
  gst_rtcp_xr_discarded_packets_setup(&block,
                                    interval_metric_flag,
                                    early_bit,
                                    this->ssrc,
                                    discarded_packets_num);

  _add_xrblock(this, (GstRTCPXRBlock*) &block);
}

void report_producer_add_xr_owd(ReportProducer *this,
                                 guint8 interval_metric_flag,
                                 guint32 median_delay,
                                 guint32 min_delay,
                                 guint32 max_delay)
{
  GstRTCPXROWDBlock block;
  gst_rtcp_xr_owd_block_setup(&block,
                              interval_metric_flag,
                              this->ssrc,
                              median_delay,
                              min_delay,
                              max_delay);
  _add_xrblock(this, (GstRTCPXRBlock*) &block);
}


void report_producer_add_xr_lost_rle(ReportProducer *this,
                                 gboolean early_bit,
                                 guint8 thinning,
                                 guint16 begin_seq,
                                 guint16 end_seq,
                                 gboolean *vector)
{
  gchar databed[1024];
  GstRTCPXRRLELostsRLEBlock *block;
  GstRTCPXRChunk chunk;
  gint bit_i, chunks_num, vector_i;
  gboolean last_is_copied = TRUE;
  memset(databed, 0, 1024);
  memset(&chunk, 0, sizeof(GstRTCPXRChunk));
  block = (GstRTCPXRRLELostsRLEBlock*) databed;
  gst_rtcp_xr_rle_losts_setup(block, early_bit, thinning, this->ssrc, begin_seq, end_seq);
  for(chunks_num = 0, bit_i = 0, vector_i=begin_seq; vector_i != end_seq; ++vector_i){
      if(vector[vector_i]){
        chunk.Bitvector.bitvector |= (guint16)(1<<bit_i);
      }
      if(++bit_i < 15) {
          last_is_copied = FALSE;
          continue;
      }
      chunk.Bitvector.chunk_type = TRUE;

      gst_rtcp_xr_chunk_hton_cpy(&block->chunks[chunks_num], &chunk);
//      gst_print_rtcp_xrchunks(block->chunks + chunks_num, &chunk);
      last_is_copied = TRUE;
      memset(&chunk, 0, sizeof(GstRTCPXRChunk));
      bit_i = 0;
      ++chunks_num;
  }
  if(!last_is_copied){
    chunk.Bitvector.chunk_type = TRUE;
    gst_rtcp_xr_chunk_hton_cpy(&block->chunks[chunks_num], &chunk);
  }

  {
    guint16 block_length;
    gst_rtcp_xr_block_getdown((GstRTCPXRBlock*) block, NULL, &block_length, NULL);
    for(; 1 < chunks_num; chunks_num-=2){
      ++block_length;
    }

    gst_rtcp_xr_block_change((GstRTCPXRBlock*) block, NULL, &block_length, NULL);
  }

  _add_xrblock(this, (GstRTCPXRBlock*) block);
}


void report_producer_add_afb(ReportProducer *this,
                                guint32 media_source_ssrc,
                                guint32  fci_id,
                                gpointer fci_dat,
                                guint fci_dat_len)
{
  _add_afb(this, media_source_ssrc, fci_id, fci_dat, fci_dat_len);
}

void report_producer_add_afb_remb(
    ReportProducer *this,
    guint32 media_source_ssrc,
    guint32 num_ssrc,
    gfloat float_num,
    guint32 ssrc_feedback,
    guint16 hssn)
{
  GstRTCPAFB_REMB remb;
  gst_rtcp_afb_remb_change(&remb, &num_ssrc, &float_num, &ssrc_feedback, &hssn);
  _add_afb(this, media_source_ssrc, RTCP_AFB_REMB_ID, &remb, sizeof(GstRTCPAFB_REMB));
}

void report_producer_add_afb_reps(
    ReportProducer *this,
    guint32 media_source_ssrc,
    guint8 sampling_num,
    gfloat float_num)
{
  GstRTCPAFB_REPS reps;
  gst_rtcp_afb_reps_change(&reps, &sampling_num, &float_num);
  _add_afb(this, media_source_ssrc, RTCP_AFB_REPS_ID, &reps, sizeof(GstRTCPAFB_REPS));
}



void report_producer_add_sr(ReportProducer *this,
                                guint64 ntp_timestamp,
                                guint32 rtp_timestamp,
                                guint32 packet_count,
                                guint32 octet_count)
{
  guint16 length;
//  guint8 block_length;
  GstRTCPSR *sr;
  sr = gst_mprtcp_riport_block_add_sr(this->block);
  gst_rtcp_srb_setup(&sr->sender_block, ntp_timestamp, rtp_timestamp, packet_count, octet_count);
  gst_rtcp_header_getdown (&sr->header, NULL, NULL, NULL, NULL, &length, NULL);
  gst_rtcp_header_change(&sr->header, NULL, NULL, NULL, NULL, NULL, &this->ssrc);
  _add_length(this, length);
}


GstBuffer *report_producer_end(ReportProducer *this, guint *length)
{
  gpointer data;
  GstBuffer* result = NULL;

  if(this->in_progress == FALSE){
    return result;
  }

  _add_xr(this);
  if(!this->length){
    goto done;
  }
//  gst_mprtcp_riport_add_block_end(this->report, this->block);
//  g_print("length: %lu\n", this->length);
//  gst_print_rtcp(this->databed);
  data = g_malloc0(this->length);
  memcpy(data, this->databed, this->length);
  result = gst_buffer_new_wrapped(data, this->length);
  if(length) {
    *length = this->length;
  }
//  mprtp_logger_open_collector(this->logfile);
//  gst_printfnc_rtcp(data, mprtp_logger_collect);
//  mprtp_logger_collect("########### Report produced for after: %lu seconds ###########\n", GST_TIME_AS_SECONDS(_now(this) - this->made));
//  mprtp_logger_close_collector();
//  gst_print_rtcp(data);
done:
  this->in_progress = FALSE;
  return result;
}


void _add_xr(ReportProducer *this)
{
  GstRTCPXR *xr;
  guint16 length;
  if(!this->xr.length){
    goto done;
  }
  xr = this->actual;
  gst_rtcp_xr_init(xr);
  gst_rtcp_xr_change(xr, &this->ssrc);
  gst_rtcp_xr_add_content(xr, this->xr.databed, this->xr.length);
  gst_rtcp_header_getdown (&xr->header, NULL, NULL, NULL, NULL, &length, NULL);
  _add_length(this, length);
done:
  return;
}

void _add_xrblock(ReportProducer *this, GstRTCPXRBlock *block)
{
  guint8 *pos;
  guint16 block_length;
  gst_rtcp_xr_block_getdown(block, NULL, &block_length, NULL);
  block_length = (block_length + 1) << 2;
  memcpy(this->xr.actual_block, block, block_length);
  pos = this->xr.actual_block;
  pos += block_length;
  this->xr.actual_block = pos;
  this->xr.length += block_length;
}

void _add_length(ReportProducer *this, guint16 length)
{
  guint16 main_length;
  guint8 block_length;
  gst_mprtcp_block_getdown(&this->block->info, NULL, &block_length, NULL);
  block_length += (guint8) length + 1;
  gst_mprtcp_block_change(&this->block->info, NULL, &block_length, NULL);

  gst_rtcp_header_getdown (&this->report->header, NULL, NULL, NULL, NULL, &main_length, NULL);
  main_length = block_length + 3;
  gst_rtcp_header_change(&this->report->header, NULL, NULL, NULL, NULL, &main_length, &this->ssrc);
  this->length = (main_length + 1)<<2;
  this->actual = this->length + (gchar*)this->databed;
}


void _add_afb(ReportProducer *this,
                                guint32 media_source_ssrc,
                                guint32  fci_id,
                                gpointer fci_dat,
                                guint fci_dat_len)
{
  guint16 length;
  GstRTCPFB *fb;
  fb = this->actual;
  gst_rtcp_afb_init(fb);
  gst_rtcp_afb_change(fb, &this->ssrc, &media_source_ssrc, &fci_id);
  gst_rtcp_afb_setup_fci_data(fb, fci_dat, fci_dat_len);
  gst_rtcp_header_getdown (&fb->header, NULL, NULL, NULL, NULL, &length, NULL);
  _add_length(this, length);
}


#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
