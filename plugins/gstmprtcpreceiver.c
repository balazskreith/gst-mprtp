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
 * SECTION:element-gstmprtcpreceiver
 *
 * The mprtcpreceiver element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v fakesrc ! mprtcpreceiver ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstmprtcpreceiver.h"
#include "mprtpssubflow.h"
#include "gstmprtcpbuffer.h"

GST_DEBUG_CATEGORY_STATIC (gst_mprtcpreceiver_debug_category);
#define GST_CAT_DEFAULT gst_mprtcpreceiver_debug_category

#define MPRTCP_PACKET_TYPE_IDENTIFIER 212

#define SUBFLOW_WRITELOCK(mprtcp_ptr) (g_rw_lock_writer_lock(&mprtcp_ptr->rwmutex))
#define SUBFLOW_WRITEUNLOCK(mprtcp_ptr) (g_rw_lock_writer_unlock(&mprtcp_ptr->rwmutex))

#define SUBFLOW_READLOCK(mprtcp_ptr) (g_rw_lock_reader_lock(&mprtcp_ptr->rwmutex))
#define SUBFLOW_READUNLOCK(mprtcp_ptr) (g_rw_lock_reader_unlock(&mprtcp_ptr->rwmutex))



static void gst_mprtcpreceiver_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_mprtcpreceiver_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_mprtcpreceiver_dispose (GObject * object);
static void gst_mprtcpreceiver_finalize (GObject * object);

static GstPad *gst_mprtcpreceiver_request_new_pad(GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps* caps);
static void gst_mprtcpreceiver_release_pad (GstElement * element, GstPad * pad);
static GstStateChangeReturn
gst_mprtcpreceiver_change_state (GstElement * element, GstStateChange transition);
static gboolean gst_mprtcpreceiver_query (GstElement * element, GstQuery * query);

static GstPadLinkReturn gst_mprtcpreceiver_sink_link (GstPad *pad, GstObject *parent, GstPad *peer);
static void gst_mprtcpreceiver_sink_unlink (GstPad *pad, GstObject *parent);
static GstFlowReturn gst_mprtcpreceiver_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buffer);

enum
{
  PROP_0,
};

/* pad templates */

static GstStaticPadTemplate gst_mprtcpreceiver_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
	GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtp;application/x-rtcp;application/x-srtcp")
    );


static GstStaticPadTemplate gst_mprtcpreceiver_rtcp_sink_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_sink",
    GST_PAD_SINK,
	GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
    );

static GstStaticPadTemplate gst_mprtcpreceiver_mprtcp_src_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
    );

static GstStaticPadTemplate gst_mprtcpreceiver_rtcp_src_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
    );


/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstMprtcpreceiver, gst_mprtcpreceiver, GST_TYPE_ELEMENT,
  GST_DEBUG_CATEGORY_INIT (gst_mprtcpreceiver_debug_category, "mprtcpreceiver", 0,
  "debug category for mprtcpreceiver element"));

static void
gst_mprtcpreceiver_class_init (GstMprtcpreceiverClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtcpreceiver_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtcpreceiver_rtcp_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtcpreceiver_mprtcp_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtcpreceiver_rtcp_src_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS(klass),
      "FIXME Long name", "Generic", "FIXME Description",
      "FIXME <fixme@example.com>");



  gobject_class->set_property = gst_mprtcpreceiver_set_property;
  gobject_class->get_property = gst_mprtcpreceiver_get_property;
  gobject_class->dispose = gst_mprtcpreceiver_dispose;
  gobject_class->finalize = gst_mprtcpreceiver_finalize;
  element_class->request_new_pad = GST_DEBUG_FUNCPTR (gst_mprtcpreceiver_request_new_pad);
  element_class->release_pad = GST_DEBUG_FUNCPTR (gst_mprtcpreceiver_release_pad);
  element_class->change_state = GST_DEBUG_FUNCPTR (gst_mprtcpreceiver_change_state);
  element_class->query = GST_DEBUG_FUNCPTR (gst_mprtcpreceiver_query);
}

