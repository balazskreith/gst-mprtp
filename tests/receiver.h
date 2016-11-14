#include <gst/gst.h>
#include <gst/rtp/rtp.h>
#include "pipeline.h"

typedef struct{
  GstElement*     element;
  Eventer*      on_caps_change;
  ObjectsHolder*  objects_holder;
  gchar           bin_name[256];
  TransferParams* transfer_params;

  gpointer         priv;
}Receiver;


Receiver* receiver_ctor(void);
void receiver_dtor(Receiver* this);
Receiver* make_receiver(TransferParams* rcv_transfer_params,
    StatParamsTuple* stat_params_tuple,
    PlayouterParams *playouter_params);
Receiver* make_receiver_custom(void);

void receiver_on_caps_change(Receiver* this, const GstCaps* caps);

GstElement* receiver_get_mprtcp_rr_src_element(Receiver* this);
