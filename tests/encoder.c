#include "encoder.h"

#include <varargs.h>

static void _setup_vp8_encoder(GstBin* encoderBin, CodecParams *params);
static void _setup_theora_encoder(GstBin* encoderBin, CodecParams *params);

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
  Encoder* this = encoder_ctor();
  GstBin*  encoderBin     = GST_BIN(gst_bin_new(NULL));

  switch(params->type){
    case CODEC_TYPE_VP8:
      _setup_vp8_encoder(encoderBin, params);
      break;
    case CODEC_TYPE_THEORA:
      _setup_theora_encoder(encoderBin, params);
      break;
  };

  this->element = GST_ELEMENT(encoderBin);
  return this;
}

static void _setup_vp8_encoder(GstBin* encoderBin, CodecParams *params)
{
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

}

static void _setup_theora_encoder(GstBin* encoderBin, CodecParams *params)
{
  GstElement *encoder = gst_element_factory_make ("theoraenc", NULL);
  GstElement *payloader = gst_element_factory_make ("rtptheorapay", NULL);

  g_object_set (payloader,
      "config-interval", 2,
      NULL);

  gst_bin_add_many (encoderBin, encoder, payloader, NULL);
  gst_element_link (encoder, payloader);

  setup_ghost_sink(encoder, encoderBin);
  setup_ghost_src (payloader, encoderBin);
}
