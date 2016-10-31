/* GStreamer
 * Copyright (C) 2016 FIXME <fixme@example.com>
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

#ifndef _GST_TRANSCEIVER_H_
#define _GST_TRANSCEIVER_H_

#include <gst/base/gstbasesink.h>

G_BEGIN_DECLS

#define GST_TYPE_TRANSCEIVER   (gst_transceiver_get_type())
#define GST_TRANSCEIVER(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TRANSCEIVER,GstTransceiver))
#define GST_TRANSCEIVER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TRANSCEIVER,GstTransceiverClass))
#define GST_IS_TRANSCEIVER(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TRANSCEIVER))
#define GST_IS_TRANSCEIVER_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TRANSCEIVER))

typedef struct _GstTransceiver GstTransceiver;
typedef struct _GstTransceiverClass GstTransceiverClass;

struct _GstTransceiver
{
  GstBaseSink    base;

  GstPad*        src;
};

struct _GstTransceiverClass
{
  GstBaseSinkClass base_transceiver_class;
};

GType gst_transceiver_get_type (void);

G_END_DECLS

#endif
