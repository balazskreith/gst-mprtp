#include <gst/gst.h>
#include <gst/rtp/rtp.h>
#include "sender.h"

static GstElement* _make_rtpsimple_sender(SenderParams *params);


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
  Sender* this = sender_ctor();
  GstBin* senderBin     = GST_BIN(gst_bin_new(NULL));
  GstElement* sender;

  switch(params->type){
    case TRANSFER_TYPE_RTPSIMPLE:
      sender = _make_rtpsimple_sender(params);
      break;
  };

  gst_bin_add_many(senderBin,

      sender,

      NULL
  );

  this->element = GST_ELEMENT(senderBin);

  setup_ghost_sink(sender, senderBin);

  return this;
}

GstElement* _make_rtpsimple_sender(SenderParams *params)
{
  GstBin *senderBin    = GST_BIN (gst_bin_new (NULL));
  GstElement *rtpSink  = gst_element_factory_make ("udpsink", NULL);
  gchar *padName;

  gst_bin_add_many (senderBin, rtpSink, NULL);

  g_object_set (rtpSink,
      "port", params->dest_port,
      "host", params->dest_ip,
//      "sync", FALSE,
//      "async", FALSE,
      NULL);

  setup_ghost_sink(rtpSink, senderBin);
  return GST_ELEMENT(senderBin);
}


