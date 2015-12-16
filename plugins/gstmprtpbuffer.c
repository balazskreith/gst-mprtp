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


gboolean gst_mprtp_buffer_init(GstMpRTPBuffer *mprtp,
                               GstBuffer *buffer,
                               guint8 mprtp_ext_header_id,
                               guint8 abs_time_ext_header_id)
{
  gpointer pointer = NULL;
  guint size;
  MPRTPSubflowHeaderExtension *subflow_infos;
  guint64 snd_time = 0;
  g_return_val_if_fail(mprtp, FALSE);
  mprtp->buffer = buffer;

  if (!gst_mprtp_buffer_get_extension_onebyte_header (mprtp,
                                                    mprtp_ext_header_id,
                                                    &pointer,
                                                    &size))
  {
    return FALSE;
  }
  subflow_infos = (MPRTPSubflowHeaderExtension *) pointer;
  mprtp->subflow_id = subflow_infos->id;
  mprtp->subflow_seq = subflow_infos->seq;
  mprtp->payload_bytes = gst_mprtp_buffer_get_payload_len(mprtp);
  if(abs_time_ext_header_id == 0 || abs_time_ext_header_id > 15)
    goto done;

  if (!gst_mprtp_buffer_get_extension_onebyte_header (mprtp,
            abs_time_ext_header_id, &pointer, &size))
  {
    return FALSE;
  }
  else
  {
    guint32 rcv_chunk = (NTP_NOW >> 14) & 0x00ffffff;
    guint64 ntp_base = NTP_NOW;
    memcpy (&snd_time, pointer, 3);
    if(rcv_chunk < snd_time){
      ntp_base-=0x0000004000000000UL;
    }
    snd_time <<= 14;
    snd_time |=  (ntp_base & 0xFFFFFFC000000000UL);
  }
  mprtp->abs_snd_ntp_time = snd_time;
  mprtp->abs_rcv_ntp_time = NTP_NOW;
  mprtp->delay = get_epoch_time_from_ntp_in_ns(NTP_NOW - snd_time);
  if(mprtp->abs_rcv_ntp_time < mprtp->abs_snd_ntp_time){
    g_print("VALAMI PROBLÉMA VAN MÁR MEGINT\n");
  }
done:
  mprtp->initialized = TRUE;
  return TRUE;
}

guint32 gst_mprtp_buffer_get_ssrc(GstMpRTPBuffer *mprtp)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint32 result;
  g_return_val_if_fail(gst_rtp_buffer_map(mprtp->buffer, GST_MAP_READ, &rtp), 0);
  result = gst_rtp_buffer_get_ssrc(&rtp);
  gst_rtp_buffer_unmap(&rtp);
  return result;
}

guint32 gst_mprtp_buffer_get_timestamp(GstMpRTPBuffer *mprtp)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint32 result;
  g_return_val_if_fail(gst_rtp_buffer_map(mprtp->buffer, GST_MAP_READ, &rtp), 0);
  result = gst_rtp_buffer_get_timestamp(&rtp);
  gst_rtp_buffer_unmap(&rtp);
  return result;
}

guint16 gst_mprtp_buffer_get_abs_seq(GstMpRTPBuffer *mprtp)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint16 result;
  g_return_val_if_fail(gst_rtp_buffer_map(mprtp->buffer, GST_MAP_READ, &rtp), 0);
  result = gst_rtp_buffer_get_seq(&rtp);
  gst_rtp_buffer_unmap(&rtp);
  return result;
}

guint8 gst_mprtp_buffer_get_payload_type(GstMpRTPBuffer *mprtp)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint8 result;
  g_return_val_if_fail(gst_rtp_buffer_map(mprtp->buffer, GST_MAP_READ, &rtp), 0);
  result = gst_rtp_buffer_get_payload_type(&rtp);
  gst_rtp_buffer_unmap(&rtp);
  return result;
}

guint gst_mprtp_buffer_get_payload_len(GstMpRTPBuffer *mprtp)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint result;
  g_return_val_if_fail(gst_rtp_buffer_map(mprtp->buffer, GST_MAP_READ, &rtp), 0);
  result = gst_rtp_buffer_get_payload_len(&rtp);
  gst_rtp_buffer_unmap(&rtp);
  return result;
}

gboolean gst_mprtp_buffer_get_extension_onebyte_header(
    GstMpRTPBuffer *mprtp, guint8 id, gpointer* data, guint *size)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  g_return_val_if_fail(gst_rtp_buffer_map(mprtp->buffer, GST_MAP_READ, &rtp), FALSE);
  gst_rtp_buffer_get_extension_onebyte_header(&rtp, id, 0, data, size);
  gst_rtp_buffer_unmap(&rtp);
  return TRUE;
}


