#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <gst/gst.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include "gstmprtcpbuffer.h"



#define RTCPHEADER_BYTES 8
#define RTCPHEADER_WORDS (RTCPHEADER_BYTES>>2)
#define RTCPSRBLOCK_BYTES 20
#define RTCPSRBLOCK_WORDS (RTCPSRBLOCK_BYTES>>2)
#define RTCPXRHEADER_BYTES 8
#define RTCPXRHEADER_WORDS (RTCPXRHEADER_BYTES>>2)

#define RTCPXR7243BLOCK_BYTES 12
#define RTCPXR7243BLOCK_WORDS (RTCPXR7243BLOCK_BYTES>>2)
#define RTCPXROWDBLOCK_BYTES 20
#define RTCPXROWDBLOCK_WORDS (RTCPXROWDBLOCK_BYTES>>2)
#define RTCPFB_BYTES 8
#define RTCPFB_WORDS (RTCPFB_BYTES>>2)
#define RTCPRRBLOCK_BYTES 24
#define RTCPRRBLOCK_WORDS (RTCPRRBLOCK_BYTES>>2)
#define MPRTCPBLOCK_BYTES 4
#define MPRTCPBLOCK_WORDS (MPRTCPBLOCK_BYTES>>2)

#define RTCPXRRFC7097BLOCK_BYTES 16
#define RTCPXRRFC7097BLOCK_WORDS (RTCPXRRFC7097BLOCK_BYTES>>2)

#define RTCPXROWDRLEBLOCK_BYTES 20
#define RTCPXROWDRLEBLOCK_WORDS (RTCPXROWDRLEBLOCK_BYTES>>2)

#define RTCPXRRFC3611BLOCK_BYTES 16
#define RTCPXRRFC3611BLOCK_WORDS (RTCPXRRFC3611BLOCK_BYTES>>2)


#include <sys/time.h>
#include <stdint.h>

void
gst_rtcp_header_init (GstRTCPHeader * header)
{
  header->version = GST_RTCP_VERSION;
  gst_rtcp_header_setup (header, FALSE, 0, 0, 0, 0);
}

void
gst_rtcp_header_setup (GstRTCPHeader * header, gboolean padding,
    guint8 reserved, guint8 payload_type, guint16 length, guint32 ssrc)
{
  header->padding = padding;
  header->reserved = reserved;
  header->payload_type = payload_type;
  header->length = g_htons (length);
  header->ssrc = g_htonl (ssrc);
}

void
gst_rtcp_header_change (GstRTCPHeader * header, guint8 * version,
    gboolean * padding, guint8 * reserved, guint8 * payload_type,
    guint16 * length, guint32 * ssrc)
{
  if (version) {
    header->version = *version;
  }
  if (padding) {
    header->padding = *padding;
  }
  if (reserved) {
    header->reserved = *reserved;
  }
  if (payload_type) {
    header->payload_type = *payload_type;
  }
  if (length) {
    header->length = g_htons (*length);
  }
  if (ssrc) {
    header->ssrc = g_htonl (*ssrc);
  }
}

void
gst_rtcp_header_getdown (GstRTCPHeader * header, guint8 * version,
    gboolean * padding, guint8 * reserved, guint8 * payload_type,
    guint16 * length, guint32 * ssrc)
{
  if (version) {
    *version = header->version;
  }
  if (padding) {
    *padding = header->padding ? TRUE : FALSE;
  }
  if (reserved) {
    *reserved = header->reserved;
  }
  if (payload_type) {
    *payload_type = header->payload_type;
  }
  if (length) {
    *length = g_ntohs (header->length);
  }
  if (ssrc) {
    *ssrc = g_ntohl (header->ssrc);
  }
}

GstRTCPHeader *
gst_rtcp_add_begin (GstRTCPBuffer * rtcp)
{
  GstRTCPHeader *header;
  guint offset;
  guint8 payload_type;
  guint16 length = 0;
  g_return_val_if_fail (rtcp != NULL, NULL);
  g_return_val_if_fail (GST_IS_BUFFER (rtcp->buffer), NULL);
  g_return_val_if_fail (rtcp->map.flags & GST_MAP_READWRITE, NULL);
  header = (GstRTCPHeader *) rtcp->map.data;
  /*It would be cool if the map.size have matched with the length of the riports
     for(offset = 0;      offset < rtcp->map.size;
     offset += (gst_rtcp_header_get_length(header) + 1) << 2)
     {
     header = (GstRTCPHeader*)rtcp->map.data + offset;
     }
   */
  for (offset = 0; offset < rtcp->map.size; offset += (length + 1) << 2) {
    header = (GstRTCPHeader *) (rtcp->map.data + offset);
    gst_rtcp_header_getdown (header, NULL, NULL, NULL,
        &payload_type, &length, NULL);

    if (payload_type != MPRTCP_PACKET_TYPE_IDENTIFIER &&
        payload_type != GST_RTCP_TYPE_SR &&
        payload_type != GST_RTCP_TYPE_RR && payload_type != GST_RTCP_TYPE_XR) {
      break;
    }
  }

  //header = rtcp->map.data + 28;
  //g_print("MÁR NEM AZ %d\n", offset);

  return header;
}

void
gst_rtcp_add_end (GstRTCPBuffer * rtcp, GstRTCPHeader * header)
{
  guint16 length;
  g_return_if_fail (rtcp != NULL);
  g_return_if_fail (header != NULL);
  g_return_if_fail (GST_IS_BUFFER (rtcp->buffer));
  g_return_if_fail (rtcp->map.flags & GST_MAP_READWRITE);

  gst_rtcp_header_getdown (header, NULL, NULL, NULL, NULL, &length, NULL);
  length = (length + 1) << 2;
  g_return_if_fail (rtcp->map.size + length <= rtcp->map.maxsize);
  rtcp->map.size += length;
}

GstMPRTCPSubflowReport *
gst_mprtcp_add_riport (GstRTCPHeader * header)
{
  GstMPRTCPSubflowReport *result = (GstMPRTCPSubflowReport *) header;
  gst_mprtcp_report_init (result);
  return result;
}

GstMPRTCPSubflowBlock *
gst_mprtcp_riport_add_block_begin (GstMPRTCPSubflowReport * riport,
    guint16 subflow_id)
{
  guint8 i, src, *ptr;
  GstMPRTCPSubflowBlock *result = &riport->blocks;

  gst_rtcp_header_getdown (&riport->header, NULL, NULL, &src, NULL, NULL, NULL);
  for (ptr = (guint8 *) result, i = 0; i < src; ++i) {
    guint8 block_length;
    gst_mprtcp_block_getdown (&result->info, NULL, &block_length, NULL);
    ptr += (block_length + 1) << 2;
  }
  result = (GstMPRTCPSubflowBlock *) ptr;
  ++src;
  gst_mprtcp_block_setup (&result->info, MPRTCP_BLOCK_TYPE_RIPORT, 0,
      subflow_id);
  gst_rtcp_header_change (&riport->header, NULL, NULL, &src, NULL, NULL, NULL);
  return result;
}



