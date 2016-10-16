#include <gst/gst.h>
#include <gst/rtp/rtp.h>
#include <stdlib.h>
#include "fbraptest.h"

GMainLoop *loop = NULL;

typedef struct _SessionData
{
  int ref;
  GstElement *rtpbin;
  guint sessionNum;
  GstCaps *caps;
  GstElement *output;
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
    gst_caps_unref (session->caps);
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

static void
setup_ghost_sink (GstElement * sink, GstBin * bin)
{
  GstPad *sinkPad = gst_element_get_static_pad (sink, "sink");
  GstPad *binPad = gst_ghost_pad_new ("sink", sinkPad);
  gst_element_add_pad (GST_ELEMENT (bin), binPad);
}

static SessionData *
make_video_session (guint sessionNum)
{
  SessionData *ret = session_new (sessionNum);

  GstBin     *bin         = GST_BIN (gst_bin_new ("video"));

  GstElement *queue       = gst_element_factory_make ("queue", NULL);
  GstElement *depayloader = gst_element_factory_make ("rtpvp8depay", NULL);
  GstElement *decoder     = gst_element_factory_make ("vp8dec", NULL);
  GstElement *converter   = gst_element_factory_make ("videoconvert", NULL);
  GstElement *sink        = gst_element_factory_make ("autovideosink", NULL);

  gst_bin_add_many (bin, depayloader, decoder, converter, queue, sink, NULL);
  gst_element_link_many (queue, depayloader, decoder, converter, sink, NULL);

  setup_ghost_sink (queue, bin);

  ret->output = GST_ELEMENT (bin);

  ret->caps = gst_caps_new_simple ("application/x-rtp",
      "media", G_TYPE_STRING, "video",
      "clock-rate", G_TYPE_INT, 90000,
      "width", G_TYPE_INT, video_width,
      "height", G_TYPE_INT, video_height,
      "framerate", GST_TYPE_FRACTION, framerate, 1,
      "encoding-name", G_TYPE_STRING, "VP8", NULL
      );

  g_object_set (sink, "sync", FALSE, NULL);
  return ret;
}
static GstCaps *
request_pt_map (GstElement * rtpbin, guint session, guint pt,
    gpointer user_data)
{
  SessionData *data = (SessionData *) user_data;
  g_print ("Looking for caps for pt %u in session %u, have %u\n", pt, session,
      data->sessionNum);
  if (session == data->sessionNum) {
    g_print ("Returning %s\n", gst_caps_to_string (data->caps));
    return gst_caps_ref (data->caps);
  }
  return NULL;
}

static void
cb_eos (GstBus * bus, GstMessage * message, gpointer data)
{
  g_print ("Got EOS\n");
  g_main_loop_quit (loop);
}

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

static void
cb_warning (GstBus * bus, GstMessage * message, gpointer data)
{
  GError *error = NULL;
  gst_message_parse_warning (message, &error, NULL);
  g_printerr ("Got warning from %s: %s\n", GST_OBJECT_NAME (message->src),
      error->message);
  g_error_free (error);
}

static void
cb_error (GstBus * bus, GstMessage * message, gpointer data)
{
  GError *error = NULL;
  gst_message_parse_error (message, &error, NULL);
  g_printerr ("Got error from %s: %s\n", GST_OBJECT_NAME (message->src),
      error->message);
  g_error_free (error);
  g_main_loop_quit (loop);
}

static void
handle_new_stream (GstElement * element, GstPad * newPad, gpointer data)
{
  SessionData *session = (SessionData *) data;
  gchar *padName;
  gchar *myPrefix;

  padName = gst_pad_get_name (newPad);
  myPrefix = g_strdup_printf ("recv_rtp_src_%u", session->sessionNum);

  g_print ("New pad: %s, looking for %s_*\n", padName, myPrefix);

  if (g_str_has_prefix (padName, myPrefix)) {
    GstPad *outputSinkPad;
    GstElement *parent;

    parent = GST_ELEMENT (gst_element_get_parent (session->rtpbin));
    gst_bin_add (GST_BIN (parent), session->output);
    gst_element_sync_state_with_parent (session->output);
    gst_object_unref (parent);

    outputSinkPad = gst_element_get_static_pad (session->output, "sink");
    g_print("%p-%p\n", newPad, outputSinkPad);
    g_assert_cmpint (gst_pad_link (newPad, outputSinkPad), ==, GST_PAD_LINK_OK);
    gst_object_unref (outputSinkPad);

    g_print ("Linked!\n");
  }
  g_free (myPrefix);
  g_free (padName);
}

static GstElement *
request_aux_receiver (GstElement * rtpbin, guint sessid, SessionData * session)
{
  GstElement *rtx, *bin;
  GstPad *pad;
  gchar *name;
  GstStructure *pt_map;

  GST_INFO ("creating AUX receiver");
  bin = gst_bin_new (NULL);
  rtx = gst_element_factory_make ("rtprtxreceive", NULL);
  pt_map = gst_structure_new ("application/x-rtp-pt-map",
      "8", G_TYPE_UINT, 98, "96", G_TYPE_UINT, 99, NULL);
  g_object_set (rtx, "payload-type-map", pt_map, NULL);
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

static void
join_session (GstElement * pipeline, GstElement * rtpBin, SessionData * session)
{
  GstElement *rtpSrc;
  GstElement *rtcpSrc;
  GstElement *rtcpSink;
  GstElement *mprtcpSink;
  GstElement *mprtpPly;
  GstElement *mprtpRcv;
  GstElement *mprtpSnd;
  gchar *padName;

  g_print ("Joining session %p\n", session);

  session->rtpbin = g_object_ref (rtpBin);

  rtpSrc     = gst_element_factory_make ("udpsrc", NULL);
  rtcpSrc    = gst_element_factory_make ("udpsrc", NULL);
  rtcpSink   = gst_element_factory_make ("udpsink", NULL);
  mprtcpSink = gst_element_factory_make ("udpsink", NULL);
  mprtpPly   = gst_element_factory_make ("mprtpplayouter", NULL);
  mprtpRcv   = gst_element_factory_make ("mprtpreceiver", NULL);
  mprtpSnd   = gst_element_factory_make ("mprtpsender", NULL);

  g_object_set (rtpSrc, "port", rcv_rtp_port, "caps", session->caps, NULL);
  g_object_set (rtcpSink, "port", snd_rtcp_port, "host", snd_ip,
      "sync", FALSE, "async", FALSE, NULL);
  g_object_set (rtcpSrc, "port", rcv_rtcp_port, NULL);
  g_object_set (mprtcpSink, "port", snd_mprtcp_port, "host", snd_ip,
        "sync", FALSE, "async", FALSE, NULL);

  g_print ("Connected: Host - %s:%d, RTCP: %d\n", rcv_ip, rcv_rtp_port, rcv_rtcp_port);
  g_print("Peer: %s:%d MPRTCP: %d\n", snd_ip, snd_rtcp_port, snd_mprtcp_port);

  /* enable RFC4588 retransmission handling by setting rtprtxreceive
   * as the "aux" element of rtpbin */
  g_signal_connect (rtpBin, "request-aux-receiver",
      (GCallback) request_aux_receiver, session);

  gst_bin_add_many (GST_BIN (pipeline),
      rtpSrc,
      rtcpSrc,
      rtcpSink,
      mprtcpSink,

      mprtpPly,
      mprtpRcv,
      mprtpSnd,
      NULL);

  g_signal_connect_data (rtpBin, "pad-added", G_CALLBACK (handle_new_stream),
      session_ref (session), (GClosureNotify) session_unref, 0);

  g_signal_connect_data (rtpBin, "request-pt-map", G_CALLBACK (request_pt_map),
      session_ref (session), (GClosureNotify) session_unref, 0);

  padName = g_strdup_printf ("recv_rtp_sink_%u", session->sessionNum);
  gst_element_link_pads(rtpSrc, "src", mprtpRcv, "sink_1");
  gst_element_link_pads(mprtpRcv, "mprtp_src", mprtpPly, "mprtp_sink");
  gst_element_link_pads(mprtpPly, "mprtp_src", rtpBin, padName);
  g_free (padName);

  padName = g_strdup_printf ("recv_rtcp_sink_%u", session->sessionNum);
  gst_element_link_pads (rtcpSrc, "src", rtpBin, padName);
  g_free (padName);

  gst_element_link_pads (mprtpRcv, "mprtcp_sr_src", mprtpPly, "mprtcp_sr_sink");
  gst_element_link_pads (mprtpPly, "mprtcp_rr_src", mprtpSnd, "mprtcp_rr_sink");
  gst_element_link_pads (mprtpSnd, "src_1", mprtcpSink, "sink");

  padName = g_strdup_printf ("send_rtcp_src_%u", session->sessionNum);
  gst_element_link_pads (rtpBin, padName, rtcpSink, "sink");
  g_free (padName);

  g_object_set(mprtpPly,
      "join-subflow", 1,
      "setup-controlling-mode", controlling_mode,
      "setup-rtcp-interval-type", rtcp_interval_type,
      "logging", logging,
      "logs-path", logs_path,
      NULL);

  session_unref (session);
}

int
main (int argc, char **argv)
{
  GstPipeline *pipe;
  SessionData *videoSession;
  GstElement  *rtpBin;
  GstBus *bus;

  GError *error = NULL;
  GOptionContext *context;

  context = g_option_context_new ("FBRAPlus receiver");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_parse (context, &argc, &argv, &error);
  if(info){
    _print_info();
    return 0;
  }
  _setup_test_params();

  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);
  pipe = GST_PIPELINE (gst_pipeline_new (NULL));

  bus = gst_element_get_bus (GST_ELEMENT (pipe));
  g_signal_connect (bus, "message::error", G_CALLBACK (cb_error), pipe);
  g_signal_connect (bus, "message::warning", G_CALLBACK (cb_warning), pipe);
  g_signal_connect (bus, "message::state-changed", G_CALLBACK (cb_state), pipe);
  g_signal_connect (bus, "message::eos", G_CALLBACK (cb_eos), NULL);
  gst_bus_add_signal_watch (bus);
  gst_object_unref (bus);

  rtpBin = gst_element_factory_make ("rtpbin", NULL);
  gst_bin_add (GST_BIN (pipe), rtpBin);
  g_object_set (rtpBin,
      "latency", 200,
      //"do-retransmission", TRUE,
      "rtp-profile", GST_RTP_PROFILE_AVPF, NULL);

  videoSession = make_video_session (0);

  join_session (GST_ELEMENT (pipe), rtpBin, videoSession);

  g_print ("starting client pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_PLAYING);

  g_main_loop_run (loop);

  g_print ("stoping client pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_NULL);

  gst_object_unref (pipe);
  g_main_loop_unref (loop);

  return 0;
}
