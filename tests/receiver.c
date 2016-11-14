#include <gst/gst.h>
#include <gst/rtp/rtp.h>
#include "receiver.h"
#include "rtpstatmaker.h"
#include "sender.h"

typedef struct{
    GstBin*     mprtpRcvBin;
    GstElement* mprtpRcv;
}Private;

#define _priv(this) ((Private*)(this->priv))
static GstElement* _make_rtpsrc(Receiver* this, guint16 bound_port);
static GstElement* _make_rtp_receiver(Receiver* this,   TransferParams *params);
static GstElement* _make_mprtp_receiver(Receiver* this, TransferParams *params);
static GstElement* _get_mprtcp_sr_src_element(Receiver* this);
static void _setup_mprtcp_pads(Receiver* this, TransferParams* transfer_params);
static GstElement* _make_mprtp_playouter(Receiver* this, TransferParams *transfer_params);
static GstElement* _make_mprtp_controller(Receiver* this, TransferParams *transfer_params);
static GstElement* _make_mprtp_fractal_controller(Receiver* this,
    PlayouterParams *scheduler_params, TransferParams* snd_transfer_params);

static int _instance_counter = 0;


Receiver* receiver_ctor(void)
{
  Receiver* this;

  this = g_malloc0(sizeof(Receiver));
  this->priv = g_malloc0(sizeof(Private));
  sprintf(this->bin_name, "ReceiverBin_%d", _instance_counter++);
  this->on_caps_change = make_eventer("on-caps-change");
  this->objects_holder = objects_holder_ctor();
  return this;
}

void receiver_dtor(Receiver* this)
{
  object_holder_dtor(this->objects_holder);
  eventer_unref(this->on_caps_change);
  g_free(this->priv);
  g_free(this);
}


//
//
//GstElement* _make_mprtp_fractal_controller(Receiver* this, PlayouterParams* playouter_params, TransferParams* rcv_transfer_params)
//{
//  GstBin*     plyBin   = gst_bin_new(NULL);
//  guint       rtpPort        = 5000;
//  guint       rtcpPort       = 5001;
//  GstElement* mprtpPly       = _make_mprtp_playouter(this, rcv_transfer_params);
//  GstElement* rtcpSink       = gst_element_factory_make("udpsink", "RTCPSink:10.0.0.1:5001");
//  GstElement* rtcpSrc        = gst_element_factory_make("udpsrc", "RTCPSrc:5001");
//  GstElement* rtpSrc         = _make_rtpsrc(this, rtpPort);
//  GstElement* mprtpreceiver  = gst_element_factory_make("mprtpreceiver", "MPRTPReceiver");
//  GstElement* mprtpsender    = gst_element_factory_make("mprtpsender", "MPRTPSender");
//
//  gst_bin_add_many(plyBin,
//      mprtpPly,
//      rtcpSink,rtcpSrc,
////      sender->element,
////      sender,
//      NULL
//  );
//
//  g_object_set(rtcpSink, "host", "10.0.0.1", "port", 5001, "sync", FALSE, "async", FALSE, NULL);
//  g_object_set(rtcpSrc, "port", 5001, NULL);
////  g_object_set(fakesink, "dump", TRUE, NULL);
//  gst_element_link_pads(rtcpSrc, "src", mprtpPly, "mprtcp_sr_sink");
////  gst_element_link_pads(rtcpSrc, "src", fakesink, "sink");
//  gst_element_link_pads(mprtpPly, "mprtcp_rr_src", rtcpSink, "sink");
//
//  g_object_set(mprtpPly,
//      "controlling-mode", 2,
//      "rtcp-interval-type", 2,
//      "max-repair-delay", 10,
//      "enforced-delay", 0,
//      NULL
//  );
//
////  _setup_mprtcp_pads(this, rcv_transfer_params);
//
////  objects_holder_add(this->objects_holder, sender, (GDestroyNotify) sender_dtor);
////  gst_element_link_pads(mprtpPly, "mprtcp_rr_src", sender_get_mprtcp_rr_sink_element(sender), "mprtcp_rr_sink");
////  gst_element_link_pads(mprtpPly, "mprtcp_rr_src", sender, "mprtcp_rr_sink");
//
//  setup_ghost_sink_by_padnames(mprtpPly, "mprtp_sink", plyBin, "sink");
//  setup_ghost_src_by_padnames(mprtpPly,  "mprtp_src", plyBin, "src");
////  setup_ghost_sink_by_padnames(mprtpPly,  "mprtcp_sr_sink", plyBin, "mprtcp_sr_sink");
//  return GST_ELEMENT(plyBin);
//}