GstRTCPSR *
gst_mprtcp_riport_block_add_sr (GstMPRTCPSubflowBlock * block)
{
  GstRTCPSR *result = &block->sender_riport;
  gst_rtcp_sr_init (result);
  return result;
}

GstRTCPRR *
gst_mprtcp_riport_block_add_rr (GstMPRTCPSubflowBlock * block)
{
  GstRTCPRR *result = &block->receiver_riport;
  gst_rtcp_rr_init (result);
  return result;
}

GstRTCPXR *
gst_mprtcp_riport_block_add_xr (GstMPRTCPSubflowBlock * block)
{
  GstRTCPXR *result = &block->xr_header;
  gst_rtcp_xr_init (result);
  return result;
}

GstRTCPFB *
gst_mprtcp_riport_block_add_fb (GstMPRTCPSubflowBlock * block)
{
  GstRTCPFB *result = &block->feedback;
  gst_rtcp_afb_init(result);
  return result;
}

void
gst_mprtcp_riport_add_block_end (GstMPRTCPSubflowReport * riport,
    GstMPRTCPSubflowBlock * block)
{
  guint16 riport_length, block_header_length;
  guint8 block_length;
  GstMPRTCPSubflowInfo *info = &block->info;
  GstRTCPHeader *riport_header = &riport->header;
  GstRTCPHeader *block_header = &block->block_header;

  gst_rtcp_header_getdown (block_header, NULL,
      NULL, NULL, NULL, &block_header_length, NULL);

  block_length = (guint8) block_header_length + 1;
  gst_mprtcp_block_change (info, NULL, &block_length, NULL);


  gst_rtcp_header_getdown (riport_header, NULL,
      NULL, NULL, NULL, &riport_length, NULL);

  riport_length += (guint16) block_length + 1;

  gst_rtcp_header_change (riport_header, NULL, NULL, NULL,
      NULL, &riport_length, NULL);

}

//---------------------------Iterator---------------------------

GstRTCPHeader *
gst_rtcp_get_first_header (GstRTCPBuffer * rtcp)
{
  g_return_val_if_fail (rtcp != NULL, NULL);
  g_return_val_if_fail (GST_IS_BUFFER (rtcp->buffer), NULL);
  g_return_val_if_fail (rtcp->map.flags & GST_MAP_READ, NULL);
  if (rtcp->map.size == 0) {
    return NULL;
  }
  return (GstRTCPHeader *) rtcp->map.data;
}

GstRTCPHeader *
gst_rtcp_get_next_header (GstRTCPBuffer * rtcp, GstRTCPHeader * actual)
{
  guint8 *ref;
  guint8 *start;
  guint offset = 0;
  g_return_val_if_fail (rtcp != NULL, NULL);
  g_return_val_if_fail (GST_IS_BUFFER (rtcp->buffer), NULL);
  g_return_val_if_fail (rtcp->map.flags & GST_MAP_READ, NULL);

  start = rtcp->map.data;
  ref = (guint8 *) actual;
  offset = (ref - start) + ((g_ntohs (actual->length) + 1) << 2);
  if (rtcp->map.size < offset) {
    return NULL;
  }
  return (GstRTCPHeader *) (rtcp->map.data + offset);
}

GstMPRTCPSubflowBlock *
gst_mprtcp_get_first_block (GstMPRTCPSubflowReport * riport)
{
  return &riport->blocks;
}

GstMPRTCPSubflowBlock *
gst_mprtcp_get_next_block (GstMPRTCPSubflowReport * report,
    GstMPRTCPSubflowBlock * actual, guint8 *act_src)
{
  guint8 *ptr    = (guint8*) actual;
  guint8 src;
  guint8 block_length;

  gst_rtcp_header_getdown (&report->header, NULL, NULL, &src, NULL,
      NULL, NULL);
  gst_mprtcp_block_getdown (&actual->info, NULL, &block_length, NULL);

  if(src <= ++*act_src) return NULL;
  ptr += (block_length + 1)<<2;
  return (GstMPRTCPSubflowBlock*) ptr;
}


//GstMPRTCPSubflowBlock *
//gst_mprtcp_get_next_block (GstMPRTCPSubflowReport * riport,
//    GstMPRTCPSubflowBlock * actual)
//{
//  guint8 *next = (guint8 *) actual;
//  guint8 *max_ptr = (guint8 *) (&riport->header);
//  guint16 riport_length;
//  guint8 block_length;
//  gst_rtcp_header_getdown (&riport->header, NULL, NULL, NULL, NULL,
//      &riport_length, NULL);
//  gst_mprtcp_block_getdown (&actual->info, NULL, &block_length, NULL);
//
//  max_ptr += riport_length << 2;
//  next += (block_length + 1) << 2;
//  if (block_length == 0) {
//    return NULL;
//  }
//  g_print("B: %d<%d->%p\n", block_length, riport_length<<2, next < max_ptr ? (GstMPRTCPSubflowBlock *) next : NULL);
//  return next < max_ptr ? (GstMPRTCPSubflowBlock *) next : NULL;
//}

/*-------------------- Sender Riport ----------------------*/


void
gst_rtcp_sr_init (GstRTCPSR * riport)
{
  gst_rtcp_header_init (&riport->header);

  gst_rtcp_header_setup (&riport->header, FALSE, 0,
      GST_RTCP_TYPE_SR, RTCPHEADER_WORDS + RTCPSRBLOCK_WORDS - 1, 0);
  gst_rtcp_srb_setup (&riport->sender_block, 0, 0, 0, 0);
}


void
gst_rtcp_srb_setup (GstRTCPSRBlock * block,
    guint64 ntp, guint32 rtp, guint32 packet_count, guint32 octet_count)
{
  GST_WRITE_UINT64_BE (&block->ntptime, ntp);
  block->rtptime = g_htonl (rtp);
  block->packet_count = g_htonl (packet_count);
  block->octet_count = g_htonl (octet_count);
}

void
gst_rtcp_srb_getdown (GstRTCPSRBlock * block,
    guint64 * ntp, guint32 * rtp, guint32 * packet_count, guint32 * octet_count)
{
  if (ntp != NULL) {
    *ntp = GST_READ_UINT64_BE (&block->ntptime);
  }
  if (rtp != NULL) {
    *rtp = g_ntohl (block->rtptime);
  }
  if (packet_count != NULL) {
    *packet_count = g_ntohl (block->packet_count);
  }
  if (octet_count != NULL) {
    *octet_count = g_ntohl (block->octet_count);
  }
}

//--------- RTCP Receiver Riport -----------------

void
gst_rtcp_rr_init (GstRTCPRR * riport)
{
  gst_rtcp_header_init (&riport->header);
  gst_rtcp_header_setup (&riport->header, FALSE, 0,
      GST_RTCP_TYPE_RR, RTCPHEADER_WORDS - 1, 0);
}

