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

#ifndef _GST_BUFFERPACER_H_
#define _GST_BUFFERPACER_H_

#include <gst/gst.h>

G_BEGIN_DECLS
#define GST_TYPE_BUFFERPACER   (gst_bufferpacer_get_type())
#define GST_BUFFERPACER(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BUFFERPACER,GstBufferPacer))
#define GST_BUFFERPACER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BUFFERPACER,GstBufferPacerClass))
#define GST_IS_BUFFERPACER(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BUFFERPACER))
#define GST_IS_BUFFERPACER_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BUFFERPACER))

typedef struct _GstBufferPacer GstBufferPacer;
typedef struct _GstBufferPacerClass GstBufferPacerClass;
typedef struct _GstBufferPacerPrivate GstBufferPacerPrivate;


struct _GstBufferPacer
{
  GstElement                    base;
  GMutex                        mutex;
  GCond                         receiving_signal;
  GCond                         waiting_signal;
  GstPad*                       sink;
  GstPad*                       src;

  gboolean                      preroll;

  GstSegment                    segment;

  GstClock*                     sysclock;

  GstClockTime                  last_pts;

  GQueue*                       packetsq;

  GstBufferPacerPrivate*     priv;


};

struct _GstBufferPacerClass
{
  GstElementClass base_class;

  void  (*mprtp_media_rate_utilization) (GstElement *,gpointer);
};

GType gst_bufferpacer_get_type (void);



G_END_DECLS
#endif //_GST_BUFFERPACER_H_
