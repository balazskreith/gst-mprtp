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
 * SECTION:element-gstmprtpreceiver
 *
 * The mprtpreceiver element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v fakesrc ! mprtpreceiver ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <stdio.h>
#include <string.h>
#include "gstmprtpreceiver.h"
#include "mprtpspath.h"
#include "mprtprpath.h"
#include "gstmprtcpbuffer.h"

GST_DEBUG_CATEGORY_STATIC (gst_mprtpreceiver_debug_category);
#define GST_CAT_DEFAULT gst_mprtpreceiver_debug_category

#define THIS_WRITELOCK(mprtcp_ptr) (g_rw_lock_writer_lock(&mprtcp_ptr->rwmutex))
#define THIS_WRITEUNLOCK(mprtcp_ptr) (g_rw_lock_writer_unlock(&mprtcp_ptr->rwmutex))

#define THIS_READLOCK(mprtcp_ptr) (g_rw_lock_reader_lock(&mprtcp_ptr->rwmutex))
#define THIS_READUNLOCK(mprtcp_ptr) (g_rw_lock_reader_unlock(&mprtcp_ptr->rwmutex))

#define PACKET_IS_RTP_OR_RTCP(b) (b > 0x7f && b < 0xc0)
#define PACKET_IS_RTCP(b) (b > 192 && b < 223)
#define PACKET_IS_DTLS(b) (b > 0x13 && b < 0x40)

typedef struct
{
  GstPad *inpad;
  guint8 id;
} Subflow;

static void gst_mprtpreceiver_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_mprtpreceiver_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_mprtpreceiver_dispose (GObject * object);
static void gst_mprtpreceiver_finalize (GObject * object);

static GstPad *gst_mprtpreceiver_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_mprtpreceiver_release_pad (GstElement * element, GstPad * pad);
static GstStateChangeReturn
gst_mprtpreceiver_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_mprtpreceiver_query (GstElement * element,
    GstQuery * query);
static gboolean gst_mprtpreceiver_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_mprtpreceiver_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstPadLinkReturn gst_mprtpreceiver_sink_link (GstPad * pad,
    GstObject * parent, GstPad * peer);
static void gst_mprtpreceiver_sink_unlink (GstPad * pad, GstObject * parent);
static GstFlowReturn gst_mprtpreceiver_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);

GstFlowReturn _send_mprtcp_buffer (GstMprtpreceiver * this, GstBuffer * buf);
enum
{
  PROP_0,
  PROP_MPRTP_EXT_HEADER_ID,
  PROP_REPORT_ONLY,
};


/* pad templates */

static GstStaticPadTemplate gst_mprtpreceiver_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate gst_mprtpreceiver_mprtcp_rr_src_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_rr_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static GstStaticPadTemplate gst_mprtpreceiver_mprtcp_sr_src_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_sr_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp")
    );


static GstStaticPadTemplate gst_mprtpreceiver_mprtp_src_template =
GST_STATIC_PAD_TEMPLATE ("mprtp_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );


/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstMprtpreceiver, gst_mprtpreceiver, GST_TYPE_ELEMENT,
    GST_DEBUG_CATEGORY_INIT (gst_mprtpreceiver_debug_category, "mprtpreceiver",
        0, "debug category for mprtpreceiver element"));

static void
gst_mprtpreceiver_class_init (GstMprtpreceiverClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpreceiver_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpreceiver_mprtcp_rr_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpreceiver_mprtcp_sr_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpreceiver_mprtp_src_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "FIXME Long name", "Generic", "FIXME Description",
      "FIXME <fixme@example.com>");

  gobject_class->set_property = gst_mprtpreceiver_set_property;
  gobject_class->get_property = gst_mprtpreceiver_get_property;
  gobject_class->dispose = gst_mprtpreceiver_dispose;
  gobject_class->finalize = gst_mprtpreceiver_finalize;

  g_object_class_install_property (gobject_class, PROP_MPRTP_EXT_HEADER_ID,
      g_param_spec_uint ("mprtp-ext-header-id",
          "Set or get the id for the RTP extension",
          "Sets or gets the id for the extension header the MpRTP based on", 0,
          15, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_REPORT_ONLY,
      g_param_spec_boolean ("report-only",
          "Indicate weather the receiver only receive reports on its subflows",
          "Indicate weather the receiver only receive reports on its subflows",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_mprtpreceiver_request_new_pad);
  element_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_mprtpreceiver_release_pad);
  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_mprtpreceiver_change_state);
  element_class->query = GST_DEBUG_FUNCPTR (gst_mprtpreceiver_query);


}

