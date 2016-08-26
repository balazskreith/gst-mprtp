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
#include "test.h"
#include "owr_arrival_time_meta.h"

/*
 * An RTP server
 *  creates two sessions and streams audio on one, video on the other, with RTCP
 *  on both sessions. The destination is 127.0.0.1.
 *
 *  In both sessions, we set "rtprtxsend" as the session's "aux" element
 *  in rtpbin, which enables RFC4588 retransmission for that session.
 *
 *  .-------.    .-------.    .-------.      .------------.       .-------.
 *  |audiots|    |alawenc|    |pcmapay|      | rtpbin     |       |udpsink|
 *  |      src->sink    src->sink    src->send_rtp_0 send_rtp_0->sink     |
 *  '-------'    '-------'    '-------'      |            |       '-------'
 *                                           |            |
 *  .-------.    .---------.    .---------.  |            |       .-------.
 *  |audiots|    |theoraenc|    |theorapay|  |            |       |udpsink|
 *  |      src->sink      src->sink  src->send_rtp_1 send_rtp_1->sink     |
 *  '-------'    '---------'    '---------'  |            |       '-------'
 *                                           |            |
 *                               .------.    |            |
 *                               |udpsrc|    |            |       .-------.
 *                               |     src->recv_rtcp_0   |       |udpsink|
 *                               '------'    |       send_rtcp_0->sink    |
 *                                           |            |       '-------'
 *                               .------.    |            |
 *                               |udpsrc|    |            |       .-------.
 *                               |     src->recv_rtcp_1   |       |udpsink|
 *                               '------'    |       send_rtcp_1->sink    |
 *                                           '------------'       '-------'
 *
 * To keep the set of ports consistent across both this server and the
 * corresponding client, a SessionData struct maps a rtpbin session number to
 * a GstBin and is used to create the corresponding udp sinks with correct
 * ports.
 */

static GstElement *encoder;

