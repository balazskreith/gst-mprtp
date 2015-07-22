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

#ifndef _GST_MPRTCPSENDER_H_
#define _GST_MPRTCPSENDER_H_

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_MPRTCPSENDER   (gst_mprtcpsender_get_type())
#define GST_MPRTCPSENDER(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPRTCPSENDER,GstMprtcpsender))
#define GST_MPRTCPSENDER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPRTCPSENDER,GstMprtcpsenderClass))
#define GST_IS_MPRTCPSENDER(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPRTCPSENDER))
#define GST_IS_MPRTCPSENDER_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPRTCPSENDER))

typedef struct _GstMprtcpsender GstMprtcpsender;
typedef struct _GstMprtcpsenderClass GstMprtcpsenderClass;

struct _GstMprtcpsender
{
  GstElement     base_mprtcpsender;
  GRWLock        rwmutex;
  GList*         subflows;
  GList*         iterator;
  GstPad*        rtcp_sinkpad;
  GstPad*        mprtcp_sinkpad;
  GstPad*        rtcp_srcpad;
};

struct _GstMprtcpsenderClass
{
  GstElementClass base_mprtcpsender_class;
};

GType gst_mprtcpsender_get_type (void);

G_END_DECLS

#endif //_GST_MPRTCPSENDER_H_
