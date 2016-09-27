/* GStreamer
 * Copyright (C) 2015 FIXME <fixme@example.com>
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

#ifndef _GST_MPRTCPBUFFER_H_
#define _GST_MPRTCPBUFFER_H_

#ifdef __WIN32__

#define PACKED
#pragma pack(push,1)


#else

#define PACKED __attribute__ ((__packed__))

#endif


//---------------------- STATIC ASSERT ----------------------------------
//Source: http://www.pixelbeat.org/programming/gcc/static_assert.html
#define ASSERT_CONCAT_(a, b) a##b
#define ASSERT_CONCAT(a, b) ASSERT_CONCAT_(a, b)
/* These can't be used after statements in c89. */
#ifdef __COUNTER__
#define STATIC_ASSERT(e,m) \
    ;enum { ASSERT_CONCAT(static_assert_, __COUNTER__) = 1/(!!(e)) }
#else
  /* This can't be used twice on the same line so ensure if using in headers
   * that the headers are not included twice (by wrapping in #ifndef...#endif)
   * Note it doesn't cause an issue when used on same line of separate modules
   * compiled with gcc -combine -fwhole-program.  */
#define STATIC_ASSERT(e,m) \
    ;enum { ASSERT_CONCAT(assert_line_, __LINE__) = 1/(!!(e)) }
#endif
//---------------------- STATIC ASSERT END----------------------------------


#include <gst/gst.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include "gstmprtpbuffer.h"
#include "mprtpdefs.h"

#define MPRTCP_PACKET_DEFAULT_MTU 1400
#define MPRTCP_PACKET_TYPE_IDENTIFIER 212
#define GST_RTCP_TYPE_XR 207
#define GST_MPRTCP_BLOCK_TYPE_SUBFLOW_INFO 0
#define GST_RTCP_XR_LOSS_RLE_BLOCK_TYPE_IDENTIFIER 1
#define GST_RTCP_XR_DISCARDED_PACKETS_BLOCK_TYPE_IDENTIFIER 24
#define GST_RTCP_XR_DISCARDED_RLE_BLOCK_TYPE_IDENTIFIER 25
#define GST_RTCP_XR_DISCARDED_BYTES_BLOCK_TYPE_IDENTIFIER 26
#define GST_RTCP_XR_OWD_BLOCK_TYPE_IDENTIFIER 28
#define RTCP_XR_RFC7243_I_FLAG_INTERVAL_DURATION 2
#define RTCP_XR_RFC7243_I_FLAG_SAMPLED_METRIC 1
#define RTCP_XR_RFC7243_I_FLAG_CUMULATIVE_DURATION 3
//Stands for MPCC in ascii
#define RTCPFB_MPCC_IDENTIFIER 0x4D504343

#define MPRTCP_BLOCK_TYPE_RIPORT 0

typedef struct PACKED _GstRTCPRiportHeader
{
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  unsigned int reserved:5;      /* RC */
  unsigned int padding:1;       /* padding flag */
  unsigned int version:2;       /* protocol version */
#elif G_BYTE_ORDER == G_BIG_ENDIAN
  unsigned int version:2;       /* protocol version */
  unsigned int padding:1;       /* padding flag */
  unsigned int reserved:5;      /* RC */
#else
#error "G_BYTE_ORDER should be big or little endian."
#endif
  guint8 payload_type;          /* Payload type */
  guint16 length;               /* length of the block */
  guint32 ssrc;                 /* synchronization source */
} GstRTCPHeader;

STATIC_ASSERT (sizeof (GstRTCPHeader) == 8, "GstRTCPHeader size is not ok");

typedef struct PACKED _GstRTCPReceiverRiportBlock
{
  guint32 ssrc;
  guint32 fraction_lost:8;
  guint32 cum_packet_lost:24;
  guint32 ext_hsn;
  guint32 jitter;
  guint32 LSR;
  guint32 DLSR;
} GstRTCPRRBlock;

