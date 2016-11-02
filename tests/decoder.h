#ifndef TESTS_ENCODER_H_
#define TESTS_ENCODER_H_

#include <gst/gst.h>
#include <string.h>

#include "pipeline.h"

typedef struct{
  GstElement* element;
}Decoder;

Decoder* decoder_ctor(void);
void decoder_dtor(Decoder* this);
Decoder* make_decoder(CodecParams *params);


#endif /* TESTS_SINK_H_ */
