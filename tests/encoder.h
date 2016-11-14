#ifndef TESTS_ENCODER_H_
#define TESTS_ENCODER_H_

#include <gst/gst.h>
#include <string.h>

#include "pipeline.h"

typedef struct{
  GstElement*    element;
  ObjectsHolder* objects_holder;
  gchar          bin_name[256];

  Subscriber       on_bitrate_change;
}Encoder;

Encoder* encoder_ctor(void);
void encoder_dtor(Encoder* this);
Encoder* make_encoder(CodecParams *codec_params, SinkParams* encodersink_params);

Subscriber* encoder_get_on_bitrate_change_subscriber(Encoder* this);

#endif /* TESTS_SINK_H_ */
