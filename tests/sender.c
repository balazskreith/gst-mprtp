#include <gst/gst.h>
#include <gst/rtp/rtp.h>

static void _setup_rtpsimple_sender(GstBin* senderBin, SenderParams *params);


Sender* sender_ctor(void)
{
  Sender* this;

  this = g_malloc0(sizeof(Sender));

  return this;
}

void sender_dtor(Sender* this)
{
  g_free(this);
}


Sender* make_sender(SenderParams *params)
{
  Sender* this = g_malloc0(sizeof(Source));
  GstBin* sourceBin     = GST_BIN(gst_bin_new(NULL));

  switch(params->type){
    case TRANSFER_TYPE_RTPSIMPLE:
      _setup_rtpsimple_sender(sourceBin, params);
      break;
  };

  this->element = GST_ELEMENT(sourceBin);
  return this;
}

void _setup_rtpsimple_sender(GstBin* senderBin, SenderParams *params)
{

}


