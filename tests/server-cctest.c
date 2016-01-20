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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>


typedef struct _UtilizationSubflowReport{
  gboolean controlled;
  gint32   max_rate;
  gint32   lost_bytes;
  gint32   discarded_bytes;
  guint64  owd;
}UtilizationSubflowReport;

typedef struct _UtilizationReport{
  guint32                  target_rate;
  UtilizationSubflowReport subflows[32];
}UtilizationReport;

typedef struct _SessionData
{
  int ref;
  guint sessionNum;
  GstElement *input;
} SessionData;

typedef struct _TestParams{
  gboolean subflow1_active;
  gboolean subflow2_active;
}TestParams;

static TestParams test_parameters_;
static gint32 profile;

static GOptionEntry entries[] =
{
  { "profile", 0, 0, G_OPTION_ARG_INT, &profile, "Profile", NULL },
  { NULL }
};


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
    g_free (session);
  }
}

static SessionData *
session_new (guint sessionNum)
{
  SessionData *ret = g_new0 (SessionData, 1);
  ret->sessionNum = sessionNum;
  return session_ref (ret);
}

/*
 * Used to generate informative messages during pipeline startup
 */
static void
cb_state (GstBus * bus, GstMessage * message, gpointer data)
{
  GstObject *pipe = GST_OBJECT (data);
  GstState old, new, pending;
  gst_message_parse_state_changed (message, &old, &new, &pending);
  if (message->src == pipe) {
    g_print ("Pipeline %s changed state from %s to %s\n",
        GST_OBJECT_NAME (message->src),
        gst_element_state_get_name (old), gst_element_state_get_name (new));
  }
}

/*
 * Creates a GstGhostPad named "src" on the given bin, pointed at the "src" pad
 * of the given element
 */
static void
setup_ghost (GstElement * src, GstBin * bin)
{
  GstPad *srcPad = gst_element_get_static_pad (src, "src");
  GstPad *binPad = gst_ghost_pad_new ("src", srcPad);
  gst_element_add_pad (GST_ELEMENT (bin), binPad);
}
static GstElement *encoder;
guint bitrate = 1024;
static SessionData *
make_video_session (guint sessionNum)
{
  GstBin *videoBin = GST_BIN (gst_bin_new (NULL));
  GstElement *videoSrc = gst_element_factory_make ("videotestsrc", NULL);
//  encoder = gst_element_factory_make ("theoraenc", NULL);
//  GstElement *encoder = gst_element_factory_make ("jpegenc", NULL);
  encoder = gst_element_factory_make ("vp8enc", NULL);
//  GstElement *payloader = gst_element_factory_make ("rtptheorapay", NULL);
//  GstElement *payloader = gst_element_factory_make ("rtpjpegpay", NULL);
  GstElement *payloader = gst_element_factory_make ("rtpvp8pay", NULL);


  GstCaps *videoCaps;
  SessionData *session;
  g_object_set (videoSrc, "is-live", TRUE, "horizontal-speed", 1, NULL);
  //g_object_set (payloader, "config-interval", 2, NULL);
  g_object_set (encoder, "target-bitrate", 50000, NULL);
  g_object_set (encoder, "keyframe-max-dist", 10, NULL);
  g_object_set (encoder, "end-usage", 1, NULL);
  g_object_set (encoder, "deadline", 20000, NULL);
  g_object_set (encoder, "cq-level", 5, NULL);
  g_object_set (encoder, "undershoot", 100, NULL);
  g_object_set (encoder, "cpu-used", 5, NULL);
//  g_object_set (encoder, "keyframe-mode", 0, NULL);

  gst_bin_add_many (videoBin, videoSrc, encoder, payloader, NULL);
  videoCaps = gst_caps_new_simple (
      "video/x-raw",
      "width", G_TYPE_INT, 352,
      "height", G_TYPE_INT, 288,
      "framerate", GST_TYPE_FRACTION, 50, 1, NULL);

  gst_element_link_filtered (videoSrc, encoder, videoCaps);
  gst_element_link (encoder, payloader);

//  g_object_set(videoSrc, "filter-caps", videoCaps, NULL);

  setup_ghost (payloader, videoBin);

  session = session_new (sessionNum);
  session->input = GST_ELEMENT (videoBin);

  return session;
}

