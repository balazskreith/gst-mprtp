#include "pipeline.h"
#include "source.h"
#include "encoder.h"
#include "sender.h"

#define MPRTP_PLUGIN_MAX_SUBFLOW_NUM 32

typedef struct{
  GstBin*  bin;

  Source*  source;
  Encoder* encoder;
  Sender*  sender;


  SourceParams* source_params;
  CodecParams*  codec_params;
  VideoParams*  video_params;

  SndTransferParams*  snd_transfer_params;
  CCSenderSideParams* cc_sender_side_params;
  StatParamsTuple*    stat_params_tuple;

  Notifier*     on_bitrate_change;
  GstCaps*      video_caps;
}SenderSide;

//static SenderSide session;

static void _print_params(SenderSide* this)
{
  g_print("Source   Params: %s\n", this->source_params->to_string);
  g_print("Codec    Params: %s\n", this->codec_params->to_string);
  g_print("Video    Params: %s\n", this->video_params->to_string);
  g_print("Transfer Params: %s\n", this->snd_transfer_params->to_string);

  g_print("CCSnd    Params: %s\n", this->cc_sender_side_params ? this->cc_sender_side_params->to_string : "None");
  g_print("Stat     Params: %s\n", this->stat_params_tuple ? this->stat_params_tuple->to_string : "None");

}

#include "sink.h"
#include "decoder.h"
static void _assemble_bin(SenderSide *this)
{
  Sink*    sink    = make_sink(make_sink_params("AUTOVIDEO"));
//  Decoder* decoder = make_decoder(make_codec_params("VP8"));
  GstElement *source     = gst_element_factory_make ("videotestsrc", NULL);
  GstElement *autosink   = gst_element_factory_make ("autovideosink", NULL);

    g_object_set (source,
        "is-live", TRUE,
        "horizontal-speed", 15,
        NULL);

  gst_bin_add_many(this->bin,
//      source,
      sink->element,
        this->source->element,
//        this->encoder->element,
//        debug_by_sink(this->encoder->element),
//        this->sender->element,
//        debug_by_sink(this->sender->element),
//        decoder->element,
//        sink->element,

   NULL);

  gst_element_link(this->source->element, sink->element);

//  gst_element_link_many(this->source->element, this->encoder->element, decoder->element, sink->element, NULL);

//  gst_element_link_filtered(this->source->element, sink->element, this->video_caps);

}

static void _connect_source_to_encoder(SenderSide *this)
{
  Source*  source = this->source;
  Encoder* encoder = this->encoder;
  GstCaps* caps;

  caps = gst_caps_new_simple ("video/x-raw",
        "width",  G_TYPE_INT, atoi(this->video_params->width),
        "height", G_TYPE_INT, atoi(this->video_params->height),
        "framerate", GST_TYPE_FRACTION, this->video_params->framerate.numerator, this->video_params->framerate.divider,
        NULL);

  gst_element_link_filtered(source->element, encoder->element, caps);
}

static void _connect_encoder_to_sender(SenderSide *this)
{
  Encoder* encoder = this->encoder;
  Sender*  sender = this->sender;
//  GstBin* bin = gst_bin_new(NULL);
//  GstElement* identity = gst_element_factory_make ("identity", NULL);
//gst_bin_add(bin, identity);
//setup_ghost_sink(identity, bin);
//setup_ghost_src(identity, bin);
  gst_element_link(encoder->element, sender->element);
//  gst_bin_add(this->bin, GST_ELEMENT(bin));
//  gst_element_link_many(encoder->element, GST_ELEMENT(bin), sender->element, NULL);
//  g_object_set(identity, "dump", TRUE, NULL);
}


static void _print_info(void){
  g_print(
      "Options for using Sender Pipeline\n"
      "\t--info Print this.\n"
      "\t--source=TESTVIDEO|RAWPROXY|LIVEFILE:...\n"
      "\t\t TESTVIDEO\n"
      "\t\t RAWPROXY:portnum(int):width(int):height(int):sampling(string)\n"
      "\t\t LIVEFILE:location(string):loop(int):width(int):height(int):GstFormatIdentifier(int):framerate(N/M)\n"
      "\t\t FILE:location(string):loop(int):width(int):height(int):GstFormatIdentifier(int):framerate(N/M)\n"
      "\n"
      "Examples: ./program --source=TESTVIDEO --codec=VP8:352:288:90000:25/1 --sender=RTP:10.0.0.6:5000"
  );
}

int main (int argc, char **argv)
{
  GstPipeline *pipe;
  GstBus *bus;
  GstCaps *videoCaps;
  GMainLoop *loop;
  SenderSide *session;
  GError *error = NULL;
  GOptionContext *context;

  session = g_malloc0(sizeof(SenderSide));
  context = g_option_context_new ("Sender");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_parse (context, &argc, &argv, &error);
  if(info){
    _print_info();
    return 0;
  }

  gst_init (&argc, &argv);

  session->source_params = make_source_params(
      _null_test(source_params_rawstring, source_params_rawstring_default)
  );

  session->codec_params = make_codec_params(
      _null_test(codec_params_rawstring, codec_params_rawstring_default)
  );

  session->snd_transfer_params = make_snd_transfer_params(
      _null_test(sndtransfer_params_rawstring, sndtransfer_params_rawstring_default)
  );

  session->cc_sender_side_params = NULL;
  session->stat_params_tuple = make_statparams_tuple_by_raw_strings(
      stat_params_rawstring,
      statlogs_sink_params_rawstring,
      packetlogs_sink_params_rawstring);

  session->video_params = make_video_params(
      _null_test(video_params_rawstring, video_params_rawstring_default)
  );


  session->on_bitrate_change = make_notifier("on-bitrate-change");

  _print_params(session);

  session->source  = make_source(session->source_params);
  session->encoder = make_encoder(session->codec_params);
  session->sender  = make_sender(session->cc_sender_side_params,
      session->stat_params_tuple,
      session->snd_transfer_params);

  loop = g_main_loop_new (NULL, FALSE);

  pipe = GST_PIPELINE (gst_pipeline_new (NULL));
  bus = gst_element_get_bus (GST_ELEMENT (pipe));
  g_signal_connect (bus, "message::state-changed", G_CALLBACK (cb_state), pipe);
  gst_bus_add_signal_watch (bus);
  gst_object_unref (bus);

  session->bin = GST_BIN (pipe);

  session->video_caps = gst_caps_new_simple ("video/x-raw",
      "width", G_TYPE_STRING, session->video_params->width,
      "height", G_TYPE_STRING, session->video_params->height,
      "framerate", GST_TYPE_FRACTION, session->video_params->framerate.numerator, session->video_params->framerate.divider,
      "format", G_TYPE_INT, 2,
      NULL);

  _assemble_bin(session);

//  _connect_source_to_encoder(session);
//  _connect_encoder_to_sender(session);

  g_print ("starting sender pipeline\n");
  notifier_do(get_sender_eventers()->on_playing, NULL);
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_PLAYING);

  g_main_loop_run (loop);

  g_print ("stopping sender pipeline\n");
  notifier_do(get_sender_eventers()->on_destroy, NULL);
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_NULL);


  gst_object_unref (pipe);
  g_main_loop_unref (loop);

  source_dtor(session->source);
  encoder_dtor(session->encoder);
  sender_dtor(session->sender);

  free_statparams_tuple(session->stat_params_tuple);
  free_cc_sender_side_params(session->cc_sender_side_params);
  free_snd_transfer_params(session->snd_transfer_params);
  g_free(session);

  return 0;
}


