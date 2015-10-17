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
 * SECTION:element-gstmprtpsender
 *
 * The mprtpsender element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v fakesrc ! mprtpsender ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <gst/gst.h>
#include "gstmprtpsender.h"
#include "mprtpspath.h"
#include "gstmprtcpbuffer.h"
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_mprtpsender_debug_category);
#define GST_CAT_DEFAULT gst_mprtpsender_debug_category

#define THIS_WRITELOCK(this) (g_rw_lock_writer_lock(&this->rwmutex))
#define THIS_WRITEUNLOCK(this) (g_rw_lock_writer_unlock(&this->rwmutex))
#define THIS_READLOCK(this) (g_rw_lock_reader_lock(&this->rwmutex))
#define THIS_READUNLOCK(this) (g_rw_lock_reader_unlock(&this->rwmutex))

#define PACKET_IS_RTP(b) (b > 0x7f && b < 0xc0)
#define PACKET_IS_DTLS(b) (b > 0x13 && b < 0x40)
#define PACKET_IS_RTCP(b) (b > 192 && b < 223)


static void _merge_sr_blocks (GstRTCPSR * src, GstRTCPSR * dst);
static void _merge_rr_blocks (GstRTCPRR * src, GstRTCPRR * dst);
static void _merge_xr_blocks (GstRTCPXR_RFC7243 * src, GstRTCPXR_RFC7243 * dst);
static void gst_mprtpsender_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_mprtpsender_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_mprtpsender_dispose (GObject * object);
static void gst_mprtpsender_finalize (GObject * object);

static GstPad *gst_mprtpsender_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_mprtpsender_release_pad (GstElement * element, GstPad * pad);
static GstStateChangeReturn
gst_mprtpsender_change_state (GstElement * element, GstStateChange transition);
//static void gst_mprtpsender_eventing_run (void *data);
static gboolean gst_mprtpsender_query (GstElement * element, GstQuery * query);

static GstPadLinkReturn gst_mprtpsender_src_link (GstPad * pad,
    GstObject * parent, GstPad * peer);
static void gst_mprtpsender_src_unlink (GstPad * pad, GstObject * parent);

static GstFlowReturn gst_mprtpsender_mprtp_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);

static GstFlowReturn
gst_mprtpsender_mprtcp_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);
static gboolean gst_mprtpsender_mprtp_sink_event_handler (GstPad * pad,
    GstObject * parent, GstEvent * event);

typedef struct
{
  GstPad *outpad;
  guint8 state;
  guint8 id;

  GstBuffer *report_buf;
  gboolean report_started;
  GstClockTime report_interval_time;
  GstClockTime report_time;
  GstClockTime urgent_report_requested_time;
  gboolean allow_early_report;
  GstClock *sysclock;
} Subflow;

static gboolean _do_subflow_report_now (Subflow * this, gboolean urgent);
static void _add_report (Subflow * this, GstMPRTCPSubflowBlock * block);
static gboolean _is_block_urgent_to_send (Subflow * subflow,
    GstMPRTCPSubflowBlock * block);

static gboolean
_select_subflow (GstMprtpsender * this, guint8 id, Subflow ** result);

enum
{
  PROP_0,
  PROP_PIVOT_OUTPAD,
};

/* pad templates */

static GstStaticPadTemplate gst_mprtpsender_src_template =
GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("ANY")
    );


static GstStaticPadTemplate gst_mprtpsender_mprtp_sink_template =
GST_STATIC_PAD_TEMPLATE ("mprtp_sink",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);


static GstStaticPadTemplate gst_mprtpsender_mprtcp_rr_sink_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_rr_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-mprtcp-b")
    );

static GstStaticPadTemplate gst_mprtpsender_mprtcp_sr_sink_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_sr_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-mprtcp-b")
    );

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstMprtpsender, gst_mprtpsender, GST_TYPE_ELEMENT,
    GST_DEBUG_CATEGORY_INIT (gst_mprtpsender_debug_category, "mprtpsender", 0,
        "debug category for mprtpsender element"));

