#include "encoder.h"
#include "decoder.h"
#include "sink.h"

#include <stdarg.h>

static GstElement* _make_vp8_encoder(CodecParams *params);
static GstElement* _make_theora_encoder(CodecParams *params);
static int _instance_counter = 0;

Encoder* encoder_ctor(void)
{
  Encoder* this;

  this = g_malloc0(sizeof(Encoder));
  sprintf(this->bin_name, "EncoderBin_%d", _instance_counter++);
  this->on_bitrate_chage = make_notifier("on-bitrate-change");
  this->objects_holder = objects_holder_ctor();
  return this;
}

void encoder_dtor(Encoder* this)
{
  object_holder_dtor(this->objects_holder);
  notifier_unref(this->on_bitrate_chage);
  g_free(this);
}

Encoder* make_encoder(CodecParams *codec_params, SinkParams* sink_params)
{
  Encoder*    this       = encoder_ctor();
  GstBin*     encoderBin = GST_BIN(gst_bin_new(this->bin_name));
  GstElement *encoder, *src, *sink;
  GstElement* encoder_sink;

  switch(codec_params->type){
    case CODEC_TYPE_VP8:
      src = sink = encoder = _make_vp8_encoder(codec_params);
      break;
    case CODEC_TYPE_THEORA:
      src = sink = encoder = _make_theora_encoder(codec_params);
      break;
  };
  gst_bin_add(encoderBin, encoder);

  if(sink_params){
    GstElement* tee     = gst_element_factory_make("tee", NULL);
    GstElement* q1      = gst_element_factory_make("queue", NULL);
    GstElement* q2      = gst_element_factory_make("queue", NULL);
    Decoder*    decoder = make_decoder(codec_params);
    Sink*       sink    = make_sink(sink_params);

    objects_holder_add(this->objects_holder, sink, (GDestroyNotify)sink_dtor);
    objects_holder_add(this->objects_holder, decoder, (GDestroyNotify)decoder_dtor);

    gst_bin_add_many(encoderBin, tee, q1, q2, decoder->element, sink->element, NULL);

    gst_element_link(encoder, tee);
    gst_element_link_pads(tee, "src_1", q1, "sink");
    src = q1;

    gst_element_link_pads(tee, "src_2", q2, "sink");
    gst_element_link_many(q2, decoder->element, sink->element, NULL);
  }

  setup_ghost_sink(sink, encoderBin);
  setup_ghost_src (src,  encoderBin);

  this->element = GST_ELEMENT(encoderBin);
  return this;
}

void encoder_on_bitrate_change(Encoder* this, gint32* target_bitrate)
{
  notifier_do(this->on_bitrate_chage, target_bitrate);
}

static void _on_change_vp8_target_bitrate(GstElement* encoder, gint32* target_bitrate){
  g_object_set (encoder, "target-bitrate", *target_bitrate, NULL);
}

GstElement* _make_vp8_encoder(CodecParams *params)
{
  GstBin* encoderBin    = gst_bin_new(NULL);
  GstElement* encoder   = gst_element_factory_make ("vp8enc", NULL);
  GstElement *payloader = gst_element_factory_make ("rtpvp8pay", NULL);


  g_object_set (encoder, "target-bitrate", target_bitrate, NULL);
  notifier_add_listener_full(get_pipeline()->on_target_bitrate_change,
      (listener)_on_change_vp8_target_bitrate, encoder);

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


static void _on_change_theora_target_bitrate(GstElement* encoder, gint32* target_bitrate){
  g_object_set (encoder, "bitrate", *target_bitrate, NULL);
}

static GstElement* _make_theora_encoder(CodecParams *params)
{
  GstBin* encoderBin = gst_bin_new(NULL);
  GstElement *encoder = gst_element_factory_make ("theoraenc", NULL);
  GstElement *payloader = gst_element_factory_make ("rtptheorapay", NULL);

  g_object_set (encoder, "bitrate", target_bitrate, NULL);
  notifier_add_listener_full(get_pipeline()->on_target_bitrate_change,
      (listener)_on_change_theora_target_bitrate, encoder);

  g_object_set (payloader,
      "config-interval", 2,
      NULL);

  gst_bin_add_many (encoderBin, encoder, payloader, NULL);
  gst_element_link (encoder, payloader);

  setup_ghost_sink(encoder, encoderBin);
  setup_ghost_src (payloader, encoderBin);

  return GST_ELEMENT(encoderBin);
}
