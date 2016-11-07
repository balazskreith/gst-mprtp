#include "pipeline.h"
#include "source.h"
#include "encoder.h"
#include "sender.h"

#define MPRTP_PLUGIN_MAX_SUBFLOW_NUM 32

typedef struct{
  GstBin*        bin;

  Source*       source;
  Encoder*      encoder;
  Sender*       sender;

  SourceParams* source_params;
  CodecParams*  codec_params;
  VideoParams*  video_params;

  SinkParams*   encodersink_params;

  SndTransferParams*  snd_transfer_params;
  SndPacketScheduler* snd_packet_scheduler_params;
  StatParamsTuple*    stat_params_tuple;

  GstCaps*      video_caps;
}SenderSide;

//static SenderSide session;

static void _print_params(SenderSide* this)
{
  g_print("Source    Params: %s\n", this->source_params->to_string);
  g_print("Codec     Params: %s\n", this->codec_params->to_string);
  g_print("Video     Params: %s\n", this->video_params->to_string);
  g_print("Transfer  Params: %s\n", this->snd_transfer_params->to_string);

  g_print("Enc. Sink Params: %s\n", this->encodersink_params ? this->encodersink_params->to_string : "NONE");
  g_print("Scheduler Params: %s\n", this->snd_packet_scheduler_params ? this->snd_packet_scheduler_params->to_string : "None");
  g_print("Stat      Params: %s\n", this->stat_params_tuple ? this->stat_params_tuple->to_string : "None");

}

//#include "sink.h"
//#include "decoder.h"
static void _assemble_bin(SenderSide *this)
{
//  Sink       *sink    = make_sink(make_sink_params("AUTOVIDEO"));
//  Decoder    *decoder = make_decoder(make_codec_params("VP8"));

  gst_bin_add_many(this->bin,
        this->source->element,
        this->encoder->element,
        this->sender->element,
//        decoder->element,
//        sink->element,

   NULL);

//  gst_element_link(this->source->element, sink->element);
//  gst_element_link_many(this->source->element, this->encoder->element, decoder->element, sink->element, NULL);

}

static void _connect_source_to_encoder(SenderSide *this)
{
  Source*  source = this->source;
  Encoder* encoder = this->encoder;
  GstCaps* caps;

//  caps = gst_caps_new_simple ("video/x-raw",
//        "width",  G_TYPE_INT, atoi(this->video_params->width),
//        "height", G_TYPE_INT, atoi(this->video_params->height),
//        "framerate", GST_TYPE_FRACTION, this->video_params->framerate.numerator, this->video_params->framerate.divider,
//        NULL);

//  gst_element_link_filtered(source->element, encoder->element, caps);

  gst_element_link(source->element, encoder->element);
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
      "\t\t FILE:location(string):loop(int):width(int):height(int):GstFormatIdentifier(int):framerate(N/M)\n"
      "\t--codec=VP8|THEORA\n"
      "\t--encoder_sink=See the sink param setting\n"
      "\t--stat=csv_type(int):touched_sync_location(string)\n"
      "\t--packetlogs_sink=See the sink param setting\n"
      "\t--statlogs_sink=See the sink param setting\n"
      "\t--stat=csv_type(int):touched_sync_location(string)\n"
      "\t\t MPRTPFBRAP\n"
      "\t--scheduler=SCREAM|MPRTP:MPRTPFBRAP\n"
      "\t\t SCREAM\n"
      "\t\t MPRTP\n"
      "\t\t MPRTPFBRAP\n"
      "\t--transfer=RTP|MPRTP\n"
      "\t\t RTP:dest_ip(string):dest_port(int)\n"
      "\t\t MPRTP:num_of_subflows(int):subflow_id(int):dest_ip(string):dest_port(int):...\n"
      "\n"
      "Examples: ./program --source=TESTVIDEO --codec=VP8 --sender=RTP:10.0.0.6:5000"
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

  session->encodersink_params = encodersink_params_rawstring ? make_sink_params(encodersink_params_rawstring) : NULL;

  session->snd_packet_scheduler_params = NULL;
//  session->stat_params_tuple = make_statparams_tuple_by_raw_strings(
//      stat_params_rawstring,
//      statlogs_sink_params_rawstring,
//      packetlogs_sink_params_rawstring);

  session->stat_params_tuple = make_statparams_tuple_by_raw_strings(
      "100:1000:1:triggered_stat",
      "FILE:snd_statlogs.txt",
      "FILE:snd_packetlogs.txt");

  session->video_params = make_video_params(
      _null_test(video_params_rawstring, video_params_rawstring_default)
  );

  _print_params(session);

  session->source  = make_source(session->source_params);
  session->encoder = make_encoder(session->codec_params, session->encodersink_params);
  session->sender  = make_sender(session->snd_packet_scheduler_params,
      session->stat_params_tuple,
      session->snd_transfer_params);

  pipeline_add_event_notifier("on-bitrate_change", session->encoder->on_bitrate_chage);
  pipeline_add_event_listener("on-playing", &session->source->on_playing);
  pipeline_add_event_listener("on-destroy", &session->source->on_destroy);

  loop = g_main_loop_new (NULL, FALSE);

  pipe = GST_PIPELINE (gst_pipeline_new (NULL));
  bus = gst_element_get_bus (GST_ELEMENT (pipe));
  g_signal_connect (bus, "message::state-changed", G_CALLBACK (cb_state), pipe);
  gst_bus_add_signal_watch (bus);
  gst_object_unref (bus);

  session->bin = GST_BIN (pipe);

  _assemble_bin(session);

  _connect_source_to_encoder(session);
  _connect_encoder_to_sender(session);

  g_print ("starting sender pipeline\n");
  pipeline_firing_event("on-playing", NULL);
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_PLAYING);

  g_main_loop_run (loop);

  g_print ("stopping sender pipeline\n");
  pipeline_firing_event("on-destroy", NULL);
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_NULL);


  gst_object_unref (pipe);
  g_main_loop_unref (loop);

  source_dtor(session->source);
  encoder_dtor(session->encoder);
  sender_dtor(session->sender);

  free_statparams_tuple(session->stat_params_tuple);
  free_snd_packet_scheduler_params(session->snd_packet_scheduler_params);
  free_snd_transfer_params(session->snd_transfer_params);
  g_free(session);

  return 0;
}


