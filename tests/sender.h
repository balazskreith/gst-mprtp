#include <gst/gst.h>
#include <gst/rtp/rtp.h>
#include "pipeline.h"

#ifndef TESTS_SENDER_H_
#define TESTS_SENDER_H_

typedef struct{
  GstElement*     element;
  ObjectsHolder*  objects_holder;
  gchar           bin_name[256];
  TransferParams* transfer_params;

  Eventer*       on_bitrate_change;
  gpointer        priv;
}Sender;


Sender* sender_ctor(void);
void sender_dtor(Sender* this);
Sender* make_sender(SchedulerParams* cc,
    StatParamsTuple* stat_params_tuple,
    TransferParams *transfer,
    ExtraDelayParams* extra_delay_params);

Eventer* sender_get_on_bitrate_change_eventer(Sender* this);

GstElement* sender_get_mprtcp_rr_sink_element(Sender* this);

#endif //TESTS_SENDER_H_
