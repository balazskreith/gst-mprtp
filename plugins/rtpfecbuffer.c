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

void rtpfecbuffer_get_inistring(GstBuffer *buf, guint8* result)
{
  GstMapInfo info = GST_MAP_INFO_INIT;
  guint16 packet_length;

  gst_buffer_map(buf, &info, GST_MAP_READ);
  memcpy(result, info.data, 8);
  packet_length = g_htons(g_ntohs(info.size-12));
  memcpy(result + 8, &packet_length, 2);
  gst_buffer_unmap(buf, &info);
}

guint16 rtpfecbuffer_get_sn_base(GstBuffer *buf)
{
  GstMapInfo info = GST_MAP_INFO_INIT;
  GstRTPFECHeader *header;
  guint16 result;
  gst_buffer_map(buf, &info, GST_MAP_READ);
  header = info.data;
  result = g_ntohs(header->sn_base);
  gst_buffer_unmap(buf, &info);
  return result;
}