STATIC_ASSERT (sizeof (GstRTCPRRBlock) == 24, "GstRTCPRRBlock size is not ok");

typedef struct PACKED _GstRTCPSenderRiportBlock
{
  guint64 ntptime;              /* NTP Timestamp */
  guint32 rtptime;              /* RTP Timestamp */
  guint32 packet_count;         /* Sender's Packet count */
  guint32 octet_count;          /* Sender's octet count */
} GstRTCPSRBlock;

STATIC_ASSERT (sizeof (GstRTCPSRBlock) == 20, "GstRTCPSRBlock size is not ok");

typedef struct PACKED _GstRTCPSR
{
  GstRTCPHeader header;
  GstRTCPSRBlock sender_block;
  GstRTCPRRBlock receiver_blocks;
} GstRTCPSR;

//STATIC_ASSERT(sizeof(GstRTCPSR) == 52, "GstRTCPSR size is not ok");

typedef struct PACKED _GstRTCPRR
{
  GstRTCPHeader header;
  GstRTCPRRBlock blocks;
} GstRTCPRR;


//---- Extended Reports


typedef struct PACKED _GstRTCPXRBitvectorChunk{
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  guint16 chunk_type:1;
  guint16 bitvector : 15;
#elif G_BYTE_ORDER == G_BIG_ENDIAN
  guint16 bitvector : 15;
  guint16 chunk_type:1;
#else
#error "G_BYTE_ORDER should be big or little endian."
#endif
}GstRTCPXRBitvectorChunk;


typedef struct PACKED _GstRTCPXRRLEChunk{
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  guint16 chunk_type:1;
  guint16 run_type:1;
  guint16 run_length:14;
#elif G_BYTE_ORDER == G_BIG_ENDIAN
  guint16 run_length:14;
  guint16 run_type:1;
  guint16 chunk_type:1;
#else
#error "G_BYTE_ORDER should be big or little endian."
#endif
}GstRTCPXRRLEChunk;


typedef union PACKED _GstRTCPXRChunk{
  GstRTCPXRRLEChunk       RLE;
  GstRTCPXRBitvectorChunk Bitvector;
}GstRTCPXRChunk;

typedef struct PACKED _GstRTCPXRDiscardedRLEBlock
{  //RTCP extended riport Discarded bytes
  guint8 block_type;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  guint8 reserved:3;
  guint8 early_bit:1;
  guint8 thinning:4;
#elif G_BYTE_ORDER == G_BIG_ENDIAN
  guint8 thinning:4;
  guint8 early_bit:1;
  guint8 reserved:3;
#else
#error "G_BYTE_ORDER should be big or little endian."
#endif
  guint16 block_length;
  guint32 ssrc;
  guint16 begin_seq;
  guint16 end_seq;
  GstRTCPXRChunk chunks[2];
} GstRTCPXRRLELostsRLEBlock;


typedef struct PACKED _GstRTCPXROWDBlock
{
  guint8 block_type;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  guint8 reserved:6;
  guint8 interval_metric:2;
#elif G_BYTE_ORDER == G_BIG_ENDIAN
  guint8 interval_metric:2;
  guint8 reserved:6;
#else
#error "G_BYTE_ORDER should be big or little endian."
#endif
  guint16 block_length;
  guint32 ssrc;
  guint32 median_delay;
  guint32 max_delay;
  guint32 min_delay;
} GstRTCPXROWDBlock;

typedef struct PACKED _GstRTCPXRDiscardedBlock
{                               //RTCP extended riport Discarded bytes
  guint8 block_type;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  guint8 reserved:5;
  guint8 early_bit:1;
  guint8 interval_metric:2;
#elif G_BYTE_ORDER == G_BIG_ENDIAN
  guint8 interval_metric:2;
  guint8 early_bit:1;
  guint8 reserved:5;
#else
#error "G_BYTE_ORDER should be big or little endian."
#endif
  guint16 block_length;
  guint32 ssrc;
  guint32 discarded_bytes_or_packets;
} GstRTCPXRDiscardedBlock;

