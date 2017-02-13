#include <gst/gst.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtpdefs.h>
#include <gst/gstobject.h>
#include "receiver.h"
#include "rtpstatmaker.h"
#include "sender.h"
#include "owr_arrival_time_meta.h"

typedef struct {
    guint session_id;

    gushort highest_seq;
    guint16 ack_vec;
    guint8 n_loss;
    guint8 n_ecn;
    guint receive_wallclock;

    guint32 ssrc;
    guint rtcp_session_id;
    guint32 fmt;
    guint   pt;
    guint32 last_feedback_wallclock;

    gboolean has_data;
    gboolean initialized;
} ScreamRx;

typedef struct{
    GstBin*     mprtpRcvBin;
    GstElement* mprtpRcv;
    ScreamRx*   scream_rx;
    GstElement* rtpbin;
    GstElement* src_for_scream;
}Private;

#define _priv(this) ((Private*)(this->priv))
#define _screamrx(this) ((ScreamRx*) _priv(this)->scream_rx)
static GstElement* _make_rtpsrc(Receiver* this, guint16 bound_port);
static GstElement* _make_rtp_receiver(Receiver* this,   TransferParams *params);
static GstElement* _make_mprtp_receiver(Receiver* this, TransferParams *params);
static GstElement* _get_mprtcp_sr_src_element(Receiver* this);
static void _setup_mprtcp_pads(Receiver* this, TransferParams* transfer_params);
static GstElement* _make_mprtp_playouter(Receiver* this, TransferParams *transfer_params);
static GstElement* _make_mprtp_controller(Receiver* this, TransferParams *transfer_params);
static GstElement* _make_mprtp_fractal_controller(Receiver* this,
    PlayouterParams *scheduler_params, TransferParams* snd_transfer_params);

static GstPadProbeReturn _scream_probe_rtp_info(GstPad *srcpad, GstPadProbeInfo *info, Receiver *this);
static gboolean _scream_on_sending_rtcp(GObject *session, GstBuffer *buffer, gboolean early, Receiver *this);
static GstPadProbeReturn probe_save_ts(GstPad *srcpad, GstPadProbeInfo *info, void *user_data);
static GstElement* _make_scream_controller (Receiver* this,
    PlayouterParams* playouter_params, TransferParams* rcv_transfer_params);

static int _instance_counter = 0;


Receiver* receiver_ctor(void)
{
  Receiver* this;

  this = g_malloc0(sizeof(Receiver));
  this->priv = g_malloc0(sizeof(Private));
  sprintf(this->bin_name, "ReceiverBin_%d", _instance_counter++);
  this->on_caps_change      = make_eventer("on-caps-change");
  this->on_add_rtpSrc_probe = make_eventer("on-rtpSrc-probe-add");
  this->objects_holder      = objects_holder_ctor();
  return this;
}

void receiver_dtor(Receiver* this)
{
  object_holder_dtor(this->objects_holder);
  eventer_unref(this->on_caps_change);
  eventer_unref(this->on_add_rtpSrc_probe);
  g_free(this->priv);
  g_free(this);
}


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
        playouter = _make_scream_controller(this, playouter_params, rcv_transfer_params);
        gst_bin_add(receiverBin, playouter);
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
  eventer_do(this->on_caps_change, (gpointer)caps);
}

static void _on_rtpSrc_caps_change(GstElement* rtpSrc, const GstCaps* caps)
{
  g_print("On rtpSrc caps canged called\n");
  g_object_set(G_OBJECT(rtpSrc), "caps", caps, NULL);

}

static void _on_add_rtpSrc_probe(GstElement* rtpSrc, ProbeParams* probe_params)
{
  GstPad *rtpSrc_pad;
  g_print("On rtpSrc probe added\n");
  rtpSrc_pad = gst_element_get_static_pad(rtpSrc, "src");
  gst_pad_add_probe(rtpSrc_pad,
      probe_params->mask,
      probe_params->callback,
      probe_params->user_data,
      probe_params->destroy_data
  );
  gst_object_unref(rtpSrc_pad);

}