static GstElement *
request_aux_sender (GstElement * rtpbin, guint sessid, SessionData * session)
{
  GstElement *rtx, *bin;
  GstPad *pad;
  gchar *name;
  GstStructure *pt_map;

  GST_INFO ("creating AUX sender");
  bin = gst_bin_new (NULL);
  rtx = gst_element_factory_make ("rtprtxsend", NULL);
  pt_map = gst_structure_new ("application/x-rtp-pt-map",
       "96", G_TYPE_UINT, 99, NULL);
//  g_object_set (rtx, "payload-type-map", pt_map, NULL);
  gst_structure_free (pt_map);
  gst_bin_add (GST_BIN (bin), rtx);

  pad = gst_element_get_static_pad (rtx, "src");
  name = g_strdup_printf ("src_%u", sessid);
  gst_element_add_pad (bin, gst_ghost_pad_new (name, pad));
  g_free (name);
  gst_object_unref (pad);

  pad = gst_element_get_static_pad (rtx, "sink");
  name = g_strdup_printf ("sink_%u", sessid);
  gst_element_add_pad (bin, gst_ghost_pad_new (name, pad));
  g_free (name);
  gst_object_unref (pad);
  return bin;
}

#define print_boundary(text) g_printf("------------------------------ %s ---------------------\n", text);
#define print_command(str,...) g_printf("[CMD] "str"\n",__VA_ARGS__)



static void
changed_event (GstElement * mprtp_sch, gpointer ptr)
{
  UtilizationReport *ur = ptr;
  gint delta, new_bitrate, get_bitrate;
  g_object_get (encoder, "target-bitrate", &get_bitrate, NULL);
  {
//    gint i;
    new_bitrate = ur->target_rate;
//    for(i=0; i<32; ++i){
//      if(!ur->subflows[i].available) continue;
//      ur->subflows[i].target_weight=0.;
//    }
//    ur->subflows[1].max_rate=subflow1_max_rate;
//    ur->subflows[1].shareability = subflow1_shareability;
//    ur->subflows[2].max_rate=subflow2_max_rate;
//    ur->subflows[2].shareability = subflow2_shareability;
  }

//  g_print("get_bitrate: %d new_bitrate: %d\n", get_bitrate, new_bitrate);
  g_object_set (encoder, "target-bitrate", new_bitrate, NULL);
done:
  return;
}

