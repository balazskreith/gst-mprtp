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
#include "mprtputils.h"
#include "messenger.h"
#include "rtpfecbuffer.h"

G_BEGIN_DECLS

#ifdef __WIN32__

#define PACKED
#pragma pack(push,1)


#else

#define PACKED __attribute__ ((__packed__))

#endif

typedef struct PACKED _RTPStatPacket
{
  guint64              tracked_ntp;
  guint16              seq_num;
  guint32              ssrc;
  guint8               subflow_id;
  guint16              subflow_seq;

  guint8               marker : 1;
  guint8               payload_type : 7;
  guint32              timestamp;

  guint                header_size;
  guint                payload_size;

  guint16              protect_begin;
  guint16              protect_end;
}RTPStatPacket;

#ifdef __WIN32__

#pragma pack(pop)
#undef PACKED

#else

#undef PACKED

#endif

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


/************************************************************/
/* This is a datagram socket client sample program for UNIX */
/* domain sockets. This program creates a socket and sends  */
/* data to a server.                                        */
/************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
typedef struct{
  gchar sock_path[256];
  int client_socket;
  struct sockaddr_un remote;
  gboolean error_reported;
}SocketWriter;


/**
 * GstRTPStatMaker2:
 *
 * Opaque #GstRTPStatMaker2 data structure
 */
struct _GstRTPStatMaker2 {
  GstBaseTransform              element;

  /*< private >*/
  GstClockID                    clock_id;
  GstClock*                     sysclock;
  gboolean                      sync;
  GstClockTime                  prev_timestamp;
  GstClockTime                  prev_duration;
  guint64                       prev_offset;
  guint64                       prev_offset_end;
  gchar*                        last_message;

//  gboolean                      touched_sync_active;
//  gchar                         touched_sync_location[256];

//  GstTask*                      thread;
//  GRecMutex                     thread_mutex;
//  GSList*                       loggers;
//  gpointer                      default_logger;
//  Messenger*                    packets;
  guint8                        mprtp_ext_header_id;
  guint8                        fec_payload_type;

  guint64                       offset;
  GstClockTime                  upstream_latency;
  GCond                         blocked_cond;
  gboolean                      blocked;
  SocketWriter*                 socket_writer;
  gpointer                      tmp_packet;
};

struct _GstRTPStatMaker2Class {
  GstBaseTransformClass parent_class;

  /* signals */
  void (*handoff) (GstElement *element, GstBuffer *buf);
};

GType gst_rtpstatmaker2_get_type (void);

G_END_DECLS

#endif /* __GST_RTPSTATMAKER2_H__ */
