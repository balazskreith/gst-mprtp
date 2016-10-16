#ifdef HAVE_CONFIG_H
#include "config.h"
#endif



#include <gst/gst.h>
#include <gst/gst.h>
#include <string.h>
#include "gstmprtpplayouter.h"
#include "gstmprtcpbuffer.h"
#include "streamjoiner.h"
#include "mprtplogger.h"


#include "rcvctrler.h"

typedef struct _SubflowSpecProp{
  #if G_BYTE_ORDER == G_LITTLE_ENDIAN
    guint32  value : 24;
    guint32  id     : 8;
  #elif G_BYTE_ORDER == G_BIG_ENDIAN
    guint32  id     : 8;
    guint32  value : 24;
  #else
  #error "G_BYTE_ORDER should be big or little endian."
  #endif
}SubflowSpecProp;

GST_DEBUG_CATEGORY_STATIC (gst_mprtpplayouter_debug_category);
#define GST_CAT_DEFAULT gst_mprtpplayouter_debug_category

#define THIS_LOCK(this) g_mutex_lock(&this->mutex)
#define THIS_UNLOCK(this) g_mutex_unlock(&this->mutex)



static void gst_mprtpplayouter_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_mprtpplayouter_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_mprtpplayouter_dispose (GObject * object);
static void gst_mprtpplayouter_finalize (GObject * object);

static GstStateChangeReturn
gst_mprtpplayouter_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_mprtpplayouter_query (GstElement * element,
    GstQuery * query);
static GstFlowReturn gst_mprtpplayouter_mprtp_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static GstFlowReturn gst_mprtpplayouter_mprtcp_sr_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static gboolean gst_mprtpplayouter_sink_query (GstPad * sinkpad,
    GstObject * parent, GstQuery * query);
static gboolean gst_mprtpplayouter_src_query (GstPad * sinkpad,
    GstObject * parent, GstQuery * query);
static gboolean gst_mprtpplayouter_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static GstFlowReturn _processing_mprtcp_packet (GstMprtpplayouter * this,
    GstBuffer * buf);

static void
_playouter_on_rtcp_ready(
    GstMprtpplayouter *this,
    GstBuffer* buffer);

static void
_playouter_on_repair_response(
    GstMprtpplayouter *this,
    GstBuffer *rtpbuf);

static void
_playout_process (
    GstMprtpplayouter *this);
#define _trash_mprtp_buffer(this, mprtp) mprtp_free(mprtp)

#define _now(this) gst_clock_get_time (this->sysclock)


enum
{
  PROP_0,
  PROP_MPRTP_EXT_HEADER_ID,
  PROP_ABS_TIME_EXT_HEADER_ID,
  PROP_FEC_PAYLOAD_TYPE,
  PROP_JOIN_SUBFLOW,
  PROP_DETACH_SUBFLOW,
  PROP_SETUP_CONTROLLING_MODE,
  PROP_SETUP_RTCP_INTERVAL_TYPE,
  PROP_RTP_PASSTHROUGH,
  PROP_LOG_ENABLED,
  PROP_LOG_PATH,

};

/* pad templates */

static GstStaticPadTemplate gst_mprtpplayouter_mprtp_sink_template =
    GST_STATIC_PAD_TEMPLATE ("mprtp_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp;application/x-rtcp;application/x-srtcp")
    );


static GstStaticPadTemplate gst_mprtpplayouter_mprtcp_sr_sink_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_sr_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static GstStaticPadTemplate gst_mprtpplayouter_mprtp_src_template =
GST_STATIC_PAD_TEMPLATE ("mprtp_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );


static GstStaticPadTemplate gst_mprtpplayouter_mprtcp_rr_src_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_rr_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstMprtpplayouter, gst_mprtpplayouter,
    GST_TYPE_ELEMENT,
    GST_DEBUG_CATEGORY_INIT (gst_mprtpplayouter_debug_category,
        "mprtpplayouter", 0, "debug category for mprtpplayouter element"));

