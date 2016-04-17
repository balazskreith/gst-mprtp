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
#include "mprtpdefs.h"


typedef struct _GstMpRTPBuffer GstMpRTPBuffer;

#define MPRTP_PLUGIN_MAX_RLE_LENGTH 20

#define MPRTP_PLUGIN_MAX_SUBFLOW_NUM 32

#define GST_MPRTP_BUFFER_INIT {FALSE, NULL, 0, 0, 0, 0, 0, 0}

#define CONSTRAIN(min,max,value) MAX(min, MIN(max, value))

struct _GstMpRTPBuffer{
//  GstRTPBuffer   rtp;
  GstBuffer     *buffer;
  guint          payload_bytes;
  guint8         subflow_id;
  guint16        subflow_seq;
  guint32        timestamp;
  guint32        ssrc;
  guint64        abs_snd_ntp_time;
  guint64        abs_rcv_ntp_time;
  GstClockTime   delay;
  guint8         payload_type;
  gboolean       fec_packet;
  guint16        abs_seq;
  gboolean       marker;
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

gboolean gst_buffer_is_mprtp(GstBuffer *buffer, guint8 mprtp_ext_header_id);
gboolean gst_buffer_is_monitoring_rtp(GstBuffer *buffer, guint8 monitoring_payload_type);
void gst_mprtp_buffer_init(GstMpRTPBuffer *mprtp,
                               GstBuffer *buffer,
                               guint8 mprtp_ext_header_id,
                               guint8 abs_time_ext_header_id,
                               guint8 monitor_payload_type);

#endif //_GST_MPRTPBUFFER_H_
