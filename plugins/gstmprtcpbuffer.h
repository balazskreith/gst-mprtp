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

#define MPRTCP_PACKET_DEFAULT_MTU 1400
#define MPRTCP_PACKET_TYPE_IDENTIFIER 212
#define GST_RTCP_TYPE_XR 207
#define GST_MPRTCP_BLOCK_TYPE_SUBFLOW_INFO 0
#define GST_RTCP_XR_RFC7243_BLOCK_TYPE_IDENTIFIER 26
#define GST_RTCP_XR_SKEW_BLOCK_TYPE_IDENTIFIER 27
#define GST_RTCP_XR_OWD_BLOCK_TYPE_IDENTIFIER 28
#define RTCP_XR_RFC7243_I_FLAG_INTERVAL_DURATION 2
#define RTCP_XR_RFC7243_I_FLAG_SAMPLED_METRIC 1
#define RTCP_XR_RFC7243_I_FLAG_CUMULATIVE_DURATION 3

#define current_unix_time_in_us g_get_real_time ()
#define current_unix_time_in_ms (current_unix_time_in_us / 1000L)
#define current_unix_time_in_s  (current_unix_time_in_ms / 1000L)
#define epoch_now_in_ns ((guint64)((current_unix_time_in_us + 2208988800000000LL) * 1000))
#define get_ntp_from_epoch_ns(epoch_in_ns) gst_util_uint64_scale (epoch_in_ns, (1LL << 32), GST_SECOND)
#define get_epoch_time_from_ntp_in_ns(ntp_time) gst_util_uint64_scale (ntp_time, GST_SECOND, (1LL << 32))
#define NTP_NOW get_ntp_from_epoch_ns(epoch_now_in_ns)

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

typedef struct PACKED _GstRTCPXR
{
  GstRTCPHeader header;
  guint8 block_type;
  guint8 reserved;
  guint16 block_length;
}GstRTCPXR;

typedef struct PACKED _GstRTCPXR_RFC7243
{                               //RTCP extended riport Discarded bytes
  GstRTCPHeader header;
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
  guint32 discarded_bytes;
} GstRTCPXR_RFC7243;


typedef struct PACKED _GstRTCPXR_OWD
{
  GstRTCPHeader header;
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
  guint16 percentile;
  guint16 invert_percentile;
  guint32 one_way_delay;
  guint32 max_delay;
  guint32 min_delay;
} GstRTCPXR_OWD;


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
    GstRTCPXR_RFC7243 xr_rfc7243_riport;
    GstRTCPXR_OWD     xr_owd;
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

GstRTCPSRBlock *gst_rtcp_sr_add_srb (GstRTCPSR * sr);

GstRTCPRRBlock *gst_rtcp_sr_add_rrb (GstRTCPSR * sr);

GstMPRTCPSubflowReport *gst_mprtcp_add_riport (GstRTCPHeader * header);

GstMPRTCPSubflowBlock *gst_mprtcp_riport_add_block_begin (GstMPRTCPSubflowReport
    * report, guint16 subflow_id);

void
gst_mprtcp_riport_append_block (GstMPRTCPSubflowReport * report,
    GstMPRTCPSubflowBlock * block);

GstRTCPSR *gst_mprtcp_riport_block_add_sr (GstMPRTCPSubflowBlock * block);

GstRTCPRR *gst_mprtcp_riport_block_add_rr (GstMPRTCPSubflowBlock * block);

GstRTCPXR_RFC7243 *gst_mprtcp_riport_block_add_xr_rfc2743 (GstMPRTCPSubflowBlock
    * block);

GstRTCPXR_OWD *
gst_mprtcp_riport_block_add_xr_owd (GstMPRTCPSubflowBlock * block);
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

void gst_rtcp_xr_block_getdown (GstRTCPXR *report, guint8 *block_type,
    guint16 * block_length, guint8 *reserved);