static void
gst_mprtpplayouter_class_init (GstMprtpplayouterClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpplayouter_mprtp_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get
      (&gst_mprtpplayouter_mprtcp_sr_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpplayouter_mprtp_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpplayouter_mprtcp_rr_src_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "MPRTP Playouter", "Generic",
      "MPRTP Playouter FIXME", "Balázs Kreith <balazs.kreith@gmail.com>");

  gobject_class->set_property = gst_mprtpplayouter_set_property;
  gobject_class->get_property = gst_mprtpplayouter_get_property;
  gobject_class->dispose = gst_mprtpplayouter_dispose;
  gobject_class->finalize = gst_mprtpplayouter_finalize;

  g_object_class_install_property (gobject_class, PROP_MPRTP_EXT_HEADER_ID,
      g_param_spec_uint ("mprtp-ext-header-id",
          "Set or get the id for the RTP extension",
          "Sets or gets the id for the extension header the MpRTP based on. The default is 3",
          0, 15, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  g_object_class_install_property (gobject_class, PROP_ABS_TIME_EXT_HEADER_ID,
      g_param_spec_uint ("abs-time-ext-header-id",
          "Set or get the id for the absolute time RTP extension",
          "Sets or gets the id for the extension header the absolute time based on. The default is 8",
          0, 15, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FEC_PAYLOAD_TYPE,
      g_param_spec_uint ("fec-payload-type",
          "Set or get the payload type of fec packets",
          "Set or get the payload type of fec packets. The default is 126",
          0, 127, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_JOIN_SUBFLOW,
      g_param_spec_uint ("join-subflow", "the subflow id requested to join",
          "Join a subflow with a given id.", 0,
          MPRTP_PLUGIN_MAX_SUBFLOW_NUM, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DETACH_SUBFLOW,
       g_param_spec_uint ("detach-subflow", "the subflow id requested to detach",
           "Detach a subflow with a given id.", 0,
           MPRTP_PLUGIN_MAX_SUBFLOW_NUM, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RTP_PASSTHROUGH,
      g_param_spec_boolean ("rtp-passthrough",
          "Indicate the passthrough mode on no active subflow case",
          "Indicate weather the schdeuler let the packets travel "
          "through the element if it hasn't any active subflow.",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LOG_ENABLED,
      g_param_spec_boolean ("logging",
          "Indicate weather a log for subflow is enabled or not",
          "Indicate weather a log for subflow is enabled or not",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LOG_PATH,
        g_param_spec_string ("logs-path",
            "Determines the path for logfiles",
            "Determines the path for logfiles",
            "NULL", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SETUP_RTCP_INTERVAL_TYPE,
        g_param_spec_uint ("setup-rtcp-interval-type",
                           "RTCP interval types: 0 - regular, 1 - early, 2 - immediate feedback",
                           "A 32bit unsigned integer for setup a target. The first 8 bit identifies the subflow, the latter the mode. "
                           "RTCP interval types: 0 - regular, 1 - early, 2 - immediate feedback",
                           0,
                           4294967295, 2, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SETUP_CONTROLLING_MODE,
      g_param_spec_uint ("setup-controlling-mode",
          "set the controlling mode to the subflow",
          "A 32bit unsigned integer for setup a target. The first 8 bit identifies the subflow, the latter the mode. "
          "0 - no sending rate controller, 1 - no controlling, but sending SRs, 2 - FBRA with MARC",
          0, 4294967295, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_change_state);
  element_class->query = GST_DEBUG_FUNCPTR (gst_mprtpplayouter_query);
}


static void
gst_mprtpplayouter_init (GstMprtpplayouter * this)
{
  init_mprtp_logger();

  this->mprtp_sinkpad =
      gst_pad_new_from_static_template (&gst_mprtpplayouter_mprtp_sink_template,
      "mprtp_sink");
  gst_element_add_pad (GST_ELEMENT (this), this->mprtp_sinkpad);

  this->mprtp_srcpad =
      gst_pad_new_from_static_template (&gst_mprtpplayouter_mprtp_src_template,
      "mprtp_src");
  gst_element_add_pad (GST_ELEMENT (this), this->mprtp_srcpad);

  this->mprtcp_sr_sinkpad =
      gst_pad_new_from_static_template
      (&gst_mprtpplayouter_mprtcp_sr_sink_template, "mprtcp_sr_sink");
  gst_element_add_pad (GST_ELEMENT (this), this->mprtcp_sr_sinkpad);

  this->mprtcp_rr_srcpad =
      gst_pad_new_from_static_template
      (&gst_mprtpplayouter_mprtcp_rr_src_template, "mprtcp_rr_src");
  gst_element_add_pad (GST_ELEMENT (this), this->mprtcp_rr_srcpad);

  gst_pad_set_query_function (this->mprtp_srcpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_src_query));
  gst_pad_set_query_function (this->mprtcp_rr_srcpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_src_query));

  gst_pad_set_chain_function (this->mprtcp_sr_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_mprtcp_sr_sink_chain));
  gst_pad_set_chain_function (this->mprtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_mprtp_sink_chain));

  gst_pad_set_query_function (this->mprtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_sink_query));
  gst_pad_set_event_function (this->mprtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_sink_event));

  g_mutex_init (&this->mutex);
  g_cond_init(&this->receive_signal);
  g_cond_init(&this->waiting_signal);

  this->sysclock                 = gst_system_clock_obtain();
  this->fec_payload_type         = FEC_PAYLOAD_DEFAULT_ID;

  this->on_rtcp_ready            = make_notifier();
  this->subflows                 = make_rcvsubflows();

  this->fec_decoder              = make_fecdecoder();
  this->jitterbuffer             = make_jitterbuffer();
  this->joiner                   = make_stream_joiner();

  this->rcvpackets               = make_rcvpackets();
  this->rcvtracker               = make_rcvtracker();

  this->controller               = make_rcvctrler(this->rcvtracker, this->subflows, this->on_rtcp_ready);

  fecdecoder_add_response_listener(this->fec_decoder,
      (ListenerFunc) _playouter_on_repair_response, this);

  rcvpackets_set_abs_time_ext_header_id(this->rcvpackets, ABS_TIME_DEFAULT_EXTENSION_HEADER_ID);
  rcvpackets_set_mprtp_ext_header_id(this->rcvpackets, MPRTP_DEFAULT_EXTENSION_HEADER_ID);

  notifier_add_listener(this->on_rtcp_ready, (ListenerFunc) _playouter_on_rtcp_ready, this);
}


void
gst_mprtpplayouter_finalize (GObject * object)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (object);

  GST_DEBUG_OBJECT (this, "finalize");
  g_object_unref (this->joiner);
  g_object_unref (this->controller);
  g_object_unref (this->sysclock);
  g_object_unref (this->jitterbuffer);
  g_object_unref (this->fec_decoder);

  /* clean up object here */
  G_OBJECT_CLASS (gst_mprtpplayouter_parent_class)->finalize (object);
//  while(!g_queue_is_empty(this->mprtp_buffer_pool)){
//    mprtp_free(g_queue_pop_head(this->mprtp_buffer_pool));
//  }
//  g_object_unref(this->mprtp_buffer_pool);
}


void
gst_mprtpplayouter_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (object);
  gboolean gboolean_value;
  guint guint_value;
//  gdouble gdouble_value;
  SubflowSpecProp *subflow_prop;
  GST_DEBUG_OBJECT (this, "set_property");

  subflow_prop = (SubflowSpecProp*) &guint_value;
  THIS_LOCK (this);
  switch (property_id) {
    case PROP_MPRTP_EXT_HEADER_ID:
      rcvpackets_set_mprtp_ext_header_id(this->rcvpackets, (guint8) g_value_get_uint (value));
      break;
    case PROP_ABS_TIME_EXT_HEADER_ID:
      rcvpackets_set_abs_time_ext_header_id(this->rcvpackets, (guint8) g_value_get_uint (value));
      break;
    case PROP_FEC_PAYLOAD_TYPE:
      this->fec_payload_type = g_value_get_uint (value);
      break;
    case PROP_JOIN_SUBFLOW:
      rcvsubflows_join(this->subflows, g_value_get_uint (value));
      break;
    case PROP_DETACH_SUBFLOW:
      rcvsubflows_detach(this->subflows, g_value_get_uint (value));
      break;
    case PROP_LOG_ENABLED:
      gboolean_value = g_value_get_boolean (value);
      this->logging = gboolean_value;
      mprtp_logger_set_state(this->logging);
      break;
    case PROP_LOG_PATH:
      mprtp_logger_set_target_directory(g_value_get_string(value));
      break;
    case PROP_SETUP_RTCP_INTERVAL_TYPE:
      guint_value = g_value_get_uint (value);
      rcvsubflows_set_rtcp_interval_type(this->subflows, subflow_prop->id, subflow_prop->value);
      break;
    case PROP_SETUP_CONTROLLING_MODE:
      guint_value = g_value_get_uint (value);
      rcvsubflows_set_congestion_controlling_type(this->subflows, subflow_prop->id, subflow_prop->value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  THIS_UNLOCK (this);
}


void
gst_mprtpplayouter_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (object);

  GST_DEBUG_OBJECT (this, "get_property");
  THIS_LOCK (this);
  switch (property_id) {
    case PROP_MPRTP_EXT_HEADER_ID:
      g_value_set_uint (value, (guint) rcvpackets_get_mprtp_ext_header_id(this->rcvpackets));
      break;
    case PROP_ABS_TIME_EXT_HEADER_ID:
      g_value_set_uint (value, (guint) rcvpackets_get_abs_time_ext_header_id(this->rcvpackets));
      break;
    case PROP_FEC_PAYLOAD_TYPE:
      g_value_set_uint (value, (guint) this->fec_payload_type);
      break;
    case PROP_LOG_ENABLED:
      g_value_set_boolean (value, this->logging);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  THIS_UNLOCK (this);
}

gboolean
gst_mprtpplayouter_src_query (GstPad * sinkpad, GstObject * parent,
    GstQuery * query)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (parent);
  gboolean result = FALSE;
  GST_DEBUG_OBJECT (this, "query");
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      gboolean live;
      GstClockTime min, max;
      GstPad *peer;
      peer = gst_pad_get_peer (this->mprtp_sinkpad);
      if ((result = gst_pad_query (peer, query))) {
          gst_query_parse_latency (query, &live, &min, &max);
//          min= GST_MSECOND;
          min = 0;
          max = -1;
          gst_query_set_latency (query, live, min, max);
      }
      gst_object_unref (peer);
    }
    break;
    default:
      result = gst_pad_peer_query (this->mprtp_srcpad, query);
      break;
  }
  return result;
}


gboolean
gst_mprtpplayouter_sink_query (GstPad * sinkpad, GstObject * parent,
    GstQuery * query)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (parent);
  gboolean result;
  GST_DEBUG_OBJECT (this, "query");
  switch (GST_QUERY_TYPE (query)) {

    default:
      result = gst_pad_peer_query (this->mprtp_srcpad, query);
      break;
  }
  return result;
}


static gboolean
gst_mprtpplayouter_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (parent);
  gboolean result;
  GstPad *peer;
  GstCaps *caps;
  const GstStructure *s;
  gint gint_value;
  guint guint_value;

  GST_DEBUG_OBJECT (this, "sink event");
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_LATENCY:
      {
        GstClockTime latency;
        gst_event_parse_latency(event, &latency);
      }
      goto default_;
    case GST_EVENT_CAPS:
      gst_event_parse_caps (event, &caps);
      s = gst_caps_get_structure (caps, 0);
      THIS_LOCK (this);
      if (gst_structure_has_field (s, "clock-rate")) {
        gst_structure_get_int (s, "clock-rate", &gint_value);
        g_print("Clock Rate set to %d\n", gint_value);
        jitterbuffer_set_clock_rate(this->jitterbuffer, gint_value);
      }
      if (gst_structure_has_field (s, "clock-base")) {
        gst_structure_get_uint (s, "clock-base", &guint_value);
        this->clock_base = (guint64) gint_value;
      } else {
        this->clock_base = -1;
      }
      THIS_UNLOCK (this);
      goto default_;
    default:
    default_:
      peer = gst_pad_get_peer (this->mprtp_srcpad);
            result = gst_pad_send_event (peer, event);
            gst_object_unref (peer);
      //result = gst_pad_event_default (pad, parent, event);
      break;
  }

  return result;
}


