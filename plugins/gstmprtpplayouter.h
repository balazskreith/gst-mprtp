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

#ifndef _GST_MPRTPPLAYOUTER_H_
#define _GST_MPRTPPLAYOUTER_H_

#include <gst/gst.h>
#include "packetforwarder.h"
#include "gstmprtcpbuffer.h"
#include "streamjoiner.h"
#include "rcvctrler.h"
#include "fecdec.h"

#if GLIB_CHECK_VERSION (2, 35, 7)
#include <gio/gnetworking.h>
#else

/* nicked from gnetworking.h */
#ifdef G_OS_WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#include <winsock2.h>
#undef interface
#include <ws2tcpip.h>           /* for socklen_t */
#endif /* G_OS_WIN32 */

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#endif

G_BEGIN_DECLS
#define GST_TYPE_MPRTPPLAYOUTER   (gst_mprtpplayouter_get_type())
#define GST_MPRTPPLAYOUTER(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPRTPPLAYOUTER,GstMprtpplayouter))
#define GST_MPRTPPLAYOUTER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPRTPPLAYOUTER,GstMprtpplayouterClass))
#define GST_IS_MPRTPPLAYOUTER(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPRTPPLAYOUTER))
#define GST_IS_MPRTPPLAYOUTER_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPRTPPLAYOUTER))
#define GST_MPRTCP_PLAYOUTER_SENT_BYTES_STRUCTURE_NAME "GstCustomQueryMpRTCPPlayouter"
#define GST_MPRTCP_PLAYOUTER_SENT_OCTET_SUM_FIELD "RTCPPlayouterSentBytes"

typedef struct _GstMprtpplayouter GstMprtpplayouter;
typedef struct _GstMprtpplayouterClass GstMprtpplayouterClass;

struct _GstMprtpplayouter
{
  GstElement      base_mprtpreceiver;
  GMutex          mutex;

  guint64         clock_base;

  GstClock*       sysclock;
  StreamJoiner*   joiner;
  RcvController*  controller;
  RcvSubflows*    subflows;
  FECDecoder*     fec_decoder;
  RcvPackets*     rcvpackets;
  RcvTracker*     rcvtracker;

  guint8          fec_payload_type;

  GstPad*         mprtp_srcpad;
  GstPad*         mprtp_sinkpad;
  GstPad*         mprtcp_sr_sinkpad;
  GstPad*         mprtcp_rr_srcpad;

  GstClockTime    repair_window_max;
  GstClockTime    repair_window_min;

  gboolean        logging;

  Mediator*        repair_channel;
  GAsyncQueue*     discarded_packets;
  GAsyncQueue*     packetsq;
  GAsyncQueue*     mprtp_out;
  GAsyncQueue*     mprtcp_out;

  gboolean         fec_requested;
//  PacketForwarder* packetforwarder;

};

struct _GstMprtpplayouterClass
{
  GstElementClass base_mprtpreceiver_class;
};

GType gst_mprtpplayouter_get_type (void);

G_END_DECLS
#endif //_GST_MPRTPPLAYOUTER_H_
