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

#ifndef _GST_MPRTPBUFFER_H_
#define _GST_MPRTPBUFFER_H_

#include <gst/gst.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include <gst/rtp/gstrtpbuffer.h>
#include "pointerpool.h"


typedef struct _GstMpRTPBuffer GstMpRTPBuffer;

#define GST_MPRTP_BUFFER_INIT {GST_RTP_BUFFER_INIT, FALSE, NULL, 0, 0, 0, 0, 0, 0}

struct _GstMpRTPBuffer{
  GstRTPBuffer   rtp;
  gboolean       initialized;
  GstBuffer     *buffer;
  guint          payload_bytes;
  guint8         subflow_id;
  guint16        subflow_seq;
  guint64        abs_snd_ntp_time;
  guint64        abs_rcv_ntp_time;
  GstClockTime   delay;
};


typedef struct _MPRTPSubflowHeaderExtension MPRTPSubflowHeaderExtension;
typedef struct _RTPAbsTimeExtension RTPAbsTimeExtension;

struct _MPRTPSubflowHeaderExtension
{
  guint8 id;
  guint16 seq;
};

struct _RTPAbsTimeExtension
{
  guint8 time[3];
};

#define gst_mprtp_ptr_buffer_get_ssrc(mprtp) gst_rtp_buffer_get_ssrc(&mprtp->rtp)
#define gst_mprtp_ptr_buffer_get_timestamp(mprtp) gst_rtp_buffer_get_timestamp(&mprtp->rtp)
#define gst_mprtp_ptr_buffer_get_abs_seq(mprtp) gst_rtp_buffer_get_seq(&mprtp->rtp)
#define gst_mprtp_ptr_buffer_get_payload_type(mprtp) gst_rtp_buffer_get_payload_type(&mprtp->rtp)

gboolean gst_mprtp_buffer_init(GstMpRTPBuffer *mprtp,
                               GstBuffer *buffer,
                               guint8 mprtp_ext_header_id,
                               guint8 abs_time_ext_header_id);
gboolean gst_mprtp_buffer_read_map(GstMpRTPBuffer *mprtp);
void gst_mprtp_buffer_read_unmap(GstMpRTPBuffer *mprtp);

#endif //_GST_MPRTPBUFFER_H_
