/* GStreamer
 * Copyright (C) 2013 Collabora Ltd.
 *   @author Torrie Fischer <torrie.fischer@collabora.co.uk>
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
#include <gst/rtp/rtp.h>
#include <stdlib.h>
#include "test.h"

typedef struct _SessionData
{
  int ref;
  GstElement *rtpbin;
  guint sessionNum;

  GstElement *receiver;
  GstElement *decoder;
  GstElement* sink;

} SessionData;

static SessionData *
session_ref (SessionData * data)
{
  g_atomic_int_inc (&data->ref);
  return data;
}

static void
session_unref (gpointer data)
{
  SessionData *session = (SessionData *) data;
  if (g_atomic_int_dec_and_test (&session->ref)) {
    g_object_unref (session->rtpbin);
    g_free (session);
  }
}

static SessionData *
session_new ()
{
  SessionData *ret = g_new0 (SessionData, 1);
  return session_ref (ret);
}

static GstElement*
make_rtp_simple_receiver (GstCaps *caps)
{
  GstElement *receiverBin = GST_BIN (gst_bin_new (NULL));
  GstElement *rtpSrc   = gst_element_factory_make ("udpsrc", NULL);
  RTPSimpleReceiverParams* params = (RTPSimpleReceiverParams*) receiver_params;

  gst_bin_add_many (GST_BIN (receiverBin),

      rtpSrc,

      NULL);

  g_object_set (rtpSrc, "port", params->bound_port, "caps", caps, NULL);

  setup_ghost_src(rtpSrc, receiverBin);

  return GST_ELEMENT(receiverBin);
}

static GstElement* make_theora_decoder(void)
{
  GstBin *decoderBin = GST_BIN (gst_bin_new (NULL));
  GstElement *depayloader = gst_element_factory_make ("rtptheoradepay", NULL);
  GstElement *decoder = gst_element_factory_make ("theoradec", NULL);
  GstElement *converter = gst_element_factory_make ("videoconvert", NULL);

  gst_bin_add_many (decoderBin, depayloader, decoder, converter, NULL);
  gst_element_link_many (depayloader, decoder, converter, NULL);

  setup_ghost_sink (depayloader, decoderBin);
  setup_ghost_src  (converter, decoderBin);

  return GST_ELEMENT(decoderBin);

}

static GstElement* make_vp8_decoder(void)
{
  GstBin *decoderBin = GST_BIN (gst_bin_new (NULL));
  GstElement *depayloader = gst_element_factory_make ("rtpvp8depay", NULL);
  GstElement *decoder     = gst_element_factory_make ("vp8dec", NULL);
  GstElement *converter   = gst_element_factory_make ("videoconvert", NULL);


  gst_bin_add_many (decoderBin, depayloader, decoder, converter, NULL);
  gst_element_link_many (depayloader, decoder, converter, NULL);

  setup_ghost_sink (depayloader, decoderBin);
  setup_ghost_src  (converter, decoderBin);

  return GST_ELEMENT(decoderBin);
}

static GstElement* make_autovideo_sink(void)
{
  GstElement *autovideosink = gst_element_factory_make ("autovideosink", NULL);

  g_object_set (autovideosink, "sync", FALSE, NULL);

  return autovideosink;
}

int
main (int argc, char **argv)
{
  GstPipeline *pipe;
  SessionData *session;
  GstCaps* caps;
  GstBus *bus;
  GMainLoop *loop = NULL;
  gchar pipeline_string[1024];

  GError *error = NULL;
  GOptionContext *context;

  memset(pipeline_string, 0, 1024);

  context = g_option_context_new ("Sender");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_parse (context, &argc, &argv, &error);
  if(info){
    _print_info();
    return 0;
  }
  _setup_test_params();
  _print_receiver_params();

  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);
  pipe = GST_PIPELINE (gst_pipeline_new (NULL));

  bus = gst_element_get_bus (GST_ELEMENT (pipe));
  g_signal_connect (bus, "message::error", G_CALLBACK (cb_error), loop);
  g_signal_connect (bus, "message::warning", G_CALLBACK (cb_warning), pipe);
  g_signal_connect (bus, "message::state-changed", G_CALLBACK (cb_state), pipe);
  g_signal_connect (bus, "message::eos", G_CALLBACK (cb_eos), loop);
  gst_bus_add_signal_watch (bus);
  gst_object_unref (bus);

  caps = gst_caps_new_simple ("application/x-rtp",
        "media", G_TYPE_STRING, "video",
        "clock-rate", G_TYPE_INT, video_params->clock_rate,
        "encoding-name", G_TYPE_STRING, video_params->codec_string,

        NULL);

  session = session_new();

  if(receiver_params->type == TRANSFER_TYPE_RTPSIMPLE){
    session->receiver = make_rtp_simple_receiver(caps);
    strcat(pipeline_string, "RTPReceiver -> ");
  }

  if(video_params->codec == CODEC_TYPE_THEORA){
    session->decoder  = make_theora_decoder();
    strcat(pipeline_string, "TheoraDecoder -> ");
  }else if(video_params->codec == CODEC_TYPE_VP8){
    session->decoder  = make_vp8_decoder();
    strcat(pipeline_string, "VP8Decoder -> ");
  }

  session->sink = make_autovideo_sink();
  strcat(pipeline_string, "AutoVideoSink");

  g_print("Pipeline: %s\n", pipeline_string);

  gst_bin_add_many (GST_BIN (pipe),

      session->receiver,
      session->decoder,
      session->sink,

      NULL);

  gst_element_link_many(session->receiver, session->decoder, session->sink, NULL);

  g_print ("starting client pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_PLAYING);

  g_main_loop_run (loop);

  g_print ("stoping client pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_NULL);

  gst_object_unref (pipe);
  g_main_loop_unref (loop);
  session_unref (session);

  return 0;
}
