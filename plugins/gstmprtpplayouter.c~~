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
#include "gstmprtpplayouter.h"
#include "gstmprtcpbuffer.h"
#include "mprtprpath.h"
#include "mprtpspath.h"
#include "streamjoiner.h"
#include "rmanctrler.h"
#include "smanctrler.h"
#include "refctrler.h"

GST_DEBUG_CATEGORY_STATIC (gst_mprtpplayouter_debug_category);
#define GST_CAT_DEFAULT gst_mprtpplayouter_debug_category

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

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
static gboolean gst_mprtpplayouter_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static void gst_mprtpplayouter_mprtcp_sender (gpointer ptr, GstBuffer * buf);
static void _processing_mprtp_packet (GstMprtpplayouter * mprtpr,
    GstBuffer * buf);
static GstFlowReturn _processing_mprtcp_packet (GstMprtpplayouter * this,
    GstBuffer * buf);
static void _join_path (GstMprtpplayouter * this, guint8 subflow_id);
static void _detach_path (GstMprtpplayouter * this, guint8 subflow_id);
static void gst_mprtpplayouter_send_mprtp_proxy (gpointer data,
    GstBuffer * buf);
static gboolean _try_get_path (GstMprtpplayouter * this, guint16 subflow_id,
    MPRTPRPath ** result);
static void _change_flow_riporting_mode (GstMprtpplayouter * this,
    guint new_flow_riporting_mode);


enum
{
  PROP_0,
  PROP_EXT_HEADER_ID,
  PROP_PIVOT_SSRC,
  PROP_JOIN_SUBFLOW,
  PROP_DETACH_SUBFLOW,
  PROP_PIVOT_CLOCK_RATE,
  PROP_AUTO_FLOW_RIPORTING,
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
    GST_STATIC_CAPS ("application/x-mprtcp-b")
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
    GST_STATIC_CAPS ("application/x-mprtcp")
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

  g_object_class_install_property (gobject_class, PROP_JOIN_SUBFLOW,
      g_param_spec_uint ("join-subflow", "the subflow id requested to join",
          "Join a subflow with a given id.", 0,
          255, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DETACH_SUBFLOW,
      g_param_spec_uint ("detach-subflow", "the subflow id requested to detach",
          "Detach a subflow with a given id.", 0,
          255, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_AUTO_FLOW_RIPORTING,
      g_param_spec_boolean ("auto-flow-riporting",
          "Automatic flow riporting means that ",
          "the playouter send RR and XR (if late discarded"
          "packets arrive) to the sender. It also puts extra "
          "bytes on the network because of the generated "
          "riports.", FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));



  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_change_state);
  element_class->query = GST_DEBUG_FUNCPTR (gst_mprtpplayouter_query);
}

static void
gst_mprtpplayouter_init (GstMprtpplayouter * this)
{

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

  gst_pad_set_chain_function (this->mprtcp_sr_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_mprtcp_sr_sink_chain));
  gst_pad_set_chain_function (this->mprtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_mprtp_sink_chain));

  gst_pad_set_query_function (this->mprtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_sink_query));
  gst_pad_set_event_function (this->mprtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_sink_event));

  this->ext_header_id = MPRTP_DEFAULT_EXTENSION_HEADER_ID;
  this->pivot_clock_rate = MPRTP_PLAYOUTER_DEFAULT_CLOCKRATE;
  this->pivot_ssrc = MPRTP_PLAYOUTER_DEFAULT_SSRC;
  this->paths = g_hash_table_new_full (NULL, NULL, NULL, g_free);
  this->joiner = (StreamJoiner *) g_object_new (STREAM_JOINER_TYPE, NULL);
  stream_joiner_set_sending (this->joiner, (gpointer) this,
      gst_mprtpplayouter_send_mprtp_proxy);
  this->controller = NULL;
  _change_flow_riporting_mode (this, FALSE);
  g_rw_lock_init (&this->rwmutex);
}

