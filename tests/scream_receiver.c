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
#include <gst/rtp/gstrtcpbuffer.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtpdefs.h>
#include <stdlib.h>
#include "test.h"
#include "owr_arrival_time_meta.h"
/*
 * RTP receiver with RFC4588 retransmission handling enabled
 *
 *  In this example we have two RTP sessions, one for video and one for audio.
 *  Video is received on port 5000, with its RTCP stream received on port 5001
 *  and sent on port 5005. Audio is received on port 5005, with its RTCP stream
 *  received on port 5006 and sent on port 5011.
 *
 *  In both sessions, we set "rtprtxreceive" as the session's "aux" element
 *  in rtpbin, which enables RFC4588 retransmission handling for that session.
 *
 *             .-------.      .----------.        .-----------.   .---------.   .-------------.
 *  RTP        |udpsrc |      | rtpbin   |        |theoradepay|   |theoradec|   |autovideosink|
 *  port=5000  |      src->recv_rtp_0 recv_rtp_0->sink       src->sink     src->sink          |
 *             '-------'      |          |        '-----------'   '---------'   '-------------'
 *                            |          |
 *                            |          |     .-------.
 *                            |          |     |udpsink|  RTCP
 *                            |  send_rtcp_0->sink     | port=5005
 *             .-------.      |          |     '-------' sync=false
 *  RTCP       |udpsrc |      |          |               async=false
 *  port=5001  |     src->recv_rtcp_0    |
 *             '-------'      |          |
 *                            |          |
 *             .-------.      |          |        .---------.   .-------.   .-------------.
 *  RTP        |udpsrc |      |          |        |pcmadepay|   |alawdec|   |autoaudiosink|
 *  port=5006  |      src->recv_rtp_1 recv_rtp_1->sink     src->sink   src->sink          |
 *             '-------'      |          |        '---------'   '-------'   '-------------'
 *                            |          |
 *                            |          |     .-------.
 *                            |          |     |udpsink|  RTCP
 *                            |  send_rtcp_1->sink     | port=5011
 *             .-------.      |          |     '-------' sync=false
 *  RTCP       |udpsrc |      |          |               async=false
 *  port=5007  |     src->recv_rtcp_1    |
 *             '-------'      '----------'
 *
 */

GMainLoop *loop = NULL;

typedef struct {
    guint session_id;

    gushort highest_seq;
    guint16 ack_vec;
    guint8 n_loss;
    guint8 n_ecn;
    guint receive_wallclock;

    guint32 ssrc;
    guint rtcp_session_id;
    guint32 fmt;
    guint   pt;
    guint32 last_feedback_wallclock;

    gboolean has_data;
    gboolean initialized;
} ScreamRx;

typedef struct _SessionData
{
  int ref;
  GstElement *rtpbin;
  guint sessionNum;
  GstCaps *caps;
  GstElement *output;

  GObject *rtp_session;
  ScreamRx* scream_rx;
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
  gint framerate = 25;
  SessionData *ret = session_new (sessionNum);
  gchar binname[20];
//  GstBin *bin = GST_BIN (gst_bin_new ("video"));
  GstBin *bin = GST_BIN (gst_bin_new (rand_string(binname, 18)));
  GstElement *queue = gst_element_factory_make ("queue", NULL);
  GstElement *depayloader = gst_element_factory_make ("rtpvp8depay", NULL);
  GstElement *decoder = gst_element_factory_make ("vp8dec", NULL);

  GstElement *converter = gst_element_factory_make ("videoconvert", NULL);
  GstElement *sink = gst_element_factory_make ("autovideosink", NULL);

  gst_bin_add_many (bin, depayloader, decoder, converter, queue, sink, NULL);
  gst_element_link_many (queue, depayloader, decoder, converter, sink, NULL);

  setup_ghost_sink (queue, bin);

  ret->output = GST_ELEMENT (bin);

  ret->caps = gst_caps_new_simple ("application/x-rtp",
      "media", G_TYPE_STRING, "video",
      "clock-rate", G_TYPE_INT, 90000,
      "width", G_TYPE_INT, yuvsrc_width,
      "height", G_TYPE_INT, yuvsrc_height,
      "framerate", GST_TYPE_FRACTION, framerate, 1,
      "encoding-name", G_TYPE_STRING, "VP8", NULL
      );

  g_object_set (sink, "sync", FALSE, NULL);
  return ret;
}



