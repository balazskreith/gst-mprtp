#ifndef TESTS_SINK_H_
#define TESTS_SINK_H_

#include <gst/gst.h>
#include "pipeline.h"

typedef struct{
  GstElement* element;
  gchar       bin_name[256];
}Sink;

Sink* sink_ctor(void);
void sink_dtor(Sink* this);
Sink* make_sink(SinkParams *params);


#endif /* TESTS_SINK_H_ */




