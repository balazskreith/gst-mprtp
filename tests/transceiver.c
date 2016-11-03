/* GStreamer
 *
 * appsrc-src2.c: example for using gst_app_src_push_sample().
 *
 * Copyright (C) 2014 Nicola Murino <nicola.murino@gmail.com>
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

#include <gst/gst.h>

#include <string.h>

#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include "transceiver.h"

static GstFlowReturn
_on_new_sample_from_receiver (GstElement * elt, GstElement* transmitter)
{
  GstSample *sample;
  GstFlowReturn result;
  GstBuffer* buffer;

  sample = gst_app_sink_pull_sample (GST_APP_SINK (elt));
  buffer = gst_sample_get_buffer(sample);
//g_print("New sample arrived\n");

  result = gst_app_src_push_buffer (GST_APP_SRC (transmitter), gst_buffer_ref(buffer));
//  result = gst_app_src_push_sample(GST_APP_SRC(transmitter), sample);
  gst_sample_unref (sample);

  return result;
}

GstElement* make_transceiver(void)
{
  GstBin* transceiver     = GST_BIN(gst_bin_new(NULL));
  GstElement* receiver    = gst_element_factory_make("appsink",  "receiver");
  GstElement* transmitter = gst_element_factory_make("appsrc", "transmitter");

  receiver    = gst_element_factory_make("appsink",  "receiver");
  transmitter = gst_element_factory_make("appsrc",   "transmitter");

  g_object_set (G_OBJECT (receiver), "emit-signals", TRUE, "sync", FALSE, "async", FALSE, NULL);
  g_signal_connect (receiver, "new-sample", G_CALLBACK (_on_new_sample_from_receiver), transmitter);

  gst_bin_add_many(transceiver,

      receiver,
      transmitter,

      NULL
  );


  g_object_set(G_OBJECT(transmitter),
      "is-live", TRUE,
      "format", GST_FORMAT_TIME,
      NULL);

  setup_ghost_sink(receiver, transceiver);
  setup_ghost_src(transmitter, transceiver);

  return GST_ELEMENT(transceiver);
}