Receiver* make_receiver(TransferParams* rcv_transfer_params,
                        StatParamsTuple* stat_params_tuple,
                        PlayouterParams *playouter_params)
{
  Receiver* this = receiver_ctor();
  GstBin* receiverBin          = GST_BIN(gst_bin_new(this->bin_name));

  GstElement* receiver         = NULL;
  GstElement* playouter        = NULL;
  GstElement* rtpstatmakerSrc  = NULL;
  GstElement* src              = NULL;

  switch(rcv_transfer_params->type){
    case TRANSFER_TYPE_RTP:
      receiver = _make_rtp_receiver(this, rcv_transfer_params);
      gst_bin_add(receiverBin, receiver);
    break;
    case TRANSFER_TYPE_MPRTP:
      receiver = _make_mprtp_receiver(this, rcv_transfer_params);
      gst_bin_add(receiverBin, receiver);
    break;
  };
  src = receiver;

  if(stat_params_tuple){
    if(playouter_params){
      rtpstatmakerSrc = make_rtpstatmaker_element(stat_params_tuple->stat_params);
    }else{
      RTPStatMaker* rtpstatmaker = make_rtpstatmaker(stat_params_tuple);
      rtpstatmakerSrc = rtpstatmaker->element;
      objects_holder_add(this->objects_holder, rtpstatmaker, (GDestroyNotify)rtpstatmaker_dtor);
    }

    gst_bin_add(receiverBin, rtpstatmakerSrc);
    gst_element_link_pads(src, "src", rtpstatmakerSrc, "sink");
    src = rtpstatmakerSrc;
  }


  if(playouter_params){
    switch(playouter_params->type){
      case TRANSFER_CONTROLLER_TYPE_SCREAM:
        break;
      case TRANSFER_CONTROLLER_TYPE_MPRTP:
        playouter = _make_mprtp_controller(this, rcv_transfer_params);
        gst_bin_add(receiverBin, playouter);
        break;
      case TRANSFER_CONTROLLER_TYPE_MPRTPRRACTAL:
        playouter = _make_mprtp_fractal_controller(this, playouter_params, rcv_transfer_params);
        gst_bin_add(receiverBin, playouter);
        gst_element_link_pads(_get_mprtcp_sr_src_element(this), "mprtcp_sr_src", playouter, "mprtcp_sr_sink");
        break;
    };
    gst_element_link_pads(src, "src", playouter, "sink");
    src = playouter;
  }

  if(stat_params_tuple && playouter_params){
    RTPStatMaker* rtpstatmaker = make_rtpstatmaker(stat_params_tuple);
    objects_holder_add(this->objects_holder, rtpstatmaker, (GDestroyNotify)rtpstatmaker_dtor);

    gst_bin_add(receiverBin, rtpstatmaker->element);
    gst_element_link_pads(rtpstatmakerSrc, "packet_src", rtpstatmaker->element, "packet_sink");
    gst_element_link_pads(src, "src", rtpstatmaker->element, "sink");
    src = rtpstatmaker->element;
  }


  setup_ghost_src(src, receiverBin);

  this->element = GST_ELEMENT(receiverBin);

  return this;
}




