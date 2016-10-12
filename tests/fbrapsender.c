#include <gst/gst.h>
#include <gst/rtp/rtp.h>
#include "fbraptest.h"

static GstElement* encoder;

typedef struct _SessionData
{
  int ref;
  guint sessionNum;
  GstElement *input;
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



static SessionData *
make_video_file_session (guint sessionNum)
{
  GstBin *videoBin = GST_BIN (gst_bin_new (NULL));
  GstElement *videoSrc = gst_element_factory_make ("multifilesrc", NULL);
  GstElement *videoParse = gst_element_factory_make ("videoparse", NULL);
  GstElement *videoConv = gst_element_factory_make("autovideoconvert", NULL);
  GstElement *payloader = gst_element_factory_make ("rtpvp8pay", NULL);
  GstCaps *videoCaps;
  SessionData *session;

  g_object_set (videoSrc,
                "location", "foreman_cif.yuv",
            "loop", TRUE,
            NULL);

  encoder = gst_element_factory_make ("vp8enc", NULL);
  g_object_set (encoder, "target-bitrate", target_bitrate, NULL);
  g_object_set(encoder,
      "end-usage", 1, /* VPX_CBR */
      "deadline", G_GINT64_CONSTANT(1), /* VPX_DL_REALTIME */
      "cpu-used", -6,
      "min-quantizer", 2,
      "buffer-initial-size", 300,
      "buffer-optimal-size", 300,
      "buffer-size", 400,
      "dropframe-threshold", 30,
      "lag-in-frames", 0,
      "timebase", 1, 90000,
      "error-resilient", 1,
//      "keyframe-mode", 1, /* VPX_KF_DISABLED */
//      "keyframe-max-dist", 128,
      NULL);


  gst_bin_add_many (videoBin, videoConv, videoSrc, videoParse, encoder, payloader, NULL);

  g_object_set (videoParse,
      "width", video_width,
      "height", video_height,
      "framerate", framerate, 1,
      "format", 2,
      NULL);

  gst_element_link (videoSrc, videoParse);
  gst_element_link (videoParse, encoder);
  gst_element_link (encoder, payloader);

  setup_ghost (payloader, videoBin);

  session = session_new (sessionNum);
  session->input = GST_ELEMENT (videoBin);

  return session;
}



static SessionData *
make_video_v4l2_session (guint sessionNum)
{
  GstBin *videoBin = GST_BIN (gst_bin_new (NULL));
  GstElement *videoSrc = gst_element_factory_make ("v4l2src", NULL);
  GstElement *videoParse = gst_element_factory_make ("videoparse", NULL);
  GstElement *videoConv = gst_element_factory_make("autovideoconvert", NULL);
  GstElement *payloader = gst_element_factory_make ("rtpvp8pay", NULL);
  GstCaps *videoCaps;
  SessionData *session;

  g_object_set (videoSrc,
                "device", "/dev/video1",
                NULL);

  encoder = gst_element_factory_make ("vp8enc", NULL);
  g_object_set (encoder, "target-bitrate", target_bitrate, NULL);
  g_object_set(encoder,
      "end-usage", 1, /* VPX_CBR */
      "deadline", G_GINT64_CONSTANT(1), /* VPX_DL_REALTIME */
      "cpu-used", -6,
      "min-quantizer", 2,
      "buffer-initial-size", 300,
      "buffer-optimal-size", 300,
      "buffer-size", 400,
      "dropframe-threshold", 30,
      "lag-in-frames", 0,
      "timebase", 1, 90000,
      "error-resilient", 1,
//      "keyframe-mode", 1, /* VPX_KF_DISABLED */
//      "keyframe-max-dist", 128,
      NULL);


  gst_bin_add_many (videoBin, videoConv, videoSrc, videoParse, encoder, payloader, NULL);

  g_object_set (videoParse,
      "width", video_width,
      "height", video_height,
      "framerate", framerate, 1,
      "format", 2,
      NULL);

  gst_element_link (videoSrc, videoParse);
  gst_element_link (videoParse, encoder);
  gst_element_link (encoder, payloader);

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

/*
 * This function sets up the UDP sinks and sources for RTP/RTCP, adds the
 * given session's bin into the pipeline, and links it to the properly numbered
 * pads on the rtpbin
 */
static void
add_stream (GstPipeline * pipe, GstElement * rtpBin, SessionData * session)
{
  GstElement *rtpSink = gst_element_factory_make ("udpsink", NULL);
  GstElement *rtcpSink = gst_element_factory_make ("udpsink", NULL);
  GstElement *rtcpSrc = gst_element_factory_make ("udpsrc", NULL);
  int basePort;
  gchar *padName;

  basePort = 5000 + (session->sessionNum * 6);

  gst_bin_add_many (GST_BIN (pipe), rtpSink, rtcpSink, rtcpSrc,
      session->input, NULL);

  /* enable retransmission by setting rtprtxsend as the "aux" element of rtpbin */
  g_signal_connect (rtpBin, "request-aux-sender",
      (GCallback) request_aux_sender, session);

  g_object_set (rtpSink, "port", path_tx_rtp_port, "host", path_tx_ip, NULL);
  g_object_set (rtcpSink, "port", rtpbin_tx_rtcp_port, "host", path_tx_ip,
       "sync",FALSE, "async", FALSE, NULL);

  g_object_set (rtcpSrc, "port", rtpbin_rx_rtcp_port, NULL);

  /* this is just to drop some rtp packets at random, to demonstrate
   * that rtprtxsend actually works */

  padName = g_strdup_printf ("send_rtp_sink_%u", session->sessionNum);
  gst_element_link_pads (session->input, "src", rtpBin, padName);
  g_free (padName);

  /* link rtpbin to udpsink directly here if you don't want
   * artificial packet loss */
  padName = g_strdup_printf ("send_rtp_src_%u", session->sessionNum);
  gst_element_link_pads (rtpBin, padName, rtpSink, "sink");
  g_free (padName);

  padName = g_strdup_printf ("send_rtcp_src_%u", session->sessionNum);
  gst_element_link_pads (rtpBin, padName, rtcpSink, "sink");
  g_free (padName);

  padName = g_strdup_printf ("recv_rtcp_sink_%u", session->sessionNum);
  gst_element_link_pads (rtcpSrc, "src", rtpBin, padName);
  g_free (padName);

  g_print ("New RTP stream on %i/%i/%i\n", basePort, basePort + 1,
      basePort + 5);

  session_unref (session);
}

int
main (int argc, char **argv)
{
  GstPipeline *pipe;
  GstBus *bus;
  SessionData *videoSession;
  SessionData *audioSession;
  GstElement *rtpBin;
  GMainLoop *loop;

  GError *error = NULL;
  GOptionContext *context;

  context = g_option_context_new ("FBRAPlus sender");
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
  g_signal_connect (bus, "message::state-changed", G_CALLBACK (cb_state), pipe);
  gst_bus_add_signal_watch (bus);
  gst_object_unref (bus);

  rtpBin = gst_element_factory_make ("rtpbin", NULL);
  g_object_set (rtpBin, "rtp-profile", GST_RTP_PROFILE_AVPF, NULL);

  gst_bin_add (GST_BIN (pipe), rtpBin);

//  videoSession = make_video_live_session (0);
//  videoSession = make_video_file_session (0);
  videoSession = make_video_v4l2_session (0);
  add_stream (pipe, rtpBin, videoSession);

  g_print ("starting server pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_PLAYING);

  g_main_loop_run (loop);

  g_print ("stopping server pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_NULL);

  gst_object_unref (pipe);
  g_main_loop_unref (loop);

  return 0;
}
