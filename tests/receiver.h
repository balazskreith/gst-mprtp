#include <gst/gst.h>
#include <gst/rtp/rtp.h>
#include "pipeline.h"

typedef struct{
  GstElement*    element;
  Notifier*      on_caps_change;
  ObjectsHolder* objects_holder;
  gchar          bin_name[256];
}Receiver;


Receiver* receiver_ctor(void);
void receiver_dtor(Receiver* this);
Receiver* make_receiver(RcvPlayouterParams *cc_receiver_side_params, StatParamsTuple* stat_params_tuple, RcvTransferParams* rcv_transfer_params);

void receiver_on_caps_change(Receiver* this, const GstCaps* caps);