static void
gst_mprtcpreceiver_init (GstMprtcpreceiver *mprtcpreceiver)
{

  mprtcpreceiver->rtcp_sinkpad = gst_pad_new_from_static_template (
    &gst_mprtcpreceiver_rtcp_sink_template, "rtcp_sink"
  );
  gst_pad_set_chain_function (mprtcpreceiver->rtcp_sinkpad,
      GST_DEBUG_FUNCPTR(gst_mprtcpreceiver_sink_chain));
  gst_element_add_pad (GST_ELEMENT(mprtcpreceiver), mprtcpreceiver->rtcp_sinkpad);

  mprtcpreceiver->rtcp_srcpad = gst_pad_new_from_static_template (
    &gst_mprtcpreceiver_rtcp_src_template, "rtcp_src"
  );
  gst_element_add_pad (GST_ELEMENT(mprtcpreceiver), mprtcpreceiver->rtcp_srcpad);

  mprtcpreceiver->mprtcp_srcpad = gst_pad_new_from_static_template (
    &gst_mprtcpreceiver_mprtcp_src_template, "mprtcp_src"
  );
  gst_element_add_pad (GST_ELEMENT(mprtcpreceiver), mprtcpreceiver->mprtcp_srcpad);

  g_rw_lock_init(&mprtcpreceiver->rwmutex);
}