static void
gst_mprtpreceiver_init (GstMprtpreceiver * mprtpreceiver)
{
  mprtpreceiver->mprtcp_sr_srcpad =
      gst_pad_new_from_static_template
      (&gst_mprtpreceiver_mprtcp_sr_src_template, "mprtcp_sr_src");
  gst_element_add_pad (GST_ELEMENT (mprtpreceiver),
      mprtpreceiver->mprtcp_sr_srcpad);

  mprtpreceiver->mprtcp_rr_srcpad =
      gst_pad_new_from_static_template
      (&gst_mprtpreceiver_mprtcp_sr_src_template, "mprtcp_rr_src");
  gst_element_add_pad (GST_ELEMENT (mprtpreceiver),
      mprtpreceiver->mprtcp_rr_srcpad);

  mprtpreceiver->mprtp_srcpad =
      gst_pad_new_from_static_template (&gst_mprtpreceiver_mprtp_src_template,
      "mprtp_src");
  gst_element_add_pad (GST_ELEMENT (mprtpreceiver),
      mprtpreceiver->mprtp_srcpad);

  g_rw_lock_init (&mprtpreceiver->rwmutex);

  mprtpreceiver->only_report_receiving = FALSE;
  mprtpreceiver->mprtp_ext_header_id = MPRTP_DEFAULT_EXTENSION_HEADER_ID;
}

void
gst_mprtpreceiver_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMprtpreceiver *this = GST_MPRTPRECEIVER (object);
  GST_DEBUG_OBJECT (this, "set_property");

  switch (property_id) {
    case PROP_MPRTP_EXT_HEADER_ID:
      THIS_WRITELOCK (this);
      this->mprtp_ext_header_id = (guint8) g_value_get_uint (value);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_REPORT_ONLY:
      THIS_WRITELOCK (this);
      this->only_report_receiving = g_value_get_boolean (value);
      THIS_WRITEUNLOCK (this);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_mprtpreceiver_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstMprtpreceiver *this = GST_MPRTPRECEIVER (object);

  GST_DEBUG_OBJECT (this, "get_property");

  switch (property_id) {
    case PROP_MPRTP_EXT_HEADER_ID:
      THIS_READLOCK (this);
      g_value_set_uint (value, (guint) this->mprtp_ext_header_id);
      THIS_READUNLOCK (this);
      break;
    case PROP_REPORT_ONLY:
      THIS_READLOCK (this);
      g_value_set_boolean (value, this->only_report_receiving);
      THIS_READUNLOCK (this);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_mprtpreceiver_dispose (GObject * object)
{
  GstMprtpreceiver *mprtpreceiver = GST_MPRTPRECEIVER (object);

  GST_DEBUG_OBJECT (mprtpreceiver, "dispose");

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (gst_mprtpreceiver_parent_class)->dispose (object);
}

void
gst_mprtpreceiver_finalize (GObject * object)
{
  GstMprtpreceiver *mprtpreceiver = GST_MPRTPRECEIVER (object);

  GST_DEBUG_OBJECT (mprtpreceiver, "finalize");

  /* clean up object here */

  G_OBJECT_CLASS (gst_mprtpreceiver_parent_class)->finalize (object);
}



static GstPad *
gst_mprtpreceiver_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{

  GstPad *sinkpad;
  GstMprtpreceiver *this;
  guint8 subflow_id;
  Subflow *subflow;

  this = GST_MPRTPRECEIVER (element);
  GST_DEBUG_OBJECT (this, "requesting pad");

  sscanf (name, "sink_%hhu", &subflow_id);

  THIS_WRITELOCK (this);

  sinkpad = gst_pad_new_from_template (templ, name);
  gst_pad_set_query_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpreceiver_sink_query));
  gst_pad_set_event_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpreceiver_sink_event));
  gst_pad_set_link_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpreceiver_sink_link));
  gst_pad_set_unlink_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpreceiver_sink_unlink));
  gst_pad_set_chain_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpreceiver_sink_chain));
  subflow = (Subflow *) g_malloc0 (sizeof (Subflow));
  subflow->id = subflow_id;
  subflow->inpad = sinkpad;
  this->subflows = g_list_prepend (this->subflows, subflow);
  THIS_WRITEUNLOCK (this);

  gst_pad_set_active (sinkpad, TRUE);

  gst_element_add_pad (GST_ELEMENT (this), sinkpad);

  return sinkpad;
}


