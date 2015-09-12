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
#include "gstmprtpreceiver.h"
#include "mprtpssubflow.h"
#include "mprtprsubflow.h"
#include "gstmprtcpbuffer.h"

GST_DEBUG_CATEGORY_STATIC (gst_mprtpreceiver_debug_category);
#define GST_CAT_DEFAULT gst_mprtpreceiver_debug_category

#define MPRTCP_PACKET_TYPE_IDENTIFIER 212

#define THIS_WRITELOCK(mprtcp_ptr) (g_rw_lock_writer_lock(&mprtcp_ptr->rwmutex))
#define THIS_WRITEUNLOCK(mprtcp_ptr) (g_rw_lock_writer_unlock(&mprtcp_ptr->rwmutex))

#define THIS_READLOCK(mprtcp_ptr) (g_rw_lock_reader_lock(&mprtcp_ptr->rwmutex))
#define THIS_READUNLOCK(mprtcp_ptr) (g_rw_lock_reader_unlock(&mprtcp_ptr->rwmutex))


typedef struct{
  GstPad* inpad;
  guint16 id;
}Subflow;

static void gst_mprtpreceiver_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_mprtpreceiver_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_mprtpreceiver_dispose (GObject * object);
static void gst_mprtpreceiver_finalize (GObject * object);

static GstPad *gst_mprtpreceiver_request_new_pad(GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps* caps);
static void gst_mprtpreceiver_release_pad (GstElement * element, GstPad * pad);
static GstStateChangeReturn
gst_mprtpreceiver_change_state (GstElement * element, GstStateChange transition);
static gboolean gst_mprtpreceiver_query (GstElement * element, GstQuery * query);
static gboolean gst_mprtpreceiver_sink_query(GstPad *pad, GstObject *parent,
        GstQuery *query);
static gboolean gst_mprtpreceiver_sink_event(GstPad *pad, GstObject *parent,
        GstEvent *event);
static GstPadLinkReturn gst_mprtpreceiver_sink_link (GstPad *pad, GstObject *parent, GstPad *peer);
static void gst_mprtpreceiver_sink_unlink (GstPad *pad, GstObject *parent);
static GstFlowReturn gst_mprtpreceiver_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buffer);

static GstPad* _select_mprtcp_outpad(GstMprtpreceiver *this, GstBuffer *buf);

enum
{
  PROP_0,
};

/* pad templates */

static GstStaticPadTemplate gst_mprtpreceiver_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
	GST_PAD_REQUEST,
    GST_STATIC_CAPS ("ANY")
    );


static GstStaticPadTemplate gst_mprtpreceiver_rtcp_sink_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_sink",
    GST_PAD_SINK,
	GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
    );

static GstStaticPadTemplate gst_mprtpreceiver_mprtcp_rr_src_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_rr_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
    );

static GstStaticPadTemplate gst_mprtpreceiver_mprtcp_sr_src_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_sr_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
    );

static GstStaticPadTemplate gst_mprtpreceiver_rtcp_src_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
    );

static GstStaticPadTemplate gst_mprtpreceiver_mprtp_src_template =
GST_STATIC_PAD_TEMPLATE ("mprtp_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );


/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstMprtpreceiver, gst_mprtpreceiver, GST_TYPE_ELEMENT,
  GST_DEBUG_CATEGORY_INIT (gst_mprtpreceiver_debug_category, "mprtpreceiver", 0,
  "debug category for mprtpreceiver element"));

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
      gst_static_pad_template_get (&gst_mprtpreceiver_rtcp_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpreceiver_mprtcp_rr_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpreceiver_mprtcp_sr_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpreceiver_mprtp_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpreceiver_rtcp_src_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS(klass),
      "FIXME Long name", "Generic", "FIXME Description",
      "FIXME <fixme@example.com>");



  gobject_class->set_property = gst_mprtpreceiver_set_property;
  gobject_class->get_property = gst_mprtpreceiver_get_property;
  gobject_class->dispose = gst_mprtpreceiver_dispose;
  gobject_class->finalize = gst_mprtpreceiver_finalize;
  element_class->request_new_pad = GST_DEBUG_FUNCPTR (gst_mprtpreceiver_request_new_pad);
  element_class->release_pad = GST_DEBUG_FUNCPTR (gst_mprtpreceiver_release_pad);
  element_class->change_state = GST_DEBUG_FUNCPTR (gst_mprtpreceiver_change_state);
  element_class->query = GST_DEBUG_FUNCPTR (gst_mprtpreceiver_query);
}

