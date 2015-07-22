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

#ifndef _GST_MPRTCPRECEIVER_H_
#define _GST_MPRTCPRECEIVER_H_

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_MPRTCPRECEIVER   (gst_mprtcpreceiver_get_type())
#define GST_MPRTCPRECEIVER(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPRTCPRECEIVER,GstMprtcpreceiver))
#define GST_MPRTCPRECEIVER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPRTCPRECEIVER,GstMprtcpreceiverClass))
#define GST_IS_MPRTCPRECEIVER(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPRTCPRECEIVER))
#define GST_IS_MPRTCPRECEIVER_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPRTCPRECEIVER))

typedef struct _GstMprtcpreceiver GstMprtcpreceiver;
typedef struct _GstMprtcpreceiverClass GstMprtcpreceiverClass;

struct _GstMprtcpreceiver
{
  GstElement     base_mprtcpreceiver;
  GRWLock        rwmutex;
  GList*         subflows;
  GstPad*        rtp_srcpad;
  GstPad*        rtcp_sinkpad;
  GstPad*        rtcp_srcpad;
  GstPad*        mprtcp_srcpad;
};

struct _GstMprtcpreceiverClass
{
  GstElementClass base_mprtcpreceiver_class;
};

GType gst_mprtcpreceiver_get_type (void);

G_END_DECLS

#endif //_GST_MPRTCPRECEIVER_H_
