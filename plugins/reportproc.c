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
_processing_afb (ReportProcessor *this,
                 GstRTCPFB *afb,
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

static void
_logging(
    ReportProcessor *this,
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

  memset(this->logfile, 0, 255);
  sprintf(this->logfile, "report_processor.log");
}

void report_processor_set_ssrc(ReportProcessor *this, guint32 ssrc)
{
  THIS_WRITELOCK(this);
  this->ssrc = ssrc;
  THIS_WRITEUNLOCK(this);
}

void report_processor_process_mprtcp(ReportProcessor * this, GstBuffer* buffer, GstMPRTCPReportSummary* result)
{
  guint32 ssrc;
  GstMapInfo map = GST_MAP_INFO_INIT;
  GstMPRTCPSubflowReport *report;
  GstMPRTCPSubflowBlock *block;

  gst_buffer_map(buffer, &map, GST_MAP_READ);
  report = (GstMPRTCPSubflowReport *)map.data;
  gst_mprtcp_report_getdown(report, &ssrc);
  //Todo: SSRC filter here
  if(0 && ssrc != this->ssrc){
      g_warning("Wrong SSRC to process");
  }
  result->ssrc = ssrc;
  result->updated = _now(this);
  block = gst_mprtcp_get_first_block(report);
  _processing_mprtcp_subflow_block(this, block, result);
  _logging(this, result);
  gst_buffer_unmap(buffer, &map);
}

void report_processor_set_logfile(ReportProcessor *this, const gchar *logfile)
{
  THIS_WRITELOCK(this);
  strcpy(this->logfile, logfile);
  THIS_WRITEUNLOCK(this);
}

void _processing_mprtcp_subflow_block (
    ReportProcessor *this,
    GstMPRTCPSubflowBlock * block,
    GstMPRTCPReportSummary* summary)
{
  guint8 pt;
  guint8 block_length;
  guint8 processed_length;
  guint8 rsvd;
  guint16 actual_length;
  guint16 subflow_id;
  GstRTCPHeader *header;
  gpointer databed, actual;
  gst_mprtcp_block_getdown(&block->info, NULL, &block_length, &subflow_id);
  summary->subflow_id = subflow_id;
  actual = databed = header = &block->block_header;
  processed_length = 0;

again:
  gst_rtcp_header_getdown (header, NULL, NULL, &rsvd, &pt, &actual_length, NULL);

  switch(pt){
    case GST_RTCP_TYPE_SR:
      {
        GstRTCPSR* sr = (GstRTCPSR*)header;
        _processing_srblock (this, &sr->sender_block, summary);
      }
    break;
    case GST_RTCP_TYPE_RTPFB:
      if(rsvd == GST_RTCP_PSFB_TYPE_AFB){
        GstRTCPFB *afb = (GstRTCPFB*) header;
        _processing_afb(this, afb, summary);
      }
      break;
    case GST_RTCP_TYPE_RR:
      {
        GstRTCPRR* rr = (GstRTCPRR*)header;
        _processing_rrblock (this, &rr->blocks, summary);
      }
    break;
    case GST_RTCP_TYPE_XR:
    {
      guint8 xr_block_type;
      GstRTCPXR* xr = (GstRTCPXR*) header;
      gst_rtcp_xr_block_getdown(xr, &xr_block_type, NULL,  NULL);
      switch(xr_block_type){
        case GST_RTCP_XR_RFC3611_BLOCK_TYPE_IDENTIFIER:
          _processing_xr_rfc3611(this, (GstRTCPXR_RFC3611*)xr, summary);
        break;
        case GST_RTCP_XR_RFC7243_BLOCK_TYPE_IDENTIFIER:
          _processing_xr_7243(this, (GstRTCPXR_RFC7243*)xr, summary);
        break;
        case GST_RTCP_XR_OWD_BLOCK_TYPE_IDENTIFIER:
          _processing_xr_owd(this, (GstRTCPXR_OWD*)xr, summary);
        break;
        case GST_RTCP_XR_RFC7097_BLOCK_TYPE_IDENTIFIER:
          _processing_xr_rfc7097(this, (GstRTCPXR_RFC7097*)xr, summary);
          break;
        case GST_RTCP_XR_OWD_RLE_BLOCK_TYPE_IDENTIFIER:
          _processing_xr_owd_rle(this, (GstRTCPXR_OWD_RLE*)xr, summary);
          break;
        default:
          GST_WARNING_OBJECT(this, "Unrecognized RTCP XR REPORT (%d)", xr_block_type);
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
  g_print("fraction lost: %d lost_rate: %f\n", fraction_lost, summary->RR.lost_rate);

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
_processing_afb (ReportProcessor *this,
                 GstRTCPFB *afb,
                 GstMPRTCPReportSummary* summary)
{
  summary->AFB.processed = TRUE;
  gst_rtcp_afb_getdown(afb,
                       NULL,
                       &summary->AFB.media_source_ssrc,
                       &summary->AFB.fci_id);

  gst_rtcp_afb_getdown_fci_data(afb,
                                summary->AFB.fci_data,
                                &summary->AFB.fci_length);
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
  summary->XR_RFC7097.length = 0;
  summary->XR_RFC7097.total  = 0;

  chunks_num = gst_rtcp_xr_rfc7097_get_chunks_num(xrb);
  for(chunk_index = 0;
      chunk_index < chunks_num;
      ++chunk_index)
  {
      chunk = gst_rtcp_xr_rfc7097_get_chunk(xrb, chunk_index);

      //Terminate chunk
      if(chunk->chunk_type == 0 && chunk->run_length == 0 && chunk->run_type == 0) break;
      gst_rtcp_xr_chunk_getdown(chunk, NULL, NULL, &running_length);
      summary->XR_RFC7097.values[chunk_index] = running_length;
      summary->XR_RFC7097.total += running_length;
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
//  summary->XR_RFC3611.length = 0;

  chunks_num = gst_rtcp_xr_rfc3611_get_chunks_num(xrb);
  for(chunk_index = 0;
      chunk_index < chunks_num;
      ++chunk_index)
  {
      chunk = gst_rtcp_xr_rfc3611_get_chunk(xrb, chunk_index);

      //Terminate chunk
      if(chunk->chunk_type == 0 && chunk->run_length == 0 && chunk->run_type == 0) break;
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
  guint32 offset;
  GstClockTime abs_offset;
  summary->XR_OWD_RLE.processed = TRUE;
  summary->XR_OWD_RLE.length = 0;

  gst_rtcp_xr_owd_rle_getdown(xrb, NULL, NULL, NULL, &offset, NULL, NULL);
  abs_offset = offset;
  abs_offset *= GST_SECOND;
  summary->XR_OWD_RLE.offset = abs_offset;
  chunks_num = gst_rtcp_xr_owd_rle_get_chunks_num(xrb);
  for(chunk_index = 0;
      chunk_index < chunks_num;
      ++chunk_index)
  {
      chunk = gst_rtcp_xr_owd_rle_get_chunk(xrb, chunk_index);

      //Terminate chunk
      if(chunk->chunk_type == 0 && chunk->run_length == 0 && chunk->run_type == 0) break;
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

void _logging(ReportProcessor *this, GstMPRTCPReportSummary* summary)
{
  mprtp_logger(this->logfile,
               "###################################################################\n"
               "Summary created at %lu\n"
               "Subflow id: %hu\n"
               "SSRC: %u\n"
               ,
               summary->created,
               summary->subflow_id,
               summary->ssrc
   );

  if(summary->RR.processed){

    mprtp_logger(this->logfile,
                 "-------------------------- Receiver Report ---------------------------\n"
                 "HSSN:            %hu\n"
                 "RTT:             %lu\n"
                 "cum_packet_lost: %u\n"
                 "cycle_num:       %hu\n"
                 "jitter:          %u\n"
                 "lost_rate:       %f\n"
                 ,
                 summary->RR.HSSN,
                 summary->RR.RTT,
                 summary->RR.cum_packet_lost,
                 summary->RR.cycle_num,
                 summary->RR.jitter,
                 summary->RR.lost_rate
    );
  }

  if(summary->SR.processed){

    mprtp_logger(this->logfile,
                 "-------------------------- Sender Report ---------------------------\n"
                 "ntptime:            %lu\n"
                 "octet_count:        %u\n"
                 "packet_count:       %u\n"
                 "rtptime:            %u\n"
                 ,
                 summary->SR.ntptime,
                 summary->SR.octet_count,
                 summary->SR.packet_count,
                 summary->SR.rtptime
    );
  }

  if(summary->XR_OWD.processed){

    mprtp_logger(this->logfile,
                 "-------------------------- XR OWD ---------------------------\n"
                 "interval_metric:    %d\n"
                 "max_delay:          %lu\n"
                 "min_delay:          %lu\n"
                 "median_delay:       %lu\n"
                 ,
                 summary->XR_OWD.interval_metric,
                 summary->XR_OWD.max_delay,
                 summary->XR_OWD.min_delay,
                 summary->XR_OWD.median_delay
    );
  }

  if(summary->XR_RFC7243.processed){

    mprtp_logger(this->logfile,
                 "-------------------------- XR_RFC7243 ---------------------------\n"
                 "interval_metric:    %d\n"
                 "early_bit:          %d\n"
                 "discarded_bytes:    %u\n"
                 ,
                 summary->XR_RFC7243.interval_metric,
                 summary->XR_RFC7243.early_bit,
                 summary->XR_RFC7243.discarded_bytes
    );
  }

  if(summary->XR_OWD_RLE.processed){
      gint i;
      mprtp_logger(this->logfile,
                   "-------------------------- XR OWD RLE ---------------------------\n"
                   "length:    %d\n"
                   "offset:    %lu\n"
                   ,
                   summary->XR_OWD_RLE.length,
                   summary->XR_OWD_RLE.offset
      );
      for(i=0; i<summary->XR_OWD_RLE.length; ++i){
          mprtp_logger(this->logfile,
                           "value %d:    %lu\n"
                           ,
                           i, summary->XR_OWD_RLE.values[i]
              );
      }
    }

  if(summary->XR_RFC3611.processed){
      gint i;
      mprtp_logger(this->logfile,
                   "-------------------------- XR_RFC3611 ---------------------------\n"
                   "length:    %d\n"
                   ,
                   summary->XR_RFC3611.length
      );
      for(i=0; i<summary->XR_RFC3611.length; ++i){
          mprtp_logger(this->logfile,
                           "value %d:    %hu\n"
                           ,
                           i, summary->XR_RFC3611.values[i]
              );
      }
    }

  if(summary->XR_RFC7097.processed){
      gint i;
      mprtp_logger(this->logfile,
                   "-------------------------- XR_RFC7097 ---------------------------\n"
                   "length:    %d\n"
                   ,
                   summary->XR_RFC7097.length
      );
      for(i=0; i<summary->XR_RFC7097.length; ++i){
          mprtp_logger(this->logfile,
                           "value %d:    %hu\n"
                           ,
                           i, summary->XR_RFC7097.values[i]
              );
      }
    }
}

#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