typedef union PACKED _GstRTCPXRBlock
{
  struct{
    guint8 block_type;
    guint8 reserved;
    guint16 block_length;
  };
  GstRTCPXROWDBlock          owd;
  GstRTCPXRDiscardedBlock    discarded;
  GstRTCPXRRLELostsRLEBlock discarded_rle;
}GstRTCPXRBlock;

typedef struct PACKED _GstRTCPXR
{
  GstRTCPHeader  header;
  GstRTCPXRBlock blocks;
}GstRTCPXR;


typedef struct PACKED _GstRTCPFB
{
  GstRTCPHeader header;
  guint32 ssrc;
  guint32 fci_id;
  guint32 fci_data;
} GstRTCPFB;

#define RTCP_AFB_REMB_ID 0x52454042 //REMB - Receiver Estimated Maximum Bitrate

typedef struct PACKED _GstRTCPAFB_REMB{
  guint32               num_ssrc : 8;
  guint32               float_num : 24;
  guint32               ssrc_feedback;
  guint16               hssn;
  guint16               reserved;
}GstRTCPAFB_REMB;

#define RTCP_AFB_REPS_ID 0x52455053 //REPS - Receiver Estimated Maximum Bitrate

//Receiver Estimated Path Stability
typedef struct PACKED _GstRTCPAFB_REPS{
//  guint32 stability;
  #if G_BYTE_ORDER == G_LITTLE_ENDIAN
    guint32               sampling_num : 8;
    guint32               tendency : 24;
  #elif G_BYTE_ORDER == G_BIG_ENDIAN
    guint32               tendency : 24;
    guint32               sampling_num : 8;
  #else
  #error "G_BYTE_ORDER should be big or little endian."
  #endif
}GstRTCPAFB_REPS;



/*MPRTCP struct polymorphism*/

typedef struct PACKED _GstMPRTCPSubflowInfo
{
  guint8 type;
  guint8 block_length;
  guint16 subflow_id;
} GstMPRTCPSubflowInfo;

typedef struct PACKED _GstMPRTCPSubflowSRBlock
{
  GstMPRTCPSubflowInfo info;
  GstRTCPSR *riport;
} GstMPRTCPSubflowSRBlock;

typedef struct PACKED _GstMPRTCPSubflowRRBlock
{
  GstMPRTCPSubflowInfo info;
  GstRTCPRR riport;
} GstMPRTCPSubflowRRBlock;

typedef struct PACKED _GstMPRTCPSubflowBlock
{
  GstMPRTCPSubflowInfo info;
  union
  {
    GstRTCPHeader block_header;
    GstRTCPRR receiver_riport;
    GstRTCPSR sender_riport;
    GstRTCPXR xr_header;
    GstRTCPFB feedback;
  };
} GstMPRTCPSubflowBlock;

typedef struct PACKED _GstMPRTCPSubflowRiport
{
  GstRTCPHeader header;
  guint32 ssrc;
  GstMPRTCPSubflowBlock blocks;
} GstMPRTCPSubflowReport;


//-------------------------- iterator functions ------------------------------
GstRTCPHeader *gst_rtcp_get_next_header (GstRTCPBuffer * rtcp,
    GstRTCPHeader * actual);

GstRTCPHeader *gst_rtcp_get_first_header (GstRTCPBuffer * rtcp);

GstMPRTCPSubflowBlock *gst_mprtcp_get_first_block (GstMPRTCPSubflowReport *
    report);
GstMPRTCPSubflowBlock *gst_mprtcp_get_next_block (GstMPRTCPSubflowReport *
    report, GstMPRTCPSubflowBlock * actual, guint8 *act_src);