void
gst_mprtpplayouter_dispose (GObject * object)
{
  GstMprtpplayouter *mprtpplayouter = GST_MPRTPPLAYOUTER (object);

  GST_DEBUG_OBJECT (mprtpplayouter, "dispose");

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (gst_mprtpplayouter_parent_class)->dispose (object);
}

static GstStateChangeReturn
gst_mprtpplayouter_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstMprtpplayouter *this;
  g_return_val_if_fail (GST_IS_MPRTPPLAYOUTER (element),
      GST_STATE_CHANGE_FAILURE);

  this = GST_MPRTPPLAYOUTER (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
        gst_pad_start_task(this->mprtp_srcpad, (GstTaskFunction)_playout_process, this, NULL);
        break;
      default:
        break;
    }

    ret =
        GST_ELEMENT_CLASS (gst_mprtpplayouter_parent_class)->change_state
        (element, transition);

    switch (transition) {
      case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
        break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_mprtpplayouter_query (GstElement * element, GstQuery * query)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (element);
  gboolean ret = TRUE;
  GST_DEBUG_OBJECT (this, "query");
  switch (GST_QUERY_TYPE (query)) {
    default:
      ret =
          GST_ELEMENT_CLASS (gst_mprtpplayouter_parent_class)->query (element,
          query);
      break;
  }

  return ret;
}