void
gst_mprtcpreceiver_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMprtcpreceiver *mprtcpreceiver = GST_MPRTCPRECEIVER (object);
  GST_DEBUG_OBJECT (mprtcpreceiver, "set_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_mprtcpreceiver_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstMprtcpreceiver *mprtcpreceiver = GST_MPRTCPRECEIVER (object);

  GST_DEBUG_OBJECT (mprtcpreceiver, "get_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_mprtcpreceiver_dispose (GObject * object)
{
  GstMprtcpreceiver *mprtcpreceiver = GST_MPRTCPRECEIVER (object);

  GST_DEBUG_OBJECT (mprtcpreceiver, "dispose");

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (gst_mprtcpreceiver_parent_class)->dispose (object);
}

void
gst_mprtcpreceiver_finalize (GObject * object)
{
  GstMprtcpreceiver *mprtcpreceiver = GST_MPRTCPRECEIVER (object);

  GST_DEBUG_OBJECT (mprtcpreceiver, "finalize");

  /* clean up object here */

  G_OBJECT_CLASS (gst_mprtcpreceiver_parent_class)->finalize (object);
}



static GstPad *
gst_mprtcpreceiver_request_new_pad (GstElement * element, GstPadTemplate * templ,
	    const gchar * name, const GstCaps* caps)
{

	GstPad *sinkpad;
	GstMprtcpreceiver *mprtcpr;
	GList *it;
	guint16 subflow_id;
	mprtcpr = GST_MPRTCPRECEIVER (element);
	GST_DEBUG_OBJECT (mprtcpr, "requesting pad");

	sscanf(name, "sink_%u", &subflow_id);

	SUBFLOW_WRITELOCK(mprtcpr);

	sinkpad = gst_pad_new_from_template (templ, name);
	gst_pad_set_link_function (sinkpad,
	            GST_DEBUG_FUNCPTR(gst_mprtcpreceiver_sink_link));
	gst_pad_set_unlink_function (sinkpad,
	            GST_DEBUG_FUNCPTR(gst_mprtcpreceiver_sink_unlink));
	gst_pad_set_chain_function (sinkpad,
	            GST_DEBUG_FUNCPTR(gst_mprtcpreceiver_sink_chain));
	mprtcpr->subflows = g_list_prepend(mprtcpr->subflows, sinkpad);
	SUBFLOW_WRITEUNLOCK(mprtcpr);

	gst_element_add_pad (GST_ELEMENT (mprtcpr), sinkpad);

	gst_pad_set_active (sinkpad, TRUE);

	return sinkpad;
}

static void
gst_mprtcpreceiver_release_pad (GstElement * element, GstPad * pad)
{

}

static GstStateChangeReturn
gst_mprtcpreceiver_change_state (GstElement * element, GstStateChange transition)
{
  GstMprtcpreceiver *mprtcpreceiver;
  GstStateChangeReturn ret;
  g_return_val_if_fail (GST_IS_MPRTCPRECEIVER (element), GST_STATE_CHANGE_FAILURE);
  mprtcpreceiver = GST_MPRTCPRECEIVER (element);

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

  ret = GST_ELEMENT_CLASS (gst_mprtcpreceiver_parent_class)->change_state (element, transition);

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
gst_mprtcpreceiver_query (GstElement * element, GstQuery * query)
{
  GstMprtcpreceiver *mprtcpreceiver = GST_MPRTCPRECEIVER (element);
  gboolean ret;

  GST_DEBUG_OBJECT (mprtcpreceiver, "query");
  switch (GST_QUERY_TYPE (query)) {
    default:
      ret = GST_ELEMENT_CLASS (gst_mprtcpreceiver_parent_class)->query (element, query);
      break;
  }

  return ret;
}


static GstPadLinkReturn
gst_mprtcpreceiver_sink_link (GstPad *pad, GstObject *parent, GstPad *peer)
{
  GstMprtcpreceiver *mprtcpreceiver;
  mprtcpreceiver = GST_MPRTCPRECEIVER (parent);
  GST_DEBUG_OBJECT(mprtcpreceiver, "link");

  return GST_PAD_LINK_OK;
}

static void
gst_mprtcpreceiver_sink_unlink (GstPad *pad, GstObject *parent)
{
  GstMprtcpreceiver *mprtcpreceiver;
  mprtcpreceiver = GST_MPRTCPRECEIVER (parent);
  GST_DEBUG_OBJECT(mprtcpreceiver, "unlink");

}

static GstFlowReturn
gst_mprtcpreceiver_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buf)
{
  GstMprtcpreceiver *mprtcpr;
  GstMapInfo info;
  GstPad *outpad = NULL;
  guint8 *data;
  GstFlowReturn result;

  mprtcpr = GST_MPRTCPRECEIVER (parent);
  GST_DEBUG_OBJECT(mprtcpr, "RTP/RTCP/MPRTCP/MPRTCP sink");
  SUBFLOW_READLOCK(mprtcpr);

  if(!gst_buffer_map(buf, &info, GST_MAP_READ)){
  	GST_WARNING("Buffer is not readable");
  	result = GST_FLOW_ERROR;
  	goto gst_mprtcpreceiver_sink_chain_done;
  }
  data = info.data + 1;
  gst_buffer_unmap(buf, &info);

  //demultiplexing based on RFC5761
  if(*data == MPRTCP_PACKET_TYPE_IDENTIFIER){
	result = gst_pad_push(mprtcpr->mprtcp_srcpad, buf);
	goto gst_mprtcpreceiver_sink_chain_done;
  }

  //the packet is either rtcp or mprtcp
  if(*data < 192 || *data > 223){
    result = gst_pad_push(mprtcpr->rtp_srcpad, buf);
    goto gst_mprtcpreceiver_sink_chain_done;
    //return GST_FLOW_OK;
  }

  //the packet is rtcp
  if (!gst_pad_is_linked (mprtcpr->rtcp_srcpad)) {
    GST_ERROR_OBJECT(mprtcpr, "The rtcp src is not connected");
    result = GST_FLOW_CUSTOM_ERROR;
    goto gst_mprtcpreceiver_sink_chain_done;
  }

  result = gst_pad_push(mprtcpr->rtcp_srcpad, buf);

gst_mprtcpreceiver_sink_chain_done:
  SUBFLOW_READUNLOCK(mprtcpr);
  return result;
}



#undef MPRTCP_PACKET_TYPE_IDENTIFIER
#undef SUBFLOW_WRITELOCK
#undef SUBFLOW_WRITEUNLOCK
#undef SUBFLOW_READLOCK
#undef SUBFLOW_READUNLOCK
