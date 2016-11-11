#include <gst/gst.h>
#include <gst/rtp/rtp.h>
#include "sender.h"
#include "rtpstatmaker.h"
#include "receiver.h"

typedef struct{
    GstBin*     mprtpSndBin;
    GstElement* mprtpSnd;
}Private;

#define _priv(this) ((Private*)(this->priv))

static GstElement* _make_rtp_sender(TransferParams *params);
static GstElement* _make_mprtp_sender(Sender* this, TransferParams *params);
static GstElement* _get_mprtcp_sr_sink_element(Sender* this);
static GstElement* _make_mprtp_scheduler(Sender* this, TransferParams* transfer_params);
static GstElement* _make_mprtp_controller(Sender* this, SchedulerParams* params, TransferParams* transfer_params);
static GstElement* _make_mprtp_fractal_controller(Sender* this,
    SchedulerParams *scheduler_params, TransferParams* snd_transfer_params);
static  GstElement* _make_mprtp_receiver(TransferParams *params);
static void _setup_mprtcp_pads(Sender* this, TransferParams* transfer_params);
static int _instance_counter = 0;


Sender* sender_ctor(void)
{
  Sender* this;

  this = g_malloc0(sizeof(Sender));
  this->priv = g_malloc0(sizeof(Private));
  this->objects_holder = objects_holder_ctor();
  sprintf(this->bin_name, "SenderBin_%d", _instance_counter++);

  return this;
}

void sender_dtor(Sender* this)
{

  object_holder_dtor(this->objects_holder);
  g_free(this->priv);
  g_free(this);
}


Sender* make_sender(SchedulerParams* scheduler_params, StatParamsTuple* stat_params_tuple, TransferParams *snd_transfer)
{
  Sender     *this = sender_ctor();
  GstBin     *senderBin = GST_BIN(gst_bin_new(this->bin_name));
  GstElement *scheduler, *statmaker, *sender, *sink;

  this->transfer_params = snd_transfer;

  //We go backwards of the listed params,
  //for determining the appropriate sink element
  //ghostly proxied to the bin.

  switch(snd_transfer->type){
    case TRANSFER_TYPE_RTP:
      sender = _make_rtp_sender(snd_transfer);
      gst_bin_add(senderBin, sender);
      break;
    case TRANSFER_TYPE_MPRTP:
      sender = _make_mprtp_sender(this, snd_transfer);
      gst_bin_add(senderBin, sender);
      break;
  };
  sink = sender;

  if(stat_params_tuple){
    RTPStatMaker* rtpstatmaker = make_rtpstatmaker(stat_params_tuple);
    statmaker = rtpstatmaker->element;
    objects_holder_add(this->objects_holder, rtpstatmaker, (GDestroyNotify)rtpstatmaker_dtor);
    gst_bin_add(senderBin, statmaker);
    gst_element_link_pads(statmaker, "src", sink, "sink");
    sink = statmaker;
  }

  if(scheduler_params){
    switch(scheduler_params->type){
      case TRANSFER_CONTROLLER_TYPE_SCREAM:
        //TODO: add scream
        break;
      case TRANSFER_CONTROLLER_TYPE_MPRTP:
        scheduler = _make_mprtp_controller(this, scheduler_params, snd_transfer);
        gst_bin_add(senderBin, scheduler);

        break;
      case TRANSFER_CONTROLLER_TYPE_MPRTPRRACTAL:
        scheduler = _make_mprtp_fractal_controller(this, scheduler_params, snd_transfer);
        gst_bin_add(senderBin, scheduler);
//        gst_element_link_pads(scheduler, "mprtcp_sr_src", _get_mprtcp_sr_sink_element(this), "mprtcp_sr_sink");
        break;
    };
    gst_element_link_pads(scheduler, "src", sink, "sink");
    sink = scheduler;
  }


  this->element = GST_ELEMENT(senderBin);
  setup_ghost_sink(sink, senderBin);

  return this;
}


static GstElement* _make_rtpsink(gchar* dest_ip, guint16 dest_port)
{
  gchar name[256];
  memset(name, 0, 256);
  sprintf(name, "UDP Sink %s:%hu", dest_ip, dest_port);
  GstElement *rtpSink  = gst_element_factory_make ("udpsink", name);

  //It is async, because RTCP must be async and at the receiver side the sinks
  //are used to forward RTCP
  g_object_set (rtpSink,
        "port", dest_port,
        "host", dest_ip,
        "sync", FALSE,
        "async", FALSE,
        NULL);
  g_print("UdpSink destination is %s:%hu\n", dest_ip, dest_port);
//  return debug_by_sink(rtpSink);
  return rtpSink;

}