void
gst_rtcp_rr_add_rrb (GstRTCPRR * riport, guint32 ssrc,
    guint8 fraction_lost, guint32 cum_packet_lost,
    guint32 ext_hsn, guint32 jitter, guint32 LSR, guint32 DLSR)
{
  guint8 i, rc;
  guint16 length;
  GstRTCPRRBlock *block;
  gst_rtcp_header_getdown (&riport->header, NULL, NULL, &rc, NULL, &length,
      NULL);
  for (i = 0, block = &riport->blocks; i < rc; ++i, ++block);
  gst_rtcp_rrb_setup (block, ssrc, fraction_lost,
      cum_packet_lost, ext_hsn, jitter, LSR, DLSR);
  ++rc;
  length += RTCPRRBLOCK_WORDS;
  gst_rtcp_header_change (&riport->header, NULL, NULL, &rc, NULL, &length,
      NULL);
}

void
gst_rtcp_rrb_setup (GstRTCPRRBlock * block, guint32 ssrc,
    guint8 fraction_lost, guint32 cum_packet_lost,
    guint32 ext_hsn, guint32 jitter, guint32 LSR, guint32 DLSR)
{
  block->ssrc = g_htonl (ssrc);
  block->fraction_lost = fraction_lost;
  //block->cum_packet_lost = g_htonl(cum_packet_lost);
  block->cum_packet_lost = (cum_packet_lost & 0x000000FF) << 16 |
      (cum_packet_lost & 0x0000FF00) | (cum_packet_lost & 0x00FF0000) >> 16;
  block->ext_hsn = g_htonl (ext_hsn);
  block->jitter = g_htonl (jitter);
  block->LSR = g_htonl (LSR);
  block->DLSR = g_htonl (DLSR);
}

void
gst_rtcp_rrb_getdown (GstRTCPRRBlock * block, guint32 * ssrc,
    guint8 * fraction_lost, guint32 * cum_packet_lost,
    guint32 * ext_hsn, guint32 * jitter, guint32 * LSR, guint32 * DLSR)
{
  if (ssrc) {
    *ssrc = g_ntohl (block->ssrc);
  }
  if (fraction_lost) {
    *fraction_lost = block->fraction_lost;
  }
  if (cum_packet_lost) {
    *cum_packet_lost = (block->cum_packet_lost & 0x000000FF) << 16 |
        (block->cum_packet_lost & 0x0000FF00) |
        (block->cum_packet_lost & 0x00FF0000) >> 16;
  }
  if (ext_hsn) {
    *ext_hsn = g_ntohl (block->ext_hsn);
  }
  if (jitter) {
    *jitter = g_ntohl (block->jitter);
  }
  if (LSR) {
    *LSR = g_ntohl (block->LSR);
  }
  if (DLSR) {
    *DLSR = g_ntohl (block->DLSR);
  }
}

void
gst_rtcp_copy_rrb_ntoh (GstRTCPRRBlock * from, GstRTCPRRBlock * to)
{
  guint32 cum_packet_lost;
  to->fraction_lost = from->fraction_lost;
  gst_rtcp_rrb_getdown (from,
      &to->ssrc,
      NULL, &cum_packet_lost, &to->ext_hsn, &to->jitter, &to->LSR, &to->DLSR);
  to->cum_packet_lost = cum_packet_lost;
}

//----------------- XRBlock ------------------------

void gst_rtcp_xr_init (GstRTCPXR * report)
{
  gst_rtcp_header_init (&report->header);
  gst_rtcp_header_setup (&report->header, FALSE, 0,
      GST_RTCP_TYPE_XR, RTCPHEADER_WORDS - 1, 0);
}

void
gst_rtcp_xr_change (GstRTCPXR *report, guint32 *ssrc)
{
  gst_rtcp_header_change(&report->header, NULL, NULL, NULL, NULL, NULL, ssrc);
}

void gst_rtcp_xr_add_content (
    GstRTCPXR *report,
    gpointer content,
    guint16 content_length_in_bytes)
{
  guint16 header_words;
  gst_rtcp_header_getdown(&report->header, NULL, NULL, NULL, NULL, &header_words, NULL);
  memcpy(&report->blocks, content, content_length_in_bytes);
  header_words += content_length_in_bytes>>2;
  gst_rtcp_header_change(&report->header, NULL, NULL, NULL, NULL, &header_words, NULL);
}

void
gst_rtcp_xr_block_getdown (GstRTCPXRBlock *block, guint8 *block_type,
    guint16 * block_length, guint8 *reserved)
{
  if (reserved) {
    *reserved = block->reserved;
  }
  if (block_type) {
    *block_type = block->block_type;
  }
  if (block_length) {
    *block_length = g_ntohs(block->block_length);
  }
}


void
gst_rtcp_xr_block_change (GstRTCPXRBlock *block, guint8 *block_type,
    guint16 * block_length, guint8 *reserved)
{
  if (reserved) {
      block->reserved = *reserved;
  }
  if (block_type) {
      block->block_type = *block_type;
  }
  if (block_length) {
      block->block_length = g_htons(*block_length);
  }
}

//------------------ XR7243 ------------------------


void
gst_rtcp_xr_discarded_bytes_setup (GstRTCPXRDiscardedBlock * block, guint8 interval_metric,
    gboolean early_bit, guint32 ssrc, guint32 discarded_bytes)
{
  block->block_length = g_htons (2);
  block->block_type = GST_RTCP_XR_DISCARDED_BYTES_BLOCK_TYPE_IDENTIFIER;
  block->discarded_bytes = g_htonl (discarded_bytes);
  block->early_bit = early_bit;
  block->interval_metric = interval_metric;
  block->reserved = 0;
  block->ssrc = g_htonl (ssrc);
}

void
gst_rtcp_xr_discarded_bytes_change (GstRTCPXRDiscardedBlock * block,
    guint8 * interval_metric, gboolean * early_bit, guint32 * ssrc,
    guint32 * discarded_bytes)
{
  if (discarded_bytes) {
      block->discarded_bytes = g_htonl (*discarded_bytes);
  }
  if (early_bit) {
      block->early_bit = *early_bit;
  }
  if (interval_metric) {
      block->interval_metric = *interval_metric;
  }
  if (ssrc) {
      block->ssrc = g_htonl (*ssrc);
  }
}

void
gst_rtcp_xr_discarded_bytes_getdown (GstRTCPXRDiscardedBlock *block,
    guint8 * interval_metric, gboolean * early_bit, guint32 * ssrc,
    guint32 * discarded_bytes)
{
  if (discarded_bytes) {
    *discarded_bytes = g_ntohl (block->discarded_bytes);
  }
  if (early_bit) {
    *early_bit = block->early_bit;
  }
  if (interval_metric) {
    *interval_metric = block->interval_metric;
  }
  if (ssrc) {
    *ssrc = g_ntohs (block->ssrc);
  }
}

//------------------ RTCP FB ------------------------
void
gst_rtcp_afb_init (GstRTCPFB * report)
{
  gst_rtcp_header_init (&report->header);
  gst_rtcp_header_setup (&report->header, FALSE, GST_RTCP_PSFB_TYPE_AFB,
      GST_RTCP_TYPE_RTPFB, RTCPHEADER_WORDS - 1 + RTCPFB_WORDS, 0);
  gst_rtcp_afb_setup (report, 0, 0, 0);
}


