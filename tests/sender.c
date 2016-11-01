#include <gst/gst.h>
#include <gst/rtp/rtp.h>
#include "test.h"
#include "transceiver.h"


typedef struct _SessionData
{
  int ref;
  GstElement *source;
  GstElement *encoder;
  GstElement *sender;

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
session_new (void)
{
  SessionData *ret = g_new0 (SessionData, 1);
  return session_ref (ret);
}


//static GstElement* make_transceiver(void)
//{
//  GstBin*     transceiver = GST_BIN(gst_bin_new(NULL));
//  GstElement* receiver    = gst_element_factory_make("appsink", NULL);
//  GstElement* transmitter = gst_element_factory_make("appsrc", NULL);
//
//  gst_bin_add_many (transceiver, receiver, transmitter, NULL);
//
//  setup_ghost_sink (receiver, transceiver);
//  setup_ghost_src  (transmitter, transceiver);
//
//  return GST_ELEMENT(transceiver);
//}


static GstElement* make_rawproxy_source(void)
{
  RawProxySourceParams* proxy_params = (RawProxySourceParams*) source_params;
  GstBin*     sourceBin   = GST_BIN(gst_bin_new(NULL));
  GstElement* receiver    = gst_element_factory_make("udpsrc", NULL);
  GstElement* rawDepay    = gst_element_factory_make("rtpvrawdepay", NULL);
  GstElement* transceiver = make_transceiver();
  GstElement* videoParse  = gst_element_factory_make("videoparse", NULL);

  GstElement* tee         = gst_element_factory_make("tee", NULL);
  GstElement* queue       = gst_element_factory_make("queue", NULL);
  GstElement* sink        = gst_element_factory_make("autovideosink", NULL);

  const GstCaps* caps        = gst_caps_new_simple ("application/x-rtp",
      "media", G_TYPE_STRING, "video",
      "clock-rate", G_TYPE_INT, proxy_params->clock_rate,
      "width", G_TYPE_STRING, "352",//TODO: replace it by pram width
      "height", G_TYPE_STRING, "288",
      "sampling", G_TYPE_STRING, "YCbCr-4:2:0",
      "framerate", GST_TYPE_FRACTION, proxy_params->framerate.numerator, proxy_params->framerate.divider,
      "encoding-name", G_TYPE_STRING, "RAW", NULL
      );

  g_print("Caps: %s\n", gst_caps_to_string(caps));

  g_object_set(G_OBJECT(receiver),
      "port", proxy_params->port,
      "caps", caps,
      NULL);

  g_object_set(G_OBJECT(videoParse),
      "format", 2,
      "width", 352,
      "height", 288,
      "framerate", 25, 1,
      NULL
  );

  gst_bin_add_many (sourceBin, receiver, rawDepay, transceiver, videoParse,
      tee, queue, sink,
      NULL);


  gst_element_link_pads(receiver, "src", rawDepay, "sink");
  gst_element_link_pads(rawDepay, "src", tee, "sink");
  gst_element_link_pads(tee, "src_1", transceiver, "sink");

  gst_element_link_pads(tee, "src_2", queue, "sink");
  gst_element_link_pads(queue, "src", videoParse, "sink");
  gst_element_link_pads(videoParse, "src", sink, "sink");

//  gst_element_link_many(receiver, rawDepay, transceiver, videoParse, NULL);

  g_print("CAPS!!!: %s\n",
      gst_caps_to_string(gst_pad_get_current_caps(gst_element_get_static_pad(videoParse, "src"))));

  setup_ghost_src (transceiver, sourceBin);

  return GST_ELEMENT(sourceBin);
}



static GstElement* make_videotest_source(void)
{
  GstElement *source = gst_element_factory_make ("videotestsrc", NULL);

  g_object_set (source,
      "is-live", TRUE,
      "horizontal-speed", 1,
      NULL);

  return source;
}

static GstElement* make_theora_encoder(void)
{
  GstBin *encoderBin = GST_BIN (gst_bin_new (NULL));
  GstElement *encoder = gst_element_factory_make ("theoraenc", NULL);
  GstElement *payloader = gst_element_factory_make ("rtptheorapay", NULL);

  g_object_set (payloader,
      "config-interval", 2,
      NULL);

  gst_bin_add_many (encoderBin, encoder, payloader, NULL);
  gst_element_link (encoder, payloader);

  setup_ghost_sink(encoder, encoderBin);
  setup_ghost_src (payloader, encoderBin);

  return GST_ELEMENT(encoderBin);
}


static GstElement* make_vp8_encoder(void)
{
  GstBin *encoderBin = GST_BIN (gst_bin_new (NULL));
  GstElement* encoder = gst_element_factory_make ("vp8enc", NULL);
  GstElement *payloader = gst_element_factory_make ("rtpvp8pay", NULL);


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


  gst_bin_add_many (encoderBin,

      encoder,
      payloader,

      NULL);

  gst_element_link (encoder, payloader);

  setup_ghost_sink(encoder, encoderBin);
  setup_ghost_src (payloader, encoderBin);

  return GST_ELEMENT(encoderBin);
}


static GstElement* make_rtp_simple_sender ()
{
  GstBin *senderBin    = GST_BIN (gst_bin_new (NULL));
  GstElement *rtpSink  = gst_element_factory_make ("udpsink", NULL);
  RTPSimpleSenderParams* params = (RTPSimpleSenderParams*) sender_params;
  gchar *padName;


  gst_bin_add_many (senderBin, rtpSink, NULL);

  g_object_set (rtpSink, "port", params->peer_port, "host", params->peer_ip, NULL);

  setup_ghost_sink(rtpSink, senderBin);
  return GST_ELEMENT(senderBin);
}



int
main (int argc, char **argv)
{
  GstPipeline *pipe;
  GstBus *bus;
  SessionData *session;
  GstCaps *videoCaps;
  GMainLoop *loop;
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

  gst_init (&argc, &argv);

  _print_video_params();
  _print_sender_params();

  loop = g_main_loop_new (NULL, FALSE);

  pipe = GST_PIPELINE (gst_pipeline_new (NULL));
  bus = gst_element_get_bus (GST_ELEMENT (pipe));
  g_signal_connect (bus, "message::state-changed", G_CALLBACK (cb_state), pipe);
  gst_bus_add_signal_watch (bus);
  gst_object_unref (bus);

  videoCaps = gst_caps_new_simple ("video/x-raw",
          "width", G_TYPE_INT, video_params->width,
          "height", G_TYPE_INT, video_params->height,
          "framerate", GST_TYPE_FRACTION, video_params->framerate.numerator, video_params->framerate.divider,
          "format", G_TYPE_STRING, "I420",
          NULL);

  session = session_new();

  if(source_params->type == SOURCE_TYPE_TESTVIDEO){
    session->source   = make_videotest_source();
    strcat(pipeline_string, "VideoTest ->");
  }else if(source_params->type == SOURCE_TYPE_RAWPROXY){
    session->source   = make_rawproxy_source();
    strcat(pipeline_string, "RawProxy ->");
  }


  if(video_params->codec == CODEC_TYPE_THEORA){
    session->encoder  = make_theora_encoder();
    strcat(pipeline_string, "TheoraEncoder ->");
  }else if(video_params->codec == CODEC_TYPE_VP8){
    session->encoder  = make_vp8_encoder();
    strcat(pipeline_string, "VP8Encoder ->");
  }
//
  if(sender_params->type == TRANSFER_TYPE_RTPSIMPLE){
    session->sender   = make_rtp_simple_sender();
    strcat(pipeline_string, "RTPSender");
  }

  g_print("Pipeline: %s\n", pipeline_string);


  gst_bin_add_many(GST_BIN (pipe),

      session->source,
      session->encoder,
      session->sender,

      NULL);

  gst_element_link_filtered(session->source, session->encoder, videoCaps);
  gst_element_link_many(session->encoder, session->sender, NULL);

  g_print ("starting server pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_PLAYING);

  g_main_loop_run (loop);

  g_print ("stopping server pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_NULL);


  gst_object_unref (pipe);
  g_main_loop_unref (loop);
  session_unref(session);

  return 0;
}