GstElement* _get_mprtcp_sr_sink_element(Sender* this)
{
  GstBin*     mprtpSndBin = _priv(this)->mprtpSndBin;
  GstElement* mprtpSnd    = _priv(this)->mprtpSnd;

  setup_ghost_sink_by_padnames(mprtpSnd, "mprtcp_sr_sink", mprtpSndBin, "mprtcp_sr_sink");
  return GST_ELEMENT(mprtpSndBin);
}


GstElement* sender_get_mprtcp_rr_sink_element(Sender* this)
{
  GstBin*     mprtpSndBin = _priv(this)->mprtpSndBin;
  GstElement* mprtpSnd    = _priv(this)->mprtpSnd;

  setup_ghost_sink_by_padnames(mprtpSnd, "mprtcp_rr_sink", mprtpSndBin, "mprtcp_rr_sink");
  setup_ghost_sink_by_padnames(GST_ELEMENT(mprtpSndBin), "mprtcp_rr_sink", this->element, "mprtcp_rr_sink");

  return this->element;
}

void _setup_mprtcp_pads(Sender* this, TransferParams* transfer_params)
{
  GstBin*     mprtpSndBin = _priv(this)->mprtpSndBin;
  GstElement* mprtpSnd    = _priv(this)->mprtpSnd;
  GSList* item;

  for(item = transfer_params->subflows; item; item = item->next){
    gchar padName[255];
    SenderSubflow* subflow = item->data;
    GstElement *rtpSink;

    memset(padName, 0, 255);
    sprintf(padName, "mprtcp_src_%d", subflow->id);
    rtpSink = _make_rtpsink(subflow->dest_ip, subflow->dest_port + 1);
    gst_bin_add(mprtpSndBin, rtpSink);
    gst_element_link_pads(mprtpSnd, padName, rtpSink, "sink");
  }
}

GstElement* _make_rtp_sender(TransferParams *params)
{
  SenderSubflow* subflow = params->subflows->data;
  return _make_rtpsink(subflow->dest_ip, subflow->dest_port);
}

GstElement* _make_mprtp_sender(Sender* this, TransferParams *params)
{
  GstBin *senderBin    = GST_BIN (gst_bin_new (NULL));
  GstElement* mprtpSnd = gst_element_factory_make ("mprtpsender", NULL);
  GSList *item;

  gst_bin_add(senderBin, mprtpSnd);

  for(item = params->subflows; item; item = item->next){
    gchar padName[255];
    SenderSubflow* subflow = item->data;
    GstElement *rtpSink;

    memset(padName, 0, 255);
    sprintf(padName, "src_%d", subflow->id);
    rtpSink = _make_rtpsink(subflow->dest_ip, subflow->dest_port);
    gst_bin_add(senderBin, rtpSink);
    gst_element_link_pads(mprtpSnd, padName, rtpSink, "sink");
  }

  setup_ghost_sink_by_padnames(mprtpSnd, "mprtp_sink", senderBin, "sink");
  _priv(this)->mprtpSnd    = mprtpSnd;
  _priv(this)->mprtpSndBin = senderBin;
//  setup_ghost_sink_by_padnames(mprtpSnd, "mprtcp_sr_sink", senderBin, "mprtcp_sr_sink");
//  setup_ghost_sink_by_padnames(mprtpSnd, "mprtcp_rr_sink", senderBin, "mprtcp_rr_sink");
  return GST_ELEMENT(senderBin);
}

GstElement* _make_mprtp_scheduler(Sender* this, TransferParams* transfer_params)
{
  GstElement* mprtpSch = gst_element_factory_make("mprtpscheduler", NULL);
  GSList* item;

  for(item = transfer_params->subflows; item; item = item->next){
    gchar padName[255];
    SenderSubflow* subflow = item->data;
    g_object_set(mprtpSch, "join-subflow", subflow->id, NULL);
    g_print("Joining subflow: %d\n", subflow->id);
  }

  return mprtpSch;
}

GstElement* _make_mprtp_controller(Sender* this, SchedulerParams* params, TransferParams* transfer_params)
{
  GstBin*     schBin   = gst_bin_new(NULL);
  GstElement* mprtpSch = _make_mprtp_scheduler(this, transfer_params);
  GSList* item;
  for(item = transfer_params->subflows; item; item = item->next){
    SenderSubflow* subflow = item->data;
    guint32 subflow_id = subflow->id;
    guint32 target_bitrate = params->mprtp.subflows[subflow->id].target_bitrate;
    guint32 parameter_value = target_bitrate | subflow_id<<24;
    g_object_set(mprtpSch, "sending-target", parameter_value, NULL);
  }

  gst_bin_add_many(schBin,
      mprtpSch,
      NULL
  );

  g_object_set(mprtpSch,
      "controlling-mode", 0,
      "obsolation-treshold", 0,
      "rtcp-interval-type", 0,
      "report-timeout", 0,
      "fec-interval", 0,
      NULL
  );

  setup_ghost_sink_by_padnames(mprtpSch, "rtp_sink",       schBin, "sink");
  setup_ghost_src_by_padnames(mprtpSch,  "mprtp_src",      schBin, "src");
  return GST_ELEMENT(schBin);
}