#define GST_MPRTPSENDER_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_MPRTPSENDER, GstMprtpsenderPrivate))

struct _GstMprtpsenderPrivate
{
  gboolean have_same_caps;

  GstBufferPool *pool;
  gboolean pool_active;
  GstAllocator *allocator;
  GstAllocationParams params;
  GstQuery *query;
};


gboolean
gst_mprtpsender_mprtp_sink_event_handler (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstMprtpsender *this;
  gboolean result = TRUE;
  GList *it;
  Subflow *subflow;

  this = GST_MPRTPSENDER (parent);
  THIS_WRITELOCK (this);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
    case GST_EVENT_CAPS:
      for (subflow = NULL, it = this->subflows; it != NULL; it = it->next) {
        subflow = it->data;
        result &= gst_pad_push_event (subflow->outpad, gst_event_copy (event));
      }
      result &= gst_pad_event_default (pad, parent, event);
      break;
    default:
      result = gst_pad_event_default (pad, parent, event);
  }

  THIS_WRITEUNLOCK (this);
  return result;
}


static void
gst_mprtpsender_class_init (GstMprtpsenderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstMprtpsenderPrivate));
  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpsender_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpsender_mprtcp_sr_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpsender_mprtcp_rr_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpsender_mprtp_sink_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "FIXME Long name", "Generic", "FIXME Description",
      "FIXME <fixme@example.com>");

  gobject_class->set_property = gst_mprtpsender_set_property;
  gobject_class->get_property = gst_mprtpsender_get_property;
  gobject_class->dispose = gst_mprtpsender_dispose;
  gobject_class->finalize = gst_mprtpsender_finalize;
  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_mprtpsender_request_new_pad);
  element_class->release_pad = GST_DEBUG_FUNCPTR (gst_mprtpsender_release_pad);
  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_mprtpsender_change_state);
  element_class->query = GST_DEBUG_FUNCPTR (gst_mprtpsender_query);

  g_object_class_install_property (gobject_class, PROP_PIVOT_OUTPAD,
      g_param_spec_uint ("pivot-outpad",
          "The id of the subflow sets to pivot for non-mp packets.",
          "The id of the subflow sets to pivot for non-mp packets. (DTLS, RTCP, Others)",
          0, 255, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
}



static void
gst_mprtpsender_init (GstMprtpsender * mprtpsender)
{

  mprtpsender->mprtcp_rr_sinkpad =
      gst_pad_new_from_static_template
      (&gst_mprtpsender_mprtcp_rr_sink_template, "mprtcp_rr_sink");
  gst_pad_set_chain_function (mprtpsender->mprtcp_rr_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpsender_mprtcp_sink_chain));
  gst_element_add_pad (GST_ELEMENT (mprtpsender),
      mprtpsender->mprtcp_rr_sinkpad);

  mprtpsender->mprtcp_sr_sinkpad =
      gst_pad_new_from_static_template
      (&gst_mprtpsender_mprtcp_sr_sink_template, "mprtcp_sr_sink");
  gst_pad_set_chain_function (mprtpsender->mprtcp_sr_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpsender_mprtcp_sink_chain));
  gst_element_add_pad (GST_ELEMENT (mprtpsender),
      mprtpsender->mprtcp_sr_sinkpad);

  mprtpsender->mprtp_sinkpad =
      gst_pad_new_from_static_template (&gst_mprtpsender_mprtp_sink_template,
      "mprtp_sink");
  gst_pad_set_chain_function (mprtpsender->mprtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpsender_mprtp_sink_chain));
  gst_pad_set_event_function (mprtpsender->mprtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpsender_mprtp_sink_event_handler));

  gst_element_add_pad (GST_ELEMENT (mprtpsender), mprtpsender->mprtp_sinkpad);

//  GST_PAD_SET_PROXY_CAPS (mprtpsender->mprtp_sinkpad);
//  GST_PAD_SET_PROXY_ALLOCATION (mprtpsender->mprtp_sinkpad);

  mprtpsender->ext_header_id = MPRTP_DEFAULT_EXTENSION_HEADER_ID;
  mprtpsender->pivot_outpad = NULL;
  //mprtpsender->events = g_queue_new();
  g_rw_lock_init (&mprtpsender->rwmutex);
}