typedef struct _SessionData
{
  int ref;
  guint sessionNum;
  GstElement *input;
  GstElement *screamqueue;
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
make_video_session (guint sessionNum)
{
  GstBin *videoBin = GST_BIN (gst_bin_new (NULL));
//  GstElement *videoSrc = gst_element_factory_make ("autovideosrc", NULL);
  GstElement *videoSrc = gst_element_factory_make ("multifilesrc", NULL);
  GstElement *queue    = gst_element_factory_make("queue", NULL);
  GstElement *identity = gst_element_factory_make ("identity", NULL);
  GstElement *videoParse = gst_element_factory_make ("videoparse", NULL);
  GstElement *videoConv = gst_element_factory_make("autovideoconvert", NULL);
  GstElement *payloader = gst_element_factory_make ("rtpvp8pay", NULL);
  GstCaps *videoCaps;
  SessionData *session;
  g_object_set (videoSrc,
                "location", yuvsrc_file,
                "loop", TRUE,
                NULL);

  encoder = gst_element_factory_make ("vp8enc", NULL);
  //g_object_set (payloader, "config-interval", 2, NULL);
  g_object_set (encoder, "target-bitrate", sending_target, NULL);
//  g_object_set (encoder, "end-usage", 1, NULL);
//  g_object_set (encoder, "deadline", 20000, NULL);
//  g_object_set (encoder, "undershoot", 100, NULL);
//  g_object_set (encoder, "cpu-used", 0, NULL);
//  g_object_set (encoder, "keyframe-mode", 0, NULL);
/* values are inspired by webrtc.org values in vp8_impl.cc */
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


  gst_bin_add_many (videoBin, videoConv, videoSrc, identity, videoParse, encoder, payloader, queue, NULL);
  g_object_set (videoParse,
      "width", yuvsrc_width,
      "height", yuvsrc_height,
      "framerate", 25, 1,
      "format", 2,
      NULL);
  g_object_set (identity, "sync", TRUE, NULL);

  gst_element_link (videoSrc, videoParse);
//    gst_element_link (videoParse, identity);
//    gst_element_link (identity, encoder);
//    gst_element_link (videoParse, queue);
//    gst_element_link (queue, encoder);
  gst_element_link (videoParse, encoder);
  gst_element_link (encoder, payloader);

//  g_object_set(videoSrc, "filter-caps", videoCaps, NULL);

  setup_ghost (payloader, videoBin);

  session = session_new (sessionNum);
  session->input = GST_ELEMENT (videoBin);

  return session;
}


static SessionData *
make_video_session2 (guint sessionNum)
{
  GstBin *videoBin = GST_BIN (gst_bin_new (NULL));
  GstElement *videoSrc = gst_element_factory_make ("videotestsrc", NULL);
  GstElement *payloader = gst_element_factory_make ("rtpvp8pay", NULL);
  GstCaps *videoCaps;
  SessionData *session;

  encoder = gst_element_factory_make ("vp8enc", NULL);
  g_object_set (encoder, "target-bitrate", sending_target, NULL);

  g_object_set (videoSrc, "is-live", TRUE, "horizontal-speed", 10, NULL);

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

  gst_bin_add_many (videoBin, videoSrc, encoder, payloader, NULL);
  videoCaps = gst_caps_new_simple ("video/x-raw",
      "width", G_TYPE_INT, yuvsrc_width,
      "height", G_TYPE_INT, yuvsrc_height,
	  "framerate", GST_TYPE_FRACTION, 25, 1, NULL);
  gst_element_link_filtered (videoSrc, encoder, videoCaps);
  gst_element_link (encoder, payloader);

  setup_ghost (payloader, videoBin);

  session = session_new (sessionNum);
  session->input = GST_ELEMENT (videoBin);

  return session;
}


static SessionData *
make_video_session3 (guint sessionNum)
{
  GstBin *videoBin = GST_BIN (gst_bin_new (NULL));
  GstElement *v4l2src = gst_element_factory_make ("v4l2src", NULL);
  GstElement *queue = gst_element_factory_make ("queue", NULL);
  GstElement *videoconvert = gst_element_factory_make ("videoconvert", NULL);
  GstElement *capsfilter = gst_element_factory_make ("capsfilter", NULL);
  GstElement *payloader = gst_element_factory_make ("rtpvp8pay", NULL);
  GstCaps *videoCaps;
  SessionData *session;

  encoder = gst_element_factory_make ("vp8enc", NULL);
  g_object_set (encoder, "target-bitrate", sending_target, NULL);

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



  gst_bin_add_many (videoBin, v4l2src, videoconvert, queue, encoder, payloader, NULL);
  videoCaps = gst_caps_new_simple (
      "video/x-raw",
      "width", G_TYPE_INT, 352,
      "height", G_TYPE_INT, 288,
      "framerate", GST_TYPE_FRACTION, 25, 1, NULL);

  gst_element_link_many (v4l2src, videoconvert, queue, encoder, payloader, NULL);

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

static void on_feedback_rtcp(GObject *session, guint type, guint fbtype, guint sender_ssrc,
    guint media_ssrc, GstBuffer *fci, SessionData *session_data)
{
    g_return_if_fail(session);
    g_return_if_fail(session_data);

    if (type == GST_RTCP_TYPE_RTPFB && fbtype == GST_RTCP_RTPFB_TYPE_SCREAM) {
        GstElement *scream_queue = NULL;
        GstMapInfo info = {NULL, 0, NULL, 0, 0, {0}, {0}}; /*GST_MAP_INFO_INIT;*/
        guint session_id = GPOINTER_TO_UINT(g_object_get_data(session, "session_id"));

        scream_queue = session_data->screamqueue;
        g_object_set(scream_queue, "scream-controller-id", 2, NULL);
        //scream_queue = gst_bin_get_by_name(GST_BIN(send_output_bin), "screamqueue");

        /* Read feedback from FCI */
        if (gst_buffer_map(fci, &info, GST_MAP_READ)) {
            guint32 timestamp;
            guint16 highest_seq;
            guint8 *fci_buf, n_loss, n_ecn;
            gboolean qbit = FALSE;

            fci_buf = info.data;
            highest_seq = GST_READ_UINT16_BE(fci_buf);
            n_loss = GST_READ_UINT8(fci_buf + 2);
            n_ecn = GST_READ_UINT8(fci_buf + 3);
            timestamp = GST_READ_UINT32_BE(fci_buf + 4);
            /* TODO: Fix qbit */

            gst_buffer_unmap(fci, &info);
//            g_print("m_ssrc: %u | ts: %u | HSSN: %hu | loss: %d | n_ecn: %d | qbit: %d\n", media_ssrc, timestamp, highest_seq, n_loss, n_ecn, qbit);
            g_signal_emit_by_name(scream_queue, "incoming-feedback", media_ssrc, timestamp, highest_seq, n_loss, n_ecn, qbit);

//            {
//            	gboolean pass_through;
//            	guint controller_id;
//              g_object_get(scream_queue, "pass-through", &pass_through,
//            		  "scream-controller-id", &controller_id, NULL);
//              g_print("Pass through: %d controller_id: %d\n", pass_through, controller_id);
//            }
        }
    }
}

static gboolean on_payload_adaptation_request(GstElement *screamqueue, guint pt,
    SessionData *session)
{
    OWR_UNUSED(screamqueue);
    return TRUE;
}

static void on_bitrate_change(GstElement *scream_queue, guint bitrate, guint ssrc, guint pt,
    SessionData *session)
{
	g_print("new target: %u\n", bitrate) ;
    g_object_set (encoder, "target-bitrate", bitrate, NULL);
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
  GstElement *scream_queue = gst_element_factory_make("screamqueue", "screamqueue");
  GstElement *queue = gst_element_factory_make("queue", "queue");
  int basePort;
  gchar *padName;

  basePort = 5000 + (session->sessionNum * 6);

  session->screamqueue = scream_queue;

  gst_bin_add_many (GST_BIN (pipe), rtpSink, rtcpSink, rtcpSrc, scream_queue,
		  queue,
      session->input, NULL);

  /* enable retransmission by setting rtprtxsend as the "aux" element of rtpbin */
  g_signal_connect (rtpBin, "request-aux-sender",
      (GCallback) request_aux_sender, session);

  g_object_set (rtpSink, "port", path1_tx_rtp_port, "host", path_1_tx_ip,
		  "enable-last-sample", FALSE,
		  "async", FALSE,
		  "sync", FALSE,
		  NULL);

  g_object_set (rtcpSink, "port", rtpbin_tx_rtcp_port, "host", path_1_tx_ip,
       "sync",FALSE, "async", FALSE, NULL);

  g_object_set (rtcpSrc, "port", rtpbin_rx_rtcp_port, NULL);

//
//  g_object_set (rtpSink, "port", basePort, "host", "10.0.0.6", NULL);
//  g_object_set (rtcpSink, "port", basePort + 1, "host", "127.0.0.1", "sync",
//      FALSE, "async", FALSE, NULL);
//  g_object_set (rtcpSrc, "port", basePort + 5, NULL);

  /* this is just to drop some rtp packets at random, to demonstrate
   * that rtprtxsend actually works */

  padName = g_strdup_printf ("send_rtp_sink_%u", session->sessionNum);
  gst_element_link_pads (session->input, "src", rtpBin, padName);
  g_free (padName);

  /* link rtpbin to udpsink directly here if you don't want
   * artificial packet loss */
  padName = g_strdup_printf ("send_rtp_src_%u", session->sessionNum);
  gst_element_link_pads (rtpBin, padName, scream_queue, "sink");
  gst_element_link (scream_queue, rtpSink);
  //gst_element_link (scream_queue, queue);
  //gst_element_link (queue, rtpSink);

  padName = g_strdup_printf ("send_rtcp_src_%u", session->sessionNum);
  gst_element_link_pads (rtpBin, padName, rtcpSink, "sink");
  g_free (padName);

  padName = g_strdup_printf ("recv_rtcp_sink_%u", session->sessionNum);
  gst_element_link_pads (rtcpSrc, "src", rtpBin, padName);
  g_free (padName);

  g_print ("New RTP stream on %i/%i/%i\n", basePort, basePort + 1,
      basePort + 5);

  {
	GObject *rtp_session = NULL;
    g_signal_emit_by_name(rtpBin, "get-internal-session", 0, &rtp_session);
	g_signal_connect(rtp_session, "on-feedback-rtcp", G_CALLBACK(on_feedback_rtcp), session);
	g_signal_connect(scream_queue, "on-bitrate-change", G_CALLBACK(on_bitrate_change), session);
    g_signal_connect(scream_queue, "on-payload-adaptation-request", (GCallback)on_payload_adaptation_request, session);
    g_object_unref(rtp_session);
  }

}

int
main (int argc, char **argv)
{
  GstPipeline *pipe;
  GstBus *bus;
  SessionData *videoSession;
  GstElement *rtpBin;
  GMainLoop *loop;
  GError *error = NULL;

	GOptionContext *context;

	context = g_option_context_new ("- test tree model performance");
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

//  videoSession = make_video_session (0);
  videoSession = make_video_session2 (0);
  add_stream (pipe, rtpBin, videoSession);

  g_print ("starting server pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_PLAYING);

  g_main_loop_run (loop);

  g_print ("stopping server pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_NULL);

  gst_object_unref (pipe);
  g_main_loop_unref (loop);

  session_unref (videoSession);

  return 0;
}
