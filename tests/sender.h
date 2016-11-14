#include <gst/gst.h>
#include <gst/rtp/rtp.h>
#include "pipeline.h"

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
Sender* make_sender(SchedulerParams* cc, StatParamsTuple* stat_params_tuple, TransferParams *transfer);

Eventer* sender_get_on_bitrate_change_eventer(Sender* this);
Sender* make_sender_custom(void);

GstElement* sender_get_mprtcp_rr_sink_element(Sender* this);
