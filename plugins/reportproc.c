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
#include "reportproc.h"
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

GST_DEBUG_CATEGORY_STATIC (report_processor_debug_category);
#define GST_CAT_DEFAULT report_processor_debug_category

G_DEFINE_TYPE (ReportProcessor, report_processor, G_TYPE_OBJECT);

#define _now(this) (gst_clock_get_time (this->sysclock))

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
report_processor_finalize (GObject * object);

static void
_processing_mprtcp_subflow_block (
    ReportProcessor *this,
    GstMPRTCPSubflowBlock * block,
    GstMPRTCPReportSummary* summary);

static void
_processing_rrblock (
    ReportProcessor *this,
    GstRTCPRRBlock * rrb,
    GstMPRTCPReportSummary* summary);

static void
_processing_xr_7243 (
    ReportProcessor *this,
    GstRTCPXR_RFC7243 * xrb,
    GstMPRTCPReportSummary* summary);

static void
_processing_xr_owd (
    ReportProcessor *this,
    GstRTCPXR_OWD * xrb,
    GstMPRTCPReportSummary* summary);

static void
_processing_xr_rfc7097 (
    ReportProcessor *this,
    GstRTCPXR_RFC7097 * xrb,
    GstMPRTCPReportSummary* summary);

static void
_processing_xr_rfc3611 (
    ReportProcessor *this,
    GstRTCPXR_RFC3611 * xrb,
    GstMPRTCPReportSummary* summary);

static void
_processing_xr_owd_rle(
    ReportProcessor *this,
    GstRTCPXR_OWD_RLE * xrb,
    GstMPRTCPReportSummary* summary);

static void
_processing_srblock (
    ReportProcessor *this,
    GstRTCPSRBlock * rrb,
    GstMPRTCPReportSummary* summary);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
report_processor_class_init (ReportProcessorClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = report_processor_finalize;

  GST_DEBUG_CATEGORY_INIT (report_processor_debug_category, "report_processor", 0,
      "MpRTP Receiving Controller");

}

void
report_processor_finalize (GObject * object)
{
  ReportProcessor *this = REPORTPROCESSOR (object);
  g_object_unref (this->sysclock);
}

void
report_processor_init (ReportProcessor * this)
{
  g_rw_lock_init (&this->rwmutex);

  this->sysclock   = gst_system_clock_obtain ();
  this->ssrc       = g_random_int();
}

void report_processor_set_ssrc(ReportProcessor *this, guint32 ssrc)
{
  THIS_WRITELOCK(this);
  this->ssrc = ssrc;
  THIS_WRITEUNLOCK(this);
}

GstMPRTCPReportSummary* report_processor_process_mprtcp(ReportProcessor * this, GstBuffer* buffer)
{
  guint32 ssrc;
  GstMPRTCPReportSummary* result;
  GstMapInfo map = GST_MAP_INFO_INIT;
  GstMPRTCPSubflowReport *report;
  GstMPRTCPSubflowBlock *block;
  result = g_malloc(sizeof(GstMPRTCPReportSummary));
  gst_buffer_map(buffer, &map, GST_MAP_READ);
  report = (GstMPRTCPSubflowReport *)map.data;
  gst_mprtcp_report_getdown(report, &ssrc);
  //Todo: SSRC filter here
  if(0 && ssrc != this->ssrc){
      g_warning("Wrong SSRC to process");
  }
  result->ssrc = ssrc;
  result->created = _now(this);
  block = gst_mprtcp_get_first_block(report);
  _processing_mprtcp_subflow_block(this, block, result);
  gst_buffer_unmap(buffer, &map);
  return result;
}