void
gst_rtcp_afb_setup (GstRTCPFB * report,
                    guint32 packet_source_ssrc,
                    guint32 media_source_ssrc,
                    guint32 fci_id)
{
  report->ssrc = g_htonl (media_source_ssrc);
  report->fci_id = g_htonl (fci_id);
}

void
gst_rtcp_afb_change (GstRTCPFB * report,
                     guint32 *packet_source_ssrc,
                     guint32 *media_source_ssrc,
                     guint32 *fci_id)
{
  if (packet_source_ssrc) {
      gst_rtcp_header_change(&report->header, NULL, NULL, NULL, NULL, NULL, packet_source_ssrc);
  }
  if (media_source_ssrc) {
      report->ssrc = g_htonl (*media_source_ssrc);
  }
  if (fci_id) {
      report->fci_id = g_htonl(*fci_id);
  }
}

void
gst_rtcp_afb_getdown (GstRTCPFB * report,
                      guint32 *packet_source_ssrc,
                      guint32 *media_source_ssrc,
                      guint32 *fci_id)
{
  if (packet_source_ssrc) {
      gst_rtcp_header_getdown(&report->header, NULL, NULL, NULL, NULL, NULL, packet_source_ssrc);
  }
  if (media_source_ssrc) {
      *media_source_ssrc = g_ntohl(report->ssrc);
  }
  if (fci_id) {
      *fci_id = g_ntohl(report->fci_id);
  }
}

void
gst_rtcp_afb_rmdi_change (GstRTCPAFB_RMDI * report,
                     guint8 *rsvd,
                     guint8 *records_num,
                     guint16 *length)
{
  if(rsvd){
    report->rsvd = *rsvd;
  }
  if(records_num){
    report->records_num = *records_num;
  }
  if(length){
    report->length = g_htons(*length);
  }
}

void
gst_rtcp_afb_rmdi_getdown (GstRTCPAFB_RMDI * report,
                      guint8 *rsvd,
                      guint8 *records_num,
                      guint16 *length)
{
  if(rsvd){
      *rsvd = report->rsvd;
  }
  if(records_num){
      *records_num = report->records_num;
  }
  if(length){
      *length = g_ntohs(report->length);
  }
}

static guint32 g_htonfloat(gfloat value){
    union v {
        gfloat      f;
        guint32     i;
    }val;
    val.f = value;
    val.i = g_htonl(val.i);
    return val.i;
};

static gfloat g_ntohfloat(guint32 value){
    union v {
        gfloat      f;
        guint32     i;
    }val;
    val.i = g_ntohl(value);
    return val.f;
};

void
gst_rtcp_afb_remb_change (GstRTCPAFB_REMB * report,
                          guint32 *num_ssrc,
                          gfloat *float_num,
                          guint32 *ssrc_feedback)
{
  if(num_ssrc){
      report->num_ssrc = *num_ssrc;
  }
  if(float_num){
      report->float_num = g_htonfloat(*float_num);
//      {
//        guint32 n;
//        guint8 *c,*k;
//        n = g_htonfloat(*float_num);
//        c = (guint8*)&n;
//        k = (guint8*)report;
//        g_print("0x%X%X%X%X-0x%X%X%X%X\n", *c, *(c+1), *(c+2), *(c+3), *k, *(k+1), *(k+2), *(k+3));
//      }
  }
  if(ssrc_feedback){
      report->ssrc_feedback = g_htonl(*ssrc_feedback);
  }
}

void
gst_rtcp_afb_remb_getdown (GstRTCPAFB_REMB * report,
                           guint32 *num_ssrc,
                           gfloat *float_num,
                           guint32 *ssrc_feedback)
{

  if(num_ssrc){
      *num_ssrc = report->num_ssrc;
  }
  if(float_num){
    *float_num = g_ntohfloat(report->float_num);
  }
  if(ssrc_feedback){
      *ssrc_feedback = g_ntohs(report->ssrc_feedback);
  }
}

void
gst_rtcp_afb_rmdi_record_change (GstRTCPAFB_RMDIRecord * record,
                      guint16 *HSSN,
                      guint16 *disc_packets_num,
                      guint32 *owd_sample)
{
  if(HSSN){
      record->HSSN = g_htons(*HSSN);
  }
  if(disc_packets_num){
      record->disc_packets_num = *disc_packets_num;
  }
  if(owd_sample){
      record->owd_sample = g_ntohl(*owd_sample);
  }
}


void
gst_rtcp_afb_rmdi_record_getdown (GstRTCPAFB_RMDIRecord * record,
                                  guint16 *HSSN,
                                  guint16 *disc_packets_num,
                                  guint32 *owd_sample)
{
  if(HSSN){
      *HSSN = g_ntohs(record->HSSN);
  }
  if(disc_packets_num){
      *disc_packets_num = g_ntohs(record->disc_packets_num);
  }
  if(owd_sample){
      *owd_sample = g_ntohl(record->owd_sample);
  }
}


void
gst_rtcp_afb_setup_fci_data(GstRTCPFB * report, gpointer fci_dat, guint fci_len)
{
  guint16 header_length;
  memcpy(&report->fci_data, fci_dat, fci_len);
  gst_rtcp_header_getdown(&report->header, NULL, NULL, NULL, NULL, &header_length, NULL);
  header_length += (fci_len>>2) - 1;
  gst_rtcp_header_change(&report->header, NULL, NULL, NULL, NULL, &header_length, NULL);
}


void
gst_rtcp_afb_getdown_fci_data(GstRTCPFB * report, gchar *fci_dat, guint *fci_len)
{
  guint16 header_length, fci_length;
  gst_rtcp_header_getdown(&report->header, NULL, NULL, NULL, NULL, &header_length, NULL);
  fci_length = header_length - (RTCPHEADER_WORDS - 1) - (RTCPFB_WORDS - 1);
  fci_length<<=2;
  if(fci_len){
    *fci_len = fci_length;
  }
  if(fci_dat){
    memcpy(fci_dat, &report->fci_data, fci_length);
  }
}

void
gst_rtcp_xr_chunk_ntoh_cpy (GstRTCPXRChunk *dst_chunk,
                       GstRTCPXRChunk *src_chunk)
{
  guint16 *src = (guint16*) src_chunk;
  guint16 dst;
  dst = g_ntohs(*src);
  memcpy(dst_chunk, &dst, sizeof(GstRTCPXRChunk));
}

void
gst_rtcp_xr_chunk_hton_cpy (GstRTCPXRChunk *dst_chunk,
                       GstRTCPXRChunk *src_chunk)
{
  guint16 *src = (guint16*) src_chunk;
  guint16 dst;
  dst = g_htons(*src);
  memcpy(dst_chunk, &dst, sizeof(GstRTCPXRChunk));
}

//------------------ XR RFC7097 ------------------------