static void
_mprtp_subflows_utilization (GstElement * mprtpSch, gpointer ptr)
{
  MPRTPPluginSignalData *signal = ptr;
//done:
//  g_print("HAHA: %d", signal->target_media_rate);
  return;
}

GstElement* _make_mprtp_fractal_controller(Sender* this, SchedulerParams *scheduler_params, TransferParams* snd_transfer_params)
{
  GstBin*     schBin   = gst_bin_new(NULL);
  GstElement* mprtpSch = _make_mprtp_scheduler(this, snd_transfer_params);
  GstElement* receiver = _make_mprtp_receiver(scheduler_params->rcv_transfer_params);
//  Receiver*   receiver = make_receiver(scheduler_params->rcv_transfer_params, NULL, NULL);
  GSList*     item;
//TODO: change it to pure udpsink and udpsrc element/.
  GstElement* rtcpSink = gst_element_factory_make("udpsink", "RTCPSink:10.0.0.6:5001");
  GstElement* rtcpSrc  = gst_element_factory_make("udpsrc", "RTCPSrc2:5001");

  gst_bin_add_many(schBin,
//      receiver->element,
//      receiver,
      rtcpSink,rtcpSrc,
      mprtpSch,
      NULL
  );

  g_object_set(rtcpSink, "host", "10.0.0.6", "port", 5001, "sync", FALSE, "async", FALSE, NULL);
  g_object_set(rtcpSrc, "port", 5001, NULL);
  gst_element_link_pads(rtcpSrc, "src", mprtpSch, "mprtcp_rr_sink");
  gst_element_link_pads(mprtpSch, "mprtcp_sr_src", rtcpSink, "sink");

  g_object_set(mprtpSch,
       "controlling-mode", 2,
       "obsolation-treshold", 0,
       "rtcp-interval-type", 2,
       "report-timeout", 0,
       "fec-interval", 0,
       NULL
   );

  g_signal_connect (mprtpSch, "mprtp-subflows-utilization", (GCallback) _mprtp_subflows_utilization, NULL);

//  _setup_mprtcp_pads(this, snd_transfer_params);

//  gst_element_link_pads(receiver_get_mprtcp_rr_src_element(receiver), "mprtcp_rr_src", mprtpSch, "mprtcp_rr_sink");
//  gst_element_link_pads(receiver, "mprtcp_rr_src", mprtpSch, "mprtcp_rr_sink");
//  objects_holder_add(this->objects_holder, receiver, (GDestroyNotify)receiver_dtor);

  setup_ghost_sink_by_padnames(mprtpSch, "rtp_sink",  schBin, "sink");
  setup_ghost_src_by_padnames(mprtpSch,  "mprtp_src", schBin, "src");
//  setup_ghost_src_by_padnames(mprtpSch,  "mprtcp_sr_src",  schBin, "mprtcp_sr_src");
  return GST_ELEMENT(schBin);

}


GstElement* _make_mprtp_receiver(TransferParams *params)
{
  GstBin *receiverBin    = GST_BIN (gst_bin_new (NULL));
  GstElement* mprtpRcv   = gst_element_factory_make ("mprtpreceiver", NULL);
  GSList *item;

  gst_bin_add(receiverBin, mprtpRcv);
  for(item = params->subflows; item; item = item->next){
    gchar padName[255];
    ReceiverSubflow* subflow = item->data;
    GstElement *rtpSrc = gst_element_factory_make("udpsrc", NULL);
    g_object_set(rtpSrc,
        "port", subflow->bound_port,
        NULL);
    memset(padName, 0, 255);
    sprintf(padName, "mprtcp_sink_%d", subflow->id);
    g_print("UDP SInk is created for port: %hu\n", subflow->bound_port);
    gst_bin_add(receiverBin, rtpSrc);
    gst_element_link_pads(rtpSrc, "src", mprtpRcv, padName);
  }

//  setup_ghost_src_by_padnames(mprtpRcv, "mprtp_src", receiverBin, "src");
  setup_ghost_src_by_padnames(mprtpRcv, "mprtcp_rr_src", receiverBin, "mprtcp_rr_src");
  return GST_ELEMENT(receiverBin);
}