void _processing_mprtcp_subflow_block (
    ReportProcessor *this,
    GstMPRTCPSubflowBlock * block,
    GstMPRTCPReportSummary* summary)
{
  guint8 pt;
  guint8 block_length;
  guint8 processed_length;
  guint16 actual_length;
  guint16 subflow_id;
  GstRTCPHeader *header;
  gpointer databed, actual;
  gst_mprtcp_block_getdown(&block->info, NULL, &block_length, &subflow_id);
  summary->subflow_id = subflow_id;
  actual = databed = header = &block->block_header;
  processed_length = 0;

again:
  gst_rtcp_header_getdown (header, NULL, NULL, NULL, &pt, &actual_length, NULL);

  switch(pt){
    case GST_RTCP_TYPE_SR:
      _processing_srblock (this, &block->sender_riport.sender_block, summary);
    break;
    case GST_RTCP_TYPE_RR:
      _processing_rrblock (this, &block->receiver_riport.blocks, summary);
    break;
    case GST_RTCP_TYPE_XR:
    {
      guint8 xr_block_type;
      gst_rtcp_xr_block_getdown((GstRTCPXR*) &block->xr_header, &xr_block_type, NULL,  NULL);
      switch(xr_block_type){
        case GST_RTCP_XR_RFC3611_BLOCK_TYPE_IDENTIFIER:
          _processing_xr_rfc3611(this, &block->xr_rfc3611_report, summary);
        break;
        case GST_RTCP_XR_RFC7243_BLOCK_TYPE_IDENTIFIER:
          _processing_xr_7243(this, &block->xr_rfc7243_riport, summary);
        break;
        case GST_RTCP_XR_OWD_BLOCK_TYPE_IDENTIFIER:
          _processing_xr_owd(this, &block->xr_owd, summary);
        break;
        case GST_RTCP_XR_RFC7097_BLOCK_TYPE_IDENTIFIER:
          _processing_xr_rfc7097(this, &block->xr_rfc7097_report, summary);
          break;
        case GST_RTCP_XR_OWD_RLE_BLOCK_TYPE_IDENTIFIER:
          _processing_xr_owd_rle(this, &block->xr_owd_rle_report, summary);
          break;
        default:
          GST_WARNING_OBJECT(this, "Unrecognized RTCP XR REPORT");
        break;
      }
    }
    break;
    default:
      GST_WARNING_OBJECT(this, "Unrecognized MPRTCP Report block");
    break;
  }
  processed_length += actual_length + 1;
  if(processed_length < block_length){
    header = actual = processed_length * 4 + (gchar*)databed;
    goto again;
  }
}

void
_processing_rrblock (ReportProcessor *this,
                     GstRTCPRRBlock * rrb,
                     GstMPRTCPReportSummary* summary)
{
  guint64 LSR, DLSR;
  guint32 LSR_read, DLSR_read, HSSN_read;
  guint8 fraction_lost;

  summary->RR.processed = TRUE;
  //--------------------------
  //validating
  //--------------------------
  gst_rtcp_rrb_getdown (rrb, NULL, &fraction_lost, &summary->RR.cum_packet_lost,
                        &HSSN_read, &summary->RR.jitter,
                        &LSR_read, &DLSR_read);
  summary->RR.HSSN = (guint16) (HSSN_read & 0x0000FFFF);
  summary->RR.cycle_num = (guint16) (HSSN_read>>16);
  LSR = (guint64) LSR_read;
  DLSR = (guint64) DLSR_read;

  if (LSR == 0 || DLSR == 0) {
    g_warning("The Last Sent Report and the Delay Since Last Sent Report can not be 0");
    return;
  }
  //--------------------------
  //processing
  //--------------------------
  {
    guint64 diff;
    diff = ((guint32)(NTP_NOW>>16)) - LSR - DLSR;
    summary->RR.RTT = get_epoch_time_from_ntp_in_ns(diff<<16);
  }

  summary->RR.lost_rate = ((gdouble) fraction_lost) / 256.;

}


void
_processing_xr_7243 (ReportProcessor *this,
                     GstRTCPXR_RFC7243 * xrb,
                     GstMPRTCPReportSummary* summary)
{
  summary->XR_RFC7243.processed = TRUE;
  gst_rtcp_xr_rfc7243_getdown (xrb,
                               &summary->XR_RFC7243.interval_metric,
                               &summary->XR_RFC7243.early_bit,
                               NULL,
                               &summary->XR_RFC7243.discarded_bytes);
}