static GstFlowReturn
gst_mprtpplayouter_mprtp_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  GstMprtpplayouter *this;
  GstMapInfo info;
  guint8  *buf_2nd_byte;
  RcvPacket* packet;
  GstFlowReturn result = GST_FLOW_OK;

  this = GST_MPRTPPLAYOUTER (parent);
  GST_DEBUG_OBJECT (this, "RTP/RTCP/MPRTP/MPRTCP sink");

  if(!GST_IS_BUFFER(buf)){
    GST_WARNING("The arrived buffer is not a buffer.");
    goto done;
  }
  if (!gst_buffer_map (buf, &info, GST_MAP_READ)) {
    GST_WARNING ("Buffer is not readable");
    result = GST_FLOW_ERROR;
    goto done;
  }
  buf_2nd_byte = info.data + 1;
  gst_buffer_unmap (buf, &info);

  //demultiplexing based on RFC5761
  if (*buf_2nd_byte == MPRTCP_PACKET_TYPE_IDENTIFIER) {
    result = _processing_mprtcp_packet (this, buf);
    goto done;
  }

  //check weather the packet is rtcp or mprtp
  if (*buf_2nd_byte > 192 && *buf_2nd_byte < 223) {
    if(GST_IS_BUFFER(buf)){
      gst_pad_push(this->mprtp_srcpad, buf);
    }
    goto done;
  }

  //if(!gst_rtp_buffer_is_mprtp(this->rcvpackets, buf)){
  if(!gst_buffer_is_mprtp(buf, rcvpackets_get_mprtp_ext_header_id(this->rcvpackets))){
    if(GST_IS_BUFFER(buf)){
      gst_pad_push(this->mprtp_srcpad, buf);
    }
    goto done;
  }

  if (*buf_2nd_byte == this->fec_payload_type) {
    fecdecoder_add_fec_buffer(this->fec_decoder, gst_buffer_ref(buf));
    goto done;
  }else{
    fecdecoder_add_rtp_buffer(this->fec_decoder, gst_buffer_ref(buf));
  }

  packet = rcvpackets_get_packet(this->rcvpackets, gst_buffer_ref(buf));