void
gst_mprtpplayouter_send_mprtp_proxy (gpointer data, GstBuffer * buf)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (data);
  gst_pad_push (this->mprtp_srcpad, buf);
}

void
gst_mprtpplayouter_finalize (GObject * object)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (object);

  GST_DEBUG_OBJECT (this, "finalize");
  g_object_unref (G_OBJECT (this->joiner));
  g_object_unref (this->controller);
  /* clean up object here */
  g_hash_table_destroy (this->paths);
  G_OBJECT_CLASS (gst_mprtpplayouter_parent_class)->finalize (object);
}


void
gst_mprtpplayouter_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (object);
  gboolean gboolean_value;
  GST_DEBUG_OBJECT (this, "set_property");

  switch (property_id) {
    case PROP_EXT_HEADER_ID:
      THIS_WRITELOCK (this);
      this->ext_header_id = g_value_get_int (value);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_PIVOT_SSRC:
      THIS_WRITELOCK (this);
      this->pivot_ssrc = g_value_get_uint (value);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_PIVOT_CLOCK_RATE:
      THIS_WRITELOCK (this);
      this->pivot_clock_rate = g_value_get_uint (value);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_JOIN_SUBFLOW:
      THIS_WRITELOCK (this);
      _join_path (this, g_value_get_uint (value));
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_DETACH_SUBFLOW:
      THIS_WRITELOCK (this);
      _detach_path (this, g_value_get_uint (value));
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_AUTO_FLOW_RIPORTING:
      THIS_WRITELOCK (this);
      gboolean_value = g_value_get_boolean (value);
      _change_flow_riporting_mode (this, gboolean_value);
      THIS_WRITEUNLOCK (this);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


void
gst_mprtpplayouter_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (object);

  GST_DEBUG_OBJECT (this, "get_property");

  switch (property_id) {
    case PROP_EXT_HEADER_ID:
      THIS_READLOCK (this);
      g_value_set_int (value, this->ext_header_id);
      THIS_READUNLOCK (this);
      break;
    case PROP_PIVOT_CLOCK_RATE:
      THIS_READLOCK (this);
      g_value_set_uint (value, this->pivot_clock_rate);
      THIS_READUNLOCK (this);
      break;
    case PROP_PIVOT_SSRC:
      THIS_READLOCK (this);
      g_value_set_uint (value, this->pivot_ssrc);
      THIS_READUNLOCK (this);
      break;
    case PROP_AUTO_FLOW_RIPORTING:
      THIS_READLOCK (this);
      g_value_set_boolean (value, this->auto_flow_riporting);
      THIS_READUNLOCK (this);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
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
  gint clock_rate;

  GST_DEBUG_OBJECT (this, "sink event");
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
      gst_event_parse_caps (event, &caps);
      s = gst_caps_get_structure (caps, 0);
      if (gst_structure_has_field (s, "clock-rate")) {
        THIS_WRITELOCK (this);
        gst_structure_get_int (s, "clock-rate", &clock_rate);
        this->pivot_clock_rate = (guint32) clock_rate;
        THIS_WRITEUNLOCK (this);
      }
      peer = gst_pad_get_peer (this->mprtp_srcpad);
      result = gst_pad_send_event (peer, event);
      gst_object_unref (peer);
      break;
    default:
      result = gst_pad_event_default (pad, parent, event);
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
  MPRTPRPath *path;
  path =
      (MPRTPRPath *) g_hash_table_lookup (this->paths,
      GINT_TO_POINTER (subflow_id));
  if (path != NULL) {
    GST_WARNING_OBJECT (this, "Join operation can not be done "
        "due to duplicated subflow id (%d)", subflow_id);
    goto exit;
  }
  path = make_mprtpr_path (subflow_id);
  g_hash_table_insert (this->paths, GINT_TO_POINTER (subflow_id), path);
  stream_joiner_add_path (this->joiner, subflow_id, path);
  this->controller_add_path (this->controller, subflow_id, path);
exit:
  return;
}

void
_detach_path (GstMprtpplayouter * this, guint8 subflow_id)
{
  MPRTPRPath *path;

  path =
      (MPRTPRPath *) g_hash_table_lookup (this->paths,
      GINT_TO_POINTER (subflow_id));
  if (path == NULL) {
    GST_WARNING_OBJECT (this, "Detach operation can not be done "
        "due to not existed subflow id (%d)", subflow_id);
    goto exit;
  }
  g_hash_table_remove (this->paths, GINT_TO_POINTER (subflow_id));
  stream_joiner_rem_path (this->joiner, subflow_id);
  this->controller_rem_path (this->controller, subflow_id);
exit:
  return;
}



static GstStateChangeReturn
gst_mprtpplayouter_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  g_return_val_if_fail (GST_IS_MPRTPPLAYOUTER (element),
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
    case GST_QUERY_CUSTOM:
      THIS_WRITELOCK (this);
      s = gst_query_writable_structure (query);
      if (!gst_structure_has_name (s,
              GST_MPRTCP_PLAYOUTER_SENT_BYTES_STRUCTURE_NAME)) {
        ret =
            GST_ELEMENT_CLASS (gst_mprtpplayouter_parent_class)->query (element,
            query);
        break;
      }
      gst_structure_set (s,
          GST_MPRTCP_PLAYOUTER_SENT_OCTET_SUM_FIELD,
          G_TYPE_UINT, this->rtcp_sent_octet_sum, NULL);
      THIS_WRITEUNLOCK (this);
      break;
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
  GstFlowReturn result;

  this = GST_MPRTPPLAYOUTER (parent);
  GST_DEBUG_OBJECT (this, "RTP/RTCP/MPRTP/MPRTCP sink");
  THIS_READLOCK (this);


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
  //the packet is either rtcp or mprtp
  if (*data > 192 && *data < 223) {
    result = gst_pad_push (this->mprtp_srcpad, buf);
    goto done;
  }

  _processing_mprtp_packet (this, buf);
  result = GST_FLOW_OK;
  if (!this->riport_flow_signal_sent) {
    this->riport_flow_signal_sent = TRUE;
    this->riport_can_flow (this->controller);
  }
done:
  THIS_READUNLOCK (this);
  return result;

}


static GstFlowReturn
gst_mprtpplayouter_mprtcp_sr_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  GstMprtpplayouter *this;
  GstMapInfo info;
  guint8 *data;
  GstFlowReturn result;

  this = GST_MPRTPPLAYOUTER (parent);
  GST_DEBUG_OBJECT (this, "RTCP/MPRTCP sink");
  THIS_READLOCK (this);
  if (!gst_buffer_map (buf, &info, GST_MAP_READ)) {
    GST_WARNING ("Buffer is not readable");
    result = GST_FLOW_ERROR;
    goto done;
  }
  data = info.data + 1;

  result = _processing_mprtcp_packet (this, buf);

  gst_buffer_unmap (buf, &info);
  if (*data != MPRTCP_PACKET_TYPE_IDENTIFIER) {
    GST_WARNING_OBJECT (this, "mprtcp_sr_sink process only MPRTCP packets");
    result = GST_FLOW_OK;
    goto done;
  }

  if (!this->riport_flow_signal_sent) {
    this->riport_flow_signal_sent = TRUE;
    this->riport_can_flow (this->controller);
  }
done:
  THIS_READUNLOCK (this);
  return result;

}

