#include <gst/gst.h>
#include <gst/rtp/rtp.h>
#include "pipeline.h"

typedef struct{
  GstElement* element;
}Sender;


Sender* sender_ctor(void);
void sender_dtor(Sender* this);
Sender* make_sender(SndPacketScheduler* cc, StatParamsTuple* stat_params_tuple, SndTransferParams *transfer);
