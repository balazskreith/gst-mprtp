#ifndef TESTS_SOURCE_H_
#define TESTS_SOURCE_H_

#include <gst/gst.h>
#include <string.h>

#include "pipeline.h"

typedef struct{
  GstElement* element;
}Source;


Source* source_ctor(void);
void source_dtor(Source* this);
Source* make_source(SourceParams *params);

#endif /* TESTS_SINK_H_ */
