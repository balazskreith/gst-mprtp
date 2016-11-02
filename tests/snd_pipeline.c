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
  SenderParams* sender_params;

  Notifier*     on_bitrate_change;
}SenderSide;

//static SenderSide session;

static void _print_params(SenderSide* this)
{
  g_print("Source Params: %s\n", this->source_params->to_string);
  g_print("Codec  Params: %s\n", this->codec_params->to_string);
  g_print("Sender Params: %s\n", this->sender_params->to_string);

}

static void _setup_bin(SenderSide *this)
{
  gst_bin_add_many(GST_BIN (pipe),

        this->source->element,
        this->encoder->element,
        this->sender->element,

   NULL);
}

static void _connect_source_to_encoder(SenderSide *this)
{
  Source*  source = this->source;
  Encoder* encoder = this->encoder;

  gst_element_link_filtered(source->element, encoder->element, videoCaps);

}

static void _connect_encoder_to_sender(SenderSide *this)
{
  Encoder* encoder = this->encoder;
  Sender*  sender = this->sender;

  gst_element_link(encoder->element, sender->element);

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
  _setup_test_params();

  gst_init (&argc, &argv);

  session->source_params = make_source_params(
      _string_test(source_params_rawstring, source_params_rawstring_default)
  );

  session->codec_params = make_codec_params(
      _string_test(codec_params_rawstring, codec_params_rawstring_default)
  );

  session->sender_params = make_sender_params(
      _string_test(sender_params_rawstring, sender_params_rawstring_default)
  );

  session->on_bitrate_change = make_notifier("on-bitrate-change");

  _print_params(session);

  session->source  = make_source(session.source_params);
  session->encoder = make_encoder(session.codec_params);
  session->sender  = make_sender(session.sender_params);

  loop = g_main_loop_new (NULL, FALSE);

  pipe = GST_PIPELINE (gst_pipeline_new (NULL));
  bus = gst_element_get_bus (GST_ELEMENT (pipe));
  g_signal_connect (bus, "message::state-changed", G_CALLBACK (cb_state), pipe);
  gst_bus_add_signal_watch (bus);
  gst_object_unref (bus);

  session->bin = GST_BIN (pipe);

  _setup_bin(session);

  _connect_source_to_encoder(session);
  _connect_encoder_to_sender(session);


  g_print ("starting server pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_PLAYING);

  g_main_loop_run (loop);

  g_print ("stopping server pipeline\n");
  gst_element_set_state (GST_ELEMENT (pipe), GST_STATE_NULL);


  gst_object_unref (pipe);
  g_main_loop_unref (loop);

  source_dtor(session->source);
  encoder_dtor(session->encoder);
  sender_dtor(session->sender);
  g_free(session);

  return 0;
}


