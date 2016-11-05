#include <gst/gst.h>
#include <gst/rtp/rtp.h>
#include "receiver.h"
#include "rtpstatmaker.h"

GstElement* _make_rtp_receiver(RcvTransferParams *params);
GstElement* _make_mprtp_receiver(RcvTransferParams *params);
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


Receiver* make_receiver(CCReceiverSideParams *cc_receiver_side_params,
                        StatParamsTuple* stat_params_tuple,
                        RcvTransferParams* rcv_transfer_params)
{
  Receiver* this = receiver_ctor();
  GstBin* receiverBin     = GST_BIN(gst_bin_new(NULL));
  GstElement* element = NULL;
  GstElement* rtpstatmakerSrc = NULL;
  GstElement* src = NULL;

  switch(rcv_transfer_params->type){
    case TRANSFER_TYPE_RTP:
      src = element = _make_rtp_receiver(rcv_transfer_params);
    break;
    case TRANSFER_TYPE_MPRTP:
      src = element = _make_mprtp_receiver(rcv_transfer_params);
    break;
  };
  gst_bin_add(receiverBin, element);

  if(stat_params_tuple){
    if(cc_receiver_side_params){
      rtpstatmakerSrc = make_transparent_rtpstatmaker(stat_params_tuple->stat_params);
    }else{
      rtpstatmakerSrc = make_rtpstatmaker(stat_params_tuple);
    }
    element = rtpstatmakerSrc;

    gst_bin_add(receiverBin, element);
    gst_element_link_pads(src, "src", element, "sink");
    src = element;
  }


  if(cc_receiver_side_params){
    switch(cc_receiver_side_params->type){
      case CONGESTION_CONTROLLER_TYPE_SCREAM:
        //TODO: add scream
        break;
      case CONGESTION_CONTROLLER_TYPE_FBRAPLUS:
        //TODO: add fbra+
        break;
    };
//    gst_bin_add(receiverBin, element);
  }

  if(stat_params_tuple && cc_receiver_side_params){
    element = make_rtpstatmaker(stat_params_tuple);

    gst_bin_add(receiverBin, element);
    gst_element_link_pads(rtpstatmakerSrc, "packet_src", element, "packet_sink");
    gst_element_link_pads(src, "src", element, "sink");
    src = element;
  }


  setup_ghost_src(src, receiverBin);

  this->element = GST_ELEMENT(receiverBin);

  return this;
}


static GstElement* _make_rtpsrc(guint16 bound_port){
  GstElement *rtpSrc   = gst_element_factory_make ("udpsrc", NULL);

  g_object_set (rtpSrc, "port", bound_port, NULL);
//  return rtpSrc;
  return debug_by_src(rtpSrc);
}

GstElement* _make_rtp_receiver(RcvTransferParams *params)
{
  return _make_rtpsrc(params->rtp.bound_port);
}

GstElement* _make_mprtp_receiver(RcvTransferParams *params)
{
  GstBin *receiverBin    = GST_BIN (gst_bin_new (NULL));
  GstElement* mprtpRcv = gst_element_factory_make ("mprtpreceiver", NULL);
  GSList *item;

  gst_bin_add(receiverBin, mprtpRcv);

  for(item = params->mprtp.subflows; item; item = item->next){
    gchar padName[255];
    ReceiverSubflow* subflow = item->data;
    GstElement *rtpSrc;

    memset(padName, 0, 255);
    rtpSrc = _make_rtpsrc(subflow->bound_port);
    sprintf(padName, "sink_%d", subflow->id);
    gst_element_link_pads(rtpSrc, "src", mprtpRcv, padName);
    gst_bin_add(receiverBin, rtpSrc);
  }

  setup_ghost_src_by_padnames(mprtpRcv, "mprtp_src", receiverBin, "src");
  return GST_ELEMENT(receiverBin);
}