static gboolean
gst_mprtpreceiver_sink_query (GstPad * sinkpad, GstObject * parent,
    GstQuery * query)
{
  GstMprtpreceiver *this = GST_MPRTPRECEIVER (parent);
  gboolean result;
  GST_DEBUG_OBJECT (this, "query");
//  g_print ("QUERY to the sink: %s\n", GST_QUERY_TYPE_NAME (query));
  switch (GST_QUERY_TYPE (query)) {

    default:
      result = gst_pad_peer_query (this->mprtp_srcpad, query);
      break;
  }

  return result;
}

static gboolean
gst_mprtpreceiver_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstMprtpreceiver *this = GST_MPRTPRECEIVER (parent);
  gboolean result = FALSE;
  GstPad *peer;

  GST_DEBUG_OBJECT (this, "sink event");
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
    case GST_EVENT_SEGMENT_DONE:
    case GST_EVENT_EOS:
    case GST_EVENT_FLUSH_START:
    case GST_EVENT_FLUSH_STOP:
    case GST_EVENT_LATENCY:
    case GST_EVENT_STREAM_START:
      if (this->only_report_receiving) {
        result = TRUE;
        goto done;
      }
    default:
      if (gst_pad_is_linked (this->mprtp_srcpad)) {
        peer = gst_pad_get_peer (this->mprtp_srcpad);
        result = gst_pad_send_event (peer, event);
        gst_object_unref (peer);
      }
      g_print ("EVENT to the sink: %s", GST_EVENT_TYPE_NAME (event));
      result = gst_pad_event_default (pad, parent, event);
      break;
  }
done:
  return result;
}

static void
gst_mprtpreceiver_release_pad (GstElement * element, GstPad * pad)
{

}

