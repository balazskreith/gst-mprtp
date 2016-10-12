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
#include "test.h"

/*
 *                                           .------------.
 *                                           | rtpbin     |
 *  .-------.    .---------.    .---------.  |            |       .--------------.    .-------------.
 *  |audiots|    |theoraenc|    |theorapay|  |            |       |   mprtp_sch  |    | mprtp_snd   |      .-------.
 *  |      src->sink      src->sink  src->send_rtp_0 send_rtp_0->rtp_sink mprtp_src->mprtp_src      |      |udpsink|
 *  '-------'    '---------'    '---------'  |            |       |              |    |            src_0->sink     |
 *                                           |            |       |   mprtcp_sr_src->mprtcp_sr_sink |      '-------'
 *                                           |            |       '--------------'    |             |      .-------.
 *                                           |            |                          mprtcp_rr_sink |      |udpsink|
 *                                           |            |                           |           src_1->sink      |
 *                               .------.    |            |                           '-------------'      '-------'
 *                               |udpsrc|    |            |       .-------.
 *                               |     src->recv_rtcp_1   |       |udpsink|
 *                               '------'    |       send_rtcp_1->sink    |
 *                                           '------------'       '-------'
 */


typedef struct _SessionData
{
  int ref;
  guint sessionNum;
  GstElement *input;
} SessionData;


typedef struct _JoinDetachData{
  gboolean active;
  gboolean stop;
  GstElement *mprtpsch;
}JoinDetachData;

static JoinDetachData join_detach_data;

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


static SessionData *
make_video_yuvfile_session (guint sessionNum)
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
      "framerate", framerate, 1,
      "format", 2,
      NULL);
  g_object_set (identity, "sync", TRUE, NULL);

