#include "encoder.h"

#include <stdarg.h>

static GstElement* _make_vp8_encoder(CodecParams *params);
static GstElement* _make_theora_encoder(CodecParams *params);

Encoder* encoder_ctor(void)
{
  Encoder* this;

  this = g_malloc0(sizeof(Encoder));

  return this;
}

void encoder_dtor(Encoder* this)
{
  g_free(this);
}

Encoder* make_encoder(CodecParams *params)
{
  Encoder* this       = encoder_ctor();
  GstBin*  encoderBin = GST_BIN(gst_bin_new(NULL));
  GstElement *encoder, *src, *sink;


  switch(params->type){
    case CODEC_TYPE_VP8:
      src = sink = encoder = _make_vp8_encoder(params);
      break;
    case CODEC_TYPE_THEORA:
      src = sink = encoder = _make_theora_encoder(params);
      break;
  };

  //TODO: because of saving we embed it for later use
  gst_bin_add_many (encoderBin,

      encoder,

      NULL);

  setup_ghost_sink(sink, encoderBin);
  setup_ghost_src (src,  encoderBin);

  this->element = GST_ELEMENT(encoderBin);
  return this;
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
  notifier_add_listener(get_sender_eventers()->on_target_bitrate_change,
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
  notifier_add_listener(get_sender_eventers()->on_target_bitrate_change,
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
