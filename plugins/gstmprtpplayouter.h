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
#include "gstmprtcpbuffer.h"
#include "mprtprpath.h"
#include "streamjoiner.h"
#include <gst/net/gstnetaddressmeta.h>
#include "gstmprtpbuffer.h"
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
  GRWLock         rwmutex;

  guint8          mprtp_ext_header_id;
  guint8          abs_time_ext_header_id;
  guint32         pivot_ssrc;
  guint32         pivot_clock_rate;
  GSocketAddress *pivot_address;
  guint8          pivot_address_subflow_id;
  guint8          fec_payload_type;
  guint64         clock_base;
  gboolean        auto_rate_and_cc;
  gboolean        rtp_passthrough;

  GstClockTime    playout_point;

  GstPad*         mprtp_srcpad;
  GstPad*         mprtp_sinkpad;
  GstPad*         mprtcp_sr_sinkpad;
  GstPad*         mprtcp_rr_srcpad;


  GstClockTime    repair_window_max;
  GstClockTime    repair_window_min;

  GHashTable*     paths;
  PacketsRcvQueue* rcvqueue;
  StreamJoiner*   joiner;
  gboolean          logging;
  RcvController*    controller;
  GstClock*       sysclock;

  guint           subflows_num;
  FECDecoder*     fec_decoder;
  GstClockTime    last_fec_clean;
  guint16         expected_seq;
  gboolean        expected_seq_init;
  guint32         rtcp_sent_octet_sum;

  GstTask*                      thread;
  GRecMutex                     thread_mutex;

};

struct _GstMprtpplayouterClass
{
  GstElementClass base_mprtpreceiver_class;
};

GType gst_mprtpplayouter_get_type (void);

G_END_DECLS
#endif //_GST_MPRTPPLAYOUTER_H_
