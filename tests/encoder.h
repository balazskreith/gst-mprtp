#ifndef TESTS_ENCODER_H_
#define TESTS_ENCODER_H_

#include <gst/gst.h>
#include <string.h>

#include "pipeline.h"

typedef struct{
  GstElement* element;
}Encoder;

Encoder* encoder_ctor(void);
void encoder_dtor(Encoder* this);
Encoder* make_encoder(CodecParams *params);


#endif /* TESTS_SINK_H_ */
