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
#include "mprtpspath.h"
#include "streamsplitter.h"
#include "gstmprtcpbuffer.h"
#include "sndctrler.h"
#include <sys/timex.h>

GST_DEBUG_CATEGORY_STATIC (gst_mprtpscheduler_debug_category);
#define GST_CAT_DEFAULT gst_mprtpscheduler_debug_category

#define PACKET_IS_RTP_OR_RTCP(b) (b > 0x7f && b < 0xc0)
#define PACKET_IS_RTCP(b) (b > 192 && b < 223)
#define THIS_WRITELOCK(this) (g_rw_lock_writer_lock(&this->rwmutex))
#define THIS_WRITEUNLOCK(this) (g_rw_lock_writer_unlock(&this->rwmutex))
#define THIS_READLOCK(this) (g_rw_lock_reader_lock(&this->rwmutex))
#define THIS_READUNLOCK(this) (g_rw_lock_reader_unlock(&this->rwmutex))

#define MPRTP_SENDER_DEFAULT_ALPHA_VALUE 0.5
#define MPRTP_SENDER_DEFAULT_BETA_VALUE 0.1
#define MPRTP_SENDER_DEFAULT_GAMMA_VALUE 0.2


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
static void
gst_mprtpscheduler_mprtp_proxy(gpointer ptr, GstBuffer * buffer);
static void
gst_mprtpscheduler_emit_signal(gpointer ptr, gpointer data);
static GstFlowReturn gst_mprtpscheduler_rtp_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static GstFlowReturn gst_mprtpscheduler_rtp_sink_chainlist (GstPad * pad,
    GstObject * parent, GstBufferList * list);

static GstFlowReturn gst_mprtpscheduler_mprtcp_rr_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * outbuf);

static gboolean gst_mprtpscheduler_src_query (GstPad * srckpad, GstObject * parent,
    GstQuery * query);

static void gst_mprtpscheduler_mprtcp_sender (gpointer ptr, GstBuffer * buf);
static void _join_subflow (GstMprtpscheduler * this, guint subflow_id);
static void _detach_subflow (GstMprtpscheduler * this, guint subflow_id);
static gboolean
_try_get_path (GstMprtpscheduler * this, guint16 subflow_id,
    MPRTPSPath ** result);
static void _change_path_state (GstMprtpscheduler * this, guint8 subflow_id,
    gboolean set_congested, gboolean set_lossy);
static void _change_auto_rate_and_cc (GstMprtpscheduler * this,
    gboolean auto_rate_and_cc);
static gboolean gst_mprtpscheduler_mprtp_src_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_mprtpscheduler_sink_eventfunc (GstPad * srckpad, GstObject * parent,
                                                   GstEvent * event);
static GstStructure *_collect_infos (GstMprtpscheduler * this);
static void _setup_paths (GstMprtpscheduler * this);
static void gst_mprtpscheduler_path_ticking_process_run (void *data);


static guint _subflows_utilization;


