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
#include "mprtprpath.h"
#include "streamjoiner.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define THIS_READLOCK(this)
#define THIS_READUNLOCK(this)
#define THIS_WRITELOCK(this)
#define THIS_WRITEUNLOCK(this)

//#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
//#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
//#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
//#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

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
_add_length(
    ReportProducer *this,
    guint16 additional_length);

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
}

void
report_producer_init (ReportProducer * this)
{
  g_rw_lock_init (&this->rwmutex);

  this->sysclock   = gst_system_clock_obtain ();
  this->ssrc       = g_random_int();
  this->report       = this->databed = g_malloc0(DATABED_LENGTH);
}

void report_producer_set_ssrc(ReportProducer *this, guint32 ssrc)
{
  THIS_WRITELOCK(this);
  this->ssrc = ssrc;
  THIS_WRITEUNLOCK(this);
}

void report_producer_begin(ReportProducer *this, guint8 subflow_id)
{
  THIS_WRITELOCK(this);
  memset(this->databed, 0, DATABED_LENGTH);
  gst_mprtcp_report_init(this->report);
  this->block = gst_mprtcp_riport_add_block_begin(this->report, (guint16) subflow_id);
  this->actual = &this->block->block_header;
  this->length = 0;
  THIS_WRITEUNLOCK(this);
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
  THIS_WRITELOCK(this);
  rr = gst_mprtcp_riport_block_add_rr(this->block);
  gst_rtcp_rr_add_rrb (rr,
                           this->ssrc,
                           fraction_lost,
                           total_lost,
                           ext_hsn,
                           jitter,
                           LSR,
                           DLSR);
  gst_rtcp_header_getdown (&rr->header, NULL, NULL, NULL, NULL, &length, NULL);
  _add_length(this, length);
  THIS_WRITEUNLOCK(this);
}

void report_producer_add_xr_rfc7243(ReportProducer *this,
                                    guint32 late_discarded_bytes)
{
  guint16 length;
//  guint8 block_length;
  GstRTCPXR_RFC7243 *xr;
  guint8 flag = RTCP_XR_RFC7243_I_FLAG_INTERVAL_DURATION;
  gboolean early_bit = FALSE;
  THIS_WRITELOCK(this);
  xr = this->actual;
  gst_rtcp_xr_rfc7243_init(xr);
  gst_rtcp_xr_rfc7243_setup(xr, flag, early_bit, this->ssrc, late_discarded_bytes);
  gst_rtcp_header_getdown (&xr->header, NULL, NULL, NULL, NULL, &length, NULL);
  _add_length(this, length);
  THIS_WRITEUNLOCK(this);
}

void report_producer_add_xr_owd(ReportProducer *this,
                                guint32 median_delay,
                                guint32 min_delay,
                                guint32 max_delay)
{
  guint16 length;
//  guint8 block_length;
  GstRTCPXR_OWD *xr;
  THIS_WRITELOCK(this);
  xr = this->actual;
  gst_rtcp_xr_owd_init(xr);
  gst_rtcp_xr_owd_change(xr, NULL, &this->ssrc, &median_delay, &min_delay, &max_delay);
  gst_rtcp_header_getdown (&xr->header, NULL, NULL, NULL, NULL, &length, NULL);
  _add_length(this, length);
  THIS_WRITEUNLOCK(this);
}

void report_producer_add_afb(ReportProducer *this,
                                guint32 media_source_ssrc,
                                guint32  fci_id,
                                gpointer fci_dat,
                                guint fci_dat_len)
{
  guint16 length;
//  guint8 block_length;
  GstRTCPFB *fb;
  THIS_WRITELOCK(this);
  fb = this->actual;
  gst_rtcp_afb_init(fb);
  gst_rtcp_afb_change(fb, &this->ssrc, &media_source_ssrc, &fci_id);
  gst_rtcp_afb_setup_fci_data(fb, fci_dat, fci_dat_len);
  gst_rtcp_header_getdown (&fb->header, NULL, NULL, NULL, NULL, &length, NULL);
  _add_length(this, length);
  THIS_WRITEUNLOCK(this);
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
  THIS_WRITELOCK(this);
  sr = gst_mprtcp_riport_block_add_sr(this->block);
  gst_rtcp_srb_setup(&sr->sender_block, ntp_timestamp, rtp_timestamp, packet_count, octet_count);
  gst_rtcp_header_getdown (&sr->header, NULL, NULL, NULL, NULL, &length, NULL);
  _add_length(this, length);
  THIS_WRITEUNLOCK(this);
}