//  gst_element_link (videoSrc, videoParse);
//    gst_element_link (videoParse, identity);
//    gst_element_link (identity, encoder);
//    gst_element_link (videoParse, queue);
//    gst_element_link (queue, encoder);
//  gst_element_link (videoParse, encoder);

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


  gst_bin_add_many (videoBin, videoConv, videoSrc, videoParse, encoder, payloader, NULL);

  g_object_set (videoParse,
      "width", yuvsrc_width,
      "height", yuvsrc_height,
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

//-------------------------- ALTERNATE make_video_yuvfile_session BEGIN -------------------


//This version of make_video_yuvfile_session saves the source yuv either!



static SessionData *
make_video_yuvfile_session_and_save (guint sessionNum)
{
  GstBin *videoBin = GST_BIN (gst_bin_new (NULL));
//  GstElement *videoSrc = gst_element_factory_make ("autovideosrc", NULL);
  GstElement *videoSrc = gst_element_factory_make ("multifilesrc", NULL);
  GstElement *videoParse = gst_element_factory_make ("videoparse", NULL);
  GstElement *videoConv = gst_element_factory_make("autovideoconvert", NULL);
  GstElement *payloader = gst_element_factory_make ("rtpvp8pay", NULL);

  GstElement *plyqueue    = gst_element_factory_make("queue", "plyqueue");
  GstElement *recqueue    = gst_element_factory_make("queue", "recqueue");
  GstElement *recorder    = gst_element_factory_make("filesink", "recorder");
  GstElement *splitter    = gst_element_factory_make("tee", "splitter");


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


  gst_bin_add_many (videoBin, videoConv, videoSrc, videoParse, encoder, payloader, NULL);

  gst_bin_add_many (videoBin, plyqueue, recqueue, recorder, splitter, NULL);
  g_object_set (recorder, "location", "source.yuv", NULL);


  g_object_set (videoParse,
      "width", yuvsrc_width,
      "height", yuvsrc_height,
      "framerate", framerate, 1,
      "format", 2,
      NULL);

//  gst_element_link (videoSrc, videoParse);
  gst_element_link (videoSrc, splitter);
  gst_element_link_pads (splitter, "src_1", plyqueue, "sink");
  gst_element_link_pads (splitter, "src_2", recqueue, "sink");
  gst_element_link (recqueue, recorder);

  gst_element_link (plyqueue, videoParse);
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

  GstElement *plyqueue    = gst_element_factory_make("queue", "plyqueue");
  GstElement *recqueue    = gst_element_factory_make("queue", "recqueue");
  GstElement *recorder    = gst_element_factory_make("filesink", "recorder");
  GstElement *splitter    = gst_element_factory_make("tee", "splitter");

  GstCaps *videoCaps;
  SessionData *session;

  encoder = gst_element_factory_make ("vp8enc", NULL);
  g_object_set (encoder, "target-bitrate", sending_target, NULL);

  g_object_set (videoSrc, "is-live", TRUE, "horizontal-speed", 3, NULL);

  g_object_set (recorder, "location", "source.yuv", NULL);

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

  gst_bin_add_many (videoBin, videoSrc, encoder, plyqueue, recqueue, recorder, splitter, payloader, NULL);

  videoCaps = gst_caps_new_simple ("video/x-raw",
      "width", G_TYPE_INT, yuvsrc_width,
      "height", G_TYPE_INT, yuvsrc_height,
	  "framerate", GST_TYPE_FRACTION, framerate, 1, NULL);

  gst_element_link_filtered (videoSrc, splitter, videoCaps);
  gst_element_link_pads (splitter, "src_1", plyqueue, "sink");
  gst_element_link_pads (splitter, "src_2", recqueue, "sink");
  gst_element_link (recqueue, recorder);

  gst_element_link (plyqueue, encoder);
  gst_element_link (encoder, payloader);

  setup_ghost (payloader, videoBin);

  session = session_new (sessionNum);
  session->input = GST_ELEMENT (videoBin);

  return session;
}

static SessionData *
make_video_session (guint sessionNum)
{
  GstBin *videoBin = GST_BIN (gst_bin_new (NULL));
  GstElement *videoSrc = gst_element_factory_make ("videotestsrc", NULL);
  GstElement *payloader = gst_element_factory_make ("rtpvp8pay", NULL);


  GstCaps *videoCaps;
  SessionData *session;

  encoder = gst_element_factory_make ("vp8enc", NULL);
  g_object_set (encoder, "target-bitrate", sending_target, NULL);

  g_object_set (videoSrc, "is-live", TRUE, "horizontal-speed", 3, NULL);

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
  g_print("framerate: %d", framerate);
  videoCaps = gst_caps_new_simple ("video/x-raw",
      "width", G_TYPE_INT, yuvsrc_width,
      "height", G_TYPE_INT, yuvsrc_height,
	  "framerate", GST_TYPE_FRACTION, framerate, 1, NULL);

  gst_element_link_filtered (videoSrc, encoder, videoCaps);
  gst_element_link (encoder, payloader);

  setup_ghost (payloader, videoBin);

  session = session_new (sessionNum);
  session->input = GST_ELEMENT (videoBin);

  return session;
}
//-------------------------- ALTERNATE make_video_yuvfile_session END -------------------


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
  MPRTPPluginSignalData *signal = ptr;

//  g_print("signal is emitted some data: subflow 1 HSSN: %hu\n", signal->subflow[1].receiver_report.HSSN);
//  gint delta;
//  gint new_bitrate;
//  gint get_bitrate;
//  g_object_get (encoder, "target-bitrate", &get_bitrate, NULL);
//  {
//    gint i;
//    new_bitrate = signal->subflow[0].
//    for(i=0; i<32; ++i){
//
//    }
//  }
//
//  g_print("signal->target_media_rate: %d\n", signal->target_media_rate);
  g_object_set (encoder, "target-bitrate", signal->target_media_rate, NULL);
//done:
  return;
}