static GstStateChangeReturn
gst_mprtpreceiver_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  g_return_val_if_fail (GST_IS_MPRTPRECEIVER (element),
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
      GST_ELEMENT_CLASS (gst_mprtpreceiver_parent_class)->change_state (element,
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
gst_mprtpreceiver_query (GstElement * element, GstQuery * query)
{
  GstMprtpreceiver *mprtpreceiver = GST_MPRTPRECEIVER (element);
  gboolean ret;

  GST_DEBUG_OBJECT (mprtpreceiver, "query");
  g_print ("QUERY to the element: %s\n", GST_QUERY_TYPE_NAME (query));
  switch (GST_QUERY_TYPE (query)) {
    default:
      ret =
          GST_ELEMENT_CLASS (gst_mprtpreceiver_parent_class)->query (element,
          query);
      break;
  }

  return ret;
}


static GstPadLinkReturn
gst_mprtpreceiver_sink_link (GstPad * pad, GstObject * parent, GstPad * peer)
{
  GstMprtpreceiver *mprtpreceiver;
  mprtpreceiver = GST_MPRTPRECEIVER (parent);
  GST_DEBUG_OBJECT (mprtpreceiver, "link");

  return GST_PAD_LINK_OK;
}

static void
gst_mprtpreceiver_sink_unlink (GstPad * pad, GstObject * parent)
{
  GstMprtpreceiver *mprtpreceiver;
  mprtpreceiver = GST_MPRTPRECEIVER (parent);
  GST_DEBUG_OBJECT (mprtpreceiver, "unlink");

}



typedef enum
{
  PACKET_IS_MPRTP,
  PACKET_IS_MPRTCP,
  PACKET_IS_NOT_MP,
} PacketTypes;

static PacketTypes
_get_packet_mptype (GstMprtpreceiver * this,
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

  if (PACKET_IS_RTP_OR_RTCP (first_byte)) {
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

    if (G_UNLIKELY (!gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp))) {
      GST_WARNING_OBJECT (this, "The RTP packet is not readable");
      goto done;
    }

    if (!gst_rtp_buffer_get_extension (&rtp)) {
      gst_rtp_buffer_unmap (&rtp);
      goto done;
    }

    if (!gst_rtp_buffer_get_extension_onebyte_header (&rtp,
            this->mprtp_ext_header_id, 0, &pointer, &size)) {
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


done:
  return result;
}


static GstFlowReturn
gst_mprtpreceiver_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstMprtpreceiver *this;
  GstFlowReturn result;
  GstMapInfo map;
  PacketTypes packet_type;
  guint8 subflow_id = 0;

  this = GST_MPRTPRECEIVER (parent);
  GST_DEBUG_OBJECT (this, "RTP/MPRTP/OTHER sink");
  if (!gst_buffer_map (buf, &map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (this, "Buffer is not readable");
    result = GST_FLOW_CUSTOM_ERROR;
    goto exit;
  }
  THIS_READLOCK (this);
  packet_type = _get_packet_mptype (this, buf, &map, &subflow_id);

  if (packet_type == PACKET_IS_MPRTCP) {
    result = _send_mprtcp_buffer (this, buf);
    gst_buffer_unmap (buf, &map);
  } else {
    gst_buffer_unmap (buf, &map);

    result = gst_pad_push (this->mprtp_srcpad, buf);
  }

  THIS_READUNLOCK (this);
exit:
  return result;
}


GstFlowReturn
_send_mprtcp_buffer (GstMprtpreceiver * this, GstBuffer * buf)
{
  GstFlowReturn result = GST_FLOW_OK;
  GstPad *outpad;
  GstRTCPBuffer rtcp = { NULL, };
  GstRTCPHeader *block_header;
  GstMPRTCPSubflowReport *report;
  GstMPRTCPSubflowBlock *block;
  GstMPRTCPSubflowInfo *info;
  guint8 info_type;
  guint8 block_length;
  guint8 block_riport_type;
  GstBuffer *buffer;
  gpointer data;
  gsize buf_length;

  if (G_UNLIKELY (!gst_rtcp_buffer_map (buf, GST_MAP_READ, &rtcp))) {
    GST_WARNING_OBJECT (this, "The RTCP packet is not readable");
    return result;
  }

  report = (GstMPRTCPSubflowReport *) gst_rtcp_get_first_header (&rtcp);
  for (block = gst_mprtcp_get_first_block (report);
      block != NULL; block = gst_mprtcp_get_next_block (report, block)) {
    info = &block->info;
    gst_mprtcp_block_getdown (info, &info_type, &block_length, NULL);
    if (info_type != 0) {
      continue;
    }
    block_header = &block->block_header;
    gst_rtcp_header_getdown (block_header, NULL, NULL, NULL,
        &block_riport_type, NULL, NULL);

    buf_length = (block_length + 1) << 2;
    data = g_malloc0 (buf_length);
    memcpy (data, (gpointer) block, buf_length);
    buffer = gst_buffer_new_wrapped (data, buf_length);
    outpad = (block_riport_type == GST_RTCP_TYPE_SR) ? this->mprtcp_sr_srcpad
        : this->mprtcp_rr_srcpad;
    if ((result = gst_pad_push (outpad, buffer)) != GST_FLOW_OK) {
      goto done;
    }
  }

done:
  return result;
}


#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef PACKET_IS_RTP_OR_RTCP
#undef PACKET_IS_DTLS