static SessionData *
make_video_session_and_save_yuvfile (guint sessionNum)
{
  SessionData *ret = session_new (sessionNum);
  gchar binname[20];
  GstBin *bin = GST_BIN (gst_bin_new (rand_string(binname, 18)));
  GstElement *queue = gst_element_factory_make ("queue", "q1");
  GstElement *depayloader = gst_element_factory_make ("rtpvp8depay", "depayloader");
  GstElement *decoder = gst_element_factory_make ("vp8dec", "decoder");

  GstElement *converter = gst_element_factory_make ("videoconvert", "converter");
  GstElement *autovideosink = gst_element_factory_make ("autovideosink", "autovideoplayer");

  GstElement *tee       = gst_element_factory_make("tee", "splitter");
  GstElement *ply_queue = gst_element_factory_make("queue", "playqueue");
  GstElement *rec_queue = gst_element_factory_make("queue", "recorderqueue");
  GstElement *filesink  = gst_element_factory_make("filesink", "recorder");

  g_object_set (filesink, "location", "destination.yuv", NULL);
//  gst_bin_add_many (bin, depayloader, decoder, converter, queue, autovideosink, NULL);
//  gst_element_link_many (queue, depayloader, decoder, converter, autovideosink, NULL);

  gst_bin_add_many (bin, queue, depayloader, decoder, converter, tee, ply_queue,
                    autovideosink, rec_queue, filesink, NULL);
  gst_element_link_many (queue, depayloader, decoder, converter, tee, NULL);

  gst_element_link_pads (tee, "src_1", ply_queue, "sink");
  gst_element_link_many (ply_queue, autovideosink, NULL);

  gst_element_link_pads (tee, "src_2", rec_queue, "sink");
  gst_element_link_many (rec_queue, filesink, NULL);

  setup_ghost_sink (queue, bin);

  ret->output = GST_ELEMENT (bin);

  ret->caps = gst_caps_new_simple ("application/x-rtp",
      "media", G_TYPE_STRING, "video",
      "clock-rate", G_TYPE_INT, 90000,
      "width", G_TYPE_INT, yuvsrc_width,
      "height", G_TYPE_INT, yuvsrc_height,
      "framerate", GST_TYPE_FRACTION, framerate, 1,
      "encoding-name", G_TYPE_STRING, "VP8", NULL
      );

  g_object_set (autovideosink, "sync", FALSE, NULL);
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


static GstPadProbeReturn probe_rtp_info(GstPad *srcpad, GstPadProbeInfo *info, SessionData *sessionData)
{
    GstBuffer *buffer = NULL;
    GstRTPBuffer rtp_buf = GST_RTP_BUFFER_INIT;
    guint64 arrival_time = GST_CLOCK_TIME_NONE;
    guint session_id = 0;
    gboolean rtp_mapped = FALSE;
    GObject *rtp_session = NULL;
    ScreamRx *scream_rx;

    scream_rx = sessionData->scream_rx;
    session_id = scream_rx->session_id;

    buffer = GST_PAD_PROBE_INFO_BUFFER(info);

    g_signal_emit_by_name(sessionData->rtpbin, "get-internal-session", session_id, &rtp_session);

    if (G_UNLIKELY(scream_rx->initialized == FALSE)) {
	    g_object_set(rtp_session, "rtcp-reduced-size", TRUE, NULL);
	    scream_rx->initialized = TRUE;
    }

    if (!gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp_buf)) {
		g_warning("Failed to map RTP buffer");
		goto end;
	}

	rtp_mapped = TRUE;

    {
        GstMeta *meta;
        const GstMetaInfo *meta_info = _owr_arrival_time_meta_get_info();
        GHashTable *rtcp_info;
        guint16 seq = 0;
        guint ssrc = 0;
        guint diff, tmp_highest_seq, tmp_seq;

        if ((meta = gst_buffer_get_meta(buffer, meta_info->api))) {
            OwrArrivalTimeMeta *atmeta = (OwrArrivalTimeMeta *) meta;
            arrival_time = atmeta->arrival_time;
        }

        if (arrival_time == GST_CLOCK_TIME_NONE) {
            GST_WARNING("No arrival time available for RTP packet");
            goto end;
        }

        scream_rx->ssrc = ssrc = gst_rtp_buffer_get_ssrc(&rtp_buf);
        seq = gst_rtp_buffer_get_seq(&rtp_buf);

        tmp_seq = seq;
        tmp_highest_seq = scream_rx->highest_seq;
        if (!scream_rx->highest_seq && !scream_rx->ack_vec) { /* Initial condition */
            scream_rx->highest_seq = seq;
            tmp_highest_seq = scream_rx->highest_seq;
        } else if ((seq < scream_rx->highest_seq) && (scream_rx->highest_seq - seq > 20000))
            tmp_seq = (guint64)seq + 65536;
        else if ((seq > scream_rx->highest_seq) && (seq - scream_rx->highest_seq > 20000))
            tmp_highest_seq += 65536;

        /* in order */
        if (tmp_seq >= tmp_highest_seq) {
            diff = tmp_seq - tmp_highest_seq;
            if (diff) {
                if (diff >= 16)
                    scream_rx->ack_vec = 0x0000; /* ack_vec can be reduced to guint16, initialize with 0xffff */
                else {
                    // Fill with potential zeros
                    scream_rx->ack_vec = scream_rx->ack_vec >> diff;
                    // Add previous highest seq nr to ack vector
                    scream_rx->ack_vec = scream_rx->ack_vec | (1 << (16 - diff));
                }
            }

            scream_rx->highest_seq = seq;
        } else { /* out of order */
            diff = tmp_highest_seq - tmp_seq;
            if (diff < 16)
                scream_rx->ack_vec = scream_rx->ack_vec | (1 << (16 - diff));
        }
        if (!(scream_rx->ack_vec & (1 << (16-5)))) {
            /*
            * Detect lost packets with a little grace time to cater
            * for out-of-order delivery
            */
            scream_rx->n_loss++; /* n_loss is a guint8 , initialize to 0 */
        }

        /*
        * ECN is not implemented but we add this just to not forget it
        * in case ECN flies some day
        */
        scream_rx->n_ecn = 0;
        scream_rx->last_feedback_wallclock = (guint32)(arrival_time / 1000000);

        scream_rx->pt = GST_RTCP_TYPE_RTPFB;
        scream_rx->fmt = GST_RTCP_RTPFB_TYPE_SCREAM;
        scream_rx->rtcp_session_id = session_id;
        scream_rx->has_data = TRUE;
        g_signal_emit_by_name(rtp_session, "send-rtcp", 20000000);
    }

end:
    if (rtp_mapped)
        gst_rtp_buffer_unmap(&rtp_buf);
    if (rtp_session)
        g_object_unref(rtp_session);

    return GST_PAD_PROBE_OK;
}