void
gst_rtcp_xr_discarded_rle_setup(GstRTCPXRDiscardedRLEBlock *block,
                      gboolean early_bit,
                      guint8 thinning,
                      guint32 ssrc,
                      guint16 begin_seq,
                      guint16 end_seq)
{
  block->reserved     = 0;
  block->block_length = g_htons (3);
  block->block_type   = GST_RTCP_XR_DISCARDED_RLE_BLOCK_TYPE_IDENTIFIER;
  memset(&block->chunks[0], 0, 4);
  gst_rtcp_xr_discarded_rle_change(block,
                             &early_bit,
                             &thinning,
                             &ssrc,
                             &begin_seq,
                             &end_seq);
}

void gst_rtcp_xr_discarded_rle_getdown (GstRTCPXRDiscardedRLEBlock *block,
                             gboolean *early_bit,
                             guint8 *thinning,
                             guint32 *ssrc,
                             guint16 *begin_seq,
                             guint16 *end_seq)
{
  if (ssrc) {
     *ssrc = g_ntohl (block->ssrc);
   }
   if(early_bit){
     *early_bit = block->early_bit;
   }
   if (thinning) {
     *thinning = block->thinning;
   }
   if (begin_seq) {
     *begin_seq = g_ntohs (block->begin_seq);
   }
   if (end_seq) {
     *end_seq = g_ntohs (block->end_seq);
   }
}


void gst_rtcp_xr_discarded_rle_change (GstRTCPXRDiscardedRLEBlock *block,
                             gboolean *early_bit,
                             guint8 *thinning,
                             guint32 *ssrc,
                             guint16 *begin_seq,
                             guint16 *end_seq)
{
  if (ssrc) {
      block->ssrc = g_htonl(*ssrc);
   }
   if(early_bit){
       block->early_bit = *early_bit;
   }
   if (thinning) {
       block->thinning = *thinning;
   }
   if (begin_seq) {
       block->begin_seq = g_htons(*begin_seq);
   }
   if (end_seq) {
       block->end_seq = g_htons(*end_seq);
   }
}

guint gst_rtcp_xr_discarded_rle_block_get_chunks_num(GstRTCPXRDiscardedRLEBlock *block)
{
  guint chunk_words_num;
  guint16 block_length;

  block_length = g_ntohs(block->block_length);
  //the number of words containing chunks
  chunk_words_num = block_length - 2;
  //multiply it by two so we get the number of chunks
  return chunk_words_num<<1;
}


//------------------ XROWD ------------------------


void
gst_rtcp_xr_owd_block_setup(GstRTCPXROWDBlock *block,
                      guint8  interval_metric_flag,
                      guint32 ssrc,
                      guint32 median_delay,
                      guint32 min_delay,
                      guint32 max_delay)
{
  block->reserved     = 0;
  block->block_length = g_htons (4);
  block->block_type   = GST_RTCP_XR_OWD_BLOCK_TYPE_IDENTIFIER;
  gst_rtcp_xr_owd_block_change(block,
                          &interval_metric_flag,
                          &ssrc,
                          &median_delay,
                          &min_delay,
                          &max_delay);
}

void
gst_rtcp_xr_owd_block_change (GstRTCPXROWDBlock *block,
                        guint8  *interval_metric,
                       guint32 *ssrc,
                       guint32 *median_delay,
                       guint32 *min_delay,
                       guint32 *max_delay)
{
  if (median_delay) {
      block->median_delay = g_htonl (*median_delay);
  }
  if (min_delay) {
      block->min_delay = g_htonl (*min_delay);
  }
  if (max_delay) {
      block->max_delay = g_htonl (*max_delay);
  }
  if (ssrc) {
      block->ssrc = g_htonl (*ssrc);
  }
  if(interval_metric){
      block->interval_metric = *interval_metric;
  }
}

void
gst_rtcp_xr_owd_block_getdown (GstRTCPXROWDBlock *block,
                         guint8  *interval_metric,
                        guint32 *ssrc,
                        guint32 *median_delay,
                        guint32 *min_delay,
                        guint32 *max_delay)
{
  if (ssrc) {
    *ssrc = g_ntohl (block->ssrc);
  }
  if(interval_metric){
    *interval_metric = block->interval_metric;
  }
  if (median_delay) {
    *median_delay = g_ntohl (block->median_delay);
  }
  if (min_delay) {
      *min_delay = g_ntohl (block->min_delay);
    }
  if (max_delay) {
      *max_delay = g_ntohl (block->max_delay);
    }
}

//------------------ MPRTCP ------------------------


void
gst_mprtcp_report_init (GstMPRTCPSubflowReport * riport)
{
  gst_rtcp_header_init (&riport->header);
  gst_rtcp_header_setup (&riport->header, FALSE, 0,
      MPRTCP_PACKET_TYPE_IDENTIFIER, RTCPHEADER_WORDS /*ssrc */ , 0);
}

void
gst_mprtcp_riport_setup (GstMPRTCPSubflowReport * riport, guint32 ssrc)
{
  riport->ssrc = g_htonl (ssrc);
}

void
gst_mprtcp_report_getdown (GstMPRTCPSubflowReport * riport, guint32 * ssrc)
{
  if (ssrc) {
    *ssrc = g_ntohl (riport->ssrc);
  }
}


void
gst_mprtcp_block_init (GstMPRTCPSubflowBlock * block)
{
  gst_mprtcp_block_setup (&block->info, 0, 0, 0);

}

void
gst_mprtcp_block_setup (GstMPRTCPSubflowInfo * info,
    guint8 type, guint8 block_length, guint16 subflow_id)
{
  info->block_length = block_length;
  info->subflow_id = g_htons (subflow_id);
  info->type = type;
}

void
gst_mprtcp_block_change (GstMPRTCPSubflowInfo * info,
    guint8 * type, guint8 * block_length, guint16 * subflow_id)
{
  if (type) {
    info->type = *type;
  }
  if (subflow_id) {
    info->subflow_id = g_ntohs (*subflow_id);
  }
  if (block_length) {
    info->block_length = *block_length;
  }
}

void
gst_mprtcp_block_getdown (GstMPRTCPSubflowInfo * info,
    guint8 * type, guint8 * block_length, guint16 * subflow_id)
{
  if (type) {
    *type = info->type;
  }
  if (block_length) {
    *block_length = info->block_length;
  }
  if (subflow_id) {
    *subflow_id = g_ntohs (info->subflow_id);
  }
}

void
gst_printfnc_rtcp_check_sr (GstRTCPBuffer * rtcp, gint offset, printfnc print)
{
  GstRTCPHeader *header;
  GstRTCPSR *riport;
  GstRTCPRRBlock *block;
  GstRTCPPacket packet;
  gint index;
  guint32 ssrc, header_ssrc;
  guint8 version, payload_type, reserved;
  guint16 length;
  gboolean padding;

  g_return_if_fail (rtcp != NULL);
  g_return_if_fail (GST_IS_BUFFER (rtcp->buffer));
  g_return_if_fail (rtcp != NULL);
  g_return_if_fail (rtcp->map.flags & GST_MAP_READ);


  gst_rtcp_buffer_get_first_packet (rtcp, &packet);
  gst_rtcp_packet_sr_get_sender_info (&packet, &ssrc, NULL, NULL, NULL, NULL);
  header = (GstRTCPHeader *) (rtcp->map.data + offset);
  riport = (GstRTCPSR *) header;

  gst_rtcp_header_getdown (header, &version, &padding, &reserved, &payload_type,
      &length, &header_ssrc);
  print ("0               1               2               3          \n"
      "0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%3d|%1d|%9d|%15d|%14d?=?%14d|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%29u ?=? %29u|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n",
      version,
      padding,
      reserved,
      payload_type,
      length, gst_rtcp_packet_get_length (&packet), header_ssrc, ssrc);

  gst_print_rtcp_check_srb (&riport->sender_block, &packet);

  for (block = &riport->receiver_blocks, index = 0;
      index < reserved; ++index, ++block) {
    gst_print_rtcp_check_rrb (block, index, &packet);
  }
}


