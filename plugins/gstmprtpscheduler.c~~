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
#include <gst/gst.h>
#include "gstmprtpscheduler.h"
#include "mprtpspath.h"
#include "streamsplitter.h"
#include "gstmprtcpbuffer.h"
#include "smanctrler.h"
#include "sefctrler.h"

GST_DEBUG_CATEGORY_STATIC (gst_mprtpscheduler_debug_category);
#define GST_CAT_DEFAULT gst_mprtpscheduler_debug_category

#define PACKET_IS_RTP(b) (b > 0x7f && b < 0xc0)

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

static GstFlowReturn gst_mprtpscheduler_rtp_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static GstFlowReturn gst_mprtpscheduler_rtp_sink_chainlist (GstPad * pad,
    GstObject * parent, GstBufferList * list);

static GstFlowReturn gst_mprtpscheduler_mprtcp_rr_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * outbuf);

static gboolean gst_mprtpscheduler_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static void gst_mprtpscheduler_mprtcp_sender (gpointer ptr, GstBuffer * buf);

static void _join_subflow (GstMprtpscheduler * this, guint subflow_id);
static void _detach_subflow (GstMprtpscheduler * this, guint subflow_id);
static gboolean
_try_get_path (GstMprtpscheduler * this, guint16 subflow_id,
    MPRTPSPath ** result);
static void _change_path_state (GstMprtpscheduler * this, guint8 subflow_id,
    gboolean set_congested, gboolean set_lossy);
static void _change_auto_flow_controlling_mode (GstMprtpscheduler * this,
    gboolean new_flow_controlling_mode);
static gboolean gst_mprtpscheduler_mprtp_src_event (GstPad * pad,
    GstObject * parent, GstEvent * event);

enum
{
  PROP_0,
  PROP_ALPHA,
  PROP_BETA,
  PROP_GAMMA,
  PROP_EXT_HEADER_ID,
  PROP_JOIN_SUBFLOW,
  PROP_DETACH_SUBFLOW,
  PROP_SET_SUBFLOW_NON_CONGESTED,
  PROP_SET_SUBFLOW_CONGESTED,
  PROP_AUTO_FLOW_CONTROLLING,
  PROP_SET_SENDING_BID,
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
    GST_STATIC_CAPS ("application/x-mprtcp-b")
    );


static GstStaticPadTemplate gst_mprtpscheduler_mprtcp_sr_src_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_sr_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-mprtcp")
    );


/* class initialization */
G_DEFINE_TYPE_WITH_CODE (GstMprtpscheduler, gst_mprtpscheduler,
    GST_TYPE_ELEMENT,
    GST_DEBUG_CATEGORY_INIT (gst_mprtpscheduler_debug_category,
        "mprtpscheduler", 0, "debug category for mprtpscheduler element"));

#define GST_MPRTPSCHEDULER_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_MPRTPSCHEDULER, GstMprtpschedulerPrivate))

struct _GstMprtpschedulerPrivate
{
  gboolean have_same_caps;

  GstBufferPool *pool;
  gboolean pool_active;
  GstAllocator *allocator;
  GstAllocationParams params;
  GstQuery *query;
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


