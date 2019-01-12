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
 * SECTION:element-gstmprtpscheduler
 *
 * The mprtpscheduler element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v fakesrc ! mprtpscheduler ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gst/gst.h>
#include "gstmprtpscheduler.h"
#include "streamsplitter.h"
#include "gstmprtcpbuffer.h"
#include "sndctrler.h"
#include "sndpackets.h"
#include <sys/timex.h>


GST_DEBUG_CATEGORY_STATIC (gst_mprtpscheduler_debug_category);
#define GST_CAT_DEFAULT gst_mprtpscheduler_debug_category

#define PACKET_IS_RTP_OR_RTCP(b) (b > 0x7f && b < 0xc0)
#define PACKET_IS_RTCP(b) (b > 192 && b < 223)
#define THIS_LOCK(this) (g_mutex_lock(&this->mutex))
#define THIS_UNLOCK(this) (g_mutex_unlock(&this->mutex))

#define _now(this) gst_clock_get_time (this->sysclock)

static void gst_mprtpscheduler_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_mprtpscheduler_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_mprtpscheduler_dispose (GObject * object);
static void gst_mprtpscheduler_finalize (GObject * object);

static GstStateChangeReturn
gst_mprtpscheduler_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_mprtpscheduler_query (GstElement * element,
    GstQuery * query);
static GstFlowReturn gst_mprtpscheduler_rtp_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static GstFlowReturn gst_mprtpscheduler_rtp_sink_chainlist (GstPad * pad,
    GstObject * parent, GstBufferList * list);

static GstFlowReturn gst_mprtpscheduler_mprtcp_rr_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * outbuf);

static gboolean gst_mprtpscheduler_src_query (GstPad * srckpad, GstObject * parent,
    GstQuery * query);

//static gboolean gst_mprtpscheduler_mprtp_src_event (GstPad * pad, GstObject * parent, GstEvent * event);
//static gboolean gst_mprtpscheduler_sink_eventfunc (GstPad * srckpad, GstObject * parent, GstEvent * event);
static gboolean gst_mprtpscheduler_mprtp_src_event (GstPad * srckpad, GstObject * parent, GstEvent * event);
static gboolean gst_mprtpscheduler_sink_event (GstPad * srckpad, GstObject * parent, GstEvent * event);


static void _on_rtcp_ready(GstMprtpscheduler * this, GstBuffer *buffer);
static void _on_monitoring_request(GstMprtpscheduler * this, SndSubflow* subflow);
static void _on_monitoring_response(GstMprtpscheduler * this, FECEncoderResponse *response);
static void _mprtpscheduler_send_packet (GstMprtpscheduler * this, SndPacket *packet);
static void mprtpscheduler_approval_process(GstMprtpscheduler *this);
static void mprtpscheduler_emitter_process(gpointer udata);


static guint _subflows_utilization;

enum
{
  PROP_0,
  PROP_MPRTP_EXT_HEADER_ID,
  PROP_ABS_TIME_EXT_HEADER_ID,
  PROP_FEC_PAYLOAD_TYPE,
  PROP_MPATH_KEYFRAME_FILTERING,
  PROP_PACKET_OBSOLATION_TRESHOLD,
  PROP_JOIN_SUBFLOW,
  PROP_DETACH_SUBFLOW,
  PROP_SETUP_CONTROLLING_MODE,
  PROP_SET_SENDING_TARGET,
  PROP_SETUP_RTCP_INTERVAL_TYPE,
  PROP_SETUP_REPORT_TIMEOUT,
  PROP_FEC_INTERVAL,
  PROP_ALLOWED_SSRC,
};

/* signals and args */
enum
{
  SIGNAL_SYSTEM_STATE,
  LAST_SIGNAL
};



/* pad templates */
static GstStaticPadTemplate gst_mprtpscheduler_rtp_sink_template =
GST_STATIC_PAD_TEMPLATE ("rtp_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );


static GstStaticPadTemplate gst_mprtpscheduler_mprtp_src_template =
GST_STATIC_PAD_TEMPLATE ("mprtp_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );


static GstStaticPadTemplate gst_mprtpscheduler_mprtcp_rr_sink_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_rr_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
//    GST_STATIC_CAPS ("application/x-rtcp")
    GST_STATIC_CAPS_ANY);


static GstStaticPadTemplate gst_mprtpscheduler_mprtcp_sr_src_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_sr_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
//    GST_STATIC_CAPS ("application/x-rtcp")
    GST_STATIC_CAPS_ANY);


/* class initialization */
G_DEFINE_TYPE_WITH_CODE (GstMprtpscheduler, gst_mprtpscheduler,
    GST_TYPE_ELEMENT,
    GST_DEBUG_CATEGORY_INIT (gst_mprtpscheduler_debug_category,
        "mprtpscheduler", 0, "debug category for mprtpscheduler element"));

