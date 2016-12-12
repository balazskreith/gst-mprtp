#include "mprtputils.h"
#include <string.h>

void gst_rtp_buffer_set_mprtp_extension(GstRTPBuffer* rtp, guint8 ext_header_id, guint8 subflow_id, guint16 subflow_seq)
{
  MPRTPSubflowHeaderExtension mprtp_ext;
  mprtp_ext.id = subflow_id;
  mprtp_ext.seq = subflow_seq;
  gst_rtp_buffer_add_extension_onebyte_header (rtp, ext_header_id, (gpointer) &mprtp_ext, sizeof (mprtp_ext));
}

void gst_rtp_buffer_get_mprtp_extension(GstRTPBuffer* rtp, guint8 ext_header_id, guint8 *subflow_id, guint16 *subflow_seq)
{
  gpointer pointer = NULL;
  guint size;
  MPRTPSubflowHeaderExtension *subflow_infos;

  gst_rtp_buffer_get_extension_onebyte_header(rtp, ext_header_id, 0, &pointer, &size);
  subflow_infos = (MPRTPSubflowHeaderExtension *) pointer;
  if(subflow_id){
    *subflow_id = subflow_infos->id;
  }

  if(subflow_seq){
    *subflow_seq = subflow_infos->seq;
  }
}

guint16 subflowseqtracker_increase(SubflowSeqTrack *subseqtracker)
{
  if(++subseqtracker->seqence_num == 0){
    ++subseqtracker->cycle_num;
  }
  return subseqtracker->seqence_num;
}

void gst_rtp_buffer_set_abs_time_extension(GstRTPBuffer* rtp, guint8 abs_time_ext_header_id)
{
    RTPAbsTimeExtension data;
    guint32 time;

    //Absolute sending time +0x83AA7E80
    //https://tools.ietf.org/html/draft-alvestrand-rmcat-remb-03
    time = (NTP_NOW >> 14) & 0x00ffffff;
    memcpy (&data, &time, 3);
    gst_rtp_buffer_add_extension_onebyte_header (rtp, abs_time_ext_header_id, (gpointer) &data, sizeof (data));
}

guint64 gst_rtp_buffer_get_abs_time_extension(GstRTPBuffer* rtp, guint8 abs_time_ext_header_id)
{
  gpointer pointer = NULL;
  guint    size;
  guint32  rcv_chunk = (NTP_NOW >> 14) & 0x00ffffff;
  guint64  ntp_base = NTP_NOW;
  guint64  snd_time = 0;

  gst_rtp_buffer_get_extension_onebyte_header(rtp, abs_time_ext_header_id, 0, &pointer, &size);
  memcpy (&snd_time, pointer, 3);
  if(rcv_chunk < snd_time){
      ntp_base-=0x0000004000000000UL;
  }
  snd_time <<= 14;
  snd_time |=  (ntp_base & 0xFFFFFFC000000000UL);

  return snd_time;
}

guint64 gst_rtp_buffer_get_abs_time_extension_new(GstRTPBuffer* rtp, guint8 abs_time_ext_header_id)
{
  gpointer pointer = NULL;
  guint    size;
  guint64  result = 0;

  gst_rtp_buffer_get_extension_onebyte_header(rtp, abs_time_ext_header_id, 0, &pointer, &size);
  memcpy (&result, pointer, 3);
  return result << 14;
}

gboolean gst_buffer_is_mprtp(GstBuffer* buffer, guint8 mprtp_ext_header_id)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  gboolean result;
  gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp);
  result = gst_rtp_buffer_is_mprtp(&rtp, mprtp_ext_header_id);
  gst_rtp_buffer_unmap(&rtp);
  return result;
}

gboolean gst_rtp_buffer_is_mprtp(GstRTPBuffer* rtp, guint8 mprtp_ext_header_id)
{
  gpointer pointer = NULL;
  guint size;
  return gst_rtp_buffer_get_extension_onebyte_header(rtp, mprtp_ext_header_id, 0, &pointer, &size);
}

gboolean gst_rtp_buffer_is_fectype(GstRTPBuffer* rtp, guint8 fec_payload_type)
{
  return gst_rtp_buffer_get_payload_type(rtp) == fec_payload_type;
}