#define GST_RTCP_RTPFB_TYPE_SCREAM 18

static gboolean on_sending_rtcp(GObject *session, GstBuffer *buffer, gboolean early,
    SessionData *sessionData)
{
    GstRTCPBuffer rtcp_buffer = {NULL, {NULL, 0, NULL, 0, 0, {0}, {0}}};
    GstRTCPPacket rtcp_packet;
    GstRTCPType packet_type;
    gboolean has_packet, do_not_suppress = FALSE;
    GValueArray *sources = NULL;
    GObject *source = NULL;
    guint session_id = 0, rtcp_session_id = 0;
    GList *it, *next;
    ScreamRx *scream_rx;

    guint pt, fmt, ssrc, last_fb_wc, highest_seq, n_loss, n_ecn;

    OWR_UNUSED(early);

    scream_rx = sessionData->scream_rx;

    session_id = GPOINTER_TO_UINT(g_object_get_data(session, "session_id"));

    if(!scream_rx->initialized || !scream_rx->has_data){
      goto done;
    }

    if (!gst_rtcp_buffer_map(buffer, GST_MAP_READ | GST_MAP_WRITE, &rtcp_buffer)) {
    	goto done;
    }

	has_packet = gst_rtcp_buffer_get_first_packet(&rtcp_buffer, &rtcp_packet);
	for (; has_packet; has_packet = gst_rtcp_packet_move_to_next(&rtcp_packet)) {
		packet_type = gst_rtcp_packet_get_type(&rtcp_packet);
		if (packet_type == GST_RTCP_TYPE_PSFB || packet_type == GST_RTCP_TYPE_RTPFB) {
			do_not_suppress = TRUE;
			break;
		}
	}

	pt = scream_rx->pt;
	ssrc = scream_rx->ssrc;

	gst_rtcp_buffer_add_packet(&rtcp_buffer, pt, &rtcp_packet);

	rtcp_session_id = scream_rx->rtcp_session_id;
	fmt = GST_RTCP_RTPFB_TYPE_SCREAM;

	guint8 *fci_buf;
	last_fb_wc = scream_rx->last_feedback_wallclock;
	highest_seq = scream_rx->highest_seq;
	n_loss = scream_rx->n_loss;
	n_ecn = scream_rx->n_ecn;

	gst_rtcp_packet_fb_set_type(&rtcp_packet, fmt);
	gst_rtcp_packet_fb_set_sender_ssrc(&rtcp_packet, 0);
	gst_rtcp_packet_fb_set_media_ssrc(&rtcp_packet, ssrc);
	gst_rtcp_packet_fb_set_fci_length(&rtcp_packet, 3);

	fci_buf = gst_rtcp_packet_fb_get_fci(&rtcp_packet);
	GST_WRITE_UINT16_BE(fci_buf, highest_seq);
	GST_WRITE_UINT8(fci_buf + 2, n_loss);
	GST_WRITE_UINT8(fci_buf + 3, n_ecn);
	GST_WRITE_UINT32_BE(fci_buf + 4, last_fb_wc);
	/* qbit not implemented yet  */
	GST_WRITE_UINT32_BE(fci_buf + 8, 0);
	do_not_suppress = TRUE;

	scream_rx->has_data = FALSE;

	gst_rtcp_buffer_unmap(&rtcp_buffer);
  done:
    return do_not_suppress;
}