#define GST_MPRTPSCHEDULER_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_MPRTPSCHEDULER, GstMprtpschedulerPrivate))

struct _GstMprtpschedulerPrivate
{

};

static void
gst_mprtpscheduler_class_init (GstMprtpschedulerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpscheduler_rtp_sink_template));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpscheduler_mprtp_src_template));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpscheduler_mprtcp_sr_src_template));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get
      (&gst_mprtpscheduler_mprtcp_rr_sink_template));


  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "MPRTP Scheduler", "Generic", "MPRTP scheduler FIXME LONG DESC",
      "Balázs Kreith <balazs.kreith@gmail.com>");

  gobject_class->set_property = gst_mprtpscheduler_set_property;
  gobject_class->get_property = gst_mprtpscheduler_get_property;
  gobject_class->dispose = gst_mprtpscheduler_dispose;
  gobject_class->finalize = gst_mprtpscheduler_finalize;
  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_mprtpscheduler_change_state);
  element_class->query = GST_DEBUG_FUNCPTR (gst_mprtpscheduler_query);


  g_object_class_install_property (gobject_class, PROP_MPRTP_EXT_HEADER_ID,
      g_param_spec_uint ("mprtp-ext-header-id",
          "Multipath RTP Header Extension ID",
          "Sets or gets the RTP header extension ID for MPRTP",
          0, 15, MPRTP_DEFAULT_EXTENSION_HEADER_ID, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ABS_TIME_EXT_HEADER_ID,
      g_param_spec_uint ("abs-time-ext-header-id",
          "Absolute time RTP extension ID",
          "Sets or gets the RTP header extension for abs NTP time.",
          0, 15, ABS_TIME_DEFAULT_EXTENSION_HEADER_ID, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FEC_PAYLOAD_TYPE,
      g_param_spec_uint ("fec-payload-type",
          "Set or get the payload type of FEC packets.",
          "Set or get the payload type of FEC packets.",
          0, 127, FEC_PAYLOAD_DEFAULT_ID, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MPATH_KEYFRAME_FILTERING,
      g_param_spec_uint ("mpath-keyframe-filtering",
          "Set or get the keyframe filtering for multiple path",
          "Set or get the keyframe filtering for multiple path. 0 - no keyframe filtering, 1 - vp8 enc/dec filtering",
          0, 255, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PACKET_OBSOLATION_TRESHOLD,
      g_param_spec_uint ("obsolation-treshold",
          "Set the obsolation treshold for packets.",
          "Set the obsolation treshold for packets.",
          0, 10000, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_JOIN_SUBFLOW,
      g_param_spec_uint ("join-subflow",
          "Join a subflow with a given id",
          "Join a subflow with a given id.",
          0, MPRTP_PLUGIN_MAX_SUBFLOW_NUM, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DETACH_SUBFLOW,
      g_param_spec_uint ("detach-subflow",
          "Detach a subflow with a given id.",
          "Detach a subflow with a given id.",
          0, MPRTP_PLUGIN_MAX_SUBFLOW_NUM, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SET_SENDING_TARGET,
      g_param_spec_uint ("sending-target",
          "Set the sending target for subflows",
          "Set the sending target for subflows",
          0, 4294967295, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SETUP_RTCP_INTERVAL_TYPE,
     g_param_spec_uint ("rtcp-interval-type",
                        "RTCP interval types: 0 - regular, 1 - early, 2 - immediate feedback",
                        "RTCP interval types: 0 - regular, 1 - early, 2 - immediate feedback",
                        0,
                        4294967295, 2, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SETUP_REPORT_TIMEOUT,
      g_param_spec_uint ("report-timeout",
          "Set a report timeout in ms for detaching",
          "Set a report timeout in ms for detaching",
          0, 4294967295, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SETUP_CONTROLLING_MODE,
      g_param_spec_uint ("controlling-mode",
          "Set the controlling mode. 0 - None, 1 - Regular, 2 - FRACTaL",
          "Set the controlling mode. 0 - None, 1 - Regular, 2 - FRACTaL",
          0, 4294967295, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FEC_INTERVAL,
      g_param_spec_uint ("fec-interval",
          "Set a stable FEC interval",
          "Set a stable FEC interval",
          0, 15, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ALLOWED_SSRC,
      g_param_spec_uint ("allowed-ssrc",
          "Apply MPRTP and Congestion Control only for a certain packet with the given SSRC (0 means any)",
          "Apply MPRTP and Congestion Control only for a certain packet with the given SSRC (0 means any)",
          0, 4294967295, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  _subflows_utilization =
      g_signal_new ("mprtp-subflows-utilization", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstMprtpschedulerClass, mprtp_media_rate_utilization),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 1,
      G_TYPE_POINTER);


}
#include "swperctester.h"

static void
gst_mprtpscheduler_init (GstMprtpscheduler * this)
{
//  {
//    swperctester_do();
//    g_print("%d", ((SndPacket*)(NULL))->abs_seq); // termin
//  }

  this->sendq = g_async_queue_new();

  this->rtp_sinkpad =
      gst_pad_new_from_static_template (&gst_mprtpscheduler_rtp_sink_template,
      "rtp_sink");

  gst_pad_set_chain_function (this->rtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpscheduler_rtp_sink_chain));
  gst_pad_set_chain_list_function (this->rtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpscheduler_rtp_sink_chainlist));

  GST_PAD_SET_PROXY_CAPS (this->rtp_sinkpad);
  GST_PAD_SET_PROXY_ALLOCATION (this->rtp_sinkpad);

  //  gst_pad_set_event_function (this->rtp_sinkpad,
  //      GST_DEBUG_FUNCPTR (gst_mprtpscheduler_sink_eventfunc));
  gst_pad_set_event_function (this->rtp_sinkpad,
        GST_DEBUG_FUNCPTR (gst_mprtpscheduler_sink_event));

  gst_element_add_pad (GST_ELEMENT (this), this->rtp_sinkpad);

  this->mprtcp_rr_sinkpad =
      gst_pad_new_from_static_template(&gst_mprtpscheduler_mprtcp_rr_sink_template,
      "mprtcp_rr_sink");

  gst_pad_set_chain_function (this->mprtcp_rr_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpscheduler_mprtcp_rr_sink_chain));

  gst_element_add_pad (GST_ELEMENT (this), this->mprtcp_rr_sinkpad);

  this->mprtcp_sr_srcpad =
      gst_pad_new_from_static_template(&gst_mprtpscheduler_mprtcp_sr_src_template,
      "mprtcp_sr_src");

  gst_element_add_pad (GST_ELEMENT (this), this->mprtcp_sr_srcpad);

  this->mprtp_srcpad =
      gst_pad_new_from_static_template (&gst_mprtpscheduler_mprtp_src_template,
      "mprtp_src");

  gst_pad_set_event_function (this->mprtp_srcpad,
      GST_DEBUG_FUNCPTR (gst_mprtpscheduler_mprtp_src_event));
  //oh crap... TODO: why is thAT if I use proxy caps than mprtp_srcpad
  //will not be linked if I linked it with mprtpreceiver mprtcp_rr_sinkpad
//  gst_pad_use_fixed_caps (this->mprtp_srcpad);
//  GST_PAD_SET_PROXY_CAPS (this->mprtp_srcpad);
  GST_PAD_SET_PROXY_ALLOCATION (this->mprtp_srcpad);

  gst_pad_set_query_function(this->mprtp_srcpad,
    GST_DEBUG_FUNCPTR(gst_mprtpscheduler_src_query));

  gst_element_add_pad (GST_ELEMENT (this), this->mprtp_srcpad);

  this->sysclock = gst_system_clock_obtain ();
  this->thread = gst_task_new (mprtpscheduler_emitter_process, this, NULL);

  g_mutex_init (&this->mutex);
  g_cond_init(&this->waiting_signal);
  g_cond_init(&this->receiving_signal);

  this->fec_payload_type = FEC_PAYLOAD_DEFAULT_ID;

  this->monitoring    = make_mediator();
  this->on_rtcp_ready = make_notifier("MPRTPSch: on-rtcp-ready");

  this->subflows      = make_sndsubflows(this->monitoring);
  this->sndpackets    = make_sndpackets();
  this->emit_msger    = make_messenger(sizeof(MPRTPPluginSignal));
  this->allowed_ssrc  = 0;

  this->sndqueue      = make_sndqueue(this->subflows);
  this->sndtracker    = make_sndtracker(this->subflows, this->sndqueue);
  this->splitter      = make_stream_splitter(this->subflows, this->sndtracker, this->sndqueue);
  this->fec_encoder   = make_fecencoder(this->monitoring);


  this->controller = make_sndctrler(
      this->sndtracker,
      this->subflows,
      this->on_rtcp_ready,
      this->emit_msger);

  fecencoder_set_payload_type(this->fec_encoder, this->fec_payload_type);

  sndsubflows_add_on_desired_bitrate_changed_cb(this->subflows,
      (ListenerFunc) stream_splitter_on_subflow_desired_target_chaned,
      this->splitter);

  sndsubflows_add_on_stable_target_bitrate_changed_cb(this->subflows,
      (ListenerFunc) stream_splitter_on_subflow_stable_target_bitrate_chaned,
      this->splitter);

  sndsubflows_add_on_subflow_state_changed_cb(this->subflows,
      (ListenerFunc) stream_splitter_on_subflow_state_changed,
      this->splitter);

  sndsubflows_add_on_subflow_state_stat_changed_cb(this->subflows,
      (ListenerFunc) stream_splitter_on_subflow_state_stat_changed,
      this->splitter);

  sndtracker_add_on_packet_sent(this->sndtracker,
      (ListenerFunc) stream_splitter_on_packet_sent,
      this->splitter);

  sndqueue_add_on_packet_queued(this->sndqueue,
      (ListenerFunc) stream_splitter_on_packet_queued,
      this->splitter);

  sndtracker_add_on_packet_obsolated(this->sndtracker,
      (ListenerFunc) stream_splitter_on_packet_obsolated,
      this->splitter);

  sndsubflows_add_on_subflow_joined_cb(this->subflows,
     (ListenerFunc) stream_splitter_on_subflow_joined,
     this->splitter);

  sndsubflows_add_on_subflow_detached_cb(this->subflows,
       (ListenerFunc) stream_splitter_on_subflow_detached,
       this->splitter);

  sndsubflows_add_on_subflow_joined_cb(this->subflows,
     (ListenerFunc) sndqueue_on_subflow_joined,
     this->sndqueue);

  sndsubflows_add_on_subflow_detached_cb(this->subflows,
     (ListenerFunc) sndqueue_on_subflow_detached,
     this->sndqueue);

  sndsubflows_add_on_desired_bitrate_changed_cb(this->subflows,
     (ListenerFunc) sndqueue_on_subflow_target_bitrate_changed,
     this->sndqueue);

  sndsubflows_add_on_subflow_state_stat_changed_cb(this->subflows,
        (ListenerFunc) sndqueue_on_subflow_state_changed,
        this->sndqueue);

  mediator_set_request_handler(this->monitoring,
      (ListenerFunc) _on_monitoring_request, this);

  mediator_set_response_handler(this->monitoring,
      (ListenerFunc) _on_monitoring_response, this);

  notifier_add_listener(this->on_rtcp_ready,
      (ListenerFunc) _on_rtcp_ready, this);

  this->obsolation_treshold = 0;//50 * GST_MSECOND;
  this->fec_responses = make_messenger(sizeof(FECEncoderResponse*));

  this->abs_time_ext_header_id   = ABS_TIME_DEFAULT_EXTENSION_HEADER_ID;
  this->cc_ts_generator             = g_object_ref(sndtracker_get_ts_generator(this->sndtracker));

}


void
gst_mprtpscheduler_dispose (GObject * object)
{
  GstMprtpscheduler *mprtpscheduler = GST_MPRTPSCHEDULER (object);

  GST_DEBUG_OBJECT (mprtpscheduler, "dispose");

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (gst_mprtpscheduler_parent_class)->dispose (object);
}

void
gst_mprtpscheduler_finalize (GObject * object)
{
  GstMprtpscheduler *this = GST_MPRTPSCHEDULER (object);

  GST_DEBUG_OBJECT (this, "finalize");

  /* clean up object here */
  gst_task_join (this->thread);
  gst_object_unref (this->thread);

  g_async_queue_unref(this->sendq);

  g_object_unref (this->sysclock);
  g_object_unref (this->subflows);
  g_object_unref (this->sndpackets);
  g_object_unref (this->splitter);
  g_object_unref (this->fec_encoder);
  g_object_unref (this->sndqueue);
  g_object_unref (this->cc_ts_generator);
  g_object_unref(this->emit_msger);

  G_OBJECT_CLASS (gst_mprtpscheduler_parent_class)->finalize (object);
}

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

void
gst_mprtpscheduler_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMprtpscheduler *this = GST_MPRTPSCHEDULER (object);
  guint guint_value;
  SubflowSpecProp *subflow_prop;

  subflow_prop = (SubflowSpecProp*) &guint_value;
  GST_DEBUG_OBJECT (this, "set_property");
  THIS_LOCK(this);
  switch (property_id) {
    case PROP_MPRTP_EXT_HEADER_ID:
      sndpackets_set_mprtp_ext_header_id(this->sndpackets, (guint8) g_value_get_uint (value));
      break;
    case PROP_ABS_TIME_EXT_HEADER_ID:
      this->abs_time_ext_header_id = g_value_get_uint (value);
      sndpackets_set_abs_time_ext_header_id(this->sndpackets, (guint8) g_value_get_uint (value));
      break;
    case PROP_FEC_PAYLOAD_TYPE:
      this->fec_payload_type = (guint8) g_value_get_uint (value);
      fecencoder_set_payload_type(this->fec_encoder, this->fec_payload_type);
      break;
    case PROP_JOIN_SUBFLOW:
      sndsubflows_join(this->subflows, g_value_get_uint (value));
      break;
    case PROP_MPATH_KEYFRAME_FILTERING:
      guint_value = g_value_get_uint (value);
      sndpackets_set_keyframe_filter_mode(this->sndpackets, guint_value);
      stream_splitter_set_keyframe_filtering(this->splitter, 0 < guint_value);
      break;
    case PROP_PACKET_OBSOLATION_TRESHOLD:
      this->obsolation_treshold = g_value_get_uint(value) * GST_MSECOND;
      break;
    case PROP_DETACH_SUBFLOW:
      sndsubflows_detach(this->subflows, g_value_get_uint (value));
      break;
    case PROP_FEC_INTERVAL:
      this->fec_interval = g_value_get_uint (value);
      break;
    case PROP_ALLOWED_SSRC:
      this->allowed_ssrc = g_value_get_uint (value);
      break;
    case PROP_SET_SENDING_TARGET:
      guint_value = g_value_get_uint (value);
      sndsubflows_set_target_bitrate(this->subflows, subflow_prop->id, subflow_prop->value);
      break;
    case PROP_SETUP_RTCP_INTERVAL_TYPE:
      guint_value = g_value_get_uint (value);
      sndsubflows_set_rtcp_interval_type(this->subflows, subflow_prop->id, subflow_prop->value);
      break;
    case PROP_SETUP_CONTROLLING_MODE:
      guint_value = g_value_get_uint (value);
      sndsubflows_set_congestion_controlling_type(this->subflows, subflow_prop->id, subflow_prop->value);
      break;
    case PROP_SETUP_REPORT_TIMEOUT:
      guint_value = g_value_get_uint (value);
      sndsubflows_set_report_timeout(this->subflows, subflow_prop->id, subflow_prop->value * GST_MSECOND);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  THIS_UNLOCK(this);
}


void
gst_mprtpscheduler_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstMprtpscheduler *this = GST_MPRTPSCHEDULER (object);

  GST_DEBUG_OBJECT (this, "get_property");
  THIS_LOCK(this);
  switch (property_id) {
    case PROP_MPRTP_EXT_HEADER_ID:
      g_value_set_uint (value, (guint) sndpackets_get_mprtp_ext_header_id(this->sndpackets));
      break;
    case PROP_ABS_TIME_EXT_HEADER_ID:
      g_value_set_uint (value, (guint) sndpackets_get_abs_time_ext_header_id(this->sndpackets));
      break;
//    case PROP_MPATH_KEYFRAME_FILTERING:
//      g_warning("path keyframe filtering is not implemented yet");
//      break;
    case PROP_FEC_PAYLOAD_TYPE:
      g_value_set_uint (value, (guint) this->fec_payload_type);
      break;
    case PROP_PACKET_OBSOLATION_TRESHOLD:
      g_value_set_uint(value, this->obsolation_treshold / GST_MSECOND);
      break;
    case PROP_FEC_INTERVAL:
      g_value_set_uint (value, (guint) this->fec_interval);
      break;
    case PROP_ALLOWED_SSRC:
      g_value_set_uint (value, (guint) this->allowed_ssrc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  THIS_UNLOCK(this);

}


static GstStateChangeReturn
gst_mprtpscheduler_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstMprtpscheduler * this;

  this = GST_MPRTPSCHEDULER (element);
  g_return_val_if_fail (GST_IS_MPRTPSCHEDULER (element),
      GST_STATE_CHANGE_FAILURE);
   
  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
       gst_pad_start_task(this->mprtp_srcpad, (GstTaskFunction)mprtpscheduler_approval_process,
         this, NULL);
       gst_task_set_lock (this->thread, &this->thread_mutex);
       gst_task_start (this->thread);
       break;
     default:
       break;
   }

   ret =
       GST_ELEMENT_CLASS (gst_mprtpscheduler_parent_class)->change_state
       (element, transition);

   switch (transition) {
     case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
       gst_task_stop (this->thread);
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
gst_mprtpscheduler_query (GstElement * element, GstQuery * query)
{
  GstMprtpscheduler *this = GST_MPRTPSCHEDULER (element);
  gboolean ret = TRUE;
  GstStructure *s = NULL;

  GST_DEBUG_OBJECT (this, "query");
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CUSTOM:
      THIS_LOCK(this);
      s = gst_query_writable_structure (query);
      if (!gst_structure_has_name (s,
              GST_MPRTCP_SCHEDULER_SENT_BYTES_STRUCTURE_NAME)) {
        ret =
            GST_ELEMENT_CLASS (gst_mprtpscheduler_parent_class)->query (element,
            query);
        break;
      }
      gst_structure_set (s,
          GST_MPRTCP_SCHEDULER_SENT_OCTET_SUM_FIELD,
          G_TYPE_UINT, this->rtcp_sent_octet_sum, NULL);
      THIS_UNLOCK(this);
      ret = TRUE;
      break;
    default:
      ret =
          GST_ELEMENT_CLASS (gst_mprtpscheduler_parent_class)->query (element,
          query);
      break;
  }

  return ret;
}


static GstFlowReturn
gst_mprtpscheduler_rtp_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstMprtpscheduler *this;
  GstFlowReturn result;
  guint8 first_byte;
  guint8 second_byte;
  SndPacket* packet = NULL;
  SndSubflow* subflow = NULL;
  guint32 third_word;

  this = GST_MPRTPSCHEDULER (parent);

  if (GST_PAD_IS_FLUSHING(pad)) {
    result = GST_FLOW_FLUSHING;
    goto done;
  }

  //g_print("Sent: %lu\n", GST_TIME_AS_MSECONDS(_now(this)));
//  prev = gst_clock_get_time(this->sysclock);
  if (gst_buffer_extract (buffer, 0, &first_byte, 1) != 1 ||
      gst_buffer_extract (buffer, 1, &second_byte, 1) != 1) {
    GST_WARNING_OBJECT (this, "could not extract first byte from buffer");
    gst_buffer_unref (buffer);
    result = GST_FLOW_ERROR;
    goto done;
  }

  if (!PACKET_IS_RTP_OR_RTCP (first_byte)) {
    GST_DEBUG_OBJECT (this, "Not RTP Packet arrived at rtp_sink");
    gst_pad_push(this->mprtp_srcpad, buffer);
    result = GST_FLOW_ERROR;
    goto done;
  }

  result = GST_FLOW_OK;

  if(PACKET_IS_RTCP(second_byte)){
    GST_DEBUG_OBJECT (this, "RTCP Packet arrived on rtp sink");
    gst_pad_push(this->mprtp_srcpad, buffer);
    goto done;
  }

  if(0 < this->allowed_ssrc && gst_buffer_extract (buffer, 8, &third_word, 4) != 1){
    guint32 packet_ssrc = g_ntohs(third_word);
    if(packet_ssrc != this->allowed_ssrc){
      GST_DEBUG_OBJECT (this, "RTP packet with SSRC %d let to pass (allowed SSRC: %d)", packet_ssrc, this->allowed_ssrc);
      gst_pad_push(this->mprtp_srcpad, buffer);
      goto done;
    }
  }

  //The problem if we add the fec here: We add an extesnion thereafter and
  //meanwhile the receiver side the bitstting considering the extension
  //here we don't at the creation.
  //fecencoder_add_rtpbuffer(this->fec_encoder, gst_buffer_ref(buffer));

  THIS_LOCK(this);
PROFILING("gst_mprtpscheduler_rtp_sink_chain",
  if(!sndsubflows_get_subflows_num(this->subflows)){
    result = gst_pad_push(this->mprtp_srcpad, buffer);
    goto unlock_and_done;
  }
  {
//    GstBuffer* newbuf = NULL;
//    gst_buffer_pool_acquire_buffer(this->mprtp_buffer_pool, &newbuf, NULL);
//    g_print("%p %d %d, %p->%p", newbuf, buffer->mini_object.refcount, gst_buffer_is_all_memory_writable(buffer),
//        buffer, gst_buffer_make_writable(buffer));
//    gst_buffer_copy_into(newbuf, buffer, GST_BUFFER_COPY_ALL, 0, -1);
//    gst_buffer_unref(buffer);
//    packet = sndpackets_make_packet(this->sndpackets, newbuf);
//    exit(0);
  }

//  packet = sndpackets_make_packet(this->sndpackets, gst_buffer_ref(buffer));
  packet = sndpackets_make_packet(this->sndpackets, buffer);

  subflow = stream_splitter_select_subflow(this->splitter, packet);
  if(subflow){
    sndpacket_setup_mprtp(packet, subflow->id, sndsubflow_get_next_subflow_seq(subflow));
    fecencoder_add_rtpbuffer(this->fec_encoder, gst_buffer_ref(packet->buffer));
    sndqueue_push_packet(this->sndqueue, packet);
    g_cond_signal(&this->receiving_signal);
  }
);
unlock_and_done:
  THIS_UNLOCK(this);

done:
  return result;
}


static GstFlowReturn
gst_mprtpscheduler_rtp_sink_chainlist (GstPad * pad, GstObject * parent,
    GstBufferList * list)
{
  GstBuffer *buffer;
  gint i, len;
  GstFlowReturn result;

  result = GST_FLOW_OK;
PROFILING2("gst_mprtpscheduler_rtp_sink_chainlist",
  /* chain each buffer in list individually */
  len = gst_buffer_list_length (list);

  if (len == 0)
    goto done;

  for (i = 0; i < len; i++) {
    buffer = gst_buffer_list_get (list, i);

    result = gst_mprtpscheduler_rtp_sink_chain (pad, parent, buffer);
    if (result != GST_FLOW_OK)
      break;
  }
);
done:
  return result;
}


static GstFlowReturn
gst_mprtpscheduler_mprtcp_rr_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buf)
{
  GstMprtpscheduler *this;
  GstFlowReturn result;

//      GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
//      gst_rtcp_buffer_map(buf, GST_MAP_READ, &rtcp);
//      gst_print_rtcp_buffer(&rtcp);
//      gst_rtcp_buffer_unmap(&rtcp);


  this = GST_MPRTPSCHEDULER (parent);

  //PROFILING("THIS_LOCK",
    THIS_LOCK(this);
  //);

  PROFILING("sndctrler_receive_mprtcp",
    sndctrler_receive_mprtcp(this->controller, buf);
    result = GST_FLOW_OK;
  );
  //PROFILING("THIS_UNLOCK",
    THIS_UNLOCK(this);
  //);

  return result;

}



static gboolean gst_mprtpscheduler_sink_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
    gboolean ret;
PROFILING2("gst_mprtpscheduler_sink_event",
    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_CAPS:
        case GST_EVENT_FLUSH_STOP:
        case GST_EVENT_STREAM_START:
        case GST_EVENT_SEGMENT:
        default:
            ret = gst_pad_event_default(pad, parent, event);
            break;
    }
);
    return ret;
}

static gboolean gst_mprtpscheduler_mprtp_src_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
    gboolean ret;
PROFILING2("gst_mprtpscheduler_mprtp_src_event",
    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_FLUSH_START:
        case GST_EVENT_RECONFIGURE:
        case GST_EVENT_FLUSH_STOP:
        default:
            ret = gst_pad_event_default(pad, parent, event);
            break;
    }
);
    return ret;
}


static gboolean
gst_mprtpscheduler_src_query (GstPad * srckpad, GstObject * parent,
    GstQuery * query)
{
  GstMprtpscheduler *this = GST_MPRTPSCHEDULER (parent);
  gboolean result;

  GST_DEBUG_OBJECT (this, "query");

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      gboolean live;
      GstClockTime min, max;
      GstPad *peer;
    PROFILING("gst_mprtpscheduler_src_query",
      peer = gst_pad_get_peer (this->rtp_sinkpad);
      if ((result = gst_pad_query (peer, query))) {
          gst_query_parse_latency (query, &live, &min, &max);
          min= GST_MSECOND;
          max = -1;
          gst_query_set_latency (query, live, min, max);
      }
      gst_object_unref (peer);
    );
    }
    break;
    default:
      result = gst_pad_query_default(srckpad, parent, query);
      break;
  }

  return result;
}

void _on_rtcp_ready(GstMprtpscheduler * this, GstBuffer *buffer)
{
//    GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
//    gst_rtcp_buffer_map(buffer, GST_MAP_READ, &rtcp);
//    gst_print_rtcp_buffer(&rtcp);
//    gst_rtcp_buffer_unmap(&rtcp);
  if(!gst_pad_is_linked(this->mprtcp_sr_srcpad)){
    GST_WARNING_OBJECT(this, "Pads are not linked for MPRTCP");
    return;
  }
//  g_print("On RTCP SR sending - %d\n", gst_pad_push(this->mprtcp_sr_srcpad, buffer));
PROFILING("_on_rtcp_ready",
  gst_pad_push(this->mprtcp_sr_srcpad, buffer);
);
}

void _on_monitoring_request(GstMprtpscheduler * this, SndSubflow* subflow)
{
PROFILING("_on_monitoring_request",
  if(this->fec_requested){
    return;
  }
  fecencoder_request_fec(this->fec_encoder, subflow->id, subflow->monitoring_interval);
  this->fec_requested = TRUE;
);
}

void _on_monitoring_response(GstMprtpscheduler * this, FECEncoderResponse *response)
{
PROFILING("_on_monitoring_response",
  messenger_push_block(this->fec_responses, response);
);
//  THIS_LOCK(this);
//  sndtracker_add_fec_response(this->sndtracker, response);
//  gst_pad_push(this->mprtp_srcpad, response->fecbuffer);
//  fecencoder_unref_response(response);
//  THIS_UNLOCK(this);
}

void
_mprtpscheduler_send_packet (GstMprtpscheduler * this, SndPacket *packet)
{
  GstBuffer *buffer;
PROFILING("_mprtpscheduler_send_packet",
  sndtracker_packet_sent(this->sndtracker, packet);
  buffer = sndpacket_retrieve(packet);
);
//  );
//  g_print("Packet sent  flow result: %d\n", gst_pad_push(this->mprtp_srcpad, buffer));

//  artifital lost
//  g_print("Sent packet: %hu\n", this->sent_packets);
//  if(this->sent_packets % 21 == 0){
//    gst_buffer_unref(buffer);
//  }else{
//    gst_pad_push(this->mprtp_srcpad, buffer);
//  }
  PROFILING("gst_pad_push",
  gst_pad_push(this->mprtp_srcpad, buffer);
  //TODO: should goes to a sent process, but we stop adding the abs_time_ext_header
  packet->sent_ts = timestamp_generator_get_ts(this->cc_ts_generator);
//  g_async_queue_push(this->sendq, buffer);
  );
  if(this->fec_requested){
    FECEncoderResponse* response;
    response = messenger_try_pop_block(this->fec_responses);
    if(response){
      sndtracker_add_fec_response(this->sndtracker, response);
      //PROFILING("fec_requested",
      gst_pad_push(this->mprtp_srcpad, response->fecbuffer);
      //);
//        g_async_queue_push(this->sendq, response->fecbuffer);
      fecencoder_unref_response(response);
      this->fec_requested = FALSE;
    }
  }

  if (!this->report_flow_signal_sent) {
    this->report_flow_signal_sent = TRUE;
    sndctrler_report_can_flow(this->controller);
  }
  return;
}

static void
mprtpscheduler_approval_process (GstMprtpscheduler *this)
{
  SndPacket *packet;
  GstClockTime now, next_time = 0;
//  RTPQueueStat* rtpqstat;

//PROFILING("mprtpscheduler_approval_process_LOCK",
  THIS_LOCK(this);
//);

  if(sndqueue_is_empty(this->sndqueue)){
    g_cond_wait(&this->receiving_signal, &this->mutex);
  }

  sndtracker_refresh(this->sndtracker);
  sndctrler_time_update(this->controller);
//  rtpqstat = sndtracker_get_rtpqstat(this->sndtracker);

  now = _now(this);
  next_time = now;
  // TODO: Pacing is turned off now, by commenting out the important code snippet in sending queue.
  packet = sndqueue_pop_packet(this->sndqueue, &next_time);
  if (now < next_time) {
    // It is really important to place a cond wait here.
    // If you do not do it, scheduler faces an issue according to
    // two active thread trying to compete. Once spinning and the other is waiting at the lock
    // to push a packet into the sndqueue.
    // It results spurious peaks on sending rate
    while(g_cond_wait_until(&this->receiving_signal, &this->mutex, GST_TIME_AS_USECONDS(next_time - now))) {
      now = _now(this);
    }
    goto done;
  }
  if (!packet) {
    goto done;
  }

  //Obsolete packets stayed in the q for a while
  if(0 < this->obsolation_treshold && packet->made < now - this->obsolation_treshold){
    gst_buffer_unref(sndpacket_retrieve(packet));
    goto done;
  }

 //  g_print("Subflow: %d\n", subflow->id);
//  this->fec_interval = 5;
//PROFILING("approval process: _on_monitoring_request",
  if(0 < this->fec_interval && (++this->sent_packets % this->fec_interval) == 0){
    SndSubflow* subflow = sndsubflows_get_subflow(this->subflows, packet->subflow_id);
    _on_monitoring_request(this, subflow);
  }
//);
// g_print("Packet %hu on %d with subflow seq %hu sent.\n", packet->abs_seq, packet->subflow_id, packet->subflow_seq);
  //PROFILING("_mprtpscheduler_send_packet",
    _mprtpscheduler_send_packet(this, packet);
  //);

done:
  THIS_UNLOCK(this);
  return;
}




void
mprtpscheduler_emitter_process (gpointer udata)
{
  GstMprtpscheduler *this;
  MPRTPPluginSignal *msg;

  this = (GstMprtpscheduler *) udata;

  messenger_lock(this->emit_msger);

  msg = messenger_pop_block_with_timeout_unlocked(this->emit_msger, 10000);

  if(!msg){
    goto done;
  }
//PROFILING("mprtpscheduler_emitter_process",
  msg->target_media_rate = stream_splitter_get_total_media_rate(this->splitter);// + 100000;
//  g_print("Requested media rate: %d\n", msg->target_media_rate);
  g_signal_emit (this, _subflows_utilization, 0 /* details */, msg);
  messenger_throw_block_unlocked(this->emit_msger, msg);
//);
done:
  messenger_unlock(this->emit_msger);
  return;
}

