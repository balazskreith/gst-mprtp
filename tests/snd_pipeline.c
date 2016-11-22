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

  SourceParams*   source_params;
  CodecParams*    codec_params;
  VideoParams*    video_params;

  SinkParams*     encodersink_params;
  SinkParams*     sourcesink_params;

  TransferParams*     snd_transfer_params;
  SchedulerParams*    scheduler_params;
  StatParamsTuple*    stat_params_tuple;
  ExtraDelayParams*   extra_delay_params;

  GstCaps*      video_caps;
}SenderSide;

//static SenderSide session;

static void _print_params(SenderSide* this)
{
  g_print("Source    Params: %s\n", this->source_params->to_string);
  g_print("Codec     Params: %s\n", this->codec_params->to_string);
  g_print("Video     Params: %s\n", this->video_params->to_string);
  g_print("Transfer  Params: %s\n", this->snd_transfer_params->to_string);

  g_print("Src. Sink Params: %s\n", this->sourcesink_params ? this->sourcesink_params->to_string : "NONE");
  g_print("Enc. Sink Params: %s\n", this->encodersink_params ? this->encodersink_params->to_string : "NONE");
  g_print("Scheduler Params: %s\n", this->scheduler_params ? this->scheduler_params->to_string : "None");
  g_print("Stat      Params: %s\n", this->stat_params_tuple ? this->stat_params_tuple->to_string : "None");
  g_print("Ext. Del. Params: %s\n", this->extra_delay_params ? this->extra_delay_params->to_string : "None");

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
  gst_element_link(encoder->element, sender->element);

  //setup feedback
  pipeline_add_eventer("on-bitrate-change", sender_get_on_bitrate_change_eventer(sender));
  pipeline_add_subscriber("on-bitrate-change", encoder_get_on_bitrate_change_subscriber(encoder));
}


static char *development_argv[] = {
    "program_name",
    "--source=FILE:foreman_cif.yuv:1:352:288:2:25/1",
//    "--source=FILE:consumed.yuv:1:352:288:2:25/1",
//    "--source=TESTVIDEO",
    "--codec=VP8",
//    "--sender=MPRTP:1:1:10.0.0.6:5000",
    "--sender=RTP:10.0.0.6:5000",
//    "--scheduler=MPRTPFRACTAL:MPRTP:1:1:5001",
    "--scheduler=SCREAM:RTP:5001",
    "--stat=100:1000:1:triggered_stat",
    "--statlogsink=FILE:temp/snd_statlogs.csv",
    "--packetlogsink=FILE:temp/snd_packetlogs.csv",
    "--sourcesink=FILE:produced.yuv"
};

#define development_argc (sizeof (development_argv) / sizeof (const char *))

int main (int argc, char **argv)
{
  GstPipeline *pipe;
  GstBus *bus;
  GstCaps *videoCaps;
  GMainLoop *loop;
  SenderSide *session;
  GError *error = NULL;
  GOptionContext *context;
  gboolean context_parse;

  //For using gdb without set args and other stuff
  if(0){
    argc = development_argc;
    argv = development_argv;
  }

  session = g_malloc0(sizeof(SenderSide));
  context = g_option_context_new ("Sender");
  g_option_context_add_main_entries (context, entries, NULL);
  context_parse = g_option_context_parse (context, &argc, &argv, &error);
  if(info){
    _print_info();
    return 0;
  }
  g_print("Errors during parse: %s, returned with: %d\n", error ? error->message : "None", context_parse);
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

  session->sourcesink_params  = sourcesink_params_rawstring ? make_sink_params(sourcesink_params_rawstring) : NULL;

  session->encodersink_params = encodersink_params_rawstring ? make_sink_params(encodersink_params_rawstring) : NULL;

  session->scheduler_params   = scheduler_params_rawstring ? make_scheduler_params(scheduler_params_rawstring) : NULL;;

  session->extra_delay_params = extradelay_params_rawstring ? make_extra_delay_params(extradelay_params_rawstring) : NULL;

  session->stat_params_tuple = make_statparams_tuple_by_raw_strings(
      stat_params_rawstring,
      statlogs_sink_params_rawstring,
      packetlogs_sink_params_rawstring);

  session->video_params = make_video_params(
      _null_test(video_params_rawstring, video_params_rawstring_default)
  );

  _print_params(session);

  session->source  = make_source(session->source_params, session->sourcesink_params);
  session->encoder = make_encoder(session->codec_params, session->encodersink_params);
  session->sender  = make_sender(session->scheduler_params,
      session->stat_params_tuple,
      session->snd_transfer_params,
      session->extra_delay_params);
//  session->sender = make_sender_custom();

  pipeline_add_subscriber("on-playing", &session->source->on_playing);
  pipeline_add_subscriber("on-destroy", &session->source->on_destroy);

  loop = g_main_loop_new (NULL, FALSE);

  pipe = GST_PIPELINE (gst_pipeline_new (NULL));
  bus = gst_element_get_bus (GST_ELEMENT (pipe));
  g_signal_connect (bus, "message::state-changed", G_CALLBACK (cb_state), pipe);
  g_signal_connect (bus, "message::warning", G_CALLBACK (cb_warning), pipe);
  g_signal_connect (bus, "message::eos", G_CALLBACK (cb_eos), loop);
  gst_bus_add_signal_watch (bus);
  gst_object_unref (bus);

  session->bin = GST_BIN (pipe);

  _assemble_bin(session);

  _connect_source_to_encoder(session);
  _connect_encoder_to_sender(session);

  //TODO: setup a start target bitrate if we have one!

  g_print ("starting sender pipeline\n");
  pipeline_firing_event("on-playing", NULL);
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_PLAYING);

  GST_DEBUG_BIN_TO_DOT_FILE(session->bin,  GST_DEBUG_GRAPH_SHOW_ALL, "snd_on_playing");

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
  free_scheduler_params(session->scheduler_params);
  transfer_params_unref(session->snd_transfer_params);
  g_free(session);

  return 0;
}