static void
add_stream (GstPipeline * pipe, GstElement * rtpBin, SessionData * session,
           gchar* file)
{

  GstElement *rtpSink_1 = gst_element_factory_make ("udpsink", NULL);
  GstElement *rtpSink_2 = gst_element_factory_make ("udpsink", NULL);
  GstElement *rtcpSink = gst_element_factory_make ("udpsink", NULL);
  GstElement *rtcpSrc = gst_element_factory_make ("udpsrc", NULL);
  GstElement *rtpSrc_1 = gst_element_factory_make ("udpsrc", NULL);
  GstElement *rtpSrc_2 = gst_element_factory_make ("udpsrc", NULL);
  GstElement *mprtpsnd = gst_element_factory_make ("mprtpsender", NULL);
  GstElement *mprtprcv = gst_element_factory_make ("mprtpreceiver", NULL);
  GstElement *mprtpsch = gst_element_factory_make ("mprtpscheduler", NULL);
  int basePort;
  gchar *padName;

  basePort = 5000 + (session->sessionNum * 20);

  gst_bin_add_many (GST_BIN (pipe), rtpSink_1, rtpSink_2, mprtprcv, mprtpsnd,
      mprtpsch, rtcpSink, rtcpSrc, rtpSrc_1, rtpSrc_2,
      session->input, NULL);

  /* enable retransmission by setting rtprtxsend as the "aux" element of rtpbin */
  g_signal_connect (rtpBin, "request-aux-sender",
      (GCallback) request_aux_sender, session);

  g_signal_connect (mprtpsch, "mprtp-subflows-utilization",
      (GCallback) changed_event, NULL);

  g_object_set (rtpSink_1, "port", basePort, "host", "10.0.0.2",
//      NULL);
      "sync",FALSE, "async", FALSE, NULL);

  g_object_set (rtpSink_2, "port", basePort + 1, "host", "10.0.1.2",
//      NULL);
      "sync",FALSE, "async", FALSE, NULL);

  g_object_set (rtcpSink, "port", basePort + 5, "host", "10.0.0.2",
//      NULL);
       "sync",FALSE, "async", FALSE, NULL);

  g_object_set (rtpSrc_1, "port", basePort + 11, NULL);
  g_object_set (rtpSrc_2, "port", basePort + 12, NULL);
  g_object_set (rtcpSrc, "port", basePort + 10, NULL);
  g_object_set (mprtpsch, "auto-flow-controlling", TRUE, NULL);

  padName = g_strdup_printf ("send_rtp_sink_%u", session->sessionNum);
  gst_element_link_pads (session->input, "src", rtpBin, padName);
  g_free (padName);

  //MPRTP Sender
  padName = g_strdup_printf ("send_rtp_src_%u", session->sessionNum);
  gst_element_link_pads (rtpBin, padName, mprtpsch, "rtp_sink");
  gst_element_link_pads (mprtpsch, "mprtp_src", mprtpsnd, "mprtp_sink");
  g_free (padName);
  gst_element_link_pads (mprtpsnd, "src_1", rtpSink_1, "sink");

  gst_element_link_pads (mprtpsnd, "src_2", rtpSink_2, "sink");

  g_object_set (mprtpsch, "join-subflow", 1, NULL);
//  g_object_set (mprtpsch, "join-subflow", 2, NULL);

  padName = g_strdup_printf ("send_rtcp_src_%u", session->sessionNum);
  gst_element_link_pads (rtpBin, padName, rtcpSink, "sink");
  g_free (padName);

  padName = g_strdup_printf ("recv_rtcp_sink_%u", session->sessionNum);
  gst_element_link_pads (rtpSrc_1, "src", mprtprcv, "sink_1");
  gst_element_link_pads (rtpSrc_2, "src", mprtprcv, "sink_2");
  gst_element_link_pads (mprtprcv, "mprtcp_rr_src", mprtpsch, "mprtcp_rr_sink");
  gst_element_link_pads (mprtpsch, "mprtcp_sr_src", mprtpsnd, "mprtcp_sr_sink");
  gst_element_link_pads (rtcpSrc, "src", rtpBin, padName);
  g_free (padName);

  g_print ("New RTP stream on %i/%i/%i\n", basePort, basePort + 1,
      basePort + 5);

  session_unref (session);
}

static void _setup_test_params(guint profile);

int
main (int argc, char **argv)
{
  GstPipeline *pipe;
  GstBus *bus;
  SessionData *videoSession;
  SessionData *audioSession;
  GstElement *rtpBin;
  GMainLoop *loop;
  gchar *testfile = NULL;

  GError *error = NULL;
  GOptionContext *context;

  context = g_option_context_new ("- test tree model performance");
  g_option_context_add_main_entries (context, entries, NULL);
//  g_option_context_add_group (context, gtk_get_option_group (TRUE));
//  gst_init (&argc, &argv);
  g_option_context_parse (context, &argc, &argv, &error);
  _setup_test_params(profile);
  gst_init (NULL, NULL);

  loop = g_main_loop_new (NULL, FALSE);

  pipe = GST_PIPELINE (gst_pipeline_new (NULL));
  bus = gst_element_get_bus (GST_ELEMENT (pipe));
  g_signal_connect (bus, "message::state-changed", G_CALLBACK (cb_state), pipe);
  gst_bus_add_signal_watch (bus);
  gst_object_unref (bus);

  rtpBin = gst_element_factory_make ("rtpbin", NULL);
  g_object_set (rtpBin, "rtp-profile", GST_RTP_PROFILE_AVPF, NULL);

  gst_bin_add (GST_BIN (pipe), rtpBin);

  if(argc > 1) testfile = argv[1];

  videoSession = make_video_session (0);
  add_stream (pipe, rtpBin, videoSession, testfile);

  g_print ("starting server pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_PLAYING);

  g_main_loop_run (loop);

  g_print ("stopping server pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_NULL);

  gst_object_unref (pipe);
  g_main_loop_unref (loop);

  return 0;
}


void _setup_test_params(guint profile)
{
  g_print("Selected test profile: %d\n", profile);
  switch(profile){
    case 1:
    g_print("Congestion control test "
            "with fixed bandwidth, no limitation\n");
    test_parameters_.subflow1_active = TRUE;
    test_parameters_.subflow2_active = FALSE;
    break;
    default:
      g_print("Profile was not recognized for testing.\n");
      break;
  }
}