void
gst_mprtpsender_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMprtpsender *this = GST_MPRTPSENDER (object);
  guint8 subflow_id;
  Subflow *subflow;
  GST_DEBUG_OBJECT (this, "set_property");

  switch (property_id) {
    case PROP_PIVOT_OUTPAD:
      THIS_WRITELOCK (this);
      subflow_id = (guint8) g_value_get_uint (value);
      if (_select_subflow (this, subflow_id, &subflow)) {
        this->pivot_outpad = subflow->outpad;
      } else {
        this->pivot_outpad = NULL;
      }
      THIS_WRITEUNLOCK (this);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_mprtpsender_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstMprtpsender *mprtpsender = GST_MPRTPSENDER (object);

  GST_DEBUG_OBJECT (mprtpsender, "get_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_mprtpsender_dispose (GObject * object)
{
  GstMprtpsender *mprtpsender = GST_MPRTPSENDER (object);

  GST_DEBUG_OBJECT (mprtpsender, "dispose");

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (gst_mprtpsender_parent_class)->dispose (object);
}

void
gst_mprtpsender_finalize (GObject * object)
{
  GstMprtpsender *mprtpsender = GST_MPRTPSENDER (object);

  GST_DEBUG_OBJECT (mprtpsender, "finalize");

  /* clean up object here */

  G_OBJECT_CLASS (gst_mprtpsender_parent_class)->finalize (object);
}



static GstPad *
gst_mprtpsender_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{

  GstPad *srcpad;
  GstMprtpsender *this;
  guint8 subflow_id;
  Subflow *subflow;

  this = GST_MPRTPSENDER (element);
  GST_DEBUG_OBJECT (this, "requesting pad");

  sscanf (name, "src_%hhu", &subflow_id);
  THIS_WRITELOCK (this);
  subflow = (Subflow *) g_malloc0 (sizeof (Subflow));

  srcpad = gst_pad_new_from_template (templ, name);
//  GST_PAD_SET_PROXY_CAPS (srcpad);
//  GST_PAD_SET_PROXY_ALLOCATION (srcpad);

  gst_pad_set_link_function (srcpad,
      GST_DEBUG_FUNCPTR (gst_mprtpsender_src_link));
  gst_pad_set_unlink_function (srcpad,
      GST_DEBUG_FUNCPTR (gst_mprtpsender_src_unlink));
  subflow->id = subflow_id;
  subflow->outpad = srcpad;
  subflow->state = 0;
  subflow->sysclock = gst_system_clock_obtain ();
  subflow->allow_early_report = TRUE;
  this->subflows = g_list_prepend (this->subflows, subflow);
  THIS_WRITEUNLOCK (this);

  gst_element_add_pad (GST_ELEMENT (this), srcpad);

  gst_pad_set_active (srcpad, TRUE);

  return srcpad;
}

static void
gst_mprtpsender_release_pad (GstElement * element, GstPad * pad)
{

}



static GstStateChangeReturn
gst_mprtpsender_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  g_return_val_if_fail (GST_IS_MPRTPSENDER (element), GST_STATE_CHANGE_FAILURE);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      //gst_task_start (mprtpsender->eventing);
      break;
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (gst_mprtpsender_parent_class)->change_state (element,
      transition);

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
gst_mprtpsender_query (GstElement * element, GstQuery * query)
{
  GstMprtpsender *mprtpsender = GST_MPRTPSENDER (element);
  gboolean ret;

  GST_DEBUG_OBJECT (mprtpsender, "query");
  switch (GST_QUERY_TYPE (query)) {
    default:
      ret =
          GST_ELEMENT_CLASS (gst_mprtpsender_parent_class)->query (element,
          query);
      break;
  }

  return ret;
}


static GstPadLinkReturn
gst_mprtpsender_src_link (GstPad * pad, GstObject * parent, GstPad * peer)
{
  GstMprtpsender *mprtpsender;
  GList *it;
  Subflow *subflow;
  GstPadLinkReturn result = GST_PAD_LINK_OK;

  mprtpsender = GST_MPRTPSENDER (parent);
  GST_DEBUG_OBJECT (mprtpsender, "link");
  THIS_READLOCK (mprtpsender);

  for (subflow = NULL, it = mprtpsender->subflows; it != NULL; it = it->next) {
    subflow = it->data;
    if (subflow->outpad == pad) {
      break;
    }
    subflow = NULL;
  }
  if (subflow == NULL) {
    goto gst_mprtpsender_src_link_done;
  }
  subflow->state = 0;

gst_mprtpsender_src_link_done:
  THIS_READUNLOCK (mprtpsender);
  return result;
}

static void
gst_mprtpsender_src_unlink (GstPad * pad, GstObject * parent)
{
  GstMprtpsender *mprtpsender;
  GList *it;
  Subflow *subflow;

  mprtpsender = GST_MPRTPSENDER (parent);
  GST_DEBUG_OBJECT (mprtpsender, "unlink");
  THIS_WRITELOCK (mprtpsender);

  for (subflow = NULL, it = mprtpsender->subflows; it != NULL; it = it->next) {
    subflow = it->data;
    if (subflow->outpad == pad) {
      break;
    }
    subflow = NULL;
  }
  if (subflow == NULL) {
    goto gst_mprtpsender_src_unlink_done;
  }
  gst_object_unref (subflow->sysclock);
  mprtpsender->subflows = g_list_remove (mprtpsender->subflows, subflow);
gst_mprtpsender_src_unlink_done:
  THIS_WRITEUNLOCK (mprtpsender);
}

typedef enum
{
  PACKET_IS_MPRTP,
  PACKET_IS_MPRTCP,
  PACKET_IS_NOT_MP,
} PacketTypes;

static PacketTypes
_get_packet_mptype (GstMprtpsender * this,
    GstBuffer * buf, GstMapInfo * info, guint8 * subflow_id)
{

  guint8 first_byte, second_byte;
  PacketTypes result = PACKET_IS_NOT_MP;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  MPRTPSubflowHeaderExtension *subflow_infos = NULL;
  guint size;
  gpointer pointer;

  if (gst_buffer_extract (buf, 0, &first_byte, 1) != 1 ||
      gst_buffer_extract (buf, 1, &second_byte, 1) != 1) {
    GST_WARNING_OBJECT (this, "could not extract first byte from buffer");
    goto done;
  }
  if (PACKET_IS_DTLS (first_byte)) {
    goto done;
  }

  if (PACKET_IS_RTP (first_byte)) {
    if (G_UNLIKELY (!gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp))) {
      GST_WARNING_OBJECT (this, "The RTP packet is not readable");
      goto done;
    }

    if (!gst_rtp_buffer_get_extension (&rtp)) {
      gst_rtp_buffer_unmap (&rtp);
      goto done;
    }

    if (!gst_rtp_buffer_get_extension_onebyte_header (&rtp, this->ext_header_id,
            0, &pointer, &size)) {
      gst_rtp_buffer_unmap (&rtp);
      goto done;
    }

    if (subflow_id) {
      subflow_infos = (MPRTPSubflowHeaderExtension *) pointer;
      *subflow_id = subflow_infos->id;
    }
    gst_rtp_buffer_unmap (&rtp);
    result = PACKET_IS_MPRTP;
    goto done;
  }

  if (PACKET_IS_RTCP (second_byte)) {
    if (second_byte != MPRTCP_PACKET_TYPE_IDENTIFIER) {
      goto done;
    }
    if (subflow_id) {
      *subflow_id = (guint8)
          g_ntohs (*((guint16 *) (info->data + 8 /*RTCP Header */  +
                  6 /*first block info until subflow id */ )));
    }
    result = PACKET_IS_MPRTCP;
    goto done;
  }
done:
  return result;
}

static GstFlowReturn
gst_mprtpsender_mprtp_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  GstMprtpsender *this;
  GstFlowReturn result;
  GstMapInfo map;
  PacketTypes packet_type;
  guint8 subflow_id;
  Subflow *subflow;
  gint n, r;
  GstPad *outpad;

  this = GST_MPRTPSENDER (parent);
  GST_DEBUG_OBJECT (this, "RTP/MPRTP/OTHER sink");
  if (!gst_buffer_map (buf, &map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (this, "Buffer is not readable");
    result = GST_FLOW_CUSTOM_ERROR;
    goto exit;
  }

  THIS_READLOCK (this);
  n = g_list_length (this->subflows);
  if (n < 1) {
    GST_ERROR_OBJECT (this, "No appropiate subflow");
    gst_buffer_unmap (buf, &map);
    result = GST_FLOW_CUSTOM_ERROR;
    goto done;
  }
  packet_type = _get_packet_mptype (this, buf, &map, &subflow_id);

  if (packet_type != PACKET_IS_NOT_MP &&
      _select_subflow (this, subflow_id, &subflow) != FALSE) {
    outpad = subflow->outpad;
  } else if (this->pivot_outpad != NULL &&
      gst_pad_is_active (this->pivot_outpad) &&
      gst_pad_is_linked (this->pivot_outpad)) {
    outpad = this->pivot_outpad;
  } else {
    if (n > 1) {
      r = g_random_int_range (0, n - 1);
      subflow = g_list_nth (this->subflows, r)->data;
      outpad = subflow->outpad;
    } else {
      subflow = this->subflows->data;
      outpad = subflow->outpad;
    }
  }

  gst_buffer_unmap (buf, &map);
  result = gst_pad_push (outpad, buf);
done:
  THIS_READUNLOCK (this);
exit:
  return result;
}

static GstFlowReturn
gst_mprtpsender_mprtcp_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  GstMprtpsender *this;
  GstFlowReturn result;
  guint16 subflow_id;
  Subflow *subflow;
  GstMapInfo map = GST_MAP_INFO_INIT;
  GstMPRTCPSubflowBlock *block;
  gboolean urgent;
  guint8 block_type;

  this = GST_MPRTPSENDER (parent);
  THIS_WRITELOCK (this);
  GST_DEBUG_OBJECT (this, "RTP/MPRTP/OTHER sink");
  if (!gst_buffer_map (buf, &map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (this, "Buffer is not readable");
    result = GST_FLOW_CUSTOM_ERROR;
    goto exit;
  }
  block = (GstMPRTCPSubflowBlock *) map.data;
  gst_mprtcp_block_getdown (&block->info, &block_type, NULL, &subflow_id);

  if (!_select_subflow (this, (guint8) subflow_id, &subflow)) {
    GST_WARNING_OBJECT (this, "No subflow exists with id: %d", subflow_id);
    goto done;
  }

  _add_report (subflow, block);
  urgent = _is_block_urgent_to_send (subflow, block);
  if (_do_subflow_report_now (subflow, urgent)) {
    gst_pad_push (subflow->outpad, subflow->report_buf);
    gst_buffer_unref (subflow->report_buf);
    subflow->report_buf = NULL;
  }
done:
  gst_buffer_unmap (buf, &map);
  THIS_WRITEUNLOCK (this);
exit:
  return result;
}

gboolean
_is_block_urgent_to_send (Subflow * subflow, GstMPRTCPSubflowBlock * block)
{
  gboolean result;
  guint8 block_payload_type;
  guint8 fraction_lost;
  gst_rtcp_header_getdown (&block->block_header, NULL, NULL,
      NULL, &block_payload_type, NULL, NULL);

  if (block_payload_type == GST_RTCP_TYPE_RR) {
    gst_rtcp_rrb_getdown (&block->receiver_riport.blocks, NULL, &fraction_lost,
        NULL, NULL, NULL, NULL, NULL);
    result = fraction_lost > 0;
  } else if (block_payload_type == GST_RTCP_TYPE_XR) {
    result = TRUE;
  } else {
    result = FALSE;
  }
  return result;
}

//don't forget to unref the buffer when you don't need it.
void
_add_report (Subflow * this, GstMPRTCPSubflowBlock * block)
{
  GstMapInfo map = GST_MAP_INFO_INIT;
  GstMPRTCPSubflowBlock *it;
  GstMPRTCPSubflowReport *report;
  guint16 length;
  gpointer src;
  guint8 payload_type, block_payload_type;
  gboolean found = FALSE;

  if (this->report_buf == NULL) {
    src = g_malloc0 (1400);
    report = (GstMPRTCPSubflowReport *) src;
    gst_mprtcp_report_init (report);
    gst_mprtcp_riport_append_block (report, block);
    gst_rtcp_header_getdown (&report->header, NULL, NULL, NULL, NULL, &length,
        NULL);
    this->report_buf = gst_buffer_new_wrapped (src, (length + 1) << 2);
    gst_buffer_ref (this->report_buf);
    goto exit;
  }

  if (!gst_buffer_map (this->report_buf, &map, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (this, "Buffer is not readable");
    goto exit;
  }
  report = (GstMPRTCPSubflowReport *) map.data;
  for (it = gst_mprtcp_get_first_block (report);
      it != NULL; it = gst_mprtcp_get_next_block (report, it)) {
    gst_rtcp_header_getdown (&block->block_header, NULL, NULL,
        NULL, &block_payload_type, NULL, NULL);
    gst_rtcp_header_getdown (&it->block_header, NULL, NULL,
        NULL, &payload_type, NULL, NULL);

    if (payload_type != block_payload_type) {
      continue;
    }
    found = TRUE;
    if (payload_type == GST_RTCP_TYPE_SR) {
      _merge_sr_blocks (&block->sender_riport, &it->sender_riport);
    } else if (payload_type == GST_RTCP_TYPE_RR) {
      _merge_rr_blocks (&block->receiver_riport, &it->receiver_riport);
    } else if (payload_type == GST_RTCP_TYPE_XR) {
      _merge_xr_blocks (&block->xr_rfc7243_riport, &it->xr_rfc7243_riport);
    } else {
      GST_WARNING_OBJECT (this, "The payload type %d cannot be "
          "merged with any existed and saved_report report", payload_type);
    }
  }

  if (!found) {
    gpointer dst;
    src = (gpointer) map.data;
    dst = g_malloc0 (1400);
    gst_rtcp_header_getdown (&report->header, NULL, NULL, NULL, NULL, &length,
        NULL);
    memcpy (dst, src, (length + 1) << 2);
    gst_buffer_unmap (this->report_buf, &map);
    gst_buffer_unref (this->report_buf);
    report = (GstMPRTCPSubflowReport *) dst;
    gst_mprtcp_riport_append_block (report, block);
    gst_rtcp_header_getdown (&report->header, NULL, NULL, NULL, NULL, &length,
        NULL);
    this->report_buf = gst_buffer_new_wrapped (dst, (length + 1) << 2);
    gst_buffer_ref (this->report_buf);
  } else {
    gst_buffer_unmap (this->report_buf, &map);
  }

exit:
  return;
}


void
_merge_sr_blocks (GstRTCPSR * src, GstRTCPSR * dst)
{
  guint64 src_ntp;
  guint32 src_rtp, src_pc, src_oc, dst_pc, dst_oc;
  gst_rtcp_srb_getdown (&src->sender_block, &src_ntp, &src_rtp, &src_pc,
      &src_oc);
  gst_rtcp_srb_getdown (&dst->sender_block, NULL, NULL, &dst_pc, &dst_oc);
  dst_pc += src_pc;
  dst_oc += src_oc;
  gst_rtcp_srb_setup (&dst->sender_block, src_ntp, src_rtp, dst_pc, dst_oc);
}

void
_merge_rr_blocks (GstRTCPRR * src, GstRTCPRR * dst)
{
  guint8 src_fraction_lost;
  guint8 dst_fraction_lost;
  guint32 src_cum_packet_lost;
  guint32 src_LSR, src_DLSR, src_ext_hsn, src_jitter;
  guint32 src_ssrc;

  gst_rtcp_rrb_getdown (&src->blocks, &src_ssrc, &src_fraction_lost,
      &src_cum_packet_lost, &src_ext_hsn, &src_jitter, &src_LSR, &src_DLSR);

  gst_rtcp_rrb_getdown (&dst->blocks, NULL, &dst_fraction_lost,
      NULL, NULL, NULL, NULL, NULL);

  dst_fraction_lost += src_fraction_lost;

  gst_rtcp_rrb_setup (&dst->blocks, src_ssrc, dst_fraction_lost,
      src_cum_packet_lost, src_ext_hsn, src_jitter, src_LSR, src_DLSR);
}

void
_merge_xr_blocks (GstRTCPXR_RFC7243 * src, GstRTCPXR_RFC7243 * dst)
{
  guint32 src_db, dst_db;
  gst_rtcp_xr_rfc7243_getdown (src, NULL, NULL, NULL, &src_db);
  gst_rtcp_xr_rfc7243_getdown (dst, NULL, NULL, NULL, &dst_db);
  dst_db += src_db;
  gst_rtcp_xr_rfc7243_change (dst, NULL, NULL, NULL, &dst_db);

}

gboolean
_select_subflow (GstMprtpsender * this, guint8 id, Subflow ** result)
{
  GList *it;
  Subflow *subflow;

  for (it = this->subflows; it != NULL; it = it->next) {
    subflow = it->data;
    if (subflow->id == id) {
      *result = subflow;
      return TRUE;
    }
  }
  *result = NULL;
  return FALSE;
}


gboolean
_do_subflow_report_now (Subflow * this, gboolean urgent)
{
  GstClockTime now;
  gboolean result = FALSE;

  now = gst_clock_get_time (this->sysclock);
  if (!this->report_started) {
    this->report_interval_time = 1 * GST_SECOND;
    this->report_time = now + this->report_interval_time;
    this->report_started = TRUE;
    goto done;
  }

  if (urgent) {
    if (!this->allow_early_report) {
      goto done;
    }
    result = TRUE;
    this->allow_early_report = FALSE;
    if (this->urgent_report_requested_time < now - 20 * GST_SECOND) {
      this->urgent_report_requested_time = now;
    }

    this->report_interval_time =
        (gdouble) this->report_interval_time / 2. * g_random_double_range (.9,
        1.1);
    if (this->report_interval_time < 500 * GST_MSECOND) {
      this->report_interval_time =
          1000. * (gdouble) GST_MSECOND *g_random_double_range (.8, 1.2);;
    }
    this->report_time = now + this->report_interval_time;
    goto done;
  }

  if (now < this->report_time) {
    result = FALSE;
    goto done;
  }
  result = TRUE;
  this->allow_early_report = TRUE;
  if (now - 10 * GST_SECOND < this->urgent_report_requested_time) {
    if (urgent) {
      this->report_interval_time =
          (gdouble) this->report_interval_time / 2. * g_random_double_range (.7,
          1.3);
    } else {
      this->report_interval_time =
          (gdouble) this->report_interval_time * g_random_double_range (.7,
          1.3);
    }
    if (this->report_interval_time < 500 * GST_MSECOND) {
      this->report_interval_time =
          1000. * (gdouble) GST_MSECOND *g_random_double_range (.8, 1.2);
    }
    if (7500 * GST_MSECOND < this->report_interval_time) {
      this->report_interval_time =
          7500. * (gdouble) GST_MSECOND *g_random_double_range (.8, 1.2);
    }
    this->report_time = now + this->report_interval_time;
    goto done;
  }

  this->report_interval_time =
      (gdouble) this->report_interval_time * g_random_double_range (0.75, 2.25);
  if (7500 * GST_MSECOND < this->report_interval_time) {
    this->report_interval_time =
        7500. * (gdouble) GST_MSECOND *g_random_double_range (.8, 1.2);
  }
  this->report_time = now + this->report_interval_time;
done:
  return result;
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef PACKET_IS_RTP
#undef PACKET_IS_DTLS
