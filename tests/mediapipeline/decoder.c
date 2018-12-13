#include "decoder.h"

static void _setup_vp8_decoder(GstBin* decoderBin, CodecParams *params);
static void _setup_vp9_decoder(GstBin* decoderBin, CodecParams *params);
static void _setup_theora_decoder(GstBin* decoderBin, CodecParams *params);
static int _instance_counter = 0;

Decoder* decoder_ctor(void)
{
  Decoder* this;
  this = g_malloc0(sizeof(Decoder));
  sprintf(this->bin_name, "DecoderBin_%d", _instance_counter++);

  return this;
}

void decoder_dtor(Decoder* this)
{
  g_free(this);
}

Decoder* make_decoder(CodecParams *params)
{
  Decoder* this = decoder_ctor();
  GstBin*  decoderBin     = GST_BIN(gst_bin_new(this->bin_name));

  switch(params->type){
    case CODEC_TYPE_VP8:
        _setup_vp8_decoder(decoderBin, params);
        break;
    case CODEC_TYPE_VP9:
        _setup_vp9_decoder(decoderBin, params);
        break;
    case CODEC_TYPE_THEORA:
      _setup_theora_decoder(decoderBin, params);
      break;
  };

  this->element = GST_ELEMENT(decoderBin);
  return this;
}

static void _setup_vp8_decoder(GstBin* decoderBin, CodecParams *params)
{
  GstElement *depayloader = gst_element_factory_make ("rtpvp8depay", NULL);
  GstElement *decoder     = gst_element_factory_make ("vp8dec", NULL);
  GstElement *converter   = gst_element_factory_make ("videoconvert", NULL);

  gst_bin_add_many (decoderBin, depayloader, decoder, converter, NULL);
  gst_element_link_many (depayloader, decoder, converter, NULL);

//    gst_bin_add_many (decoderBin, depayloader, decoder, NULL);
//      gst_element_link_many (depayloader, decoder, NULL);

  setup_ghost_sink (depayloader, decoderBin);
  setup_ghost_src  (converter, decoderBin);
//  setup_ghost_src  (decoder, decoderBin);

}

static void _setup_vp9_decoder(GstBin* decoderBin, CodecParams *params)
{
  GstElement *depayloader = gst_element_factory_make ("rtpvp9depay", NULL);
  GstElement *decoder     = gst_element_factory_make ("vp9dec", NULL);
  GstElement *converter   = gst_element_factory_make ("videoconvert", NULL);

  gst_bin_add_many (decoderBin, depayloader, decoder, converter, NULL);
  gst_element_link_many (depayloader, decoder, converter, NULL);

  setup_ghost_sink (depayloader, decoderBin);
  setup_ghost_src  (converter, decoderBin);

}

static void _setup_theora_decoder(GstBin* decoderBin, CodecParams *params)
{
  GstElement *depayloader = gst_element_factory_make ("rtptheoradepay", NULL);
  GstElement *decoder = gst_element_factory_make ("theoradec", NULL);
  GstElement *converter = gst_element_factory_make ("videoconvert", NULL);

  gst_bin_add_many (decoderBin, depayloader, decoder, converter, NULL);
  gst_element_link_many (depayloader, decoder, converter, NULL);

  setup_ghost_sink (depayloader, decoderBin);
  setup_ghost_src  (converter, decoderBin);
}