  g_object_class_install_property (gobject_class, PROP_JOIN_SUBFLOW,
      g_param_spec_uint ("join-subflow", "the subflow id requested to join",
          "Join a subflow with a given id.", 0,
          255, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DETACH_SUBFLOW,
      g_param_spec_uint ("detach-subflow", "the subflow id requested to detach",
          "Detach a subflow with a given id.", 0,
          255, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SET_SUBFLOW_CONGESTED,
      g_param_spec_uint ("congested-subflow", "set the subflow congested",
          "Set the subflow congested", 0,
          255, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_SET_SUBFLOW_NON_CONGESTED,
      g_param_spec_uint ("non-congested-subflow",
          "set the subflow non-congested", "Set the subflow non-congested", 0,
          255, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_AUTO_FLOW_CONTROLLING,
      g_param_spec_boolean ("auto-flow-controlling",
          "Automatic flow controlling means that ",
          "the scheduler makes decision"
          "based on receiver and XR riports for properly"
          "distribute the flow considering their capacities and "
          "events (losts, discards, etc...) It also puts extra "
          "bytes on the network because of the generated "
          "sender riports.",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SET_SENDING_BID,
      g_param_spec_uint ("setup-sending-bid",
          "set the sending bid for a subflow",
          "A 32bit unsigned integer for setup a bid. The first 8 bit identifies the subflow, the latter the bid for the subflow",
          0, 4294967295, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

}

static void
gst_mprtpscheduler_init (GstMprtpscheduler * this)
{
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

  gst_pad_set_query_function (this->rtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpscheduler_sink_query));

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

  gst_element_add_pad (GST_ELEMENT (this), this->mprtp_srcpad);

  this->alpha_value = MPRTP_SENDER_DEFAULT_ALPHA_VALUE;
  this->beta_value = MPRTP_SENDER_DEFAULT_BETA_VALUE;
  this->gamma_value = MPRTP_SENDER_DEFAULT_GAMMA_VALUE;

  g_rw_lock_init (&this->rwmutex);
  this->paths = g_hash_table_new_full (NULL, NULL, NULL, g_free);
  this->ext_header_id = MPRTP_DEFAULT_EXTENSION_HEADER_ID;
  this->paths = g_hash_table_new_full (NULL, NULL, NULL, g_free);
  this->splitter = (StreamSplitter *) g_object_new (STREAM_SPLITTER_TYPE, NULL);
  this->controller = NULL;
  _change_auto_flow_controlling_mode (this, FALSE);
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
  GstMprtpscheduler *mprtpscheduler = GST_MPRTPSCHEDULER (object);

  GST_DEBUG_OBJECT (mprtpscheduler, "finalize");

  /* clean up object here */

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
  guint subflow_bid;

  GST_DEBUG_OBJECT (this, "set_property");

  switch (property_id) {
    case PROP_ALPHA:
      THIS_WRITELOCK (this);
      this->alpha_value = g_value_get_float (value);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_BETA:
      THIS_WRITELOCK (this);
      this->beta_value = g_value_get_float (value);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_GAMMA:
      THIS_WRITELOCK (this);
      this->gamma_value = g_value_get_float (value);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_EXT_HEADER_ID:
      THIS_WRITELOCK (this);
      this->ext_header_id = g_value_get_int (value);
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
    case PROP_AUTO_FLOW_CONTROLLING:
      THIS_WRITELOCK (this);
      gboolean_value = g_value_get_boolean (value);
      _change_auto_flow_controlling_mode (this, gboolean_value);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_SET_SENDING_BID:
      THIS_WRITELOCK (this);
      guint_value = g_value_get_uint (value);
      subflow_id = (guint8) ((guint_value >> 24) & 0x000000FF);
      subflow_bid = guint_value & 0x00FFFFFFUL;
      stream_splitter_setup_sending_bid (this->splitter, subflow_id,
          subflow_bid);
      stream_splitter_commit_changes (this->splitter);
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
    case PROP_ALPHA:
      THIS_READLOCK (this);
      g_value_set_float (value, this->alpha_value);
      THIS_READUNLOCK (this);
      break;
    case PROP_BETA:
      THIS_READLOCK (this);
      g_value_set_float (value, this->beta_value);
      THIS_READUNLOCK (this);
      break;
    case PROP_GAMMA:
      THIS_READLOCK (this);
      g_value_set_float (value, this->gamma_value);
      THIS_READUNLOCK (this);
      break;
    case PROP_EXT_HEADER_ID:
      THIS_READLOCK (this);
      g_value_set_int (value, this->ext_header_id);
      THIS_READUNLOCK (this);
      break;
    case PROP_AUTO_FLOW_CONTROLLING:
      THIS_READLOCK (this);
      g_value_set_boolean (value, this->flow_controlling_mode);
      THIS_READUNLOCK (this);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
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
      result = gst_pad_event_default (pad, parent, event);
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
  path = make_mprtps_path ((guint8) subflow_id);
  g_hash_table_insert (this->paths, GINT_TO_POINTER (subflow_id),
      make_mprtps_path (subflow_id));
  stream_splitter_add_path (this->splitter, subflow_id, path);
  this->controller_add_path (this->controller, subflow_id, path);

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
  stream_splitter_rem_path (this->splitter, subflow_id);
  this->controller_rem_path (this->controller, subflow_id);
}



static GstStateChangeReturn
gst_mprtpscheduler_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;

  g_return_val_if_fail (GST_IS_MPRTPSCHEDULER (element),
      GST_STATE_CHANGE_FAILURE);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (gst_mprtpscheduler_parent_class)->change_state
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

static GstFlowReturn
gst_mprtpscheduler_rtp_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstMprtpscheduler *this;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  GstBuffer *outbuf;
  GstFlowReturn result;
  guint8 first_byte;

  this = GST_MPRTPSCHEDULER (parent);

  if (gst_buffer_extract (buffer, 0, &first_byte, 1) != 1) {
    GST_WARNING_OBJECT (this, "could not extract first byte from buffer");
    gst_buffer_unref (buffer);
    return GST_FLOW_OK;
  }

  if (!PACKET_IS_RTP (first_byte)) {
    GST_WARNING_OBJECT (this, "Not RTP Packet arrived ar rtp_sink");
    return gst_pad_push (this->mprtp_srcpad, buffer);
  }
  //the packet is rtp
  outbuf = gst_buffer_make_writable (buffer);
  if (G_UNLIKELY (!gst_rtp_buffer_map (outbuf, GST_MAP_READWRITE, &rtp))) {
    GST_WARNING_OBJECT (this, "The RTP packet is not writeable");
    return GST_FLOW_ERROR;
  }
  THIS_READLOCK (this);
  stream_splitter_process_rtp_packet (this->splitter, &rtp);

  //gst_print_rtp_packet_info(&rtp);
  gst_rtp_buffer_unmap (&rtp);
  result = gst_pad_push (this->mprtp_srcpad, outbuf);
  if (!this->riport_flow_signal_sent) {
    this->riport_flow_signal_sent = TRUE;
    this->riport_can_flow (this->controller);
  }

  THIS_READUNLOCK (this);

  GST_DEBUG_OBJECT (this, "chain is unlocked");

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
  GstMapInfo info;
  guint8 *data;
  GstFlowReturn result;


  this = GST_MPRTPSCHEDULER (parent);
  GST_DEBUG_OBJECT (this, "RTCP/MPRTCP sink");
  THIS_READLOCK (this);
  if (!gst_buffer_map (buf, &info, GST_MAP_READ)) {
    GST_WARNING ("Buffer is not readable");
    result = GST_FLOW_ERROR;
    goto done;
  }
  data = info.data + 1;
  gst_buffer_unmap (buf, &info);

  if (*data != MPRTCP_PACKET_TYPE_IDENTIFIER) {
    GST_WARNING_OBJECT (this, "mprtcp_sr_sink process only MPRTCP packets");
    result = GST_FLOW_OK;
    goto done;
  }

  this->mprtcp_receiver (this->controller, buf);
  result = GST_FLOW_OK;
done:
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
gst_mprtpscheduler_sink_query (GstPad * sinkpad, GstObject * parent,
    GstQuery * query)
{
  GstMprtpscheduler *this = GST_MPRTPSCHEDULER (parent);
  gboolean result;
  GST_DEBUG_OBJECT (this, "query");
  switch (GST_QUERY_TYPE (query)) {

    default:
      result = gst_pad_peer_query (this->mprtp_srcpad, query);
      break;
  }

  return result;
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
_change_auto_flow_controlling_mode (GstMprtpscheduler * this,
    gboolean new_flow_controlling_mode)
{
  gpointer key, val;
  MPRTPSPath *path;
  GHashTableIter iter;
  guint8 subflow_id;
  if (this->controller
      && this->flow_controlling_mode == new_flow_controlling_mode) {
    return;
  }
  if (this->controller != NULL) {
    g_object_unref (this->controller);
  }
  if (new_flow_controlling_mode) {
    this->controller = g_object_new (SEFCTRLER_TYPE, NULL);
    sefctrler_setup (this->controller, this->splitter);
    this->mprtcp_receiver = sefctrler_setup_mprtcp_exchange (this->controller,
        this, gst_mprtpscheduler_mprtcp_sender);

    sefctrler_set_callbacks (&this->riport_can_flow,
        &this->controller_add_path, &this->controller_rem_path);
  } else {
    this->controller = g_object_new (SMANCTRLER_TYPE, NULL);
    this->mprtcp_receiver = smanctrler_setup_mprtcp_exchange (this->controller,
        this, gst_mprtpscheduler_mprtcp_sender);

    smanctrler_set_callbacks (&this->riport_can_flow,
        &this->controller_add_path, &this->controller_rem_path);
  }
  this->riport_flow_signal_sent = FALSE;
  g_hash_table_iter_init (&iter, this->paths);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    path = (MPRTPSPath *) val;
    subflow_id = (guint8) GPOINTER_TO_INT (key);
    this->controller_add_path (this->controller, subflow_id, path);
  }

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
#undef PACKET_IS_RTP
