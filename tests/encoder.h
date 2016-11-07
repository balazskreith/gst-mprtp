#ifndef TESTS_ENCODER_H_
#define TESTS_ENCODER_H_

#include <gst/gst.h>
#include <string.h>

#include "pipeline.h"

typedef struct{
  GstElement*    element;
  Notifier*      on_bitrate_chage;
  ObjectsHolder* objects_holder;
  gchar          bin_name[256];
}Encoder;

Encoder* encoder_ctor(void);
void encoder_dtor(Encoder* this);
Encoder* make_encoder(CodecParams *codec_params, SinkParams* encodersink_params);
void encoder_on_bitrate_change(Encoder* this, gint32* target_bitrate);

#endif /* TESTS_SINK_H_ */
