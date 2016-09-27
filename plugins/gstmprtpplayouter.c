/* GStreamer
 * Copyright (C) 2015 FIXME <fixme@example.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstmprtpplayouter
 *
 * The mprtpplayouter element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v fakesrc ! mprtpplayouter ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif



#include <gst/gst.h>
#include <gst/gst.h>
#include <string.h>
#include "gstmprtpplayouter.h"
#include "gstmprtcpbuffer.h"
#include "mprtprpath.h"
#include "mprtpspath.h"
#include "streamjoiner.h"
#include "gstmprtpbuffer.h"
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

#define THIS_LOCK(this) g_mutex_lock_lock(&this->mutex)
#define THIS_UNLOCK(this) g_mutex_unlock(&this->mutex)

#define MPRTP_PLAYOUTER_DEFAULT_SSRC 0
#define MPRTP_PLAYOUTER_DEFAULT_CLOCKRATE 90000


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
static void
gst_mprtpplayouter_mprtcp_sender (gpointer ptr, GstBuffer * buf);

static void _processing_mprtp_packet (GstMprtpplayouter * mprtpr,
    GstBuffer * buf);
static GstFlowReturn _processing_mprtcp_packet (GstMprtpplayouter * this,
    GstBuffer * buf);
static void _join_path (GstMprtpplayouter * this, guint8 subflow_id);
static void _detach_path (GstMprtpplayouter * this, guint8 subflow_id);

static void _time_updater_process(gpointer udata);
static void _rtppacket_transceiver (GstMprtpplayouter *this, GstBuffer *buffer);
#define _trash_mprtp_buffer(this, mprtp) mprtp_free(mprtp)

#define _now(this) gst_clock_get_time (this->sysclock)


enum
{
  PROP_0,
  PROP_MPRTP_EXT_HEADER_ID,
  PROP_ABS_TIME_EXT_HEADER_ID,
  PROP_FEC_PAYLOAD_TYPE,
  PROP_PIVOT_SSRC,
  PROP_JOIN_SUBFLOW,
  PROP_DETACH_SUBFLOW,
  PROP_PIVOT_CLOCK_RATE,
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

  g_object_class_install_property (gobject_class, PROP_PIVOT_CLOCK_RATE,
      g_param_spec_uint ("pivot-clock-rate", "Clock rate of the pivot stream",
          "Sets the clock rate of the pivot stream used for calculating "
          "skew and playout delay at the receiver", 0,
          G_MAXUINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PIVOT_SSRC,
      g_param_spec_uint ("pivot-ssrc", "SSRC of the pivot stream",
          "Sets the ssrc of the pivot stream used selecting MPRTP packets "
          "for playout delay at the receiver", 0,
          G_MAXUINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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
  this->pivot_clock_rate         = MPRTP_PLAYOUTER_DEFAULT_CLOCKRATE;
  this->pivot_ssrc               = MPRTP_PLAYOUTER_DEFAULT_SSRC;
  this->joiner                   = make_stream_joiner();
  this->controller               = g_object_new(RCVCTRLER_TYPE, NULL);
  this->pivot_address_subflow_id = 0;
  this->pivot_address            = NULL;
  this->fec_decoder              = make_fecdecoder();
  this->rtppackets               = make_rtppackets();
  this->rcvtracker               = make_rcvtracker();

  this->discarded_packets_in                 = g_async_queue_new();
  this->packetforwarder          = make_packetforwarder(this->mprtp_srcpad, this->mprtcp_rr_srcpad);

  rcvctrler_setup(this->controller, this->joiner, this->fec_decoder);
  rcvctrler_setup_callbacks(this->controller, this, gst_mprtpplayouter_mprtcp_sender);

  rtppackets_set_abs_time_ext_header_id(this->rtppackets, ABS_TIME_DEFAULT_EXTENSION_HEADER_ID);
  rtppackets_set_mprtp_ext_header_id(this->rtppackets, MPRTP_DEFAULT_EXTENSION_HEADER_ID);
  rtppackets_set_fec_payload_type(this->rtppackets, FEC_PAYLOAD_DEFAULT_ID);
}


void
gst_mprtpplayouter_finalize (GObject * object)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (object);

  GST_DEBUG_OBJECT (this, "finalize");
  g_object_unref (this->joiner);
  g_object_unref (this->controller);
  g_object_unref (this->sysclock);
  g_object_unref (this->discarded_packets_in);
  g_object_unref (this->packetforwarder);

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
  gdouble gdouble_value;
  SubflowSpecProp *subflow_prop;
  GST_DEBUG_OBJECT (this, "set_property");

  subflow_prop = (SubflowSpecProp*) &guint_value;

  THIS_LOCK (this);
  switch (property_id) {
    case PROP_MPRTP_EXT_HEADER_ID:
      rtppackets_set_mprtp_ext_header_id(this->rtppackets, (guint8) g_value_get_uint (value));
      break;
    case PROP_ABS_TIME_EXT_HEADER_ID:
      rtppackets_set_abs_time_ext_header_id(this->rtppackets, (guint8) g_value_get_uint (value));
      break;
    case PROP_FEC_PAYLOAD_TYPE:
      rtppackets_set_fec_payload_type(this->rtppackets, (guint8) g_value_get_uint (value));
      break;
    case PROP_PIVOT_SSRC:
      this->pivot_ssrc = g_value_get_uint (value);
      break;
    case PROP_PIVOT_CLOCK_RATE:
      this->pivot_clock_rate = g_value_get_uint (value);
      break;
    case PROP_JOIN_SUBFLOW:
      _join_path (this, g_value_get_uint (value));
      break;
    case PROP_DETACH_SUBFLOW:
      _detach_path (this, g_value_get_uint (value));
      break;
    case PROP_LOG_ENABLED:
      gboolean_value = g_value_get_boolean (value);
      this->logging = gboolean_value;
      if(this->logging)
        enable_mprtp_logger();
      else
        disable_mprtp_logger();
      break;
    case PROP_LOG_PATH:
      mprtp_logger_set_target_directory(g_value_get_string(value));
      break;
    case PROP_SETUP_RTCP_INTERVAL_TYPE:
      guint_value = g_value_get_uint (value);
      rcvctrler_change_interval_type(this->controller, subflow_prop->id, subflow_prop->value);
      break;
    case PROP_SETUP_CONTROLLING_MODE:
      guint_value = g_value_get_uint (value);
      rcvctrler_change_controlling_mode(this->controller, subflow_prop->id, subflow_prop->value);
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
      g_value_set_uint (value, (guint) rtppackets_get_mprtp_ext_header_id(this->rtppackets));
      break;
    case PROP_ABS_TIME_EXT_HEADER_ID:
      g_value_set_uint (value, (guint) rtppackets_get_abs_time_ext_header_id(this->rtppackets));
      break;
    case PROP_FEC_PAYLOAD_TYPE:
      g_value_set_uint (value, (guint) rtppackets_get_fec_payload_type(this->rtppackets));
      break;
    case PROP_PIVOT_CLOCK_RATE:
      g_value_set_uint (value, this->pivot_clock_rate);
      break;
    case PROP_PIVOT_SSRC:
      g_value_set_uint (value, this->pivot_ssrc);
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
        this->pivot_clock_rate = (guint32) gint_value;
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

void
_join_path (GstMprtpplayouter * this, guint8 subflow_id)
{

}

void
_detach_path (GstMprtpplayouter * this, guint8 subflow_id)
{

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
      gst_pad_start_task(this->mprtp_srcpad, (GstTaskFunction)_time_updater_process,
         this, NULL);
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
  GstStructure *s;
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
  guint8 *data;
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
  data = info.data + 1;
  gst_buffer_unmap (buf, &info);
  //demultiplexing based on RFC5761
  if (*data == MPRTCP_PACKET_TYPE_IDENTIFIER) {
    result = _processing_mprtcp_packet (this, buf);
    goto done;
  }
  //check weather the packet is rtcp or mprtp
  if (*data > 192 && *data < 223) {
    if(GST_IS_BUFFER(buf))
      packetforwarder_add_rtppad_buffer(this->packetforwarder, gst_buffer_ref(buf));
    goto done;
  }

  _rtppacket_transceiver(this, buf);

  result = GST_FLOW_OK;
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
  guint8 *data;

  this = GST_MPRTPPLAYOUTER (parent);
  GST_DEBUG_OBJECT (this, "RTCP/MPRTCP sink");
  THIS_LOCK (this);
  if (!gst_buffer_map (buf, &info, GST_MAP_READ)) {
    GST_WARNING ("Buffer is not readable");
    result = GST_FLOW_ERROR;
    goto done;
  }

  data = info.data + 1;
  gst_buffer_unmap (buf, &info);
  //demultiplexing based on RFC5761
  if (*data != this->rtppackets->fec_payload_type) {
    result = _processing_mprtcp_packet (this, buf);
  }else{
    _processing_mprtp_packet(this, buf);
    result = GST_FLOW_OK;
  }

done:
  THIS_UNLOCK (this);
  return result;

}

void
gst_mprtpplayouter_mprtcp_sender (gpointer ptr, GstBuffer * buf)
{
  GstMprtpplayouter *this;
  this = GST_MPRTPPLAYOUTER (ptr);
  THIS_LOCK (this);
  gst_pad_push (this->mprtcp_rr_srcpad, buf);
  THIS_UNLOCK (this);
}


GstFlowReturn
_processing_mprtcp_packet (GstMprtpplayouter * this, GstBuffer * buf)
{
  GstFlowReturn result;
  rcvctrler_receive_mprtcp(this->controller, buf);
  result = GST_FLOW_OK;
  return result;
}

//static GstClockTime in_prev;
void
_processing_mprtp_packet (GstMprtpplayouter * this, GstBuffer * buf)
{
  MpRTPRPath *path = NULL;
  GstNetAddressMeta *meta;
  GstMpRTPBuffer *mprtp = NULL;

  mprtp = _make_mprtp_buffer(this, buf);
  if (this->pivot_ssrc != MPRTP_PLAYOUTER_DEFAULT_SSRC &&
      mprtp->ssrc != this->pivot_ssrc) {

    _trash_mprtp_buffer(this, mprtp);
    GST_DEBUG_OBJECT (this, "RTP packet ssrc is %u, the pivot ssrc is %u",
        this->pivot_ssrc, mprtp->ssrc);
    if(GST_IS_BUFFER(buf))
      gst_pad_push (this->mprtp_srcpad, buf);
    return;
  }

  //to avoid the check_collision problem in rtpsession.
  meta = gst_buffer_get_net_address_meta (buf);
  if (meta) {
    if (!this->pivot_address) {
      this->pivot_address_subflow_id = mprtp->subflow_id;
      this->pivot_address = G_SOCKET_ADDRESS (g_object_ref (meta->addr));
    } else if (mprtp->subflow_seq != this->pivot_address_subflow_id) {
      if(gst_buffer_is_writable(buf))
        gst_buffer_add_net_address_meta (buf, this->pivot_address);
      else{
        buf = gst_buffer_make_writable(buf);
        gst_buffer_add_net_address_meta (buf, this->pivot_address);
      }
    }
  }
  if (_try_get_path (this, mprtp->subflow_id, &path) == FALSE) {
    _join_path (this, mprtp->subflow_id);
    if (_try_get_path (this, mprtp->subflow_id, &path) == FALSE) {
      GST_WARNING_OBJECT (this, "Subflow not found");
      _trash_mprtp_buffer(this, mprtp);
      return;
    }
  }

  if(mprtp->fec_packet){
    if(0 < this->repair_window_max){
      fecdecoder_add_fec_buffer(this->fec_decoder, mprtp);
    }
    _trash_mprtp_buffer(this, mprtp);
  }else{
    if(0 < this->repair_window_max){
      fecdecoder_add_rtp_packet(this->fec_decoder, mprtp);
    }
    stream_joiner_push(this->joiner, mprtp);
  }
  return;
}



void
_time_updater_process(gpointer udata)
{
  GstMprtpplayouter *this = udata;
  DiscardedPacket *discarded_packet;

  discarded_packet = (DiscardedPacket *)g_async_queue_timeout_pop(this->discarded_packets_in, 1000);

  THIS_LOCK(this);
  if(!discarded_packet){
    rcvtracker_refresh(this->rcvtracker);
    goto done;
  }

  rcvtracker_add_discarded_seq(this->rcvtracker, discarded_packet->abs_seq);

done:
  rcvctrler_time_update(this->controller);
  THIS_UNLOCK(this);
  return;
}

void
_rtppacket_transceiver (GstMprtpplayouter *this, GstBuffer *buffer)
{
  RTPPacket *packet;

  THIS_LOCK(this);

  //check weather the packet is mprtp
  if(!gst_rtp_buffer_is_mprtp(this->rtppackets, buffer)){
    if(GST_IS_BUFFER(buffer))
      packetforwarder_add_rtppad_buffer(this->packetforwarder, buffer);
    goto done;
  }

  //check weather packet is fec type
  if(rtppackets_buffer_is_fecmprtp(this->rtppackets, buffer)){
    fecdecoder_add_fec_buffer(this->rtppackets, packet);
    goto done;
  }

  packet = rtppackets_retrieve_packet_at_receiving(this->rtppackets, buffer);
  fecdecoder_add_rtp_packet(this->fec_decoder, packet);

  rcvtracker_add_packet(this->rcvtracker, packet);
  stream_joiner_add_packet(this->joiner, packet);
  rtppackets_packet_forwarded(this->rtppackets, packet);

done:
  THIS_UNLOCK(this);
  return;
}

#undef THIS_LOCK
#undef THIS_UNLOCK