//  return gst_pad_push(this->mprtp_srcpad, rcvpacket_retrieve_buffer_and_unref(packet));


  THIS_LOCK(this);

  packet = rcvpackets_get_packet(this->rcvpackets, gst_buffer_ref(buf));
  rcvtracker_add_packet(this->rcvtracker, packet);
  stream_joiner_push_packet(this->joiner, packet);
  g_cond_signal(&this->receive_signal);

  rcvctrler_time_update(this->controller);
  THIS_UNLOCK(this);

done:
  return result;

}


static GstFlowReturn
gst_mprtpplayouter_mprtcp_sr_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  GstMprtpplayouter *this;
  GstMapInfo info;
  GstFlowReturn result = GST_FLOW_OK;
  guint8 *buf_2nd_byte;

  this = GST_MPRTPPLAYOUTER (parent);
  GST_DEBUG_OBJECT (this, "RTCP/MPRTCP sink");
  if (!gst_buffer_map (buf, &info, GST_MAP_READ)) {
    GST_WARNING ("Buffer is not readable");
    result = GST_FLOW_ERROR;
    goto done;
  }

  buf_2nd_byte = info.data + 1;
  gst_buffer_unmap (buf, &info);

  if (*buf_2nd_byte == this->fec_payload_type) {
    fecdecoder_add_fec_buffer(this->fec_decoder, gst_buffer_ref(buf));
    goto done;
  }

  result = _processing_mprtcp_packet (this, buf);

