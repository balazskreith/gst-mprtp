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

#ifndef _GST_MPRTPSCHEDULER_H_
#define _GST_MPRTPSCHEDULER_H_

#include <gst/gst.h>

#include "gstmprtcpbuffer.h"
#include "sndctrler.h"
#include "streamsplitter.h"
#include "mprtplogger.h"
#include "fecenc.h"
#include "packetforwarder.h"
#include "mediator.h"

G_BEGIN_DECLS
#define GST_TYPE_MPRTPSCHEDULER   (gst_mprtpscheduler_get_type())
#define GST_MPRTPSCHEDULER(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPRTPSCHEDULER,GstMprtpscheduler))
#define GST_MPRTPSCHEDULER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPRTPSCHEDULER,GstMprtpschedulerClass))
#define GST_IS_MPRTPSCHEDULER(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPRTPSCHEDULER))
#define GST_IS_MPRTPSCHEDULER_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPRTPSCHEDULER))
#define GST_MPRTCP_SCHEDULER_SENT_BYTES_STRUCTURE_NAME "GstCustomQueryMpRTCPScheduler"
#define GST_MPRTCP_SCHEDULER_SENT_OCTET_SUM_FIELD "RTCPSchedulerSentBytes"

typedef struct _GstMprtpscheduler GstMprtpscheduler;
typedef struct _GstMprtpschedulerClass GstMprtpschedulerClass;
typedef struct _GstMprtpschedulerPrivate GstMprtpschedulerPrivate;


struct _GstMprtpscheduler
{
  GstElement                    base_object;
  GMutex                        mutex;
  GCond                         cond;
  GstPad*                       rtp_sinkpad;
  GstPad*                       mprtp_srcpad;
  GstPad*                       mprtcp_rr_sinkpad;
  GstPad*                       mprtcp_sr_srcpad;

  gboolean                      preroll;

  SndSubflows*                  subflows;
  SndPackets*                   sndpackets;
  StreamSplitter*               splitter;
  SndController*                controller;
  SndTracker*                   sndtracker;
  gboolean                      riport_flow_signal_sent;
  GstSegment                    segment;
  GstClockTime                  position_out;

  guint8                        fec_payload_type;
  GstClockTime                  obsolation_treshold;
  GstClock*                     sysclock;

  GstClockTime                  last_pts;

  guint32                       rtcp_sent_octet_sum;

  GstTask*                      thread;
  GRecMutex                     thread_mutex;
  FECEncoder*                   fec_encoder;
  guint32                       fec_interval;
  guint32                       sent_packets;

  Mediator*                     monitoring;
  GAsyncQueue*                  packetsq;
  GAsyncQueue*                  emitterq;
  Notifier*                     on_rtcp_ready;

  GstMprtpschedulerPrivate*     priv;


};

struct _GstMprtpschedulerClass
{
  GstElementClass base_class;

  void  (*mprtp_media_rate_utilization) (GstElement *,gpointer);
};

GType gst_mprtpscheduler_get_type (void);



G_END_DECLS
#endif //_GST_MPRTPSCHEDULER_H_
