#include "pipeline.h"
#include "receiver.h"
#include "decoder.h"
#include "sink.h"

typedef struct{
  GstBin*    bin;

  Receiver*  receiver;
  Decoder*   decoder;
  Sink*      sink;


  ReceiverParams* receiver_params;
  CodecParams*    codec_params;
  SinkParams*     sink_params;

}ReceiverSide;

//static SenderSide session;

static void _print_params(ReceiverSide* this)
{
  g_print("Receiver Params: %s\n", this->receiver_params->to_string);
  g_print("Codec    Params: %s\n", this->codec_params->to_string);
  g_print("Sink     Params: %s\n", this->sink_params->to_string);

}

static void _setup_bin(ReceiverSide *this)
{
  gst_bin_add_many(GST_BIN (pipe),

        this->receiver->element,
        this->decoder->element,
        this->sink->element,

   NULL);
}

static void _connect_receiver_to_decoder(ReceiverSide *this)
{
  Receiver*  receiver = this->receiver;
  Decoder*   decoder = this->decoder;

  gst_element_link_filtered(receiver->element, decoder->element, videoCaps);

}

static void _connect_decoder_to_sink(ReceiverSide *this)
{
  Decoder* decoder = this->decoder;
  Sink*    sink    = this->sink;

  gst_element_link(decoder->element, sink->element);

}


int main (int argc, char **argv)
{
  GstPipeline *pipe;
  GstBus *bus;
  GstCaps *videoCaps;
  GMainLoop *loop;
  ReceiverSide *session;
  GError *error = NULL;
  GOptionContext *context;

  session = g_malloc0(sizeof(ReceiverSide));
  context = g_option_context_new ("Receiver");
  g_option_context_add_main_entries (context, entries, NULL);
  g_option_context_parse (context, &argc, &argv, &error);
  if(info){
    _print_info();
    return 0;
  }
  _setup_test_params();

  gst_init (&argc, &argv);

  session->receiver_params = make_receiver_params(
      _string_test(receiver_params_rawstring, receiver_params_rawstring_default)
  );

  session->codec_params = make_codec_params(
      _string_test(codec_params_rawstring, codec_params_rawstring_default)
  );

  session->sink_params = make_sink_params(
      _string_test(sink_params_rawstring, sink_params_rawstring_default)
  );

  _print_params(session);

  session->receiver  = make_receiver(session.receiver_params);
  session->decoder   = make_decoder(session.codec_params);
  session->sink      = make_sink(session.sink_params);

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

  _setup_bin(session);

  _connect_receiver_to_decoder(session);
  _connect_decoder_to_sink(session);


  g_print ("starting server pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_PLAYING);

  g_main_loop_run (loop);

  g_print ("stopping server pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_NULL);


  gst_object_unref (pipe);
  g_main_loop_unref (loop);

  receiver_dtor(session->receiver);
  decoder_dtor(session->decoder);
  sink_dtor(session->sink);
  g_free(session);

  return 0;
}


