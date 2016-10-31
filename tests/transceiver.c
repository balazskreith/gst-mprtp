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
#include "test.h"


/* called when the appsink notifies us that there is a new buffer ready for
 * processing */
static GstFlowReturn
on_new_sample_from_receiver (GstElement * elt, GstElement* transmitter)
{
  GstSample *sample;
  GstFlowReturn result;

  /* get the sample from appsink */
  sample = gst_app_sink_pull_sample (GST_APP_SINK (elt));

  /* get source an push new sample */
  result = gst_app_src_push_sample (GST_APP_SRC (transmitter), sample);
g_print("HERE");
  /* we don't need the appsink sample anymore */
  gst_sample_unref (sample);

  return result;
}

/* called when we get a GstMessage from the source pipeline when we get EOS, we
 * notify the appsrc of it. */
static gboolean
on_receiver_message (GstBus * bus, GstMessage * message, GstElement *transmitter)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:
      g_print ("The source got dry\n");
      gst_app_src_end_of_stream (GST_APP_SRC (transmitter));
      break;
    case GST_MESSAGE_ERROR:
      g_print ("Received error\n");
      break;
    default:
      break;
  }
  return TRUE;
}

/* called when we get a GstMessage from the sink pipeline when we get EOS, we
 * exit the mainloop and this testapp. */
static gboolean
on_transmitter_message (GstBus * bus, GstMessage * message, GstElement *receiver)
{
  /* nil */
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:
      g_print ("Finished playback\n");
      break;
    case GST_MESSAGE_ERROR:
      g_print ("Received error\n");
      break;
    default:
      break;
  }
  return TRUE;
}



GstElement* make_transceiver()
{
  GstBin* transceiver     = GST_BIN(gst_bin_new(NULL));
  GstElement* receiver    = gst_element_factory_make("appsink",  "receiver");
  GstElement* transmitter = gst_element_factory_make("appsrc", "transmitter");
  GstBus*     recvBus;
  GstBus*     transmBus;
  GstCaps*    videoCaps;

  gst_bin_add_many(transceiver,

      receiver,
      transmitter,

      NULL
  );

  /* to be notified of messages from this pipeline, mostly EOS */
//  recvBus = gst_element_get_bus (receiver);
//  gst_bus_add_watch (recvBus, (GstBusFunc) on_receiver_message, transmitter);
//  gst_object_unref (recvBus);

  /* we use appsink in push mode, it sends us a signal when data is available
   * and we pull out the data in the signal callback. We want the appsink to
   * push as fast as it can, hence the sync=false */
  g_object_set (G_OBJECT (receiver), "emit-signals", TRUE, "sync", FALSE, "async", FALSE, NULL);
  g_signal_connect (receiver, "new-sample", G_CALLBACK (on_new_sample_from_receiver), transmitter);

  /* configure for time-based format */
  g_object_set (transmitter, "format", GST_FORMAT_TIME, NULL);
  /* uncomment the next line to block when appsrc has buffered enough */
  /* g_object_set (testsource, "block", TRUE, NULL); */


  videoCaps        = gst_caps_new_simple ("application/x-rtp",
        "media", G_TYPE_STRING, "video",
        "clock-rate", G_TYPE_INT, 90000,
        "width", G_TYPE_STRING, "352",//TODO: replace it by pram width
        "height", G_TYPE_STRING, "288",
        "framerate", GST_TYPE_FRACTION, 25, 1,
        "encoding-name", G_TYPE_STRING, "RAW", NULL
        );

  g_object_set(transmitter, "caps", videoCaps, NULL);

//  transmBus = gst_element_get_bus (transmitter);
//  gst_bus_add_watch (transmBus, (GstBusFunc) on_transmitter_message, receiver);
//  gst_object_unref (transmBus);

  setup_ghost_sink(receiver, transceiver);
  setup_ghost_src(transmitter, transceiver);

  return GST_ELEMENT(transceiver);
}