static void
add_stream (GstPipeline * pipe, GstElement * rtpBin, SessionData * session,
           gchar* file)
{

  GstElement *rtpSink_1 = gst_element_factory_make ("udpsink", "rtpsink_1");
  GstElement *rtpSink_2 = gst_element_factory_make ("udpsink", "rtpsink_2");
  GstElement *rtpSink_3 = gst_element_factory_make ("udpsink", "rtpsink_3");
  GstElement *async_tx_rtcpSink_1 = gst_element_factory_make ("udpsink", "async_tx_rtpsink_1");
  GstElement *async_tx_rtcpSink_2 = gst_element_factory_make ("udpsink", "async_tx_rtpsink_2");
  GstElement *async_tx_rtcpSink_3 = gst_element_factory_make ("udpsink", "async_tx_rtpsink_3");
  GstElement *rtcpSink = gst_element_factory_make ("udpsink", NULL);
  GstElement *rtcpSrc = gst_element_factory_make ("udpsrc", NULL);
  GstElement *async_rx_rtcpSrc_1 = gst_element_factory_make ("udpsrc", "async_rx_rtpsink_1");
  GstElement *async_rx_rtcpSrc_2 = gst_element_factory_make ("udpsrc", "async_rx_rtpsink_2");
  GstElement *async_rx_rtcpSrc_3 = gst_element_factory_make ("udpsrc", "async_rx_rtpsink_3");
  GstElement *mprtpsnd = gst_element_factory_make ("mprtpsender", NULL);
  GstElement *mprtprcv = gst_element_factory_make ("mprtpreceiver", NULL);
  GstElement *mprtpsch = gst_element_factory_make ("mprtpscheduler", NULL);
  GstElement *mq = gst_element_factory_make ("multiqueue", "rtpq");
  int basePort;
  gchar *padName;

  join_detach_data.stop = FALSE;
  join_detach_data.active = FALSE;
  join_detach_data.mprtpsch = mprtpsch;

  basePort = 5000;

  gst_bin_add_many (GST_BIN (pipe), rtpSink_1, rtpSink_2, rtpSink_3,
      mprtprcv, mprtpsnd, mprtpsch, rtcpSink, rtcpSrc,
      async_tx_rtcpSink_1, async_tx_rtcpSink_2, async_tx_rtcpSink_3,
      async_rx_rtcpSrc_1, async_rx_rtcpSrc_2, async_rx_rtcpSrc_3,
      mq, session->input, NULL);

  /* enable retransmission by setting rtprtxsend as the "aux" element of rtpbin */
  g_signal_connect (rtpBin, "request-aux-sender",
      (GCallback) request_aux_sender, session);

  g_signal_connect (mprtpsch, "mprtp-subflows-utilization",
      (GCallback) changed_event, NULL);

    g_object_set (mq,
                  "max-size-bytes", 0,
                  "max-size-buffers", 0,
                  "max-size-time", 1,
                  NULL);


    g_object_set (rtpSink_1, "port", path1_tx_rtp_port, "host", path_1_tx_ip, NULL);
//    g_object_set (rtpSink_1, "port", path1_tx_rtp_port, "host", path_1_tx_ip, "sync", FALSE, "async", FALSE, NULL);
    g_object_set (rtpSink_2, "port", path2_tx_rtp_port, "host", path_2_tx_ip, NULL);
    g_object_set (rtpSink_3, "port", path3_tx_rtp_port, "host", path_3_tx_ip, NULL);

    g_object_set (async_tx_rtcpSink_1, "port", path1_tx_rtcp_port, "host", path_1_tx_ip, "sync", FALSE, "async", FALSE, NULL);
    g_object_set (async_tx_rtcpSink_2, "port", path2_tx_rtcp_port, "host", path_2_tx_ip, "sync", FALSE, "async", FALSE, NULL);
    g_object_set (async_tx_rtcpSink_3, "port", path3_tx_rtcp_port, "host", path_3_tx_ip, "sync", FALSE, "async", FALSE, NULL);
    g_object_set (async_rx_rtcpSrc_1, "port",  path1_rx_rtcp_port, NULL);
    g_object_set (async_rx_rtcpSrc_2, "port",  path2_rx_rtcp_port, NULL);
    g_object_set (async_rx_rtcpSrc_3, "port",  path3_rx_rtcp_port, NULL);

    g_object_set (rtcpSink, "port", rtpbin_tx_rtcp_port, "host", path_1_tx_ip,
//        NULL);
         "sync",FALSE, "async", FALSE, NULL);

    g_object_set (rtcpSrc, "port", rtpbin_rx_rtcp_port, NULL);

  padName = g_strdup_printf ("send_rtp_sink_%u", session->sessionNum);
  gst_element_link_pads (session->input, "src", rtpBin, padName);
  g_free (padName);

  //MPRTP Sender
  padName = g_strdup_printf ("send_rtp_src_%u", session->sessionNum);
//  gst_element_link_pads (rtpBin, padName, mprtpsnd, "mprtp_sink");
  gst_element_link_pads (rtpBin, padName, mprtpsch, "rtp_sink");
  gst_element_link_pads (mprtpsch, "mprtp_src", mprtpsnd, "mprtp_sink");
  g_free (padName);

  if(extra_delay){
    gst_element_link_pads (mprtpsnd, "src_1", mq, "sink_1");
    gst_element_link_pads (mq, "src_1", rtpSink_1, "sink");
    g_object_set(mq, "min-treshold-time", (GstClockTime) extra_delay * GST_MSECOND, NULL);
  }else{
    gst_element_link_pads (mprtpsnd, "src_1", rtpSink_1, "sink");
  }


  gst_element_link_pads (mprtpsnd, "src_2", mq, "sink_2");
  gst_element_link_pads (mq, "src_2", rtpSink_2, "sink");

  gst_element_link_pads (mprtpsnd, "src_3", mq, "sink_3");
  gst_element_link_pads (mq, "src_3", rtpSink_3, "sink");

//  g_object_set (mprtpsch, "setup-keep-alive-period", (1 << 24) | 100, NULL);

  if(path1_active){
      g_object_set (mprtpsch, "join-subflow", 1, NULL);
    }
  if(path2_active){
      g_object_set (mprtpsch, "join-subflow", 2, NULL);
    }
  if(path3_active){
      g_object_set (mprtpsch, "join-subflow", 3, NULL);
    }

  g_object_set (mprtpsch,
            "logging", logging,
            "logs-path", logsdir,
            "setup-rtcp-interval-type", rtcp_interval_type,
            "fec-interval", fec_interval,
            "obsolation-treshold", obsolation_th,
            "setup-report-timeout", report_timeout,
            "setup-sending-target", sending_target,
            "setup-controlling-mode", controlling_mode,
            "mpath-keyframe-filtering", mpath_keyfiltering,
//            "extra-delay", extra_delay,
            NULL);

  if(testseq){
      g_object_set (mprtpsch, "testseq", testseq, NULL);
  }

  padName = g_strdup_printf ("send_rtcp_src_%u", session->sessionNum);
  gst_element_link_pads (rtpBin, padName, rtcpSink, "sink");
  g_free (padName);

  padName = g_strdup_printf ("recv_rtcp_sink_%u", session->sessionNum);

  gst_element_link_pads (async_rx_rtcpSrc_1, "src", mprtprcv, "async_sink_1");
  gst_element_link_pads (async_rx_rtcpSrc_2, "src", mprtprcv, "async_sink_2");
  gst_element_link_pads (async_rx_rtcpSrc_3, "src", mprtprcv, "async_sink_3");

  gst_element_link_pads (mprtpsnd, "async_src_1", async_tx_rtcpSink_1, "sink");
  gst_element_link_pads (mprtpsnd, "async_src_2", async_tx_rtcpSink_2, "sink");
  gst_element_link_pads (mprtpsnd, "async_src_3", async_tx_rtcpSink_3, "sink");

  gst_element_link_pads (mprtprcv, "mprtcp_rr_src", mprtpsch, "mprtcp_rr_sink");
  gst_element_link_pads (mprtpsch, "mprtcp_sr_src", mprtpsnd, "mprtcp_sr_sink");
  gst_element_link_pads (rtcpSrc, "src", rtpBin, padName);
  g_free (padName);

  session_unref (session);
}