static GstPadProbeReturn probe_save_ts(GstPad *srcpad, GstPadProbeInfo *info, void *user_data)
{
    GstBuffer *buffer = NULL;
    OWR_UNUSED(user_data);
    OWR_UNUSED(srcpad);

    buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    _owr_buffer_add_arrival_time_meta(buffer, GST_BUFFER_DTS(buffer));

    return GST_PAD_PROBE_OK;
}


static void
join_session (GstElement * pipeline, GstElement * rtpBin, SessionData * session)
{
  GstElement *rtpSrc;
  GstElement *rtcpSrc;
  GstElement *rtcpSink;
  gchar *padName;
  guint basePort;
  GstPad *rtp_sink_pad;
  GstPad *rtpSrc_pad;

  g_print ("Joining session %p\n", session);

  session->rtpbin = g_object_ref (rtpBin);
  g_signal_emit_by_name(session->rtpbin, "get-internal-session", session->sessionNum, &session->rtp_session);

  basePort = 5000 + (session->sessionNum * 6);

  rtpSrc = gst_element_factory_make ("udpsrc", NULL);
  rtcpSrc = gst_element_factory_make ("udpsrc", NULL);
  rtcpSink = gst_element_factory_make ("udpsink", NULL);
//  g_object_set (rtpSrc, "port", basePort, "caps", session->caps, NULL);
//  g_object_set (rtcpSink, "port", basePort + 5, "host", "127.0.0.1", "sync",
//      FALSE, "async", FALSE, NULL);
//  g_object_set (rtcpSrc, "port", basePort + 1, NULL);

  g_object_set (rtpSrc, "port", path1_tx_rtp_port, "caps", session->caps, NULL);
  g_object_set (rtcpSrc, "port", rtpbin_tx_rtcp_port, NULL);
  g_object_set (rtcpSink, "port", rtpbin_rx_rtcp_port, "host", path_1_rx_ip,
		  "async", FALSE, NULL);

  g_print ("Connecting to %i/%i/%i\n", basePort, basePort + 1, basePort + 5);

  /* enable RFC4588 retransmission handling by setting rtprtxreceive
   * as the "aux" element of rtpbin */
  g_signal_connect (rtpBin, "request-aux-receiver",
      (GCallback) request_aux_receiver, session);

  //g_signal_connect_after(session->rtp_session, "on-sending-rtcp", G_CALLBACK(on_sending_rtcp), session);

  gst_bin_add_many (GST_BIN (pipeline), rtpSrc, rtcpSrc, rtcpSink, NULL);

  rtpSrc_pad = gst_element_get_static_pad(rtpSrc, "src");
  gst_pad_add_probe(rtpSrc_pad, GST_PAD_PROBE_TYPE_BUFFER, probe_save_ts, NULL, NULL);
  gst_object_unref(rtpSrc_pad);

  g_signal_connect_data (rtpBin, "pad-added", G_CALLBACK (handle_new_stream),
      session_ref (session), (GClosureNotify) session_unref, 0);

  g_signal_connect_data (rtpBin, "request-pt-map", G_CALLBACK (request_pt_map),
      session_ref (session), (GClosureNotify) session_unref, 0);

  padName = g_strdup_printf ("recv_rtp_sink_%u", session->sessionNum);
  gst_element_link_pads (rtpSrc, "src", rtpBin, padName);
  rtp_sink_pad = gst_element_get_static_pad(rtpBin, padName);
  g_free (padName);

  padName = g_strdup_printf ("recv_rtcp_sink_%u", session->sessionNum);
  gst_element_link_pads (rtcpSrc, "src", rtpBin, padName);
  g_free (padName);

  padName = g_strdup_printf ("send_rtcp_src_%u", session->sessionNum);
  gst_element_link_pads (rtpBin, padName, rtcpSink, "sink");
  g_free (padName);

  {
  	GObject *rtp_session = NULL;
    ScreamRx *scream_rx;
    g_signal_emit_by_name(rtpBin, "get-internal-session", 0, &rtp_session);
    g_signal_connect_after(rtp_session, "on-sending-rtcp", G_CALLBACK(on_sending_rtcp), session);  //	g_signal_connect_after(rtp_session, "on-receiving-rtcp", G_CALLBACK(on_receiving_rtcp), NULL);

    session->scream_rx = scream_rx = g_new0(ScreamRx, 1);
    scream_rx->session_id = 0;
    gst_pad_add_probe(rtp_sink_pad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_rtp_info,
    		session, g_free);

    g_object_unref(rtp_session);
  }

}