void
gst_printfnc_rtcp_check_rrb (GstRTCPRRBlock * block, gint index,
    GstRTCPPacket * packet, printfnc print)
{
  guint32 ssrc, exthighestseq, jitter, lsr, dlsr;
  guint32 packetslost;
  guint8 fraction_lost;

  guint32 r_ssrc, r_exthighestseq, r_jitter, r_lsr, r_dlsr;
  gint32 r_packetslost;
  guint8 r_fraction_lost;
  gst_rtcp_packet_get_rb (packet, index, &r_ssrc, &r_fraction_lost,
      &r_packetslost, &r_exthighestseq, &r_jitter, &r_lsr, &r_dlsr);

  gst_rtcp_rrb_getdown (block, &ssrc, &fraction_lost, &packetslost,
      &exthighestseq, &jitter, &lsr, &dlsr);
  print ("+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+\n"
      "|%29u ?=? %29u|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%7d?=?%6d|%21d ?=? %20d|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%29u ?=? %29u|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%29u ?=? %29u|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%29u ?=? %29u|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%29u ?=? %29u|\n"
      "+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+\n",
      ssrc,
      r_ssrc,
      fraction_lost,
      r_fraction_lost,
      packetslost,
      r_packetslost,
      exthighestseq,
      r_exthighestseq, jitter, r_jitter, lsr, r_lsr, dlsr, r_dlsr);
}

void
gst_printfnc_rtcp_check_srb (GstRTCPSRBlock * block, GstRTCPPacket * packet, printfnc print)
{
  guint32 r_ssrc, r_rtptime, r_packet_count, r_octet_count;
  guint64 r_ntptime;
  guint32 rtptime, packet_count, octet_count;
  guint64 ntptime;

  gst_rtcp_packet_sr_get_sender_info (packet, &r_ssrc, &r_ntptime,
      &r_rtptime, &r_packet_count, &r_octet_count);
  gst_rtcp_srb_getdown (block, &ntptime, &rtptime, &packet_count, &octet_count);
  print ("+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+\n"
      "|%29lX ?=? %29lX|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%29X ?=? %29X|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%29X ?=? %29X|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%29X ?=? %29X|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n",
      ntptime,
      r_ntptime,
      rtptime,
      r_rtptime, packet_count, r_packet_count, octet_count, r_octet_count);
}




//------------------------------------


void
gst_printfnc_rtcp_buffer (GstRTCPBuffer * buffer, printfnc print)
{
  print ("0               1               2               3          \n"
         "0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2\n");

  gst_printfnc_rtcp ((GstRTCPHeader *) buffer->map.data, print);

  print("+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n");
}

void
gst_printfnc_rtcp (GstRTCPHeader * header, printfnc print)
{
  gboolean ok = TRUE;
  guint16 offset = 0;
  guint8 payload_type;
  guint8 rsvd;
  guint16 length;
  guint8 *step = (guint8 *) header;

  for (; ok; step = (guint8 *) step + offset) {
    gst_rtcp_header_getdown ((GstRTCPHeader *) step, NULL, NULL,
        &rsvd, &payload_type, &length, NULL);
    switch (payload_type) {
      case MPRTCP_PACKET_TYPE_IDENTIFIER:
        gst_printfnc_mprtcp ((GstMPRTCPSubflowReport *) step, print);
        break;
      case GST_RTCP_TYPE_SR:
        gst_printfnc_rtcp_sr ((GstRTCPSR *) step, print);
        break;
      case GST_RTCP_TYPE_RR:
        gst_printfnc_rtcp_rr ((GstRTCPRR *) step, print);
        break;
      case GST_RTCP_TYPE_RTPFB:
        if(rsvd == GST_RTCP_PSFB_TYPE_AFB){
          GstRTCPFB *afb = (GstRTCPFB *) step;
          guint32 fci_id;
          gst_rtcp_afb_getdown(afb, NULL, NULL, &fci_id);
          gst_printfnc_rtcp_afb(afb, print);
          if(fci_id == RTCP_AFB_RMDI_ID){
              gst_printfnc_rtcp_afb_rmdi(&afb->fci_data, print);
          }

        }
        break;
      case GST_RTCP_TYPE_XR:
        gst_printfnc_rtcp_xr((GstRTCPXR*) step, print);
        break;
      default:
        ok = FALSE;
        break;
    }
    offset = length + 1;
    offset <<= 2;
  }
}

void
gst_printfnc_mprtcp (GstMPRTCPSubflowReport * riport, printfnc print)
{
  gint index;
  GstMPRTCPSubflowBlock *block = &riport->blocks;

  GstRTCPHeader *riport_header = &riport->header;
  guint32 ssrc;
  guint8 src;
  gst_mprtcp_report_getdown (riport, &ssrc);

  print ("0               1               2               3          \n"
         "0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2\n");

  gst_printfnc_rtcp_header (riport_header, print);
  gst_rtcp_header_getdown (riport_header, NULL, NULL, &src, NULL, NULL, NULL);

  print ("+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+\n"
      "|%63u|\n", ssrc);

  for (index = 0; index < src; ++index) {
    guint8 block_length;
    gst_printfnc_mprtcp_block (block, &block_length, print);
    block =
        (GstMPRTCPSubflowBlock *)
        ((guint8 *) block + ((block_length + 1) << 2));
  }

  print("+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n");
}


void
gst_printfnc_mprtcp_block (GstMPRTCPSubflowBlock * block, guint8 * block_length, printfnc print)
{
  guint8 type;
  guint16 subflow_id;
  GstMPRTCPSubflowInfo *info;
  guint8 *_block_length = NULL, _substitue;

  if (!block_length)
    _block_length = &_substitue;
  else
    _block_length = block_length;


  info = &block->info;
  gst_mprtcp_block_getdown (info, &type, _block_length, &subflow_id);

  print
      ("+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+\n"
      "|%15u|%15u|%31d|\n", type, *_block_length, subflow_id);

  gst_printfnc_rtcp (&block->block_header, print);

}

void
gst_printfnc_rtcp_header (GstRTCPHeader * header, printfnc print)
{
  guint32 ssrc;
  guint8 version, payload_type, reserved;
  guint16 length;
  gboolean padding;

  gst_rtcp_header_getdown (header, &version, &padding,
      &reserved, &payload_type, &length, &ssrc);

  print ("+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%3d|%1d|%9d|%15d|%31d|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%63u|\n", version, padding, reserved, payload_type, length, ssrc);
}