//-------------------------- begin, end, add functions declarations ----------
GstRTCPHeader *gst_rtcp_add_begin (GstRTCPBuffer * rtcp);

void gst_rtcp_add_end (GstRTCPBuffer * rtcp, GstRTCPHeader * header);

GstRTCPSR *gst_rtcp_add_sr (GstRTCPHeader * header);

GstMPRTCPSubflowReport *gst_mprtcp_add_riport (GstRTCPHeader * header);

GstMPRTCPSubflowBlock *gst_mprtcp_riport_add_block_begin (GstMPRTCPSubflowReport
    * report, guint16 subflow_id);


GstRTCPSR *gst_mprtcp_riport_block_add_sr (GstMPRTCPSubflowBlock * block);

GstRTCPRR *gst_mprtcp_riport_block_add_rr (GstMPRTCPSubflowBlock * block);

GstRTCPXR *gst_mprtcp_riport_block_add_xr (GstMPRTCPSubflowBlock * block);

GstRTCPFB *gst_mprtcp_riport_block_add_fb (GstMPRTCPSubflowBlock * block);

void
gst_mprtcp_riport_add_block_end (GstMPRTCPSubflowReport * report,
    GstMPRTCPSubflowBlock * block);


//----------------- MPRTCP ---------------------------------------

void gst_rtcp_header_init (GstRTCPHeader * header);
void gst_rtcp_header_setup (GstRTCPHeader * header, gboolean padding,
    guint8 reserved, guint8 payload_type, guint16 length, guint32 ssrc);
void gst_rtcp_header_change (GstRTCPHeader * header, guint8 * version,
    gboolean * padding, guint8 * reserved, guint8 * payload_type,
    guint16 * length, guint32 * ssrc);
void gst_rtcp_header_getdown (GstRTCPHeader * header, guint8 * version,
    gboolean * padding, guint8 * reserved, guint8 * payload_type,
    guint16 * length, guint32 * ssrc);

void gst_rtcp_sr_init (GstRTCPSR * riport_ptr);
void gst_rtcp_srb_getdown (GstRTCPSRBlock * riport_ptr,
    guint64 * ntp, guint32 * rtp, guint32 * packet_count,
    guint32 * octet_count);
void gst_rtcp_srb_setup (GstRTCPSRBlock * riport_ptr, guint64 ntp, guint32 rtp,
    guint32 packet_count, guint32 octet_count);


void gst_rtcp_rr_init (GstRTCPRR * riport_ptr);
void gst_rtcp_rrb_getdown (GstRTCPRRBlock * block_ptr, guint32 * ssrc,
    guint8 * fraction_lost, guint32 * cum_packet_lost,
    guint32 * ext_hsn, guint32 * jitter, guint32 * LSR, guint32 * DLSR);
void gst_rtcp_rr_add_rrb (GstRTCPRR * report, guint32 ssrc,
    guint8 fraction_lost, guint32 cum_packet_lost,
    guint32 ext_hsn, guint32 jitter, guint32 LSR, guint32 DLSR);
void gst_rtcp_rrb_setup (GstRTCPRRBlock * block_ptr, guint32 ssrc,
    guint8 fraction_lost, guint32 cum_packet_lost,
    guint32 ext_hsn, guint32 jitter, guint32 LSR, guint32 DLSR);
void gst_rtcp_copy_rrb_ntoh (GstRTCPRRBlock * from, GstRTCPRRBlock * to);


void gst_rtcp_xr_init (
    GstRTCPXR * report);

void gst_rtcp_xr_change (
    GstRTCPXR *report,
    guint32 *ssrc);

void gst_rtcp_xr_add_content (
    GstRTCPXR *report,
    gpointer content,
    guint16 content_length_in_bytes);

void gst_rtcp_xr_block_getdown (
    GstRTCPXRBlock *block,
    guint8 *block_type,
    guint16 * block_length,
    guint8 *reserved);