void
_processing_xr_owd (ReportProcessor *this,
                    GstRTCPXR_OWD * xrb,
                    GstMPRTCPReportSummary* summary)
{
  guint32 median_delay,min_delay,max_delay;

  summary->XR_OWD.processed = TRUE;

  gst_rtcp_xr_owd_getdown(xrb,
                          &summary->XR_OWD.interval_metric,
                          NULL,
                          &median_delay,
                          &min_delay,
                          &max_delay);

  summary->XR_OWD.median_delay = median_delay;
  summary->XR_OWD.median_delay<<=16;
  summary->XR_OWD.median_delay = get_epoch_time_from_ntp_in_ns(summary->XR_OWD.median_delay);
  summary->XR_OWD.min_delay = min_delay;
  summary->XR_OWD.min_delay<<=16;
  summary->XR_OWD.min_delay = get_epoch_time_from_ntp_in_ns(summary->XR_OWD.min_delay);
  summary->XR_OWD.max_delay = max_delay;
  summary->XR_OWD.max_delay<<=16;
  summary->XR_OWD.max_delay = get_epoch_time_from_ntp_in_ns(summary->XR_OWD.max_delay);

//  gst_print_rtcp_xr_owd(xrb);
  //--------------------------
  //evaluating
  //--------------------------
}



void
_processing_xr_rfc7097 (ReportProcessor *this,
                        GstRTCPXR_RFC7097 * xrb,
                        GstMPRTCPReportSummary* summary)
{
  guint chunks_num, chunk_index;
  guint16 running_length;
  GstRTCPXR_Chunk *chunk;

  summary->XR_RFC7097.processed = TRUE;

  chunks_num = gst_rtcp_xr_rfc7097_get_chunks_num(xrb);
  for(chunk_index = 0;
      chunk_index < chunks_num;
      ++chunk_index)
  {
      chunk = gst_rtcp_xr_rfc7097_get_chunk(xrb, chunk_index);

      //Terminate chunk
      if(*((guint16*)chunk) == 0) break;
      gst_rtcp_xr_chunk_getdown(chunk, NULL, NULL, &running_length);
      summary->XR_RFC7097.values[chunk_index] = running_length;
      ++summary->XR_RFC7097.length;
  }
}

void
_processing_xr_rfc3611 (ReportProcessor *this,
                        GstRTCPXR_RFC3611 * xrb,
                        GstMPRTCPReportSummary* summary)
{
  guint chunks_num, chunk_index;
  guint16 running_length;
  GstRTCPXR_Chunk *chunk;

  summary->XR_RFC3611.processed = TRUE;

  chunks_num = gst_rtcp_xr_rfc3611_get_chunks_num(xrb);
  for(chunk_index = 0;
      chunk_index < chunks_num;
      ++chunk_index)
  {
      chunk = gst_rtcp_xr_rfc3611_get_chunk(xrb, chunk_index);

      //Terminate chunk
      if(*((guint16*)chunk) == 0) break;
      gst_rtcp_xr_chunk_getdown(chunk, NULL, NULL, &running_length);
      summary->XR_RFC3611.values[chunk_index] = running_length;
      ++summary->XR_RFC3611.length;
  }
}



void
_processing_xr_owd_rle(ReportProcessor *this,
                       GstRTCPXR_OWD_RLE * xrb,
                       GstMPRTCPReportSummary* summary)
{
  guint chunks_num, chunk_index;
  guint16 running_length;
  guint64 owd;
  GstRTCPXR_Chunk *chunk;

  summary->XR_OWD_RLE.processed = TRUE;

  chunks_num = gst_rtcp_xr_owd_rle_get_chunks_num(xrb);
  for(chunk_index = 0;
      chunk_index < chunks_num;
      ++chunk_index)
  {
      chunk = gst_rtcp_xr_owd_rle_get_chunk(xrb, chunk_index);

      //Terminate chunk
      if(*((guint16*)chunk) == 0) break;
      gst_rtcp_xr_chunk_getdown(chunk, NULL, NULL, &running_length);
      if(running_length < 0x3FFF){
        gdouble x = (gdouble) running_length / 16384.;
        x *= (gdouble) GST_SECOND;
        owd = x;
      }else{
        owd = GST_SECOND;
      }
      //_irt0(subflow)->rle_delays.values[chunk_index] = (GstClockTime)running_length * GST_MSECOND;
      summary->XR_OWD_RLE.values[chunk_index] = owd;
      ++summary->XR_OWD_RLE.length;
  }
}


void
_processing_srblock(ReportProcessor *this,
                GstRTCPSRBlock * srb,
                GstMPRTCPReportSummary* summary)
{
  summary->SR.processed = TRUE;

  gst_rtcp_srb_getdown(srb,
                       &summary->SR.ntptime,
                       &summary->SR.rtptime,
                       &summary->SR.packet_count,
                       &summary->SR.octet_count);
}


#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
