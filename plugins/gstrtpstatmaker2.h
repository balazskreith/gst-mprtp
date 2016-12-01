/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstrtpstatmaker2.h:
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


#ifndef __GST_RTPSTATMAKER2_H__
#define __GST_RTPSTATMAKER2_H__


#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include "monitor.h"

G_BEGIN_DECLS


#define GST_TYPE_RTPSTATMAKER2 \
  (gst_rtpstatmaker2_get_type())
#define GST_RTPSTATMAKER2(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_RTPSTATMAKER2,GstRTPStatMaker2))
#define GST_RTPSTATMAKER2_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_RTPSTATMAKER2,GstRTPStatMaker2Class))
#define GST_IS_RTPSTATMAKER2(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_RTPSTATMAKER2))
#define GST_IS_RTPSTATMAKER2_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_RTPSTATMAKER2))

typedef struct _GstRTPStatMaker2 GstRTPStatMaker2;
typedef struct _GstRTPStatMaker2Class GstRTPStatMaker2Class;

/**
 * GstRTPStatMaker2:
 *
 * Opaque #GstRTPStatMaker2 data structure
 */
struct _GstRTPStatMaker2 {
  GstBaseTransform   element;

  /*< private >*/
  GstClockID                    clock_id;
  GstClock*                     sysclock;
  gboolean                      sync;
  GstClockTime                  prev_timestamp;
  GstClockTime                  prev_duration;
  guint64                       prev_offset;
  guint64                       prev_offset_end;
  gchar*                        last_message;

  GstPad*                       packet_sinkpad;
  GstPad*                       packet_srcpad;

  GstBufferPool*                packetbufferpool;

  GstClockTime                  last_statlog;
  GstClockTime                  last_packetlog;
  GQueue*                       packetlogs2write;
  GQueue*                       packetlogstr2recycle;

  gboolean                      touched_sync_active;
  gchar                         touched_sync_location[256];
  gchar                         packetslog_file[256];
  gchar                         statslog_file[256];
  guint8                        mprtp_ext_header_id;

  guint8                        fec_payload_type;
  GstClockTime                  accumulation_length;
  GstClockTime                  sampling_time;
  GCond                         waiting_signal;

  GMutex                        mutex;
  Monitor*                      monitor;
  GstTask*                      thread;
  GRecMutex                     thread_mutex;

  gboolean                      csv_logging;
  gboolean                      packetsrc_linked;
  gboolean                      packetlogs_linked;
  gboolean                      statlogs_linked;

  guint64                       offset;
  GstClockTime                  upstream_latency;
  GCond                         blocked_cond;
  gboolean                      blocked;
};

struct _GstRTPStatMaker2Class {
  GstBaseTransformClass parent_class;

  /* signals */
  void (*handoff) (GstElement *element, GstBuffer *buf);
};

GType gst_rtpstatmaker2_get_type (void);

G_END_DECLS

#endif /* __GST_RTPSTATMAKER2_H__ */
