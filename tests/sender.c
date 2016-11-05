#include <gst/gst.h>
#include <gst/rtp/rtp.h>
#include "sender.h"
#include "rtpstatmaker.h"

static GstElement* _make_rtp_sender(SndTransferParams *params);
static GstElement* _make_mprtp_sender(SndTransferParams *params);


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


Sender* make_sender(CCSenderSideParams* cc, StatParamsTuple* stat_params_tuple, SndTransferParams *transfer)
{
  Sender* this = sender_ctor();
  GstBin* senderBin     = GST_BIN(gst_bin_new(NULL));
  GstElement *element, *sink;

  switch(transfer->type){
    case TRANSFER_TYPE_RTP:
      sink = element = _make_rtp_sender(transfer);
      break;
    case TRANSFER_TYPE_MPRTP:
      sink = element = _make_mprtp_sender(transfer);
      break;
  };
  gst_bin_add(senderBin, element);

  if(stat_params_tuple){
    element = make_rtpstatmaker(stat_params_tuple);
    gst_bin_add(senderBin, element);
    gst_element_link_pads(element, "src", sink, "sink");
    sink = element;
  }


  if(cc){
    switch(cc->type){
      case CONGESTION_CONTROLLER_TYPE_SCREAM:
        //TODO: add scream
        break;
      case CONGESTION_CONTROLLER_TYPE_FBRAPLUS:
        //TODO: add fbra+
        break;
    };
    gst_bin_add(senderBin, element);
  }


  this->element = GST_ELEMENT(senderBin);
  setup_ghost_sink(sink, senderBin);

  return this;
}


static GstElement* _make_rtpsink(gchar* dest_ip, guint16 dest_port){
  GstElement *rtpSink  = gst_element_factory_make ("udpsink", NULL);

  g_object_set (rtpSink,
        "port", dest_port,
        "host", dest_ip,
//        "sync", FALSE,
//        "async", FALSE,
        NULL);

  {
    gint32 port;
    gchar ip[256];
    g_object_get(rtpSink, "port", &port);
    g_object_get(rtpSink, "host", ip);
    g_print("Port: %s:%hu", dest_ip, port);
  }

//
//  GstElement *rtpSink  = gst_element_factory_make ("fakesink", NULL);
//  g_object_set (rtpSink,
//        "dump", TRUE,
//        NULL);
  return rtpSink;

}

GstElement* _make_rtp_sender(SndTransferParams *params)
{
  return _make_rtpsink(params->rtp.dest_ip, params->rtp.dest_port);
}

GstElement* _make_mprtp_sender(SndTransferParams *params)
{
  GstBin *senderBin    = GST_BIN (gst_bin_new (NULL));
  GstElement* mprtpSnd = gst_element_factory_make ("mprtpsender", NULL);
  GSList *item;

  gst_bin_add(senderBin, mprtpSnd);

  for(item = params->mprtp.subflows; item; item = item->next){
    gchar padName[255];
    SenderSubflow* subflow = item->data;
    GstElement *rtpSink;

    memset(padName, 0, 255);
    sprintf(padName, "src_%d", subflow->id);
    rtpSink = _make_rtpsink(subflow->dest_ip, subflow->dest_port);
    gst_element_link_pads(mprtpSnd, padName, rtpSink, "sink");
    gst_bin_add(senderBin, rtpSink);
  }

  setup_ghost_sink_by_padnames(mprtpSnd, "mprtp_sink", senderBin, "sink");
  return GST_ELEMENT(senderBin);
}