done:
  return result;

}


GstFlowReturn
_processing_mprtcp_packet (GstMprtpplayouter * this, GstBuffer * buf)
{
  GstFlowReturn result;
  PROFILING("_processing_mprtcp_packet",
    THIS_LOCK (this);
    rcvctrler_receive_mprtcp(this->controller, buf);
    result = GST_FLOW_OK;
    THIS_UNLOCK (this);
  );

//  {
//      GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
//      gst_rtcp_buffer_map(buf, GST_MAP_READ, &rtcp);
//      gst_print_rtcp_buffer(&rtcp);
//      gst_rtcp_buffer_unmap(&rtcp);
//  }
  return result;
}

void _playouter_on_rtcp_ready(GstMprtpplayouter *this, GstBuffer* buffer)
{
//  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
//  gst_rtcp_buffer_map(buffer, GST_MAP_READ, &rtcp);
//  gst_print_rtcp_buffer(&rtcp);
//  gst_rtcp_buffer_unmap(&rtcp);

  gst_pad_push(this->mprtcp_rr_srcpad, buffer);
}

void _playouter_on_repair_response(GstMprtpplayouter *this, GstBuffer *rtpbuf)
{
  THIS_LOCK(this);
  this->discarded_packet.repairedbuf = rtpbuf;
  g_cond_signal(&this->waiting_signal);
  THIS_UNLOCK(this);
}


static void _wait(GstMprtpplayouter *this, GstClockTime waiting_time, gint64 step_in_microseconds)
{
  GstClockTime start = _now(this);
  while(_now(this) - start < waiting_time){
    g_cond_wait_until(&this->waiting_signal, &this->mutex, step_in_microseconds);
  }

//  while(!g_cond_wait_until(&this->cond, &this->mutex, step_in_microseconds)){
//    if(waiting_time <= _now(this) - start){
//      return;
//    }
//  }
}

static void
_playout_process (GstMprtpplayouter *this)
{
  RcvPacket *packet;
  GstClockTime playout_time;
  guint16 gap_seq;

  THIS_LOCK(this);
  playout_time = _now(this);
  while((packet = stream_joiner_pop_packet(this->joiner)) != NULL){
    jitterbuffer_push_packet(this->jitterbuffer, packet);
  }

  if(jitterbuffer_has_repair_request(this->jitterbuffer, &playout_time, &gap_seq)){
    this->discards = TRUE;
    this->discarded_packet.abs_seq = gap_seq;
    if(this->discarded_packet.repairedbuf){
      gst_buffer_unref(this->discarded_packet.repairedbuf);
      this->discarded_packet.repairedbuf = NULL;
    }
    fecdecoder_request_repair(this->fec_decoder, gap_seq);

    _wait(this, playout_time - _now(this), 100);

    if(this->discarded_packet.repairedbuf){
      gst_pad_push(this->mprtp_srcpad, this->discarded_packet.repairedbuf);
      this->discarded_packet.repairedbuf = NULL;
      this->discards = FALSE;
    }
    goto done;
  }

  packet = jitterbuffer_pop_packet(this->jitterbuffer, &playout_time);
  if(!packet){
    if(!playout_time){
      g_cond_wait(&this->receive_signal, &this->mutex);
    }else if(_now(this) < playout_time){
//      g_print("before playout_time, the diff is  %lu\n", playout_time - _now(this));
      _wait(this, playout_time - _now(this), 100);
//      g_print("after waiting until playout_time, the diff is  %lu\n", _now(this) - playout_time);
    }
    goto done;
  }

  if(this->discards){
    if(packet->abs_seq != this->discarded_packet.abs_seq){
      rcvtracker_add_discarded_packet(this->rcvtracker, &this->discarded_packet);
    }
    this->discards = FALSE;
  }

  gst_pad_push(this->mprtp_srcpad, packet->buffer);

done:
  THIS_UNLOCK(this);
  return;
}

//void _playout_process(GstMprtpplayouter *this)
//{
//   PROFILING("_playout_process",
//       _playout_process_(this);
//   );
//}

#undef THIS_LOCK
#undef THIS_UNLOCK