void
gst_printfnc_rtcp_sr (GstRTCPSR * riport, printfnc print)
{
  gint index;
  guint8 rc;
  GstRTCPRRBlock *block = &riport->receiver_blocks;
  gst_rtcp_header_getdown (&riport->header, NULL, NULL, &rc, NULL, NULL, NULL);

  gst_printfnc_rtcp_header (&riport->header, print);

  gst_printfnc_rtcp_srb (&riport->sender_block, print);

  for (index = 0; index < rc; ++index, ++block) {
    gst_printfnc_rtcp_rrb (block, print);
  }
}

void
gst_printfnc_rtcp_rr (GstRTCPRR * riport, printfnc print)
{
  gint index;
  guint8 rc;
  GstRTCPRRBlock *block = &riport->blocks;
  gst_rtcp_header_getdown (&riport->header, NULL, NULL, &rc, NULL, NULL, NULL);

  gst_printfnc_rtcp_header (&riport->header, print);

  for (index = 0; index < rc; ++index, ++block) {
    gst_printfnc_rtcp_rrb (block, print);
  }
}

void
gst_printfnc_rtcp_xr (GstRTCPXR * report, printfnc print)
{
  GstRTCPXRBlock *block;
  guint8 block_type;
  guint16 block_words;
  guint16 header_words;
  guint16 read_words;

  gst_printfnc_rtcp_header(&report->header, print);
  gst_rtcp_header_getdown(&report->header, NULL, NULL, NULL, NULL, &header_words, NULL);
  block = &report->blocks;
  read_words = 1;
again:
  gst_rtcp_xr_block_getdown(block, &block_type, &block_words, NULL);
  switch(block_type){
    case GST_RTCP_XR_OWD_BLOCK_TYPE_IDENTIFIER:
        gst_printfnc_rtcp_xr_owd_block((GstRTCPXROWDBlock*)block, print);
        break;
    case GST_RTCP_XR_LOSS_RLE_BLOCK_TYPE_IDENTIFIER:
        //todo: implement
        break;
    case GST_RTCP_XR_DISCARDED_RLE_BLOCK_TYPE_IDENTIFIER:
        gst_printfnc_rtcp_xr_discarded_rle_block((GstRTCPXRDiscardedRLEBlock*)block, print);
        break;
    case GST_RTCP_XR_DISCARDED_BYTES_BLOCK_TYPE_IDENTIFIER:
      gst_printfnc_rtcp_xr_discarded_bytes_block((GstRTCPXRDiscardedBlock*) block, print);
        break;
    default:
      g_warning("Unrecognized XR block to print out at gst_printfnc_rtcp_xr");
      goto done;
      break;
  }
  read_words += block_words + 1;
  if(read_words < header_words){
    guint8 *pos;
    pos = (guint8*)block;
    pos += (block_words + 1) << 2;
    block = (GstRTCPXRBlock*) pos;
    goto again;
  }
done:
  return;
}

void
gst_printfnc_rtcp_xr_discarded_bytes_block (GstRTCPXRDiscardedBlock * block, printfnc print)
{
  guint8 interval_metric;
  gboolean early_bit;
  guint32 ssrc;
  guint32 discarded_bytes;

  gst_rtcp_xr_discarded_bytes_getdown (block, &interval_metric, &early_bit, &ssrc,
      &discarded_bytes);
  print ("+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+\n"
      "|%15d|%3d|%1d|%9d|%31d|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%63X|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%63X|\n", block->block_type, interval_metric, early_bit, 0,
      g_ntohs (block->block_length), ssrc, discarded_bytes);
}


void
gst_printfnc_rtcp_afb (GstRTCPFB * report, printfnc print)
{
  guint32 ssrc;
  guint32 fci_id;

  gst_rtcp_afb_getdown(report, NULL, &ssrc, &fci_id);
  gst_printfnc_rtcp_header (&report->header, print);
  print ("+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+\n"
      "|%63X|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%63X|\n", ssrc, fci_id);


}

void
gst_printfnc_rtcp_afb_rmdi (gpointer data, printfnc print)
{
  GstRTCPAFB_RMDIRecord *record;
  GstRTCPAFB_RMDI *fbm; //feedback message
  guint8 rsvd, records_num;
  guint16 length;
  gint i;
  fbm = data;
  gst_rtcp_afb_rmdi_getdown(fbm, &rsvd, &records_num, &length);
  print (
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%15d|%31hu|%15d|\n"
      ,
      rsvd,
      records_num,
      length
      );

  record = &fbm->records[0];
  for(i=0; i< records_num; ++i, ++record){
      guint16 HSSN,disc_packets_num;
      guint32 owd;
      gst_rtcp_afb_rmdi_record_getdown(record, &HSSN, &disc_packets_num, &owd);
      print (
            "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
            "|%31hu|%31hu|\n"
            "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
            "|%63X|\n"
          ,
          g_ntohs(record->HSSN), g_ntohs(record->disc_packets_num),
          g_ntohl(record->owd_sample)
          );
  }
}


void
gst_printfnc_rtcp_xr_owd_block (GstRTCPXROWDBlock * block, printfnc print)
{
  guint8 flag;
  guint32 median_delay;
  guint32 ssrc;
  guint32 min_delay;
  guint32 max_delay;

  gst_rtcp_xr_owd_block_getdown (block, &flag, &ssrc,
                           &median_delay, &min_delay, &max_delay);
  print (
      "+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+\n"
      "|%15d|%2d|%12d|%31d|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%63X|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%63u|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%63u|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%63u|\n"
      ,
      block->block_type, flag, block->reserved,
      g_ntohs (block->block_length), ssrc,
      median_delay,
      min_delay, max_delay);
}

