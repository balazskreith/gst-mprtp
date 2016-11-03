#include <gst/gst.h>
#include <gst/rtp/rtp.h>
#include "receiver.h"

static GstElement* _make_rtpsimple_receiver(ReceiverParams *params);


Receiver* receiver_ctor(void)
{
  Receiver* this;

  this = g_malloc0(sizeof(Receiver));

  return this;
}

void receiver_dtor(Receiver* this)
{
  g_free(this);
}


Receiver* make_receiver(ReceiverParams *params)
{
  Receiver* this = receiver_ctor();
  GstBin* receiverBin     = GST_BIN(gst_bin_new(NULL));
  GstElement* receiver;
  GstElement* src;

  switch(params->type){
    case TRANSFER_TYPE_RTPSIMPLE:
      src = receiver = _make_rtpsimple_receiver(params);
      gst_bin_add(receiverBin, receiver);
      break;
  };

  setup_ghost_src(src, receiverBin);

  this->element = GST_ELEMENT(receiverBin);

  return this;
}

GstElement* _make_rtpsimple_receiver(ReceiverParams *params)
{
  GstElement *rtpSrc   = gst_element_factory_make ("udpsrc", NULL);
  g_object_set (rtpSrc, "port", params->bound_port, NULL);

  return rtpSrc;
}