void gst_rtcp_xr_block_change (
    GstRTCPXRBlock *block,
    guint8 *block_type,
    guint16 * block_length,
    guint8 *reserved);


void
gst_rtcp_xr_discarded_packets_setup (GstRTCPXRDiscardedBlock * block, guint8 interval_metric,
    gboolean early_bit, guint32 ssrc, guint32 discarded_packets);
void
gst_rtcp_xr_discarded_packets_getdown (GstRTCPXRDiscardedBlock *block,
    guint8 * interval_metric, gboolean * early_bit, guint32 * ssrc,
    guint32 * discarded_packets);

void gst_rtcp_xr_discarded_bytes_setup (GstRTCPXRDiscardedBlock * block,
    guint8 interval_metric, gboolean early_bit,
    guint32 ssrc, guint32 discarded_bytes);
void gst_rtcp_xr_discarded_bytes_change (GstRTCPXRDiscardedBlock * block,
    guint8 * interval_metric, gboolean * early_bit, guint32 * ssrc,
    guint32 * discarded_bytes);
void gst_rtcp_xr_discarded_bytes_getdown (GstRTCPXRDiscardedBlock *block,
    guint8 * interval_metric, gboolean * early_bit, guint32 * ssrc,
    guint32 * discarded_bytes);


void
gst_rtcp_afb_init (GstRTCPFB * report);
void
gst_rtcp_afb_setup (GstRTCPFB * report,
                    guint32 packet_source_ssrc,
                    guint32 media_source_ssrc,
                    guint32 fci_id);
void
gst_rtcp_afb_change (GstRTCPFB * report,
                     guint32 *packet_source_ssrc,
                     guint32 *media_source_ssrc,
                     guint32 *fci_id);
void
gst_rtcp_afb_getdown (GstRTCPFB * report,
                      guint32 *packet_source_ssrc,
                      guint32 *media_source_ssrc,
                      guint32 *fci_id);

void
gst_rtcp_afb_remb_change (GstRTCPAFB_REMB * report,
                          guint32 *num_ssrc,
                          gfloat *float_num,
                          guint32 *ssrc_feedback,
                          guint16 *hssn);
void
gst_rtcp_afb_remb_getdown (GstRTCPAFB_REMB * report,
                           guint32 *num_ssrc,
                           gfloat *float_num,
                           guint32 *ssrc_feedback,
                           guint16 *hssn);

void
gst_rtcp_afb_reps_change (GstRTCPAFB_REPS * report,
                          guint8 *sampling_num,
                          gfloat *stability);

void
gst_rtcp_afb_reps_getdown (GstRTCPAFB_REPS * report,
                           guint8 *sampling_num,
                           gfloat *stability);

void
gst_rtcp_afb_setup_fci_data(
    GstRTCPFB * report,
    gpointer fci_dat,
    guint fci_len);

void
gst_rtcp_afb_getdown_fci_data(GstRTCPFB * report,
                              gchar *fci_dat,
                              guint *fci_len);

void
gst_rtcp_xr_chunk_ntoh_cpy (GstRTCPXRChunk *dst_chunk,
                       GstRTCPXRChunk *src_chunk);

void
gst_rtcp_xr_chunk_hton_cpy (GstRTCPXRChunk *dst_chunk,
                       GstRTCPXRChunk *src_chunk);


void
gst_rtcp_xr_rle_losts_setup(GstRTCPXRRLELostsRLEBlock *block,
                      gboolean early_bit,
                      guint8 thinning,
                      guint32 ssrc,
                      guint16 begin_seq,
                      guint16 end_seq);

void gst_rtcp_xr_rle_losts_getdown (GstRTCPXRRLELostsRLEBlock *block,
                             gboolean *early_bit,
                             guint8 *thinning,
                             guint32 *ssrc,
                             guint16 *begin_seq,
                             guint16 *end_seq);