void
gst_printfnc_rtcp_xrchunks(GstRTCPXRChunk * chunk1, GstRTCPXRChunk * chunk2, printfnc print)
{
  guint16 u16_chunk1 = 0;
  guint16 u16_chunk2 = 0;
//  g_print("BAZDMEG: %X-%X\n", *((guint16*)chunk1), *((guint16*)chunk2));
  gst_rtcp_xr_chunk_ntoh_cpy((GstRTCPXRChunk *)&u16_chunk1, chunk1);
  gst_rtcp_xr_chunk_ntoh_cpy((GstRTCPXRChunk *)&u16_chunk2, chunk2);

//  g_print("BAZDMEG2: %X-%X\n", u16_chunk1, u16_chunk2);

  if((u16_chunk1&1) == 1){ //both are bitvectors, I don't care if someone wants to mix it.
    print ("+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n");
    print("|%1d| %1d%1d%1d %1d%1d%1d%1d %1d%1d%1d%1d %1d%1d%1d%-11d|",
          (u16_chunk1>>0) & 1,
          (u16_chunk1>>1) & 1,
          (u16_chunk1>>2) & 1,
          (u16_chunk1>>3) & 1,

          (u16_chunk1>>4) & 1,
          (u16_chunk1>>5) & 1,
          (u16_chunk1>>6) & 1,
          (u16_chunk1>>7) & 1,

          (u16_chunk1>>8) & 1,
          (u16_chunk1>>9) & 1,
          (u16_chunk1>>10) & 1,
          (u16_chunk1>>11) & 1,

          (u16_chunk1>>12) & 1,
          (u16_chunk1>>13) & 1,
          (u16_chunk1>>14) & 1,
          (u16_chunk1>>15) & 1
          );

    print("%1d| %1d%1d%1d %1d%1d%1d%1d %1d%1d%1d%1d %1d%1d%1d%-11d|\n",
          (u16_chunk2>>0) & 1,
          (u16_chunk2>>1) & 1,
          (u16_chunk2>>2) & 1,
          (u16_chunk2>>3) & 1,

          (u16_chunk2>>4) & 1,
          (u16_chunk2>>5) & 1,
          (u16_chunk2>>6) & 1,
          (u16_chunk2>>7) & 1,

          (u16_chunk2>>8) & 1,
          (u16_chunk2>>9) & 1,
          (u16_chunk2>>10) & 1,
          (u16_chunk2>>11) & 1,

          (u16_chunk2>>12) & 1,
          (u16_chunk2>>13) & 1,
          (u16_chunk2>>14) & 1,
          (u16_chunk2>>15) & 1
          );
  }else{
      GstRTCPXRRLEChunk *ch1,*ch2;
      ch1 = (GstRTCPXRRLEChunk *) &u16_chunk1;
      ch2 = (GstRTCPXRRLEChunk *) &u16_chunk2;
      print (
            "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
            "|%1d|%1d|%27d|%1d|%1d|%27d|\n"

            ,
            ch1->chunk_type, ch1->run_type, ch1->run_length,
            ch2->chunk_type, ch2->run_type, ch2->run_length
            );
  }

}

void
gst_printfnc_rtcp_xr_discarded_rle_block(GstRTCPXRDiscardedRLEBlock * block, printfnc print)
{
  gboolean early_bit;
  guint chunk_index;
  guint8 thinning;
  guint32 ssrc;
  guint chunks_num;
  guint16 begin_seq, end_seq;

  gst_rtcp_xr_discarded_rle_getdown (block, &early_bit, &thinning, &ssrc, &begin_seq, &end_seq);

  print (
      "+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+\n"
      "|%15d|%5d|%1d|%7d|%31d|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%63X|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%31d|%31d|\n"
      ,
      block->block_type, block->reserved, early_bit, thinning,
      g_ntohs (block->block_length), ssrc, begin_seq, end_seq);

   chunks_num = gst_rtcp_xr_discarded_rle_block_get_chunks_num(block);

   for(chunk_index = 0;
       chunk_index < chunks_num;
       chunk_index+=2)
   {
       GstRTCPXRChunk *chunk1, *chunk2;
       chunk1 = &block->chunks[chunk_index];
       chunk2 = &block->chunks[chunk_index + 1];
       gst_printfnc_rtcp_xrchunks(chunk1, chunk2, print);
   }
}




void
gst_printfnc_rtcp_rrb (GstRTCPRRBlock * block, printfnc print)
{
  guint32 ssrc, exthighestseq, jitter, lsr, dlsr;
  guint32 packetslost;
  guint8 fraction_lost;


  gst_rtcp_rrb_getdown (block, &ssrc, &fraction_lost, &packetslost,
      &exthighestseq, &jitter, &lsr, &dlsr);

  print ("+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+\n"
      "|%63u|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%15d|%47d|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%63u|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%63u|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%63X|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%63X|\n",
      ssrc, fraction_lost, packetslost, exthighestseq, jitter, lsr, dlsr);
}

void
gst_printfnc_rtcp_srb (GstRTCPSRBlock * block, printfnc print)
{
  guint32 rtptime, packet_count, octet_count;
  guint64 ntptime;

  gst_rtcp_srb_getdown (block, &ntptime, &rtptime, &packet_count, &octet_count);

  print ("+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+\n"
      "|%63lX|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%63X|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%63X|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%63X|\n", ntptime, rtptime, packet_count, octet_count);
}


void
gst_printfnc_rtp_buffer (GstBuffer * buf, printfnc print)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  gst_rtp_buffer_map(buf, GST_MAP_READ, &rtp);
  gst_printfnc_rtp_packet_info(&rtp, print);
  gst_rtp_buffer_unmap(&rtp);
}


void
gst_printfnc_rtp_packet_info (GstRTPBuffer * rtp, printfnc print)
{
  gboolean extended;
  print ("0               1               2               3          \n"
      "0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 \n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%3d|%1d|%1d|%7d|%1d|%13d|%31d|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%63u|\n"
      "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
      "|%63u|\n",
      gst_rtp_buffer_get_version (rtp),
      gst_rtp_buffer_get_padding (rtp),
      extended = gst_rtp_buffer_get_extension (rtp),
      gst_rtp_buffer_get_csrc_count (rtp),
      gst_rtp_buffer_get_marker (rtp),
      gst_rtp_buffer_get_payload_type (rtp),
      gst_rtp_buffer_get_seq (rtp),
      gst_rtp_buffer_get_timestamp (rtp), gst_rtp_buffer_get_ssrc (rtp)
      );

  if (extended) {
    guint16 bits;
    guint8 *pdata;
    guint wordlen;
    gulong index = 0;

    gst_rtp_buffer_get_extension_data (rtp, &bits, (gpointer) & pdata,
        &wordlen);


    print
        ("+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+\n"
        "|0x%-29X|%31d|\n"
        "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n",
        bits, wordlen);

    for (index = 0; index < wordlen; ++index) {
      print ("|0x%-5X = %5d|0x%-5X = %5d|0x%-5X = %5d|0x%-5X = %5d|\n"
          "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n",
          *(pdata + index * 4), *(pdata + index * 4),
          *(pdata + index * 4 + 1), *(pdata + index * 4 + 1),
          *(pdata + index * 4 + 2), *(pdata + index * 4 + 2),
          *(pdata + index * 4 + 3), *(pdata + index * 4 + 3));
    }
    print
        ("+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+\n");
  }
}

gboolean gst_mprtp_get_subflow_extension(GstRTPBuffer *rtp,
                                         guint8 ext_header_id,
                                         MPRTPSubflowHeaderExtension **subflow_info)
{
  gpointer pointer;
  guint size;
  if (!gst_rtp_buffer_get_extension_onebyte_header (rtp,
                                                    ext_header_id,
                                                    0,
                                                    &pointer,
                                                    &size)) {
      return FALSE;
    }

    if(subflow_info) *subflow_info = (MPRTPSubflowHeaderExtension *) pointer;
    return TRUE;
}



gboolean gst_mprtp_get_abs_send_time_extension(GstRTPBuffer *rtp,
                                               guint8 ext_header_id,
                                               RTPAbsTimeExtension **abs_time)
{
  gpointer pointer;
  guint size;
  if (!gst_rtp_buffer_get_extension_onebyte_header (rtp,
                                                    ext_header_id,
                                                    0,
                                                    &pointer,
                                                    &size)) {
      return FALSE;
    }

    if(abs_time) *abs_time = (RTPAbsTimeExtension *) pointer;
    return TRUE;
}