GstElement* _make_rtpsrc(Receiver* this, guint16 bound_port)
{

  gchar name[256];
  sprintf(name, "UDP Source:%hu", bound_port);
  GstElement *rtpSrc   = gst_element_factory_make ("udpsrc", name);
  g_object_set (rtpSrc, "port", bound_port, NULL);

  g_print("UdpSrc is bound to %hu\n", bound_port);
  eventer_add_subscriber_full(this->on_caps_change, (subscriber) _on_rtpSrc_caps_change, rtpSrc);
  eventer_add_subscriber_full(this->on_add_rtpSrc_probe, (subscriber) _on_add_rtpSrc_probe, rtpSrc);
//  return debug_by_src(rtpSrc);
  return rtpSrc;
}


GstElement* receiver_get_mprtcp_rr_src_element(Receiver* this)
{
  GstBin*     mprtpRcvBin = _priv(this)->mprtpRcvBin;
  GstElement* mprtpRcv    = _priv(this)->mprtpRcv;

  setup_ghost_src_by_padnames(mprtpRcv, "mprtcp_rr_src", mprtpRcvBin, "mprtcp_rr_src");
  setup_ghost_src_by_padnames(GST_ELEMENT(mprtpRcvBin), "mprtcp_rr_src", GST_BIN(this->element), "mprtcp_rr_src");

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
    SenderSubflow* subflow = item->data;
    g_object_set(mprtpPly, "join-subflow", subflow->id, NULL);
    g_print("Joining subflow: %d\n", subflow->id);
  }

  return mprtpPly;
}


