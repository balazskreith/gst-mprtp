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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstmprtcpreceiver
 *
 * The mprtcpreceiver element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v fakesrc ! mprtcpreceiver ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <gst/gst.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include "gstmprtpbuffer.h"
#include "gstmprtcpbuffer.h"

gboolean gst_buffer_is_mprtp(GstBuffer *buffer, guint8 mprtp_ext_header_id)
{
  gpointer pointer = NULL;
  guint size;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  gboolean result;
  g_return_val_if_fail(GST_IS_BUFFER(buffer), FALSE);
  g_return_val_if_fail(gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp), FALSE);
  result = gst_rtp_buffer_get_extension_onebyte_header(&rtp, mprtp_ext_header_id, 0, &pointer, &size);
  gst_rtp_buffer_unmap(&rtp);
  return result;
}

gboolean gst_buffer_is_monitoring_rtp(GstBuffer *buffer, guint8 monitoring_payload_type)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  gboolean result;
  g_return_val_if_fail(GST_IS_BUFFER(buffer), FALSE);
  g_return_val_if_fail(gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp), FALSE);
  result = gst_rtp_buffer_get_payload_type(&rtp) == monitoring_payload_type;
  gst_rtp_buffer_unmap(&rtp);
  return result;
}

void gst_mprtp_buffer_init(GstMpRTPBuffer *mprtp,
                               GstBuffer *buffer,
                               guint8 mprtp_ext_header_id,
                               guint8 abs_time_ext_header_id,
                               guint8 fec_payload_type)
{
  gpointer pointer = NULL;
  guint size;
  MPRTPSubflowHeaderExtension *subflow_infos;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  g_return_if_fail(mprtp);
  mprtp->buffer = buffer;
  g_return_if_fail(gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp));
  gst_rtp_buffer_get_extension_onebyte_header(&rtp, mprtp_ext_header_id,
                                              0, &pointer, &size);
  subflow_infos = (MPRTPSubflowHeaderExtension *) pointer;

  mprtp->subflow_id       = subflow_infos->id;
  mprtp->subflow_seq      = subflow_infos->seq;
  mprtp->payload_bytes    = gst_rtp_buffer_get_payload_len(&rtp);
  mprtp->ssrc             = gst_rtp_buffer_get_ssrc(&rtp);
  mprtp->timestamp        = gst_rtp_buffer_get_timestamp(&rtp);
  mprtp->marker           = gst_rtp_buffer_get_marker(&rtp);
  mprtp->abs_seq          = gst_rtp_buffer_get_seq(&rtp);
  mprtp->payload_type     = gst_rtp_buffer_get_payload_type(&rtp);
  mprtp->abs_rcv_ntp_time = NTP_NOW;
  mprtp->fec_packet   = mprtp->payload_type == fec_payload_type;
  size=0;
  pointer = NULL;
  if(0 < abs_time_ext_header_id &&
     gst_rtp_buffer_get_extension_onebyte_header(&rtp, abs_time_ext_header_id, 0, &pointer, &size))
  {
    guint32 rcv_chunk = (NTP_NOW >> 14) & 0x00ffffff;
    guint64 ntp_base = NTP_NOW;
    guint64 snd_time = 0;
    memcpy (&snd_time, pointer, 3);
//    g_print("rcv_chunk: %X:%u\nsnd_chunk: %X:%u\n",
//            rcv_chunk, rcv_chunk,
//            (guint32)snd_time, (guint32)snd_time);
    if(rcv_chunk < snd_time){
      ntp_base-=0x0000004000000000UL;
    }
    snd_time <<= 14;
    snd_time |=  (ntp_base & 0xFFFFFFC000000000UL);
    mprtp->abs_snd_ntp_time = snd_time;
    mprtp->delay = get_epoch_time_from_ntp_in_ns(NTP_NOW - snd_time);
//    g_print("Delay: %lu, ts: %lu, dur: %lu, off: %lu, pts: %lu\n",
//            mprtp->delay,
//            GST_BUFFER_TIMESTAMP(mprtp->buffer),
//            GST_BUFFER_DURATION(mprtp->buffer),
//            GST_BUFFER_OFFSET(mprtp->buffer),
//            GST_BUFFER_PTS(mprtp->buffer));
//    g_print("%lX:%lu (R)\n%lX:%lu (R) => %lu ->Delay: %lu\n",
//            mprtp->abs_rcv_ntp_time, mprtp->abs_rcv_ntp_time,
//            mprtp->abs_snd_ntp_time, mprtp->abs_snd_ntp_time,
//            mprtp->abs_rcv_ntp_time - mprtp->abs_snd_ntp_time,
//            mprtp->delay);

    if(mprtp->abs_rcv_ntp_time < mprtp->abs_snd_ntp_time){
      g_print("VALAMI PROBLÉMA VAN MÁR MEGINT\n");
    }
  }else{
    mprtp->abs_snd_ntp_time = 0;
    mprtp->delay = 0;
  }
  gst_rtp_buffer_unmap(&rtp);
}