void gst_rtcp_xr_rfc7243_init (GstRTCPXR_RFC7243 * report);
void gst_rtcp_xr_rfc7243_setup (GstRTCPXR_RFC7243 * report,
    guint8 interval_metric, gboolean early_bit,
    guint32 ssrc, guint32 discarded_bytes);
void gst_rtcp_xr_rfc7243_change (GstRTCPXR_RFC7243 * report,
    guint8 * interval_metric, gboolean * early_bit, guint32 * ssrc,
    guint32 * discarded_bytes);
void gst_rtcp_xr_rfc7243_getdown (GstRTCPXR_RFC7243 * report,
    guint8 * interval_metric, gboolean * early_bit, guint32 * ssrc,
    guint32 * discarded_bytes);



void gst_rtcp_xr_owd_init (GstRTCPXR_OWD * report);

void gst_rtcp_xr_owd_setup(GstRTCPXR_OWD *report,
                            guint8  interval_metric,
                            guint32 ssrc,
                            guint16 percentile,
                            guint16 invert_percentile,
                            guint32 one_way_delay,
                            guint32 min_delay,
                            guint32 max_delay);

void gst_rtcp_xr_owd_change (GstRTCPXR_OWD *report,
                             guint8  *interval_metric,
                            guint32 *ssrc,
                            guint16 *percentile,
                            guint16 *invert_percentile,
                            guint32 *one_way_delay,
                            guint32 *min_delay,
                            guint32 *max_delay);

void gst_rtcp_xr_owd_getdown(GstRTCPXR_OWD *report,
                             guint8  *interval_metric,
                             guint32 *ssrc,
                             guint16 *percentile,
                             guint16 *invert_percentile,
                             guint32 *one_way_delay,
                             guint32 *min_delay,
                             guint32 *max_delay);



void gst_mprtcp_report_init (GstMPRTCPSubflowReport * report);
void gst_mprtcp_riport_setup (GstMPRTCPSubflowReport * report, guint32 ssrc);
void gst_mprtcp_riport_getdown (GstMPRTCPSubflowReport * report,
    guint32 * ssrc);


void gst_mprtcp_block_init (GstMPRTCPSubflowBlock * block);
void gst_mprtcp_block_setup (GstMPRTCPSubflowInfo * info,
    guint8 type, guint8 block_length, guint16 subflow_id);
void gst_mprtcp_block_change (GstMPRTCPSubflowInfo * info,
    guint8 * type, guint8 * block_length, guint16 * subflow_id);
void gst_mprtcp_block_getdown (GstMPRTCPSubflowInfo * info,
    guint8 * type, guint8 * block_length, guint16 * subflow_id);

void gst_print_mprtcp (GstMPRTCPSubflowReport * report);
void gst_print_mprtcp_block (GstMPRTCPSubflowBlock * block, guint8 *block_length);
void gst_print_rtcp_check_sr (GstRTCPBuffer * rtcp, gint offset);
void gst_print_rtcp_check_srb (GstRTCPSRBlock * block_ptr,
    GstRTCPPacket * packet);
void gst_print_rtcp_check_rrb (GstRTCPRRBlock * block_ptr, gint index,
    GstRTCPPacket * packet);

void gst_print_rtcp_buffer (GstRTCPBuffer * buffer);
void gst_print_rtcp (GstRTCPHeader * header);
void gst_print_rtcp_header (GstRTCPHeader * header);
void gst_print_rtcp_sr (GstRTCPSR * report);
void gst_print_rtcp_rr (GstRTCPRR * report);
void gst_print_rtcp_xr_7243 (GstRTCPXR_RFC7243 * report);
void gst_print_rtcp_xr_owd (GstRTCPXR_OWD * report);
void gst_print_rtcp_srb (GstRTCPSRBlock * block_ptr);
void gst_print_rtcp_rrb (GstRTCPRRBlock * block_ptr);



#include <gst/rtp/gstrtpbuffer.h>
void gst_print_rtp_packet_info (GstRTPBuffer * rtp);

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