static void
gst_mprtpreceiver_init (GstMprtpreceiver *mprtpreceiver)
{

  mprtpreceiver->rtcp_sinkpad = gst_pad_new_from_static_template (
    &gst_mprtpreceiver_rtcp_sink_template, "rtcp_sink"
  );
  gst_pad_set_chain_function (mprtpreceiver->rtcp_sinkpad,
      GST_DEBUG_FUNCPTR(gst_mprtpreceiver_sink_chain));
  gst_element_add_pad (GST_ELEMENT(mprtpreceiver), mprtpreceiver->rtcp_sinkpad);

  mprtpreceiver->rtcp_srcpad = gst_pad_new_from_static_template (
    &gst_mprtpreceiver_mprtcp_sr_src_template, "rtcp_src"
  );
  gst_element_add_pad (GST_ELEMENT(mprtpreceiver), mprtpreceiver->rtcp_srcpad);

	mprtpreceiver->mprtcp_sr_srcpad = gst_pad_new_from_static_template (
	  &gst_mprtpreceiver_mprtcp_sr_src_template, "mprtcp_sr_src"
	  );
	gst_element_add_pad (GST_ELEMENT(mprtpreceiver), mprtpreceiver->mprtcp_sr_srcpad);

	mprtpreceiver->mprtcp_rr_srcpad = gst_pad_new_from_static_template (
	  &gst_mprtpreceiver_mprtcp_sr_src_template, "mprtcp_rr_src"
	  );
	gst_element_add_pad (GST_ELEMENT(mprtpreceiver), mprtpreceiver->mprtcp_rr_srcpad);

  mprtpreceiver->mprtp_srcpad = gst_pad_new_from_static_template (
    &gst_mprtpreceiver_mprtp_src_template, "mprtp_src"
  );
  gst_element_add_pad (GST_ELEMENT(mprtpreceiver), mprtpreceiver->mprtp_srcpad);

  g_rw_lock_init(&mprtpreceiver->rwmutex);
  mprtpreceiver->caps_not_set = TRUE;
}