int
main (int argc, char **argv)
{
  GstPipeline *pipe;
  GstBus *bus;
  SessionData *videoSession;
//  SessionData *audioSession;
  GstElement *rtpBin;
  GMainLoop *loop;
  gchar *testfile = NULL;
  GObject *rtp_session;

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
  gst_init (NULL, NULL);

  loop = g_main_loop_new (NULL, FALSE);

  pipe = GST_PIPELINE (gst_pipeline_new (NULL));
  bus = gst_element_get_bus (GST_ELEMENT (pipe));
  g_signal_connect (bus, "message::state-changed", G_CALLBACK (cb_state), pipe);
  gst_bus_add_signal_watch (bus);
  gst_object_unref (bus);

  rtpBin = gst_element_factory_make ("rtpbin", NULL);
  g_object_set (rtpBin, "rtp-profile", GST_RTP_PROFILE_SAVPF, NULL);

  gst_bin_add (GST_BIN (pipe), rtpBin);

  if(argc > 1) testfile = argv[1];

//  framerate = use_testsourcevideo ? 100 : 25;
  if(0 && use_testsourcevideo){
    videoSession = save_received_yuvfile ? make_video_session2(0) : make_video_session(0);
  }else if(0){
	  videoSession = save_received_yuvfile ? make_video_yuvfile_session_and_save (0) : make_video_yuvfile_session(0);
  }
  videoSession = make_video_v4l2_session (0);

  //videoSession = make_video_session2 (0);
  add_stream (pipe, rtpBin, videoSession, testfile);

  g_signal_emit_by_name(rtpBin, "get-internal-session", videoSession->sessionNum, &rtp_session);
  g_object_set(rtp_session,
               "rtcp-min-interval", GST_SECOND,
               "bandwidth", 0.0,
               "rtp-profile", GST_RTP_PROFILE_SAVPF,
               NULL);

  g_print ("starting server pipeline\n");
  {
    GstStateChangeReturn changeresult;
    GstState actual, pending;
    changeresult = gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_PLAYING);
//    g_print("changeresult: %d\n", changeresult);
    changeresult = gst_element_get_state(GST_ELEMENT (pipe), &actual, &pending, 0);
    g_print("actual state: %d, pending: %d, changeresult: %d\n", actual, pending, changeresult);
  }
  g_main_loop_run (loop);

  g_print ("stopping server pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_NULL);

  gst_object_unref (pipe);
  g_main_loop_unref (loop);

  return 0;
}