void
gst_mprtpplayouter_mprtcp_sender (gpointer ptr, GstBuffer * buf)
{
  GstMprtpplayouter *this;
  this = GST_MPRTPPLAYOUTER (ptr);
  THIS_READLOCK (this);
  gst_pad_push (this->mprtcp_rr_srcpad, buf);
  THIS_READUNLOCK (this);
}


GstFlowReturn
_processing_mprtcp_packet (GstMprtpplayouter * this, GstBuffer * buf)
{
  GstFlowReturn result;
  this->mprtcp_receiver (this->controller, buf);
  result = GST_FLOW_OK;
  return result;
}


void
_processing_mprtp_packet (GstMprtpplayouter * this, GstBuffer * buf)
{
  gpointer pointer = NULL;
  MPRTPSubflowHeaderExtension *subflow_infos = NULL;
  MPRTPRPath *path = NULL;
  guint size;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  if (G_UNLIKELY (!gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp))) {
    GST_WARNING_OBJECT (this, "The received Buffer is not readable");
    return;
  }

  if (!gst_rtp_buffer_get_extension (&rtp)) {
    //Backward compatibility in a way to process rtp packet must be implemented here!!!

    GST_WARNING_OBJECT (this,
        "The received buffer extension bit is 0 thus it is not an MPRTP packet.");
    gst_rtp_buffer_unmap (&rtp);
    return;
  }
  //_print_rtp_packet_info(rtp);

  if (!gst_rtp_buffer_get_extension_onebyte_header (&rtp, this->ext_header_id,
          0, &pointer, &size)) {
    GST_WARNING_OBJECT (this,
        "The received buffer extension is not processable");
    gst_rtp_buffer_unmap (&rtp);
    return;
  }
  subflow_infos = (MPRTPSubflowHeaderExtension *) pointer;
  if (_try_get_path (this, subflow_infos->id, &path) == FALSE) {
    _join_path (this, subflow_infos->id);
    if (_try_get_path (this, subflow_infos->id, &path) == FALSE) {
      gst_rtp_buffer_unmap (&rtp);
      return;
    }
  }

  if (gst_rtp_buffer_get_ssrc (&rtp) == this->pivot_ssrc ||
      this->pivot_ssrc == MPRTP_PLAYOUTER_DEFAULT_SSRC) {
    guint32 rtptime;
    rtptime = gst_rtp_buffer_get_timestamp (&rtp);
    mprtpr_path_add_packet_skew (path, rtptime, this->pivot_clock_rate);
  }
  gst_rtp_buffer_unmap (&rtp);

  mprtpr_path_process_mprtp_packet (path, buf, subflow_infos->seq);

}

