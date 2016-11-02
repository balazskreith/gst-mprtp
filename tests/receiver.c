#include <gst/gst.h>
#include <gst/rtp/rtp.h>

static void _setup_rtpsimple_receiver(GstBin* receiverBin, ReceiverParams *params);


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
  GstBin* sourceBin     = GST_BIN(gst_bin_new(NULL));

  switch(params->type){
    case TRANSFER_TYPE_RTPSIMPLE:
      _setup_rtpsimple_receiver(sourceBin, params);
      break;
  };

  this->element = GST_ELEMENT(sourceBin);
  return this;
}

void _setup_rtpsimple_receiver(GstBin* receiverBin, ReceiverParams *params)
{

}


