#ifndef TESTS_RTPSTATMAKER_H_
#define TESTS_RTPSTATMAKER_H_

#include <gst/gst.h>
#include "pipeline.h"

typedef struct{
  ObjectsHolder* holder;
  GstElement*    element;
  gchar          bin_name[256];
}RTPStatMaker;

RTPStatMaker* rtpstatmaker_ctor(void);
void rtpstatmaker_dtor(RTPStatMaker* this);
GstElement* make_rtpstatmaker_element(StatParams* stat_params);

#endif /* TESTS_RTPSTATMAKER_H_ */