void gst_rtcp_xr_rle_losts_change (GstRTCPXRRLELostsRLEBlock *block,
                             gboolean *early_bit,
                             guint8 *thinning,
                             guint32 *ssrc,
                             guint16 *begin_seq,
                             guint16 *end_seq);

guint gst_rtcp_xr_rle_losts_block_get_chunks_num(GstRTCPXRRLELostsRLEBlock *block);




void gst_rtcp_xr_owd_block_setup(GstRTCPXROWDBlock *report,
                            guint8  interval_metric,
                            guint32 ssrc,
                            guint32 median_delay,
                            guint32 min_delay,
                            guint32 max_delay);

void gst_rtcp_xr_owd_block_change (GstRTCPXROWDBlock *block,
                             guint8  *interval_metric,
                            guint32 *ssrc,
                            guint32 *median_delay,
                            guint32 *min_delay,
                            guint32 *max_delay);

void gst_rtcp_xr_owd_block_getdown(GstRTCPXROWDBlock *block,
                             guint8  *interval_metric,
                             guint32 *ssrc,
                             guint32 *median_delay,
                             guint32 *min_delay,
                             guint32 *max_delay);




void gst_mprtcp_report_init (GstMPRTCPSubflowReport * report);
void gst_mprtcp_riport_setup (GstMPRTCPSubflowReport * report, guint32 ssrc);
void gst_mprtcp_report_getdown (GstMPRTCPSubflowReport * report,
    guint32 * ssrc);


void gst_mprtcp_block_init (GstMPRTCPSubflowBlock * block);
void gst_mprtcp_block_setup (GstMPRTCPSubflowInfo * info,
    guint8 type, guint8 block_length, guint16 subflow_id);
void gst_mprtcp_block_change (GstMPRTCPSubflowInfo * info,
    guint8 * type, guint8 * block_length, guint16 * subflow_id);
void gst_mprtcp_block_getdown (GstMPRTCPSubflowInfo * info,
    guint8 * type, guint8 * block_length, guint16 * subflow_id);


typedef void (*printfnc)(const gchar* format, ...);

#define gst_print_mprtcp(report) \
  gst_printfnc_mprtcp(report, g_print)
void gst_printfnc_mprtcp (GstMPRTCPSubflowReport * riport, printfnc print);

#define gst_print_mprtcp_block(block, block_length) \
  gst_printfnc_mprtcp_block(block, block_length, g_print)
void gst_printfnc_mprtcp_block (GstMPRTCPSubflowBlock * block, guint8 *block_length, printfnc print);

#define gst_print_rtcp_check_sr(block, offset) \
  gst_printfnc_mprtcp_block(block, offset, g_print)
void gst_printfnc_rtcp_check_sr (GstRTCPBuffer * rtcp, gint offset, printfnc print);

#define gst_print_rtcp_check_srb(block, packet) \
    gst_printfnc_rtcp_check_srb(block, packet, g_print)
void gst_printfnc_rtcp_check_srb (GstRTCPSRBlock * block_ptr,
    GstRTCPPacket * packet, printfnc print);

#define gst_print_rtcp_check_rrb(block, index, packet) \
    gst_printfnc_rtcp_check_rrb(block, index, packet, g_print)
void gst_printfnc_rtcp_check_rrb (GstRTCPRRBlock * block_ptr, gint index,
    GstRTCPPacket * packet, printfnc print);

#define gst_print_rtcp_buffer(buffer) \
    gst_printfnc_rtcp_buffer(buffer, g_print)
void gst_printfnc_rtcp_buffer (GstRTCPBuffer * buffer, printfnc print);

#define gst_print_rtcp(header) \
    gst_printfnc_rtcp(header, g_print)
void gst_printfnc_rtcp (GstRTCPHeader * header, printfnc print);

#define gst_print_rtcp_header(header) \
    gst_printfnc_rtcp_header(header, g_print)
void gst_printfnc_rtcp_header (GstRTCPHeader * header, printfnc print);