void report_producer_add_xr_rfc7097(ReportProducer *this,
                                    guint8 thinning,
                                    guint16 begin_seq,
                                    guint16 end_seq,
                                    GstRTCPXR_Chunk *chunks,
                                    guint chunks_num)
{
  guint16 length;
  guint16 xr_block_length;
  GstRTCPXR_RFC7097 *xr;
  gboolean early_bit = FALSE;
  THIS_WRITELOCK(this);
  xr = this->actual;
  gst_rtcp_xr_rfc7097_init(xr);
  memcpy(xr->chunks, chunks, chunks_num * 2);
  gst_rtcp_header_getdown(&xr->header, NULL, NULL, NULL, NULL, &length, NULL);
  gst_rtcp_xr_block_getdown((GstRTCPXR*) xr, NULL, &xr_block_length, NULL);
  length+=(chunks_num-2)>>1;
  xr_block_length += (chunks_num-2)>>1;
  gst_rtcp_xr_block_change((GstRTCPXR*) xr, NULL, &xr_block_length, NULL);
  gst_rtcp_xr_rfc7097_change(xr, &early_bit, &thinning, &this->ssrc, &begin_seq, &end_seq);
  gst_rtcp_header_change(&xr->header, NULL, NULL, NULL, NULL, &length, NULL);
  _add_length(this, length);
  THIS_WRITEUNLOCK(this);
}

void report_producer_add_xr_rfc3611(ReportProducer *this,
                                    guint8 thinning,
                                    guint16 begin_seq,
                                    guint16 end_seq,
                                    GstRTCPXR_Chunk *chunks,
                                    guint chunks_num)
{
  guint16 length;
  guint16 xr_block_length;
  GstRTCPXR_RFC3611 *xr;
  gboolean early_bit = FALSE;
  THIS_WRITELOCK(this);
  xr = this->actual;
  gst_rtcp_xr_rfc3611_init(xr);
  memcpy(xr->chunks, chunks, chunks_num * 2);
  gst_rtcp_header_getdown(&xr->header, NULL, NULL, NULL, NULL, &length, NULL);
  gst_rtcp_xr_block_getdown((GstRTCPXR*) xr, NULL, &xr_block_length, NULL);
  length+=(chunks_num-2)>>1;
  xr_block_length += (chunks_num-2)>>1;
  gst_rtcp_xr_block_change((GstRTCPXR*) xr, NULL, &xr_block_length, NULL);
  gst_rtcp_xr_rfc3611_change(xr, &early_bit, &thinning, &this->ssrc, &begin_seq, &end_seq);
  gst_rtcp_header_change(&xr->header, NULL, NULL, NULL, NULL, &length, NULL);
  _add_length(this, length);
  THIS_WRITEUNLOCK(this);
}

void report_producer_add_xr_owd_rle(ReportProducer *this,
                                    guint8 resolution,
                                    guint16 begin_seq,
                                    guint16 end_seq,
                                    GstRTCPXR_Chunk *chunks,
                                    guint chunks_num,
                                    guint32 offset)
{
  guint16 length;
  guint16 xr_block_length;
  GstRTCPXR_OWD_RLE *xr;
  gboolean early_bit = FALSE;
  THIS_WRITELOCK(this);
  xr = this->actual;
  gst_rtcp_xr_owd_rle_init(xr);
  memcpy(xr->chunks, chunks, chunks_num * 2);
  gst_rtcp_header_getdown(&xr->header, NULL, NULL, NULL, NULL, &length, NULL);
  gst_rtcp_xr_block_getdown((GstRTCPXR*) xr, NULL, &xr_block_length, NULL);
  length+=(chunks_num-2)>>1;
  xr_block_length += (chunks_num-2)>>1;
  gst_rtcp_xr_block_change((GstRTCPXR*) xr, NULL, &xr_block_length, NULL);
  gst_rtcp_xr_owd_rle_change(xr, &early_bit, &resolution, &this->ssrc, &offset, &begin_seq, &end_seq);
  gst_rtcp_header_change(&xr->header, NULL, NULL, NULL, NULL, &length, NULL);
  _add_length(this, length);
  THIS_WRITEUNLOCK(this);
}

GstBuffer *report_producer_end(ReportProducer *this, guint *length)
{
  gpointer data;
  GstBuffer* result = NULL;
  THIS_WRITELOCK(this);
//  gst_mprtcp_riport_add_block_end(this->report, this->block);
//  g_print("length: %lu\n", this->length);
//  gst_print_rtcp(this->databed);
  data = g_malloc0(this->length);
  memcpy(data, this->databed, this->length);
  result = gst_buffer_new_wrapped(data, this->length);
  if(length) {
    *length = this->length;
  }
  THIS_WRITEUNLOCK(this);
  return result;
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





#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