int
main (int argc, char **argv)
{
  GstPipeline *pipe;
  SessionData *videoSession;
  GstElement *rtpBin;
  GstBus *bus;
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
  g_signal_connect (bus, "message::error", G_CALLBACK (cb_error), pipe);
  g_signal_connect (bus, "message::warning", G_CALLBACK (cb_warning), pipe);
  g_signal_connect (bus, "message::state-changed", G_CALLBACK (cb_state), pipe);
  g_signal_connect (bus, "message::eos", G_CALLBACK (cb_eos), NULL);
  gst_bus_add_signal_watch (bus);
  gst_object_unref (bus);

  rtpBin = gst_element_factory_make ("rtpbin", NULL);
  gst_bin_add (GST_BIN (pipe), rtpBin);
  g_object_set (rtpBin,
		  //"latency", 200,
		  "do-retransmission", FALSE,
          "rtp-profile", GST_RTP_PROFILE_AVPF,
		  NULL);

  framerate = use_testsourcevideo ? 100 : 25;
  if(save_received_yuvfile){
    videoSession = make_video_session_and_save_yuvfile (0);
  }else{
    videoSession = make_video_session (0);
  }
  join_session (GST_ELEMENT (pipe), rtpBin, videoSession);

  g_print ("starting client pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_PLAYING);

  g_main_loop_run (loop);

  g_print ("stoping client pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_NULL);

  gst_object_unref (pipe);
  g_main_loop_unref (loop);
  session_unref (videoSession);

  return 0;
}