#define gst_print_rtcp_sr(report) \
    gst_printfnc_rtcp_sr(report, g_print)
void gst_printfnc_rtcp_sr (GstRTCPSR * report, printfnc print);

#define gst_print_rtcp_rr(report) \
    gst_printfnc_rtcp_rr(report, g_print)
void gst_printfnc_rtcp_rr (GstRTCPRR * report, printfnc print);

#define gst_print_rtcp_xr(report) \
    gst_printfnc_rtcp_xr(report, g_print)
void gst_printfnc_rtcp_xr (GstRTCPXR * report, printfnc print);

#define gst_print_rtcp_xr_discarded_packets(report) \
    gst_printfnc_rtcp_xr_discarded_packets_block(report, g_print)
void
gst_printfnc_rtcp_xr_discarded_packets_block (GstRTCPXRDiscardedBlock * block, printfnc print);

#define gst_print_rtcp_xr_discarded_bytes(report) \
    gst_printfnc_rtcp_xr_discarded_bytes_block(report, g_print)
void gst_printfnc_rtcp_xr_discarded_bytes_block (GstRTCPXRDiscardedBlock * block, printfnc print);

#define gst_print_rtcp_afb(report) \
    gst_printfnc_rtcp_afb(report, g_print)
void gst_printfnc_rtcp_afb (GstRTCPFB * report, printfnc print);

#define gst_print_rtcp_afb_data(report) \
    gst_printfnc_rtcp_afb_data(report, g_print)
void gst_printfnc_rtcp_afb_data (GstRTCPFB * report, printfnc print);

#define gst_print_rtcp_xr_owd(report) \
    gst_printfnc_rtcp_xr_owd_block(report, g_print)
void gst_printfnc_rtcp_xr_owd_block (GstRTCPXROWDBlock * block, printfnc print);

#define gst_print_rtcp_xrchunks(chunk1, chunk2) \
    gst_printfnc_rtcp_xrchunks(chunk1, chunk2, g_print)
void gst_printfnc_rtcp_xrchunks(GstRTCPXRChunk * chunk1, GstRTCPXRChunk * chunk2, printfnc print);

#define gst_print_rtcp_xr_7097(report) \
    gst_printfnc_rtcp_xr_7097(report, g_print)
void gst_printfnc_rtcp_xr_rle_losts_block(GstRTCPXRRLELostsRLEBlock * block, printfnc print);

#define gst_print_rtcp_srb(block_ptr) \
    gst_printfnc_rtcp_srb(block_ptr, g_print)
void gst_printfnc_rtcp_srb (GstRTCPSRBlock * block_ptr, printfnc print);

#define gst_print_rtcp_rrb(block_ptr) \
    gst_printfnc_rtcp_rrb(block_ptr, g_print)
void gst_printfnc_rtcp_rrb (GstRTCPRRBlock * block_ptr, printfnc print);



#include <gst/rtp/gstrtpbuffer.h>
#define gst_print_rtp_buffer(buf) \
    gst_printfnc_rtp_buffer(buf, g_print)
void gst_printfnc_rtp_buffer (GstBuffer * buf, printfnc print);

#define gst_print_rtp_packet_info(buf) \
    gst_printfnc_rtp_packet_info(buf, g_print)
void gst_printfnc_rtp_packet_info (GstRTPBuffer * rtp, printfnc print);

gboolean gst_mprtp_get_subflow_extension(GstRTPBuffer *rtp,
                                         guint8 ext_header_id,
                                         MPRTPSubflowHeaderExtension **subflow_info);
gboolean gst_mprtp_get_abs_send_time_extension(GstRTPBuffer *rtp,
                                               guint8 ext_header_id,
                                               RTPAbsTimeExtension **abs_time);
#ifdef __WIN32__

#pragma pack(pop)
#undef PACKED

#else

#undef PACKED

#endif

#endif //_GST_MPRTCPBUFFER_H_
