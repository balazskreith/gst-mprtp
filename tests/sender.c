#include <gst/gst.h>
#include <gst/rtp/rtp.h>
#include "sender.h"
#include "rtpstatmaker.h"
#include "receiver.h"
#include "owr_arrival_time_meta.h"

typedef struct{
    GstBin*     mprtpSndBin;
    GstElement* mprtpSnd;
    GstElement* screamqueue;
    GstElement* rtpbin;
}Private;

#define _priv(this) ((Private*)(this->priv))

static GstElement* _make_rtp_sender(TransferParams *params);
static GstElement* _make_mprtp_sender(Sender* this, TransferParams *params);
static GstElement* _get_mprtcp_sr_sink_element(Sender* this);
static GstElement* _make_mprtp_scheduler(Sender* this, TransferParams* transfer_params);
static GstElement* _make_mprtp_controller(Sender* this, SchedulerParams* params, TransferParams* transfer_params);
static GstElement* _make_mprtp_fractal_controller(Sender* this,
    SchedulerParams *scheduler_params, TransferParams* snd_transfer_params);
static GstElement* _make_scream_controller(Sender* this, SchedulerParams *scheduler_params,
    TransferParams* snd_transfer_params);
static void _setup_mprtcp_pads(Sender* this, TransferParams* transfer_params);
static int _instance_counter = 0;


Sender* sender_ctor(void)
{
  Sender* this;

  this = g_malloc0(sizeof(Sender));
  this->priv = g_malloc0(sizeof(Private));
  this->objects_holder = objects_holder_ctor();
  sprintf(this->bin_name, "SenderBin_%d", _instance_counter++);
  this->on_bitrate_change = make_eventer("on-bitrate-change");

  this->on_keyframe.subscriber_func = on_fi_called;
  return this;
}

void sender_dtor(Sender* this)
{

  eventer_unref(this->on_bitrate_change);
  object_holder_dtor(this->objects_holder);
  g_free(this->priv);
  g_free(this);
}


Sender* make_sender(SchedulerParams* scheduler_params,
    StatParams* stat_params,
    TransferParams *snd_transfer,
    ExtraDelayParams* extra_delay_params)
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

  if(extra_delay_params){
    GstClockTime delay = (GstClockTime) extra_delay_params->extra_delay_in_ms * GST_MSECOND;
    GstElement* queue = gst_element_factory_make("queue", NULL);
    g_object_set(queue, "min-threshold-time", delay, NULL);
    gst_bin_add(senderBin, queue);
    gst_element_link(queue, sink);
    sink = queue;
  }

  if(stat_params){
    statmaker = make_rtpstatmaker_element(stat_params);
    gst_bin_add(senderBin, statmaker);
    gst_element_link_pads(statmaker, "src", sink, "sink");
    sink = statmaker;
  }

  if(scheduler_params){
    switch(scheduler_params->type){
      case TRANSFER_CONTROLLER_TYPE_SCREAM:
        scheduler = _make_scream_controller(this, scheduler_params, snd_transfer);
        gst_bin_add(senderBin, scheduler);
        break;
      case TRANSFER_CONTROLLER_TYPE_MPRTP:
        scheduler = _make_mprtp_controller(this, scheduler_params, snd_transfer);
        gst_bin_add(senderBin, scheduler);

        break;
      case TRANSFER_CONTROLLER_TYPE_MPRTPRRACTAL:
        scheduler = _make_mprtp_fractal_controller(this, scheduler_params, snd_transfer);
        gst_bin_add(senderBin, scheduler);
        gst_element_link_pads(scheduler, "mprtcp_sr_src", _get_mprtcp_sr_sink_element(this), "mprtcp_sr_sink");
        break;
    };
    gst_element_link_pads(scheduler, "src", sink, "sink");
    sink = scheduler;
  }


  this->element = GST_ELEMENT(senderBin);
  setup_ghost_sink(sink, senderBin);

  return this;
}

