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

#include "rtpfecbuffer.h"
#include <gst/rtp/gstrtpbuffer.h>
#include <string.h>
#include "gstmprtcpbuffer.h"


void rtpfecbuffer_cpy_header_data(GstBuffer *buf, GstRTPFECHeader *result)
{
  GstRTPBuffer       rtp = GST_RTP_BUFFER_INIT;
  gst_rtp_buffer_map(buf, GST_MAP_READ, &rtp);
  memcpy(result, gst_rtp_buffer_get_payload(&rtp), sizeof(GstRTPFECHeader));
  gst_rtp_buffer_unmap(&rtp);
}

void rtpfecbuffer_init_segment(GstRTPFECSegment *segment)
{
  memset(segment, 0, sizeof(GstRTPFECSegment));
  segment->base_sn = -1;
}


void rtpfecbuffer_setup_bitstring(GstBuffer *buf, guint8 *bitstring, gint16 *bitstring_length)
{
  GstMapInfo info = GST_MAP_INFO_INIT;
  guint16 length, written_length;
  gst_buffer_map(buf, &info, GST_MAP_READ);
  memcpy(bitstring, info.data, 8);
  length = info.size-12;
  written_length = g_htons(length);
  memcpy(bitstring + 8, &written_length, 2);
  memcpy(bitstring + 10, info.data + 12, length);
  *bitstring_length = length + 10;
  gst_buffer_unmap(buf, &info);
}



GstBuffer* rtpfecbuffer_get_rtpbuffer_by_fec(GstRTPFECSegment *segment, GstBuffer *fec, guint16 seq)
{
  GstRTPBuffer       rtp = GST_RTP_BUFFER_INIT;
  GstBasicRTPHeader *rtpheader;
  gpointer           databed;
  guint8*            fecdata;
  guint8*            rtpdata;
  guint16*           lengthptr;
  gint i, length;

  gst_rtp_buffer_map(fec, GST_MAP_READ, &rtp);
  fecdata = (guint8*) gst_rtp_buffer_get_payload(&rtp);
  segment->parity_bytes[0]^=fecdata[0];
  segment->parity_bytes[1]^=fecdata[1];

  segment->parity_bytes[4]^=fecdata[4];
  segment->parity_bytes[5]^=fecdata[5];
  segment->parity_bytes[6]^=fecdata[6];
  segment->parity_bytes[7]^=fecdata[7];

  segment->parity_bytes[8]^=fecdata[2];
  segment->parity_bytes[9]^=fecdata[3];
  lengthptr = (guint16*)&segment->parity_bytes[8];
  length = g_ntohs(*lengthptr) + 12;
  databed = g_malloc0(length);
  rtpdata = databed;
  rtpheader = (GstBasicRTPHeader*) rtpdata;

  rtpheader->version = 2;

  rtpheader->P       = segment->parity_bytes[0]>>2;
  rtpheader->X       = segment->parity_bytes[0]>>3;
  rtpheader->CC      = segment->parity_bytes[0]>>4;
  rtpheader->M       = segment->parity_bytes[1];
  rtpheader->PT      = segment->parity_bytes[1]>>1;
  rtpheader->seq_num = g_htons(seq);

  memcpy(&rtpheader->TS, &segment->parity_bytes[4], sizeof(guint32));
  rtpheader->ssrc = g_htonl(segment->ssrc);
  for(i=0; i<*lengthptr; ++i){
    rtpdata[i+12] = segment->parity_bytes[i+10] ^ fecdata[i+20];
  }
  gst_rtp_buffer_unmap(&rtp);

  return gst_buffer_new_wrapped(databed, length);

}


void gst_print_rtpfec_buffer(GstBuffer *rtpfec)
{
  GstRTPBuffer       rtp = GST_RTP_BUFFER_INIT;
  gst_rtp_buffer_map(rtpfec, GST_MAP_READ, &rtp);
  gst_print_rtp_packet_info(&rtp);
  gst_print_rtpfec_payload(gst_rtp_buffer_get_payload(&rtp));
  gst_rtp_buffer_unmap(&rtp);
}


void gst_print_rtpfec_payload(GstRTPFECHeader *header)
{

  g_print (
           "|%1d|%1d|%1d|%1d|%7d|%1d|%13d|%31d|\n"
           "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
           "|%63u|\n"
           "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
           "|%15d|%47d|\n"
           "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
           "|%63u|\n"
           "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
           "|%31d|%15d|%15d|\n"
           "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
           ,
           header->F,
           header->R,
           header->P,
           header->X,
           header->CC,
           header->M,
           header->PT,
           g_ntohs(header->length_recovery),
           g_ntohl(header->TS),
           header->SSRC_Count,
           header->reserved,
           g_ntohl(header->ssrc),
           g_ntohs(header->sn_base),
           header->N_MASK,
           header->M_MASK
        );

}