Receiver* make_receiver_custom(void)
{
  Receiver* this = receiver_ctor();
  GstBin* receiverBin          = GST_BIN(gst_bin_new(this->bin_name));
  GstElement* mprtpreceiver    = gst_element_factory_make("mprtpreceiver",  "RcvMPRTPReceiver");
  GstElement* rtpSrc           = gst_element_factory_make("udpsrc",         "RcvRTPSrc:5000");
  GstElement* rtcpSrc          = gst_element_factory_make("udpsrc",         "RcvRTCPSrc:5001");
  GstElement* mprtpsender      = gst_element_factory_make("mprtpsender",    "RcvMPRTPSender");
  GstElement* rtcpSink         = gst_element_factory_make("udpsink",        "RcvRTCPSink:10.0.0.1:5001");
  GstElement* playouter        = gst_element_factory_make("mprtpplayouter", "RcvMPRTPPlayouter");

  gst_bin_add_many(receiverBin,
      mprtpreceiver,
      rtpSrc,
      rtcpSrc,
      mprtpsender,
      rtcpSink,
      playouter,
      NULL);

//  g_object_set(G_OBJECT(mprtpreceiver), "", NULL);
  g_object_set(G_OBJECT(rtpSrc),
      "port", 5000,
      "caps", gst_caps_new_simple ("application/x-rtp",
                "media", G_TYPE_STRING, "video",
                "clock-rate", G_TYPE_INT, 90000,
                "encoding-name", G_TYPE_STRING, "VP8",
                NULL),
      NULL);

  g_object_set(G_OBJECT(rtcpSrc), "port", 5001, NULL);
//  g_object_set(G_OBJECT(mprtpsender), NULL);
  g_object_set(G_OBJECT(rtcpSink), "host", "10.0.0.1", "port", 5001, "sync", FALSE, "async", FALSE, NULL);
  g_object_set(G_OBJECT(playouter),
      "join-subflow", 1,
      "controlling-mode", 2,
      "rtcp-interval-type", 2,
      "max-repair-delay", 10,
      "enforced-delay", 0,
      NULL);

  gst_element_link_pads(rtpSrc,        "src",           mprtpreceiver, "sink_1");
  gst_element_link_pads(rtcpSrc,       "src",           mprtpreceiver, "mprtcp_sink_1");
  gst_element_link_pads(mprtpreceiver, "mprtp_src",     playouter,     "mprtp_sink");
  gst_element_link_pads(mprtpreceiver, "mprtcp_sr_src", playouter,     "mprtcp_sr_sink");
  gst_element_link_pads(playouter,     "mprtcp_rr_src", mprtpsender,   "mprtcp_rr_sink");
  gst_element_link_pads(mprtpsender,   "mprtcp_src_1",  rtcpSink,      "sink");

  setup_ghost_src_by_padnames(playouter, "mprtp_src", receiverBin, "src");
  this->element = GST_ELEMENT(receiverBin);

  return this;
}

void receiver_on_caps_change(Receiver* this, const GstCaps* caps)
{
  eventer_do(this->on_caps_change, caps);
}

static void _on_rtpSrc_caps_change(GstElement* rtpSrc, const GstCaps* caps)
{
  g_print("On rtpSrc caps canged called\n");
  g_object_set(G_OBJECT(rtpSrc), "caps", caps, NULL);

}

GstElement* _make_rtpsrc(Receiver* this, guint16 bound_port)
{

  gchar name[256];
  sprintf(name, "UDP Source:%hu", bound_port);
  GstElement *rtpSrc   = gst_element_factory_make ("udpsrc", name);
  g_object_set (rtpSrc, "port", bound_port, NULL);

  g_print("UdpSrc is bound to %hu\n", bound_port);
  eventer_add_subscriber_full(this->on_caps_change, (subscriber) _on_rtpSrc_caps_change, rtpSrc);

//  return debug_by_src(rtpSrc);
  return rtpSrc;
}


GstElement* receiver_get_mprtcp_rr_src_element(Receiver* this)
{
  GstBin*     mprtpRcvBin = _priv(this)->mprtpRcvBin;
  GstElement* mprtpRcv    = _priv(this)->mprtpRcv;

  setup_ghost_src_by_padnames(mprtpRcv, "mprtcp_rr_src", mprtpRcvBin, "mprtcp_rr_src");
  setup_ghost_src_by_padnames(GST_ELEMENT(mprtpRcvBin), "mprtcp_rr_src", this->element, "mprtcp_rr_src");

  return this->element;
}


GstElement* _get_mprtcp_sr_src_element(Receiver* this)
{
  GstBin*     mprtpRcvBin = _priv(this)->mprtpRcvBin;
  GstElement* mprtpRcv    = _priv(this)->mprtpRcv;

  setup_ghost_src_by_padnames(mprtpRcv, "mprtcp_sr_src", mprtpRcvBin, "mprtcp_sr_src");

  return GST_ELEMENT(mprtpRcvBin);
}


void _setup_mprtcp_pads(Receiver* this, TransferParams* transfer_params)
{

  GstBin*     mprtpRcvBin = _priv(this)->mprtpRcvBin;
  GstElement* mprtpRcv    = _priv(this)->mprtpRcv;
  GSList* item;

  for(item = transfer_params->subflows; item; item = item->next){
    gchar padName[255];
    ReceiverSubflow* subflow = item->data;
    GstElement *rtpSrc;
    gchar       udpsrc_name[255];

    memset(padName, 0, 255);
    memset(udpsrc_name, 0, 255);
    sprintf(udpsrc_name, "UDP Source:%hu", subflow->bound_port + 1);
    sprintf(padName, "mprtcp_sink_%d", subflow->id);
    rtpSrc = gst_element_factory_make("udpsrc", udpsrc_name);

    g_object_set(rtpSrc, "port", subflow->bound_port + 1, NULL);
    gst_bin_add(mprtpRcvBin, rtpSrc);
    gst_element_link_pads(rtpSrc, "src", mprtpRcv, padName);
  }
}