Eventer* sender_get_on_bitrate_change_eventer(Sender* this){
  return this->on_bitrate_change;
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

static void _on_keyframe_filtering(GstElement *mprtpSch, CodecTypes* codec_type){

  guint keyframe_mode = 0;

  switch(*codec_type){
    case CODEC_TYPE_VP8:
      keyframe_mode = 1;
    break;
    default:
      keyframe_mode = 0;
    break;
  }
  g_object_set(mprtpSch,
         "mpath-keyframe-filtering", keyframe_mode,
         NULL
     );
  g_print("Setup Mpath keyframe mode to %d\n", keyframe_mode);
}

static void
_mprtp_subflows_utilization (GstElement * mprtpSch, MPRTPPluginSignalData *utilization, Sender* this)
{
//  g_print("Sender Name: %s", this->bin_name);
  eventer_do(this->on_bitrate_change, &utilization->target_media_rate);
  return;
}

GstElement* _make_mprtp_fractal_controller(Sender* this, SchedulerParams *scheduler_params, TransferParams* snd_transfer_params)
{
  GstBin*     schBin   = gst_bin_new(NULL);
  GstElement* mprtpSch = _make_mprtp_scheduler(this, snd_transfer_params);
  Receiver*   receiver = make_receiver(scheduler_params->rcv_transfer_params, NULL, NULL, NULL);
  GSList*     item;

  gst_bin_add_many(schBin,
      receiver->element,
      mprtpSch,
      NULL
  );

  g_object_set(mprtpSch,
       "controlling-mode", 2,
       "obsolation-treshold", 0,
       "rtcp-interval-type", 2,
       "report-timeout", 0,
       "fec-interval", 0,
       NULL
   );

  this->on_keyframe.subscriber_func = (subscriber)_on_keyframe_filtering;
  this->on_keyframe.subscriber_obj = mprtpSch;

  g_signal_connect (mprtpSch, "mprtp-subflows-utilization", (GCallback) _mprtp_subflows_utilization, this);
  if(snd_transfer_params->type != TRANSFER_TYPE_MPRTP){
    g_print("Configuration Error: FRACTaL congestion controller only works with MPRTP'n");
  }
  _setup_mprtcp_pads(this, snd_transfer_params);

  gst_element_link_pads(receiver_get_mprtcp_rr_src_element(receiver), "mprtcp_rr_src", mprtpSch, "mprtcp_rr_sink");
  objects_holder_add(this->objects_holder, receiver, (GDestroyNotify)receiver_dtor);

  setup_ghost_sink_by_padnames(mprtpSch, "rtp_sink",  schBin, "sink");
  setup_ghost_src_by_padnames(mprtpSch,  "mprtp_src", schBin, "src");
  setup_ghost_src_by_padnames(mprtpSch,  "mprtcp_sr_src",  schBin, "mprtcp_sr_src");
  return GST_ELEMENT(schBin);

}



static void _on_feedback_rtcp(GObject *session, guint type, guint fbtype, guint sender_ssrc,
    guint media_ssrc, GstBuffer *fci, Sender *this)
{
    g_return_if_fail(session);
    g_return_if_fail(this);

    if (type == GST_RTCP_TYPE_RTPFB && fbtype == GST_RTCP_RTPFB_TYPE_SCREAM) {
        GstElement *scream_queue = NULL;
        GstMapInfo info = {NULL, 0, NULL, 0, 0, {0}, {0}}; /*GST_MAP_INFO_INIT;*/
        guint session_id = GPOINTER_TO_UINT(g_object_get_data(session, "session_id"));

        scream_queue = _priv(this)->screamqueue;
        g_object_set(scream_queue, "scream-controller-id", 2, NULL);
        //scream_queue = gst_bin_get_by_name(GST_BIN(send_output_bin), "screamqueue");

        /* Read feedback from FCI */
        if (gst_buffer_map(fci, &info, GST_MAP_READ)) {
            guint32 timestamp;
            guint16 highest_seq;
            guint8 *fci_buf, n_loss, n_ecn;
            gboolean qbit = FALSE;

            fci_buf = info.data;
            highest_seq = GST_READ_UINT16_BE(fci_buf);
            n_loss = GST_READ_UINT8(fci_buf + 2);
            n_ecn = GST_READ_UINT8(fci_buf + 3);
            timestamp = GST_READ_UINT32_BE(fci_buf + 4);
            /* TODO: Fix qbit */

            gst_buffer_unmap(fci, &info);
//            g_print("m_ssrc: %u | ts: %u | HSSN: %hu | loss: %d | n_ecn: %d | qbit: %d\n", media_ssrc, timestamp, highest_seq, n_loss, n_ecn, qbit);
            g_signal_emit_by_name(scream_queue, "incoming-feedback", media_ssrc, timestamp, highest_seq, n_loss, n_ecn, qbit);

//            {
//              gboolean pass_through;
//              guint controller_id;
//              g_object_get(scream_queue, "pass-through", &pass_through,
//                  "scream-controller-id", &controller_id, NULL);
//              g_print("Pass through: %d controller_id: %d\n", pass_through, controller_id);
//            }
        }
    }
}

static gboolean _on_scream_payload_adaptation_request(GstElement *screamqueue, guint pt,
    Sender *this)
{
    guint pt_rtx;

    OWR_UNUSED(screamqueue);
    return TRUE;
}

static void _on_scream_bitrate_change(GstElement *scream_queue, guint bitrate, guint ssrc, guint pt,
    Sender *this)
{
  gint32 new_bitrate = bitrate;
  eventer_do(this->on_bitrate_change, &new_bitrate);
}

GstElement* _make_scream_controller(Sender* this, SchedulerParams *scheduler_params, TransferParams* snd_transfer_params)
{
  GstBin*     screamBin    = gst_bin_new(NULL);
  GstElement* rtpBin       = _priv(this)->rtpbin = gst_element_factory_make("rtpbin", NULL);
  GstElement* sinkIdentity = gst_element_factory_make("identity", NULL);
  GstElement* screamqueue  = gst_element_factory_make("screamqueue", NULL);
  Receiver*   receiver     = make_receiver(scheduler_params->rcv_transfer_params, NULL, NULL, NULL);
  GstElement* rtcpSink     = gst_element_factory_make("udpsink", NULL);
  gchar*      padName;
  guint       sessionNum   = 0;

  gst_bin_add_many(screamBin,
        receiver->element,
        sinkIdentity,
        rtpBin,
        rtcpSink,
        screamqueue,
        NULL
  );

  _priv(this)->screamqueue = screamqueue;
  objects_holder_add(this->objects_holder, receiver, (GDestroyNotify)receiver_dtor);
  g_object_set (rtpBin, "rtp-profile", GST_RTP_PROFILE_AVPF, NULL);

  if(snd_transfer_params->type != TRANSFER_TYPE_RTP){
    g_print("Configuration error: Scream works only with RTP transfer types");
  }
  {
    SenderSubflow* subflow = snd_transfer_params->subflows->data;
    g_object_set(rtcpSink, "host", subflow->dest_ip, "port", subflow->dest_port + 1,
        "sync", FALSE, "async", FALSE, NULL);
  }

  padName = g_strdup_printf ("send_rtp_sink_%u", sessionNum);
  gst_element_link_pads (sinkIdentity, "src", rtpBin, padName);
  g_free (padName);

  padName = g_strdup_printf ("send_rtp_src_%u", sessionNum);
  gst_element_link_pads (rtpBin, padName, screamqueue, "sink");
  g_free(padName);

  padName = g_strdup_printf ("send_rtcp_src_%u", sessionNum);
  gst_element_link_pads (rtpBin, padName, rtcpSink, "sink");
  g_free (padName);

  padName = g_strdup_printf ("recv_rtcp_sink_%u", sessionNum);
  gst_element_link_pads (receiver->element, "src", rtpBin, padName);
  g_free (padName);

  {
    GObject *rtp_session = NULL;
    g_signal_emit_by_name(rtpBin,  "get-internal-session", 0, &rtp_session);
    g_signal_connect(rtp_session,  "on-feedback-rtcp", G_CALLBACK(_on_feedback_rtcp), this);
    g_signal_connect(screamqueue, "on-bitrate-change", G_CALLBACK(_on_scream_bitrate_change), this);
    g_signal_connect(screamqueue, "on-payload-adaptation-request", (GCallback)_on_scream_payload_adaptation_request, this);
    g_object_unref(rtp_session);
  }

  setup_ghost_sink(sinkIdentity, screamBin);
  setup_ghost_src(screamqueue, screamBin);

  return GST_ELEMENT(screamBin);
}




