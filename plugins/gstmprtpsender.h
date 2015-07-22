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

#ifndef _GST_MPRTPSENDER_H_
#define _GST_MPRTPSENDER_H_

#include <gst/gst.h>
#include "mprtpssubflow.h"
#include "schtree.h"
#include "gstmprtcpbuffer.h"

G_BEGIN_DECLS

#define GST_TYPE_MPRTPSENDER   (gst_mprtpsender_get_type())
#define GST_MPRTPSENDER(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPRTPSENDER,GstMprtpsender))
#define GST_MPRTPSENDER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPRTPSENDER,GstMprtpsenderClass))
#define GST_IS_MPRTPSENDER(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPRTPSENDER))
#define GST_IS_MPRTPSENDER_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPRTPSENDER))

typedef struct _GstMprtpsender GstMprtpsender;
typedef struct _GstMprtpsenderPrivate GstMprtpsenderPrivate;
typedef struct _GstMprtpsenderClass GstMprtpsenderClass;

struct _GstMprtpsender
{
  GstElement     base_object;

  GstPad        *rtp_sinkpad;
  GstPad        *rtcp_sinkpad;
  GstPad        *rtcp_srcpad;
  GstPad        *mprtcp_sinkpad;

  GMutex         subflows_mutex;
  guint32        ssrc;
  GstSegment     segment;
  guint8         ext_header_id;
  guint16        mprtcp_mtu;
  gfloat         alpha_charge;
  gfloat         alpha_discharge;
  gfloat         beta;
  gfloat         gamma;
  guint32        max_delay;
  GList*         subflows;
  gboolean       no_active_subflows;
  GList*         rtcp_send_item;
  SchTree*       schtree;
  GstTask*       scheduler;
  guint32        scheduler_state;
  GRecMutex      scheduler_mutex;
  GstTask*       riporter;
  GRecMutex      riporter_mutex;
  GCond          scheduler_cond;
  GstClockTime   scheduler_last_run;

  GstMprtpsenderPrivate *priv;
};

struct _GstMprtpsenderClass
{
  GstElementClass base_class;
};

GType gst_mprtpsender_get_type (void);

G_END_DECLS

#endif
