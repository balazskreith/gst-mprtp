#ifndef TESTS_DECODER_H_
#define TESTS_DECODER_H_

#include <gst/gst.h>
#include <string.h>

#include "pipeline.h"

typedef struct{
  GstElement*    element;
  gchar          bin_name[256];
}Decoder;

Decoder* decoder_ctor(void);
void decoder_dtor(Decoder* this);
Decoder* make_decoder(CodecParams *params);


#endif /* TESTS_DECODER_H_ */