gboolean
_try_get_path (GstMprtpplayouter * this, guint16 subflow_id,
    MPRTPRPath ** result)
{
  MPRTPRPath *path;
  path =
      (MPRTPRPath *) g_hash_table_lookup (this->paths,
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
_change_flow_riporting_mode (GstMprtpplayouter * this,
    guint new_flow_riporting_mode)
{
  gpointer key, val;
  MPRTPRPath *path;
  GHashTableIter iter;
  guint8 subflow_id;
  if (this->controller && this->auto_flow_riporting == new_flow_riporting_mode) {
    return;
  }
  if (this->controller != NULL) {
    g_object_unref (this->controller);
  }
  if (new_flow_riporting_mode) {
    this->controller = g_object_new (REFCTRLER_TYPE, NULL);
    this->mprtcp_receiver = refctrler_setup_mprtcp_exchange (this->controller,
        this, gst_mprtpplayouter_mprtcp_sender);

    refctrler_set_callbacks (&this->riport_can_flow,
        &this->controller_add_path, &this->controller_rem_path);
  } else {
    this->controller = g_object_new (RMANCTRLER_TYPE, NULL);
    this->mprtcp_receiver = rmanctrler_setup_mprtcp_exchange (this->controller,
        this, gst_mprtpplayouter_mprtcp_sender);

    rmanctrler_set_callbacks (&this->riport_can_flow,
        &this->controller_add_path, &this->controller_rem_path);
  }

  g_hash_table_iter_init (&iter, this->paths);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    path = (MPRTPRPath *) val;
    subflow_id = (guint8) GPOINTER_TO_INT (key);
    this->controller_add_path (this->controller, subflow_id, path);
  }
}


#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