enum
{
  PROP_0,
  PROP_MPRTP_EXT_HEADER_ID,
  PROP_ABS_TIME_EXT_HEADER_ID,
  PROP_MONITORING_PAYLOAD_TYPE,
  PROP_JOIN_SUBFLOW,
  PROP_DETACH_SUBFLOW,
  PROP_SET_SUBFLOW_NON_CONGESTED,
  PROP_SET_SUBFLOW_CONGESTED,
  PROP_AUTO_RATE_AND_CC,
  PROP_SET_SENDING_TARGET,
  PROP_INITIAL_DISABLING,
  PROP_SUBFLOWS_STATS,
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
          "Set or get the id for the Multipath RTP extension",
          "Sets or gets the id for the extension header the MpRTP based on. The default is 3",
          0, 15, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ABS_TIME_EXT_HEADER_ID,
      g_param_spec_uint ("abs-time-ext-header-id",
          "Set or get the id for the absolute time RTP extension",
          "Sets or gets the id for the extension header the absolute time based on. The default is 8",
          0, 15, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MONITORING_PAYLOAD_TYPE,
      g_param_spec_uint ("monitoring-payload-type",
          "Set or get the payload type of monitoring packets",
          "Set or get the payload type of monitoring packets. The default is 8",
          0, 127, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_JOIN_SUBFLOW,
      g_param_spec_uint ("join-subflow", "the subflow id requested to join",
          "Join a subflow with a given id.", 0,
          MPRTP_PLUGIN_MAX_SUBFLOW_NUM, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DETACH_SUBFLOW,
      g_param_spec_uint ("detach-subflow", "the subflow id requested to detach",
          "Detach a subflow with a given id.", 0,
          MPRTP_PLUGIN_MAX_SUBFLOW_NUM, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SET_SUBFLOW_CONGESTED,
      g_param_spec_uint ("congested-subflow", "set the subflow congested",
          "Set the subflow congested", 0,
          MPRTP_PLUGIN_MAX_SUBFLOW_NUM, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_SET_SUBFLOW_NON_CONGESTED,
      g_param_spec_uint ("non-congested-subflow",
          "set the subflow non-congested", "Set the subflow non-congested", 0,
          MPRTP_PLUGIN_MAX_SUBFLOW_NUM, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_AUTO_RATE_AND_CC,
      g_param_spec_boolean ("auto-rate-and-cc",
          "Enables automatic rate and congestion controller",
          "Enables automatic rate and congestion controller",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SET_SENDING_TARGET,
      g_param_spec_uint ("setup-sending-target",
          "set the sending target of the subflow",
          "A 32bit unsigned integer for setup a target. The first 8 bit identifies the subflow, the latter the target",
          0, 4294967295, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_INITIAL_DISABLING,
      g_param_spec_uint64 ("initial-disabling",
          "set an initial disabling time for rate controller in order to collect statistics at the beginning.",
          "set an initial disabling time for rate controller in order to collect statistics at the beginning.",
          0, 3600 * GST_SECOND, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SUBFLOWS_STATS,
      g_param_spec_string ("subflow-stats",
          "Extract subflow stats",
          "Collect subflow statistics and return with "
          "a structure contains it",
          "NULL", G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  _subflows_utilization =
      g_signal_new ("mprtp-subflows-utilization", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstMprtpschedulerClass, mprtp_media_rate_utilization),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 1,
      G_TYPE_POINTER);


}

static void
gst_mprtpscheduler_init (GstMprtpscheduler * this)
{
//  GstMprtpschedulerPrivate *priv;
//  priv = this->priv = GST_MPRTPSCHEDULER_GET_PRIVATE (this);

  this->rtp_sinkpad =
      gst_pad_new_from_static_template (&gst_mprtpscheduler_rtp_sink_template,
      "rtp_sink");

  gst_pad_set_chain_function (this->rtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpscheduler_rtp_sink_chain));
  gst_pad_set_chain_list_function (this->rtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpscheduler_rtp_sink_chainlist));

  GST_PAD_SET_PROXY_CAPS (this->rtp_sinkpad);
  GST_PAD_SET_PROXY_ALLOCATION (this->rtp_sinkpad);


  gst_element_add_pad (GST_ELEMENT (this), this->rtp_sinkpad);

  this->mprtcp_rr_sinkpad =
      gst_pad_new_from_static_template
      (&gst_mprtpscheduler_mprtcp_rr_sink_template, "mprtcp_rr_sink");

  gst_pad_set_chain_function (this->mprtcp_rr_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpscheduler_mprtcp_rr_sink_chain));

  gst_pad_set_event_function (this->rtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpscheduler_sink_eventfunc));

  gst_element_add_pad (GST_ELEMENT (this), this->mprtcp_rr_sinkpad);

  this->mprtcp_sr_srcpad =
      gst_pad_new_from_static_template
      (&gst_mprtpscheduler_mprtcp_sr_src_template, "mprtcp_sr_src");

  gst_element_add_pad (GST_ELEMENT (this), this->mprtcp_sr_srcpad);

  this->mprtp_srcpad =
      gst_pad_new_from_static_template (&gst_mprtpscheduler_mprtp_src_template,
      "mprtp_src");

  gst_pad_set_event_function (this->mprtp_srcpad,
      GST_DEBUG_FUNCPTR (gst_mprtpscheduler_mprtp_src_event));
  gst_pad_use_fixed_caps (this->mprtp_srcpad);
  GST_PAD_SET_PROXY_CAPS (this->mprtp_srcpad);
  GST_PAD_SET_PROXY_ALLOCATION (this->mprtp_srcpad);

  gst_pad_set_query_function(this->mprtp_srcpad,
    GST_DEBUG_FUNCPTR(gst_mprtpscheduler_src_query));

  gst_element_add_pad (GST_ELEMENT (this), this->mprtp_srcpad);

  this->path_ticking_thread =
      gst_task_new (gst_mprtpscheduler_path_ticking_process_run, this, NULL);
  this->sysclock = gst_system_clock_obtain ();
  g_rw_lock_init (&this->rwmutex);
  this->paths = g_hash_table_new_full (NULL, NULL, NULL, g_free);
  this->mprtp_ext_header_id = MPRTP_DEFAULT_EXTENSION_HEADER_ID;
  this->abs_time_ext_header_id = ABS_TIME_DEFAULT_EXTENSION_HEADER_ID;
  this->monitor_payload_type = MONITOR_PAYLOAD_DEFAULT_ID;
  this->paths = g_hash_table_new_full (NULL, NULL, NULL, g_free);
  this->splitter = (StreamSplitter *) g_object_new (STREAM_SPLITTER_TYPE, NULL);
  this->controller = (SndController*) g_object_new(SNDCTRLER_TYPE, NULL);
  sndctrler_setup(this->controller, this->splitter);
  sndctrler_setup_callbacks(this->controller,
                            this, gst_mprtpscheduler_mprtcp_sender,
                            this, gst_mprtpscheduler_emit_signal
                            );
  this->monitorpackets = make_monitorpackets();
  stream_splitter_set_monitor_payload_type(this->splitter, this->monitor_payload_type);
  _change_auto_rate_and_cc (this, FALSE);
  _setup_paths(this);
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
  gst_task_join (this->path_ticking_thread);
  gst_object_unref (this->path_ticking_thread);

  g_object_unref (this->sysclock);
  G_OBJECT_CLASS (gst_mprtpscheduler_parent_class)->finalize (object);
}




void
gst_mprtpscheduler_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMprtpscheduler *this = GST_MPRTPSCHEDULER (object);
  guint guint_value;
  gboolean gboolean_value;
  guint8 subflow_id;
  guint subflow_target;
  guint64 guint64_value;

  GST_DEBUG_OBJECT (this, "set_property");

  switch (property_id) {
    case PROP_MPRTP_EXT_HEADER_ID:
      THIS_WRITELOCK (this);
      this->mprtp_ext_header_id = (guint8) g_value_get_uint (value);
      _setup_paths(this);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_ABS_TIME_EXT_HEADER_ID:
      THIS_WRITELOCK (this);
      this->abs_time_ext_header_id = (guint8) g_value_get_uint (value);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_MONITORING_PAYLOAD_TYPE:
      THIS_WRITELOCK (this);
      this->monitor_payload_type = (guint8) g_value_get_uint (value);
      _setup_paths(this);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_JOIN_SUBFLOW:
      THIS_WRITELOCK (this);
      _join_subflow (this, g_value_get_uint (value));
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_DETACH_SUBFLOW:
      THIS_WRITELOCK (this);
      _detach_subflow (this, g_value_get_uint (value));
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_SET_SUBFLOW_CONGESTED:
      THIS_WRITELOCK (this);
      guint_value = g_value_get_uint (value);
      _change_path_state (this, (guint8) guint_value, TRUE, FALSE);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_SET_SUBFLOW_NON_CONGESTED:
      THIS_WRITELOCK (this);
      guint_value = g_value_get_uint (value);
      _change_path_state (this, (guint8) guint_value, FALSE, FALSE);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_AUTO_RATE_AND_CC:
      THIS_WRITELOCK (this);
      gboolean_value = g_value_get_boolean (value);
      _change_auto_rate_and_cc (this, gboolean_value);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_SET_SENDING_TARGET:
      THIS_WRITELOCK (this);
      guint_value = g_value_get_uint (value);
      subflow_id = (guint8) ((guint_value >> 24) & 0x000000FF);
      subflow_target = guint_value & 0x00FFFFFFUL;
      stream_splitter_setup_sending_target (this->splitter, subflow_id,
          subflow_target);
      stream_splitter_commit_changes (this->splitter);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_INITIAL_DISABLING:
      THIS_WRITELOCK (this);
      guint64_value = g_value_get_uint64 (value);
      sndctrler_set_initial_disabling(this->controller, guint64_value);
      THIS_WRITEUNLOCK (this);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


void
gst_mprtpscheduler_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstMprtpscheduler *this = GST_MPRTPSCHEDULER (object);

  GST_DEBUG_OBJECT (this, "get_property");

  switch (property_id) {
    case PROP_MPRTP_EXT_HEADER_ID:
      THIS_READLOCK (this);
      g_value_set_uint (value, (guint) this->mprtp_ext_header_id);
      THIS_READUNLOCK (this);
      break;
    case PROP_ABS_TIME_EXT_HEADER_ID:
      THIS_READLOCK (this);
      g_value_set_uint (value, (guint) this->abs_time_ext_header_id);
      THIS_READUNLOCK (this);
      break;
    case PROP_MONITORING_PAYLOAD_TYPE:
      THIS_READLOCK (this);
      g_value_set_uint (value, (guint) this->monitor_payload_type);
      THIS_READUNLOCK (this);
      break;
    case PROP_AUTO_RATE_AND_CC:
      THIS_READLOCK (this);
      g_value_set_boolean (value, this->auto_rate_and_cc);
      THIS_READUNLOCK (this);
      break;
    case PROP_SUBFLOWS_STATS:
      THIS_READLOCK (this);
      g_value_set_string (value,
          gst_structure_to_string (_collect_infos (this)));
      THIS_READUNLOCK (this);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

GstStructure *
_collect_infos (GstMprtpscheduler * this)
{
  GstStructure *result;
  GHashTableIter iter;
  gpointer key, val;
  MPRTPSPath *path;
  gint index = 0;
  GValue g_value = { 0 };
  gchar *field_name;
  result = gst_structure_new ("SchedulerSubflowReports",
      "length", G_TYPE_UINT, this->subflows_num, NULL);
  g_value_init (&g_value, G_TYPE_UINT);
  g_hash_table_iter_init (&iter, this->paths);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    path = (MPRTPSPath *) val;

    field_name = g_strdup_printf ("subflow-%d-id", index);
    g_value_set_uint (&g_value, mprtps_path_get_id (path));
    gst_structure_set_value (result, field_name, &g_value);
    g_free (field_name);

    field_name = g_strdup_printf ("subflow-%d-sent_packet_num", index);
    g_value_set_uint (&g_value, mprtps_path_get_total_sent_packets_num (path));
    gst_structure_set_value (result, field_name, &g_value);
    g_free (field_name);

    field_name = g_strdup_printf ("subflow-%d-sent_payload_bytes", index);
    g_value_set_uint (&g_value,
        mprtps_path_get_total_sent_payload_bytes (path));
    gst_structure_set_value (result, field_name, &g_value);
    g_free (field_name);

    ++index;
  }
  return result;
}


void
_setup_paths (GstMprtpscheduler * this)
{
  GHashTableIter iter;
  gpointer key, val;
  MPRTPSPath *path;

  g_hash_table_iter_init (&iter, this->paths);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    path = (MPRTPSPath *) val;
    mprtps_path_set_monitor_payload_id(path, this->monitor_payload_type);
    mprtps_path_set_mprtp_ext_header_id(path, this->mprtp_ext_header_id);
    mprtps_path_set_monitor_packet_provider(path, this->monitorpackets);
  }
  stream_splitter_set_monitor_payload_type(this->splitter, this->monitor_payload_type);
}


gboolean
gst_mprtpscheduler_mprtp_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstMprtpscheduler *this;
  gboolean result;

  this = GST_MPRTPSCHEDULER (parent);
  THIS_READLOCK (this);
  switch (GST_EVENT_TYPE (event)) {
    default:
      result = gst_pad_push_event (this->rtp_sinkpad, event);
//      result = gst_pad_event_default (pad, parent, event);
  }
  THIS_READUNLOCK (this);
  return result;
}

void
_join_subflow (GstMprtpscheduler * this, guint subflow_id)
{
  MPRTPSPath *path;
  path =
      (MPRTPSPath *) g_hash_table_lookup (this->paths,
      GINT_TO_POINTER (subflow_id));
  if (path != NULL) {
    GST_WARNING_OBJECT (this, "Join operation can not be done "
        "due to duplicated subflow id (%d)", subflow_id);
    return;
  }
  path = make_mprtps_path ((guint8) subflow_id, gst_mprtpscheduler_mprtp_proxy, this);
  g_hash_table_insert (this->paths, GINT_TO_POINTER (subflow_id), path);
  mprtps_path_set_monitor_payload_id(path, this->monitor_payload_type);
  mprtps_path_set_mprtp_ext_header_id(path, this->mprtp_ext_header_id);
  sndctrler_add_path(this->controller, subflow_id, path);
  ++this->subflows_num;
}

void
_detach_subflow (GstMprtpscheduler * this, guint subflow_id)
{
  MPRTPSPath *path;

  path =
      (MPRTPSPath *) g_hash_table_lookup (this->paths,
      GINT_TO_POINTER (subflow_id));
  if (path == NULL) {
    GST_WARNING_OBJECT (this, "Detach operation can not be done "
        "due to not existed subflow id (%d)", subflow_id);
    return;
  }
  g_hash_table_remove (this->paths, GINT_TO_POINTER (subflow_id));
  sndctrler_rem_path(this->controller, subflow_id);
  --this->subflows_num;
}



static GstStateChangeReturn
gst_mprtpscheduler_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstMprtpscheduler *this;

  g_return_val_if_fail (GST_IS_MPRTPSCHEDULER (element),
      GST_STATE_CHANGE_FAILURE);
  this = GST_MPRTPSCHEDULER (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      gst_task_set_lock (this->path_ticking_thread, &this->path_ticking_mutex);
      gst_task_start (this->path_ticking_thread);
      break;
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (gst_mprtpscheduler_parent_class)->change_state
      (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      gst_task_stop (this->path_ticking_thread);
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
      THIS_READLOCK (this);
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
      THIS_READUNLOCK (this);
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
static void _setup_timestamp(GstMprtpscheduler *this, GstRTPBuffer *rtp);
void _setup_timestamp(GstMprtpscheduler *this, GstRTPBuffer *rtp)
{
  RTPAbsTimeExtension data;
  guint32 time;

  //Absolute sending time +0x83AA7E80
  //https://tools.ietf.org/html/draft-alvestrand-rmcat-remb-03
  time = (NTP_NOW >> 14) & 0x00ffffff;
  memcpy (&data, &time, 3);
  gst_rtp_buffer_add_extension_onebyte_header (rtp,
      this->abs_time_ext_header_id, (gpointer) &data, sizeof (data));
}

void
gst_mprtpscheduler_mprtp_proxy(gpointer ptr, GstBuffer * buffer)
{
  GstMprtpscheduler *this;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  GstBuffer *outbuf;

  this = GST_MPRTPSCHEDULER(ptr);
  outbuf = gst_buffer_make_writable (buffer);
  if (G_UNLIKELY (!gst_rtp_buffer_map (outbuf, GST_MAP_READWRITE, &rtp))) {
    GST_WARNING_OBJECT (this, "The RTP packet is not writeable");
    goto done;
  }

  if(gst_rtp_buffer_get_payload_type(&rtp) != this->monitor_payload_type){
    _setup_timestamp(this, &rtp);
  }

//  g_print("Send: %u payload: %u\n", gst_rtp_buffer_get_seq(&rtp), gst_rtp_buffer_get_payload_type(&rtp));
  gst_rtp_buffer_unmap (&rtp);
  if(this->auto_rate_and_cc){
    monitorpackets_push_rtp_packet(this->monitorpackets, outbuf);
  }
  gst_pad_push (this->mprtp_srcpad, outbuf);
  if (!this->riport_flow_signal_sent) {
    this->riport_flow_signal_sent = TRUE;
    sndctrler_riport_can_flow(this->controller);
  }

done:
  return;
}

void
gst_mprtpscheduler_emit_signal(gpointer ptr, gpointer data)
{
  GstMprtpscheduler *this = ptr;
//  g_signal_emit (this,_subflows_utilization, 0 /* details */, 1);
  g_signal_emit (this,_subflows_utilization, 0 /* details */, data);
}
//static GstClockTime prev;
static GstFlowReturn
gst_mprtpscheduler_rtp_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstMprtpscheduler *this;
  MPRTPSPath *path;
  GstFlowReturn result;
  guint8 first_byte;
  guint8 second_byte;
  gboolean suggest_to_skip = FALSE;
  GstBuffer *outbuf;
  result = GST_FLOW_OK;

  this = GST_MPRTPSCHEDULER (parent);

//  g_print("Sent: %lu\n", GST_TIME_AS_MSECONDS(gst_clock_get_time(this->sysclock)-prev));
//  prev = gst_clock_get_time(this->sysclock);
  if (gst_buffer_extract (buffer, 0, &first_byte, 1) != 1 ||
      gst_buffer_extract (buffer, 1, &second_byte, 1) != 1) {
    GST_WARNING_OBJECT (this, "could not extract first byte from buffer");
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

  if (!PACKET_IS_RTP_OR_RTCP (first_byte)) {
    GST_WARNING_OBJECT (this, "Not RTP Packet arrived at rtp_sink");
    return gst_pad_push (this->mprtp_srcpad, buffer);
  }
  if(PACKET_IS_RTCP(second_byte)){
    GST_WARNING_OBJECT (this, "RTCP Packet arrived on rtp sink");
    return gst_pad_push (this->mprtp_srcpad, buffer);
  }
  //the packet is rtp
  THIS_READLOCK (this);
  path = stream_splitter_get_next_path(this->splitter, buffer);
  if(!path){
    GST_WARNING_OBJECT(this, "No active subflow");
    goto done;
  }
  if(suggest_to_skip){
    gst_buffer_unref(buffer);
    goto done;
  }
  outbuf = gst_buffer_make_writable (buffer);
  mprtps_path_process_rtp_packet(path, outbuf);
  result = GST_FLOW_OK;
done:
  THIS_READUNLOCK (this);
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

done:
  return result;
}


static GstFlowReturn
gst_mprtpscheduler_mprtcp_rr_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  GstMprtpscheduler *this;
  GstFlowReturn result;

  this = GST_MPRTPSCHEDULER (parent);
  GST_DEBUG_OBJECT (this, "RTCP/MPRTCP sink");
  THIS_READLOCK (this);

  sndctrler_receive_mprtcp(this->controller, buf);

  result = GST_FLOW_OK;
  THIS_READUNLOCK (this);
  return result;

}

void
gst_mprtpscheduler_mprtcp_sender (gpointer ptr, GstBuffer * buf)
{
  GstMprtpscheduler *this;
  this = GST_MPRTPSCHEDULER (ptr);
  THIS_READLOCK (this);
  gst_pad_push (this->mprtcp_sr_srcpad, buf);
  THIS_READUNLOCK (this);
}


static gboolean
gst_mprtpscheduler_sink_eventfunc (GstPad * srckpad, GstObject * parent,
                                   GstEvent * event)
{
  GstMprtpscheduler * this;
  gboolean result = TRUE, forward = TRUE;

  this = GST_MPRTPSCHEDULER(parent);
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      break;
    case GST_EVENT_FLUSH_STOP:
      /* we need new segment info after the flush. */
      gst_segment_init (&this->segment, GST_FORMAT_UNDEFINED);
      this->position_out = GST_CLOCK_TIME_NONE;
      break;
    case GST_EVENT_EOS:
      break;
    case GST_EVENT_TAG:
      break;
    case GST_EVENT_SEGMENT:
    {
      gst_event_copy_segment (event, &this->segment);
      GST_DEBUG_OBJECT (this, "received SEGMENT %" GST_SEGMENT_FORMAT,
          &this->segment);
      break;
    }
    default:
      break;
  }

  if (result && forward)
    result = gst_pad_push_event (this->mprtp_srcpad, event);
  else
    gst_event_unref (event);

  return result;
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
      peer = gst_pad_get_peer (this->rtp_sinkpad);
      if ((result = gst_pad_query (peer, query))) {
          gst_query_parse_latency (query, &live, &min, &max);
          min= GST_MSECOND;
          max = -1;
          gst_query_set_latency (query, live, min, max);
      }
      gst_object_unref (peer);
    }
    break;
    default:
      result = gst_pad_query_default(srckpad, parent, query);
      break;
  }

  return result;
}


void
gst_mprtpscheduler_path_ticking_process_run (void *data)
{
  GstMprtpscheduler *this;
  GstClockID clock_id;
  GstClockTime next_scheduler_time;
  GHashTableIter iter;
  gpointer key, val;
  MPRTPSPath *path;
  GstClockTime now;
  this = (GstMprtpscheduler *) data;

  THIS_WRITELOCK (this);
  now = gst_clock_get_time (this->sysclock);
  g_hash_table_iter_init (&iter, this->paths);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    path = (MPRTPSPath *) val;
    mprtps_path_tick(path);
  }
  next_scheduler_time = now + 1 * GST_MSECOND;
  clock_id = gst_clock_new_single_shot_id (this->sysclock, next_scheduler_time);
  THIS_WRITEUNLOCK (this);

  if (gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED) {
    GST_WARNING_OBJECT (this, "The scheduler clock wait is interrupted");
  }
  gst_clock_id_unref (clock_id);
}


gboolean
_try_get_path (GstMprtpscheduler * this, guint16 subflow_id,
    MPRTPSPath ** result)
{
  MPRTPSPath *path;
  path =
      (MPRTPSPath *) g_hash_table_lookup (this->paths,
      GINT_TO_POINTER (subflow_id));
  if (path == NULL) {
    return FALSE;
  }
  if (result) {
    *result = path;
  }
  return TRUE;
}


void
_change_auto_rate_and_cc (GstMprtpscheduler * this,
    gboolean auto_rate_and_cc)
{
  if(this->auto_rate_and_cc ^ auto_rate_and_cc){
    if(auto_rate_and_cc) sndctrler_enable_auto_rate_and_cc(this->controller);
    else sndctrler_disable_auto_rate_and_congestion_control(this->controller);
  }
  this->auto_rate_and_cc = auto_rate_and_cc;
}


void
_change_path_state (GstMprtpscheduler * this, guint8 subflow_id,
    gboolean set_congested, gboolean set_lossy)
{
  MPRTPSPath *path;
  if (!_try_get_path (this, subflow_id, &path)) {
    GST_WARNING_OBJECT (this,
        "Change state can not be done due to unknown subflow id (%d)",
        subflow_id);
    return;
  }

  if (!set_congested) {
    mprtps_path_set_non_congested (path);
  } else {
    mprtps_path_set_congested (path);
  }

  if (!set_lossy) {
    mprtps_path_set_non_lossy (path);
  } else {
    mprtps_path_set_lossy (path);
  }

}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef MPRTP_SENDER_DEFAULT_CHARGE_VALUE
#undef MPRTP_SENDER_DEFAULT_ALPHA_VALUE
#undef MPRTP_SENDER_DEFAULT_BETA_VALUE
#undef MPRTP_SENDER_DEFAULT_GAMMA_VALUE
#undef PACKET_IS_RTP_OR_RTCP
