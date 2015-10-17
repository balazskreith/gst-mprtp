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
#include "mprtpspath.h"
#include "streamsplitter.h"

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


struct _GstMprtpscheduler
{
  GstElement base_object;

  GstPad *rtp_sinkpad;
  GstPad *mprtp_srcpad;
  //GstPad        *rtcp_sinkpad;
  //GstPad        *rtcp_srcpad;
  GstPad *mprtcp_rr_sinkpad;
  GstPad *mprtcp_sr_srcpad;

  gfloat alpha_value;
  gfloat beta_value;
  gfloat gamma_value;
  guint8 ext_header_id;
  guint flow_controlling_mode;
  GHashTable *paths;
  GRWLock rwmutex;
  StreamSplitter *splitter;
  gpointer controller;
  gboolean riport_flow_signal_sent;

  void (*controller_add_path) (gpointer, guint8, MPRTPSPath *);
  void (*controller_rem_path) (gpointer, guint8);
  void (*mprtcp_receiver) (gpointer, GstBuffer *);
  void (*riport_can_flow) (gpointer);
  guint32 rtcp_sent_octet_sum;

  /*
     gboolean       flowable;
     guint          rtcp_sent_octet_sum;
     GMutex         subflows_mutex;
     guint32        ssrc;
     GstSegment     segment;
     guint8         ext_header_id;
     gboolean       subflow_riports_enabled;
     gboolean       auto_sending_rates_enabled;
     guint16        mprtcp_mtu;
     gfloat         charge_value;

     guint32        max_delay;
     GList*         subflows;
     gboolean       has_new_manual_sending_rate;
     gboolean       no_active_subflows;
     GstTask*       scheduler;
     guint32        scheduler_state;
     GRecMutex      scheduler_mutex;
     GstTask*       riporter;
     GRecMutex      riporter_mutex;
     GCond          scheduler_cond;
     GstClockTime   scheduler_last_run;
     GstClockTime   last_schtree_commit;
   */

};

struct _GstMprtpschedulerClass
{
  GstElementClass base_class;
};

GType gst_mprtpscheduler_get_type (void);


G_END_DECLS
#endif //_GST_MPRTPSCHEDULER_H_