GstElement* _make_rtp_receiver(Receiver* this, TransferParams *params)
{
  ReceiverSubflow* subflow = params->subflows->data;
  return _make_rtpsrc(this, subflow->bound_port);
}

GstElement* _make_mprtp_receiver(Receiver* this, TransferParams *params)
{
  GstBin *receiverBin    = GST_BIN (gst_bin_new (NULL));
  GstElement* mprtpRcv = gst_element_factory_make ("mprtpreceiver", NULL);
  GSList *item;

  gst_bin_add(receiverBin, mprtpRcv);
  for(item = params->subflows; item; item = item->next){
    gchar padName[255];
    ReceiverSubflow* subflow = item->data;
    GstElement *rtpSrc;

    memset(padName, 0, 255);
    rtpSrc = _make_rtpsrc(this, subflow->bound_port);
    sprintf(padName, "sink_%d", subflow->id);
    gst_bin_add(receiverBin, rtpSrc);
    gst_element_link_pads(rtpSrc, "src", mprtpRcv, padName);
  }

  setup_ghost_src_by_padnames(mprtpRcv, "mprtp_src", receiverBin, "src");
  _priv(this)->mprtpRcv    = mprtpRcv;
  _priv(this)->mprtpRcvBin = receiverBin;
//  setup_ghost_src_by_padnames(mprtpRcv, "mprtcp_sr_src", receiverBin, "mprtcp_sr_src");
//  setup_ghost_src_by_padnames(mprtpRcv, "mprtcp_rr_src", receiverBin, "mprtcp_rr_src");
  return GST_ELEMENT(receiverBin);
}


GstElement*  _make_mprtp_playouter(Receiver* this, TransferParams *transfer_params)
{
  GstElement* mprtpPly = gst_element_factory_make("mprtpplayouter", NULL);
  GSList* item;

  for(item = transfer_params->subflows; item; item = item->next){
    gchar padName[255];
    SenderSubflow* subflow = item->data;
    g_object_set(mprtpPly, "join-subflow", subflow->id, NULL);
    g_print("Joining subflow: %d\n", subflow->id);
  }

  return mprtpPly;
}


GstElement* _make_mprtp_controller(Receiver* this, TransferParams* rcv_transfer_params)
{
  GstBin*     plyBin   = gst_bin_new(NULL);
  GstElement* mprtpPly =  _make_mprtp_playouter(this, rcv_transfer_params);

  gst_bin_add_many(plyBin,
      mprtpPly,
      NULL
  );

  g_object_set(mprtpPly,
      "controlling-mode", 0,
      "rtcp-interval-type", 0,
      "max-repair-delay", 0,
      "enforced-delay", 0,
      NULL
  );

  setup_ghost_sink_by_padnames(mprtpPly, "mprtp_sink", plyBin, "sink");
  setup_ghost_src_by_padnames(mprtpPly,  "mprtp_src", plyBin, "src");
  return GST_ELEMENT(plyBin);
}

GstElement* _make_mprtp_fractal_controller(Receiver* this, PlayouterParams* playouter_params, TransferParams* rcv_transfer_params)
{
  GstBin*     plyBin   = gst_bin_new(NULL);
  GstElement* mprtpPly = _make_mprtp_playouter(this, rcv_transfer_params);
  Sender*     sender   = make_sender(NULL, NULL, playouter_params->snd_transfer_params);

  gst_bin_add_many(plyBin,
      mprtpPly,
      sender->element,
      NULL
  );

  g_object_set(mprtpPly,
      "controlling-mode", 2,
      "rtcp-interval-type", 2,
      "max-repair-delay", 10,
      "enforced-delay", 0,
      NULL
  );

  _setup_mprtcp_pads(this, rcv_transfer_params);

  objects_holder_add(this->objects_holder, sender, (GDestroyNotify) sender_dtor);
  gst_element_link_pads(mprtpPly, "mprtcp_rr_src", sender_get_mprtcp_rr_sink_element(sender), "mprtcp_rr_sink");

  setup_ghost_sink_by_padnames(mprtpPly, "mprtp_sink", plyBin, "sink");
  setup_ghost_src_by_padnames(mprtpPly,  "mprtp_src", plyBin, "src");
  setup_ghost_sink_by_padnames(mprtpPly,  "mprtcp_sr_sink", plyBin, "mprtcp_sr_sink");
  return GST_ELEMENT(plyBin);
}