void
gst_mprtpreceiver_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMprtpreceiver *mprtpreceiver = GST_MPRTPRECEIVER (object);
  GST_DEBUG_OBJECT (mprtpreceiver, "set_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_mprtpreceiver_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstMprtpreceiver *mprtpreceiver = GST_MPRTPRECEIVER (object);

  GST_DEBUG_OBJECT (mprtpreceiver, "get_property");

  switch (property_id) {
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
	    const gchar * name, const GstCaps* caps)
{

	GstPad *sinkpad;
	GstMprtpreceiver *this;
	guint16 subflow_id;
	Subflow *subflow;

	this = GST_MPRTPRECEIVER (element);
	GST_DEBUG_OBJECT (this, "requesting pad");

	sscanf(name, "sink_%u", &subflow_id);

	THIS_WRITELOCK(this);

	sinkpad = gst_pad_new_from_template (templ, name);
	gst_pad_set_query_function(sinkpad,
			GST_DEBUG_FUNCPTR(gst_mprtpreceiver_sink_query));
	gst_pad_set_event_function(sinkpad,
			GST_DEBUG_FUNCPTR(gst_mprtpreceiver_sink_event));
	gst_pad_set_link_function (sinkpad,
	            GST_DEBUG_FUNCPTR(gst_mprtpreceiver_sink_link));
	gst_pad_set_unlink_function (sinkpad,
	            GST_DEBUG_FUNCPTR(gst_mprtpreceiver_sink_unlink));
	gst_pad_set_chain_function (sinkpad,
	            GST_DEBUG_FUNCPTR(gst_mprtpreceiver_sink_chain));
	subflow = (Subflow*)g_malloc0(sizeof(Subflow));
	subflow->id = subflow_id;
	subflow->inpad = sinkpad;
	this->subflows = g_list_prepend(this->subflows, subflow);
	THIS_WRITEUNLOCK(this);

	gst_element_add_pad (GST_ELEMENT (this), sinkpad);

	gst_pad_set_active (sinkpad, TRUE);

	return sinkpad;
}

static void
gst_mprtpreceiver_release_pad (GstElement * element, GstPad * pad)
{

}

static GstStateChangeReturn
gst_mprtpreceiver_change_state (GstElement * element, GstStateChange transition)
{
  GstMprtpreceiver *mprtpreceiver;
  GstStateChangeReturn ret;
  g_return_val_if_fail (GST_IS_MPRTPRECEIVER (element), GST_STATE_CHANGE_FAILURE);
  mprtpreceiver = GST_MPRTPRECEIVER (element);

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

  ret = GST_ELEMENT_CLASS (gst_mprtpreceiver_parent_class)->change_state (element, transition);

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

static gboolean gst_mprtpreceiver_sink_query(GstPad *sinkpad, GstObject *parent,
        GstQuery *query)
{
	GstMprtpreceiver *this = GST_MPRTPRECEIVER (parent);
	gboolean result;
	GST_DEBUG_OBJECT (this, "query");
	g_print("QUERY to the sink: %s\n",GST_QUERY_TYPE_NAME(query));
	switch (GST_QUERY_TYPE (query)) {

	default:
	    result = gst_pad_peer_query(this->mprtp_srcpad, query);
	  break;
	}

	return result;
}

static gboolean gst_mprtpreceiver_sink_event(GstPad *pad, GstObject *parent,
        GstEvent *event)
{
	GstMprtpreceiver *this = GST_MPRTPRECEIVER (parent);
	gboolean result;
	GstPad *peer;

	GST_DEBUG_OBJECT (this, "sink event");
	g_print("EVENT to the sink: %s\n",GST_EVENT_TYPE_NAME(event));
	switch (GST_QUERY_TYPE (event)) {
	default:
	  peer = gst_pad_get_peer(this->mprtp_srcpad);
      result = gst_pad_send_event(peer, event);
	  break;
	}

	return result;
}

static gboolean
gst_mprtpreceiver_query (GstElement * element, GstQuery * query)
{
  GstMprtpreceiver *mprtpreceiver = GST_MPRTPRECEIVER (element);
  gboolean ret;

  GST_DEBUG_OBJECT (mprtpreceiver, "query");
  g_print("QUERY to the element: %s\n",GST_QUERY_TYPE_NAME(query));
  switch (GST_QUERY_TYPE (query)) {
    default:
      ret = GST_ELEMENT_CLASS (gst_mprtpreceiver_parent_class)->query (element, query);
      break;
  }

  return ret;
}


static GstPadLinkReturn
gst_mprtpreceiver_sink_link (GstPad *pad, GstObject *parent, GstPad *peer)
{
  GstMprtpreceiver *mprtpreceiver;
  mprtpreceiver = GST_MPRTPRECEIVER (parent);
  GST_DEBUG_OBJECT(mprtpreceiver, "link");

  return GST_PAD_LINK_OK;
}

static void
gst_mprtpreceiver_sink_unlink (GstPad *pad, GstObject *parent)
{
  GstMprtpreceiver *mprtpreceiver;
  mprtpreceiver = GST_MPRTPRECEIVER (parent);
  GST_DEBUG_OBJECT(mprtpreceiver, "unlink");

}

static GstFlowReturn
gst_mprtpreceiver_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buf)
{
  GstMprtpreceiver *this;
  GstMapInfo info;
  GstPad *outpad = NULL;
  guint8 *data;
  GstFlowReturn result;

  this = GST_MPRTPRECEIVER (parent);
  GST_DEBUG_OBJECT(this, "RTCP/MPRTCP sink");
  THIS_READLOCK(this);

  if(!gst_buffer_map(buf, &info, GST_MAP_READ)){
  	GST_WARNING("Buffer is not readable");
  	result = GST_FLOW_ERROR;
  	goto gst_mprtpreceiver_sink_chain_done;
  }
  data = info.data + 1;
  gst_buffer_unmap(buf, &info);

  //demultiplexing based on RFC5761

  //the packet is either rtcp or mprtcp
  if(*data < 192 || *data > 223){
    result = gst_pad_push(this->mprtp_srcpad, buf);
    goto gst_mprtpreceiver_sink_chain_done;
    //return GST_FLOW_OK;
  }

  if(*data != MPRTCP_PACKET_TYPE_IDENTIFIER){
	if(gst_pad_is_linked(this->rtcp_srcpad)){
  	  result = gst_pad_push(this->rtcp_srcpad, buf);
	}else{
	  result = gst_pad_push(this->mprtp_srcpad, buf);
	}
  	goto gst_mprtpreceiver_sink_chain_done;
  }
  //The packet is MPRTCP packet

  outpad = _select_mprtcp_outpad(this, buf);
  result = gst_pad_push(outpad, buf);

gst_mprtpreceiver_sink_chain_done:
  THIS_READUNLOCK(this);
  return result;
}



GstPad*
_select_mprtcp_outpad(GstMprtpreceiver *this, GstBuffer *buf)
{
  GstPad *result;
  GstRTCPBuffer rtcp = {NULL, };
  GstRTCPHeader *header, *block_header;
  GstMPRTCPSubflowRiport *riport;
  GstMPRTCPSubflowBlock *block;
  GstMPRTCPSubflowInfo *info;
  guint8 info_type;
  guint16 subflow_id;
  GList *it;
  MPRTPRSubflow *subflow;
  guint8 block_riport_type;
  gboolean sr_riport = FALSE;


  if (G_UNLIKELY (!gst_rtcp_buffer_map(buf, GST_MAP_READ, &rtcp))){
    GST_WARNING_OBJECT(this, "The RTP packet is not readable");
    return NULL;
  }

  riport = (GstMPRTCPSubflowRiport*)gst_rtcp_get_first_header(&rtcp);
  for(block = gst_mprtcp_get_first_block(riport);
	  block != NULL;
	  block = gst_mprtcp_get_next_block(riport, block))
  {
    info = &block->info;
    gst_mprtcp_block_getdown(info, &info_type, NULL, &subflow_id);
    if(info_type != 0){
      continue;
    }
    block_header = &block->block_header;
    gst_rtcp_header_getdown(block_header, NULL, NULL, NULL,
    	&block_riport_type, NULL, NULL);
    if(block_riport_type == GST_RTCP_TYPE_SR){
      sr_riport = TRUE;
    }

  }

  result = sr_riport ? this->mprtcp_sr_srcpad : this->mprtcp_rr_srcpad;
  return result;
}


#undef MPRTCP_PACKET_TYPE_IDENTIFIER
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
