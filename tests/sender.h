#include <gst/gst.h>
#include <gst/rtp/rtp.h>
#include "pipeline.h"

typedef struct{
  GstElement*    element;
  ObjectsHolder* objects_holder;
  gchar          bin_name[256];
}Sender;


Sender* sender_ctor(void);
void sender_dtor(Sender* this);
Sender* make_sender(SndPacketScheduler* cc, StatParamsTuple* stat_params_tuple, SndTransferParams *transfer);
