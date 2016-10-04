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
_playouter_on_repair_request(
    GstMprtpplayouter *this,
    DiscardedPacket *new_discarded_packet);

static void
_playouter_on_response_request(
    GstMprtpplayouter *this,
    DiscardedPacket *discarded_packet);

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

  this->sysclock                 = gst_system_clock_obtain();
  this->fec_payload_type         = FEC_PAYLOAD_DEFAULT_ID;

  this->controller               = g_object_new(RCVCTRLER_TYPE, NULL);
  this->subflows                 = make_rcvsubflows();

  this->repair_channel           = make_mediator();
  this->fec_decoder              = make_fecdecoder(this->repair_channel);
  this->joiner                   = make_stream_joiner(this->repair_channel);

  this->rcvpackets               = make_rcvpackets();
  this->rcvtracker               = make_rcvtracker();

  this->discarded_packets        = g_async_queue_new();
  this->packetsq                 = g_async_queue_new();
  this->fec_requested              = FALSE;

  mediator_set_request_handler(this->repair_channel,
      (ListenerFunc) fecdecoder_on_discarded_packet, this->fec_decoder);

  mediator_set_request_handler(this->repair_channel,
      (ListenerFunc) _playouter_on_repair_request, this);

  mediator_set_response_handler(this->repair_channel,
      (ListenerFunc) _playouter_on_response_request, this);

  rcvpackets_set_abs_time_ext_header_id(this->rcvpackets, ABS_TIME_DEFAULT_EXTENSION_HEADER_ID);
  rcvpackets_set_mprtp_ext_header_id(this->rcvpackets, MPRTP_DEFAULT_EXTENSION_HEADER_ID);

  rcvtracker_add_on_stat_changed_cb(this->rcvtracker,
      (ListenerFunc) stream_joiner_on_rcvtracker_stat_change, this->joiner);
}


void
gst_mprtpplayouter_finalize (GObject * object)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (object);

  GST_DEBUG_OBJECT (this, "finalize");
  g_object_unref (this->joiner);
  g_object_unref (this->controller);
  g_object_unref (this->sysclock);
  g_object_unref (this->discarded_packets);
  g_object_unref (this->packetsq);
  g_object_unref (this->repair_channel);

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
      gst_pad_start_task(this->mprtp_srcpad, (GstTaskFunction)_playout_process, this, NULL);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
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
  GstFlowReturn result = GST_FLOW_OK;

  this = GST_MPRTPPLAYOUTER (parent);
  GST_DEBUG_OBJECT (this, "RTP/RTCP/MPRTP/MPRTCP sink");
//  g_print("START PROCESSING RTP\n");
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
    fecdecoder_add_fec_buffer(this->fec_decoder, buf);
    goto done;
  }else{
    fecdecoder_add_rtp_buffer(this->fec_decoder, buf);
  }

  g_async_queue_push(this->packetsq, gst_buffer_ref(buf));

done:
//  g_print("END PROCESSING RTP\n");
  return result;

}


static GstFlowReturn
gst_mprtpplayouter_mprtcp_sr_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  GstMprtpplayouter *this;
  GstMapInfo info;
  GstFlowReturn result;
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
    fecdecoder_add_fec_buffer(this->fec_decoder, buf);
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
  THIS_LOCK (this);
  rcvctrler_receive_mprtcp(this->controller, buf);
  result = GST_FLOW_OK;
  THIS_UNLOCK (this);
  return result;
}

void _playouter_on_repair_request(GstMprtpplayouter *this, DiscardedPacket *new_discarded_packet)
{
  DiscardedPacket *discarded_packet;
  while((discarded_packet = g_async_queue_try_pop(this->discarded_packets)) != NULL){
    rcvtracker_add_discarded_packet(this->rcvtracker, discarded_packet);
    g_slice_free(DiscardedPacket, discarded_packet);
    discarded_packet = NULL;
  }
  this->fec_requested = TRUE;
}

void _playouter_on_response_request(GstMprtpplayouter *this, DiscardedPacket *discarded_packet)
{
  g_async_queue_push(this->discarded_packets, discarded_packet);
}


void
_playout_process (GstMprtpplayouter *this)
{
  GstBuffer* buffer;
  RcvPacket *packet;

  THIS_LOCK(this);
  if((buffer = g_async_queue_timeout_pop(this->packetsq, 100)) == NULL){
    goto done;
  }

  packet = rcvpackets_make_packet(this->rcvpackets, buffer);

  rcvtracker_add_packet(this->rcvtracker, packet);
  stream_joiner_push_packet(this->joiner, packet);

  packet = stream_joiner_pop_packet(this->joiner);
  if(packet){
    gst_pad_push(this->mprtp_srcpad, rcvpacket_play_and_retrieve(packet));
    goto done;
  }

  if(this->fec_requested){
    DiscardedPacket* discarded_packet;
    discarded_packet = g_async_queue_timeout_pop(this->discarded_packets, 500);
    if(!discarded_packet){
      goto done;
    }
    rcvtracker_add_discarded_packet(this->rcvtracker, discarded_packet);
    gst_pad_push(this->mprtp_srcpad, discarded_packet->repairedbuf);
    g_slice_free(DiscardedPacket, discarded_packet);
    this->fec_requested = FALSE;
    goto done;
  }

done:
  rcvctrler_time_update(this->controller);
  THIS_UNLOCK(this);
  return;
}

#undef THIS_LOCK
#undef THIS_UNLOCK
