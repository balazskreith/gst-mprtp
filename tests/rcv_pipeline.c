#include "pipeline.h"
#include "receiver.h"
#include "decoder.h"
#include "sink.h"

typedef struct{
  GstBin*    bin;

  Receiver*  receiver;
  Decoder*   decoder;
  Sink*      sink;

  StatParams*           rcv_stat_params;
  StatParams*           ply_stat_params;
  TransferParams*       rcv_transfer_params;
  PlayouterParams*      playouter_params;

  CodecParams*    codec_params;
  SinkParams*     sink_params;
  VideoParams*    video_params;

}ReceiverSide;

//static SenderSide session;

static void _print_params(ReceiverSide* this)
{
  g_print("Transfer Params: %s\n", this->rcv_transfer_params->to_string);

  g_print("Codec     Params: %s\n", this->codec_params->to_string);
  g_print("Sink      Params: %s\n", this->sink_params->to_string);
  g_print("Video     Params: %s\n", this->video_params->to_string);

  g_print("Playouter Params: %s\n", this->playouter_params ? this->playouter_params->to_string : "None");
  g_print("Rcv Stat  Params: %s\n", this->rcv_stat_params ? this->rcv_stat_params->to_string : "None");
  g_print("Ply Stat  Params: %s\n", this->ply_stat_params ? this->ply_stat_params->to_string : "None");

}

static void _assemble_bin(ReceiverSide *this)
{
  gst_bin_add_many(this->bin,

        //debug_element(this->receiver->element),
        this->receiver->element,
        this->decoder->element,
        this->sink->element,

   NULL);
}

static void _connect_receiver_to_decoder(ReceiverSide *this)
{
  Receiver*  receiver = this->receiver;
  Decoder*   decoder = this->decoder;

  GstCaps* caps = gst_caps_new_simple ("application/x-rtp",
          "media", G_TYPE_STRING, "video",
          "clock-rate", G_TYPE_INT, this->video_params->clock_rate,
          "encoding-name", G_TYPE_STRING, this->codec_params->type_str,
          NULL);

  pipeline_firing_event("on-caps-change", caps);

  gst_element_link_filtered(receiver->element, decoder->element, caps);
}

static void _connect_decoder_to_sink(ReceiverSide *this)
{
  Decoder* decoder = this->decoder;
  Sink*    sink    = this->sink;

  gst_element_link(decoder->element, sink->element);
}

static char *development_argv[] = {
    "program_name",
//    "--receiver=MPRTP:1:1:5000",
    "--receiver=RTP:5000",
    "--codec=VP8",
//    "--sink=AUTOVIDEO",
    "--sink=FILE:consumed.yuv",
//    "--playouter=MPRTPFRACTAL:MPRTP:1:1:10.0.0.1:5001",
    "--playouter=SCREAM:RTP:10.0.0.1:5001",
    "--stat=100:1000:1:triggered_stat",
    "--statlogsink=FILE:temp/rcv_statlogs.csv",
    "--packetlogsink=FILE:temp/rcv_packetlogs.csv"
};

#define development_argc (sizeof (development_argv) / sizeof (const char *))


int main (int argc, char **argv)
{
  GstPipeline *pipe;
  GstBus *bus;
  GstCaps *videoCaps;
  GMainLoop *loop;
  ReceiverSide *session;
  GError *error = NULL;
  GOptionContext *context;
  gboolean context_parse;

  if(0){
    argc = development_argc;
    argv = development_argv;
    system("rm triggered_stat");
    system("rm temp/*");
//    system("rm produced.yuv");
//    system("rm consumed.yuv");
  }

  session = g_malloc0(sizeof(ReceiverSide));
  context = g_option_context_new ("Receiver");
  g_option_context_add_main_entries (context, entries, NULL);
  context_parse = g_option_context_parse (context, &argc, &argv, &error);
  if(info){
    _print_info();
    return 0;
  }

  g_print("Errors during parse: %s, returned with: %d\n", error ? error->message : "None", context_parse);
  gst_init (&argc, &argv);

  session->rcv_transfer_params = make_rcv_transfer_params(
      _null_test(rcvtransfer_params_rawstring, rcvtransfer_params_rawstring_default)
  );

  session->playouter_params = playouter_params_rawstring ? make_playouter_params(playouter_params_rawstring) : NULL;

  session->rcv_stat_params = make_stat_params(stat_params_rawstring);
  if(ply_stat_params_rawstring){
    session->ply_stat_params = make_stat_params(ply_stat_params_rawstring);
  }

  session->codec_params = make_codec_params(
      _null_test(codec_params_rawstring, codec_params_rawstring_default)
  );

  session->sink_params = make_sink_params(
      _null_test(sink_params_rawstring, sink_params_rawstring_default)
  );

  session->video_params = make_video_params(
      _null_test(video_params_rawstring, video_params_rawstring_default)
  );

  _print_params(session);

  session->receiver  = make_receiver(session->rcv_transfer_params,
      session->rcv_stat_params,
      session->playouter_params,
      session->ply_stat_params);
//  session->receiver  = make_receiver_custom();
  session->decoder   = make_decoder(session->codec_params);
  session->sink      = make_sink(session->sink_params);

  pipeline_add_eventer("on-caps-change", session->receiver->on_caps_change);

  loop = g_main_loop_new (NULL, FALSE);

  pipe = GST_PIPELINE (gst_pipeline_new (NULL));
  bus = gst_element_get_bus (GST_ELEMENT (pipe));
  g_signal_connect (bus, "message::error", G_CALLBACK (cb_error), loop);
  g_signal_connect (bus, "message::warning", G_CALLBACK (cb_warning), pipe);
  g_signal_connect (bus, "message::state-changed", G_CALLBACK (cb_state), pipe);
  g_signal_connect (bus, "message::eos", G_CALLBACK (cb_eos), loop);
  gst_bus_add_signal_watch (bus);
  gst_object_unref (bus);

  session->bin = GST_BIN (pipe);

  _assemble_bin(session);

  _connect_receiver_to_decoder(session);
  _connect_decoder_to_sink(session);

  g_print ("starting receiver pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_PLAYING);
  GST_DEBUG_BIN_TO_DOT_FILE(session->bin,  GST_DEBUG_GRAPH_SHOW_ALL, "rcv_on_playing");

  g_main_loop_run (loop);

  g_print ("stopping receiver pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_NULL);


  gst_object_unref (pipe);
  g_main_loop_unref (loop);

  g_free(session->rcv_stat_params);
  g_free(session->ply_stat_params);
  free_playouter_params(session->playouter_params);
  transfer_params_unref(session->rcv_transfer_params);

  receiver_dtor(session->receiver);
  decoder_dtor(session->decoder);
  sink_dtor(session->sink);
  g_free(session);

  return 0;
}