GstElement* _make_mprtp_controller(Receiver* this, TransferParams* rcv_transfer_params)
{
  GstBin*     plyBin   = GST_BIN (gst_bin_new(NULL));
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
  GstBin*     plyBin   = GST_BIN (gst_bin_new(NULL));
  GstElement* mprtpPly = _make_mprtp_playouter(this, rcv_transfer_params);
  Sender*     sender   = make_sender(NULL, NULL, playouter_params->snd_transfer_params, NULL);
  gint32      subflows_num = g_slist_length(playouter_params->snd_transfer_params->subflows);

  gst_bin_add_many(plyBin,
      mprtpPly,
      sender->element,
      NULL
  );

  g_object_set(mprtpPly,
      "controlling-mode", 2,
      "rtcp-interval-type", 2,
      "max-repair-delay", 10,
      "max-join-delay", 1 < subflows_num ? 100 : 0,
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




GstPadProbeReturn _scream_probe_rtp_info(GstPad *srcpad, GstPadProbeInfo *info, Receiver *this)
{
    GstBuffer *buffer = NULL;
    GstRTPBuffer rtp_buf = GST_RTP_BUFFER_INIT;
    guint64 arrival_time = GST_CLOCK_TIME_NONE;
    guint session_id = 0;
    gboolean rtp_mapped = FALSE;
    GObject *rtp_session = NULL;
    ScreamRx *scream_rx;

    scream_rx = _screamrx(this);
    session_id = scream_rx->session_id;

    buffer = GST_PAD_PROBE_INFO_BUFFER(info);

    g_signal_emit_by_name(_priv(this)->rtpbin, "get-internal-session", session_id, &rtp_session);

    if (G_UNLIKELY(scream_rx->initialized == FALSE)) {
      g_object_set(rtp_session, "rtcp-reduced-size", TRUE, NULL);
      scream_rx->initialized = TRUE;
    }

    if (!gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp_buf)) {
    g_warning("Failed to map RTP buffer");
    goto end;
  }

  rtp_mapped = TRUE;

    {
        GstMeta *meta;
        const GstMetaInfo *meta_info = _owr_arrival_time_meta_get_info();
//        GHashTable *rtcp_info;
        guint16 seq = 0;
        guint ssrc = 0;
        guint diff, tmp_highest_seq, tmp_seq;

        if ((meta = gst_buffer_get_meta(buffer, meta_info->api))) {
            OwrArrivalTimeMeta *atmeta = (OwrArrivalTimeMeta *) meta;
            arrival_time = atmeta->arrival_time;
        }

        if (arrival_time == GST_CLOCK_TIME_NONE) {
            GST_WARNING("No arrival time available for RTP packet");
            goto end;
        }

        scream_rx->ssrc = ssrc = gst_rtp_buffer_get_ssrc(&rtp_buf);
        seq = gst_rtp_buffer_get_seq(&rtp_buf);

        tmp_seq = seq;
        tmp_highest_seq = scream_rx->highest_seq;
        if (!scream_rx->highest_seq && !scream_rx->ack_vec) { /* Initial condition */
            scream_rx->highest_seq = seq;
            tmp_highest_seq = scream_rx->highest_seq;
        } else if ((seq < scream_rx->highest_seq) && (scream_rx->highest_seq - seq > 20000))
            tmp_seq = (guint64)seq + 65536;
        else if ((seq > scream_rx->highest_seq) && (seq - scream_rx->highest_seq > 20000))
            tmp_highest_seq += 65536;

        /* in order */
        if (tmp_seq >= tmp_highest_seq) {
            diff = tmp_seq - tmp_highest_seq;
            if (diff) {
                if (diff >= 16)
                    scream_rx->ack_vec = 0x0000; /* ack_vec can be reduced to guint16, initialize with 0xffff */
                else {
                    // Fill with potential zeros
                    scream_rx->ack_vec = scream_rx->ack_vec >> diff;
                    // Add previous highest seq nr to ack vector
                    scream_rx->ack_vec = scream_rx->ack_vec | (1 << (16 - diff));
                }
            }

            scream_rx->highest_seq = seq;
        } else { /* out of order */
            diff = tmp_highest_seq - tmp_seq;
            if (diff < 16)
                scream_rx->ack_vec = scream_rx->ack_vec | (1 << (16 - diff));
        }
        if (!(scream_rx->ack_vec & (1 << (16-5)))) {
            /*
            * Detect lost packets with a little grace time to cater
            * for out-of-order delivery
            */
            scream_rx->n_loss++; /* n_loss is a guint8 , initialize to 0 */
        }

        /*
        * ECN is not implemented but we add this just to not forget it
        * in case ECN flies some day
        */
        scream_rx->n_ecn = 0;
        scream_rx->last_feedback_wallclock = (guint32)(arrival_time / 1000000);

        scream_rx->pt = GST_RTCP_TYPE_RTPFB;
        scream_rx->fmt = GST_RTCP_RTPFB_TYPE_SCREAM;
        scream_rx->rtcp_session_id = session_id;
        scream_rx->has_data = TRUE;
        g_signal_emit_by_name(rtp_session, "send-rtcp", 20000000);
    }

end:
    if (rtp_mapped)
        gst_rtp_buffer_unmap(&rtp_buf);
    if (rtp_session)
        g_object_unref(rtp_session);

    return GST_PAD_PROBE_OK;
}

//#define GST_RTCP_RTPFB_TYPE_SCREAM 18

gboolean _scream_on_sending_rtcp(GObject *session, GstBuffer *buffer, gboolean early, Receiver *this)
{
    GstRTCPBuffer rtcp_buffer = {NULL, {NULL, 0, NULL, 0, 0, {0}, {0}}};
    GstRTCPPacket rtcp_packet;
    GstRTCPType packet_type;
    gboolean has_packet, do_not_suppress = FALSE;
//    GValueArray *sources = NULL;
//    GObject *source = NULL;
//    guint session_id = 0, rtcp_session_id = 0;
//    GList *it, *next;
    ScreamRx *scream_rx;

    guint pt, fmt, ssrc, last_fb_wc, highest_seq, n_loss, n_ecn;

    OWR_UNUSED(early);

    scream_rx = _screamrx(this);

//    session_id = GPOINTER_TO_UINT(g_object_get_data(session, "session_id"));

    if(!scream_rx->initialized || !scream_rx->has_data){
      goto done;
    }

    if (!gst_rtcp_buffer_map(buffer, GST_MAP_READ | GST_MAP_WRITE, &rtcp_buffer)) {
      goto done;
    }

  has_packet = gst_rtcp_buffer_get_first_packet(&rtcp_buffer, &rtcp_packet);
  for (; has_packet; has_packet = gst_rtcp_packet_move_to_next(&rtcp_packet)) {
    packet_type = gst_rtcp_packet_get_type(&rtcp_packet);
    if (packet_type == GST_RTCP_TYPE_PSFB || packet_type == GST_RTCP_TYPE_RTPFB) {
      do_not_suppress = TRUE;
      break;
    }
  }

  pt = scream_rx->pt;
  ssrc = scream_rx->ssrc;

  gst_rtcp_buffer_add_packet(&rtcp_buffer, pt, &rtcp_packet);

//  rtcp_session_id = scream_rx->rtcp_session_id;
  fmt = GST_RTCP_RTPFB_TYPE_SCREAM;

  guint8 *fci_buf;
  last_fb_wc = scream_rx->last_feedback_wallclock;
  highest_seq = scream_rx->highest_seq;
  n_loss = scream_rx->n_loss;
  n_ecn = scream_rx->n_ecn;

  gst_rtcp_packet_fb_set_type(&rtcp_packet, fmt);
  gst_rtcp_packet_fb_set_sender_ssrc(&rtcp_packet, 0);
  gst_rtcp_packet_fb_set_media_ssrc(&rtcp_packet, ssrc);
  gst_rtcp_packet_fb_set_fci_length(&rtcp_packet, 3);

  fci_buf = gst_rtcp_packet_fb_get_fci(&rtcp_packet);
  GST_WRITE_UINT16_BE(fci_buf, highest_seq);
  GST_WRITE_UINT8(fci_buf + 2, n_loss);
  GST_WRITE_UINT8(fci_buf + 3, n_ecn);
  GST_WRITE_UINT32_BE(fci_buf + 4, last_fb_wc);
  /* qbit not implemented yet  */
  GST_WRITE_UINT32_BE(fci_buf + 8, 0);
  do_not_suppress = TRUE;

  scream_rx->has_data = FALSE;

  gst_rtcp_buffer_unmap(&rtcp_buffer);
  done:
    return do_not_suppress;
}

GstPadProbeReturn probe_save_ts(GstPad *srcpad, GstPadProbeInfo *info, void *user_data)
{
    GstBuffer *buffer = NULL;
    OWR_UNUSED(user_data);
    OWR_UNUSED(srcpad);

    buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    _owr_buffer_add_arrival_time_meta(buffer, GST_BUFFER_DTS(buffer));

    return GST_PAD_PROBE_OK;
}

static GstCaps *
_scream_request_pt_map (GstElement * rtpbin, guint session, guint pt,
    gpointer user_data)
{
  return gst_caps_new_simple ("application/x-rtp",
      "media", G_TYPE_STRING, "video",
      "clock-rate", G_TYPE_INT, 90000,
      "width", G_TYPE_INT, 352,
      "height", G_TYPE_INT, 288,
      "framerate", GST_TYPE_FRACTION, 25, 1,
      "encoding-name", G_TYPE_STRING, "VP8", NULL
      );
}

static void
_scream_handle_new_stream (GstElement * element, GstPad * newPad, gpointer data)
{
  Receiver *this = (Receiver *) data;
  gchar*    padName;
  guint     sessionNum = 0;
  gchar*    myPrefix;

  padName = gst_pad_get_name (newPad);
  myPrefix = g_strdup_printf ("recv_rtp_src_%u", sessionNum);

  if (g_str_has_prefix (padName, myPrefix)) {
    GstPad *outputSinkPad;
    GstElement *parent;

    outputSinkPad = gst_element_get_static_pad (_priv(this)->src_for_scream, "sink");
    g_assert_cmpint (gst_pad_link (newPad, outputSinkPad), ==, GST_PAD_LINK_OK);
    gst_object_unref (outputSinkPad);

  }
  g_free (myPrefix);
  g_free (padName);
}

GstElement*
_make_scream_controller (Receiver* this,  PlayouterParams* playouter_params, TransferParams* rcv_transfer_params)
{
  GstBin* screamBin = GST_BIN (gst_bin_new("screamBin"));

  guint        sessionNum   = 0;
  Sender*      sender       = make_sender(NULL, NULL, playouter_params->snd_transfer_params, NULL);
  GstElement * rtpBin       = _priv(this)->rtpbin = gst_element_factory_make("rtpbin", NULL);
  GstElement*  rtcpSrc      = gst_element_factory_make("udpsrc", NULL);
  GstElement*  srcIdentity  = gst_element_factory_make("identity", NULL);
  GstElement*  sinkIdentity = gst_element_factory_make("identity", NULL);
  GstPad*      rtp_sink_pad = NULL;
  gchar*       padName;
  ProbeParams  probe_params;

  objects_holder_add(this->objects_holder, sender, (GDestroyNotify) sender_dtor);

  gst_bin_add_many(screamBin,
      rtpBin,
      sinkIdentity,
      sender->element,
      rtcpSrc,
      srcIdentity,
      NULL);

  g_object_set(rtpBin, "buffer-mode", 1, NULL);//Jitterbuffer in slave mode.

  if(rcv_transfer_params->type != TRANSFER_TYPE_RTP){
    g_print("Configuration error: Scream works only with RTP transfer types");
  }
  {
    ReceiverSubflow* subflow = rcv_transfer_params->subflows->data;
    g_object_set(rtcpSrc, "port", subflow->bound_port + 1, NULL);
  }

  probe_params.mask         = GST_PAD_PROBE_TYPE_BUFFER;
  probe_params.callback     = probe_save_ts;
  probe_params.destroy_data = NULL;
  probe_params.user_data    = NULL;
  eventer_do(this->on_add_rtpSrc_probe, &probe_params);

  g_signal_connect_data (rtpBin, "request-pt-map", G_CALLBACK (_scream_request_pt_map),
      this, NULL, 0);

  g_signal_connect_data (rtpBin, "pad-added", G_CALLBACK (_scream_handle_new_stream),
     this, NULL, 0);

  padName = g_strdup_printf ("recv_rtp_src_%u", sessionNum);
  _priv(this)->src_for_scream = srcIdentity;
//  gst_element_link_pads (rtpBin, padName, srcIdentity, "sink");
  g_free (padName);

  padName = g_strdup_printf ("recv_rtp_sink_%u", sessionNum);
  gst_element_link_pads (sinkIdentity, "src", rtpBin, padName);
  rtp_sink_pad = gst_element_get_static_pad(rtpBin, padName);
  g_free (padName);

  padName = g_strdup_printf ("recv_rtcp_sink_%u", sessionNum);
  gst_element_link_pads (rtcpSrc, "src", rtpBin, padName);
  g_free (padName);

  padName = g_strdup_printf ("send_rtcp_src_%u", sessionNum);
  gst_element_link_pads (rtpBin, padName, sender->element, "sink");
  g_free (padName);

  {
    GObject *rtp_session = NULL;
    ScreamRx *scream_rx;
    g_signal_emit_by_name(rtpBin, "get-internal-session", sessionNum, &rtp_session);
    g_signal_connect_after(rtp_session, "on-sending-rtcp", G_CALLBACK(_scream_on_sending_rtcp), this);  // g_signal_connect_after(rtp_session, "on-receiving-rtcp", G_CALLBACK(on_receiving_rtcp), NULL);

    _priv(this)->scream_rx = scream_rx = g_new0(ScreamRx, 1);
    scream_rx->session_id  = sessionNum;
    gst_pad_add_probe(rtp_sink_pad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)_scream_probe_rtp_info,
        this, NULL);

    g_object_unref(rtp_session);
  }

  setup_ghost_sink(sinkIdentity, screamBin);
  setup_ghost_src(srcIdentity, screamBin);

  return GST_ELEMENT(screamBin);
}




