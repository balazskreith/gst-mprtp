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
 * SECTION:element-gstmprtcpsender
 *
 * The mprtcpsender element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v fakesrc ! mprtcpsender ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/gst.h>
#include "gstmprtcpsender.h"
#include "mprtpssubflow.h"
#include "gstmprtcpbuffer.h"

GST_DEBUG_CATEGORY_STATIC (gst_mprtcpsender_debug_category);
#define GST_CAT_DEFAULT gst_mprtcpsender_debug_category

#define SUBFLOW_WRITELOCK(mprtcp_ptr) (g_rw_lock_writer_lock(&mprtcp_ptr->rwmutex))
#define SUBFLOW_WRITEUNLOCK(mprtcp_ptr) (g_rw_lock_writer_unlock(&mprtcp_ptr->rwmutex))

#define SUBFLOW_READLOCK(mprtcp_ptr) (g_rw_lock_reader_lock(&mprtcp_ptr->rwmutex))
#define SUBFLOW_READUNLOCK(mprtcp_ptr) (g_rw_lock_reader_unlock(&mprtcp_ptr->rwmutex))


static void gst_mprtcpsender_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_mprtcpsender_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_mprtcpsender_dispose (GObject * object);
static void gst_mprtcpsender_finalize (GObject * object);

static GstPad *gst_mprtcpsender_request_new_pad(GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps* caps);
static void gst_mprtcpsender_release_pad (GstElement * element, GstPad * pad);
static GstStateChangeReturn
gst_mprtcpsender_change_state (GstElement * element, GstStateChange transition);
static gboolean gst_mprtcpsender_query (GstElement * element, GstQuery * query);

static GstPadLinkReturn gst_mprtcpsender_src_link (GstPad *pad, GstObject *parent, GstPad *peer);
static void gst_mprtcpsender_src_unlink (GstPad *pad, GstObject *parent);
static GstFlowReturn gst_mprtcpsender_rtcp_sink_chain (GstPad *pad,
  GstObject *parent, GstBuffer *buffer);


typedef struct{
  GstPad* outpad;
  guint16 id;
}Subflow;

enum
{
  PROP_0,
};

/* pad templates */

static GstStaticPadTemplate gst_mprtcpsender_src_template =
GST_STATIC_PAD_TEMPLATE ("src_%u",
	GST_PAD_SRC,
	GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
    );


static GstStaticPadTemplate gst_mprtcpsender_rtcp_src_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
    );

static GstStaticPadTemplate gst_mprtcpsender_rtcp_sink_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_sink",
	GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
    );

static GstStaticPadTemplate gst_mprtcpsender_mprtcp_sink_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_sink",
	GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
    );


/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstMprtcpsender, gst_mprtcpsender, GST_TYPE_ELEMENT,
  GST_DEBUG_CATEGORY_INIT (gst_mprtcpsender_debug_category, "mprtcpsender", 0,
  "debug category for mprtcpsender element"));

static void
gst_mprtcpsender_class_init (GstMprtcpsenderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtcpsender_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtcpsender_rtcp_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtcpsender_rtcp_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtcpsender_mprtcp_sink_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS(klass),
      "FIXME Long name", "Generic", "FIXME Description",
      "FIXME <fixme@example.com>");

  gobject_class->set_property = gst_mprtcpsender_set_property;
  gobject_class->get_property = gst_mprtcpsender_get_property;
  gobject_class->dispose = gst_mprtcpsender_dispose;
  gobject_class->finalize = gst_mprtcpsender_finalize;
  element_class->request_new_pad = GST_DEBUG_FUNCPTR (gst_mprtcpsender_request_new_pad);
  element_class->release_pad = GST_DEBUG_FUNCPTR (gst_mprtcpsender_release_pad);
  element_class->change_state = GST_DEBUG_FUNCPTR (gst_mprtcpsender_change_state);
  element_class->query = GST_DEBUG_FUNCPTR (gst_mprtcpsender_query);
}

static void
gst_mprtcpsender_init (GstMprtcpsender *mprtcpsender)
{

  mprtcpsender->rtcp_sinkpad = gst_pad_new_from_static_template (
    &gst_mprtcpsender_rtcp_sink_template, "rtcp_sink"
  );
  gst_pad_set_chain_function (mprtcpsender->rtcp_sinkpad,
      GST_DEBUG_FUNCPTR(gst_mprtcpsender_rtcp_sink_chain));
  gst_element_add_pad (GST_ELEMENT(mprtcpsender), mprtcpsender->rtcp_sinkpad);

  mprtcpsender->rtcp_srcpad = gst_pad_new_from_static_template (
    &gst_mprtcpsender_rtcp_src_template, "rtcp_src"
  );
  gst_element_add_pad (GST_ELEMENT(mprtcpsender), mprtcpsender->rtcp_srcpad);

  mprtcpsender->mprtcp_sinkpad = gst_pad_new_from_static_template (
    &gst_mprtcpsender_mprtcp_sink_template, "mprtcp_sink"
  );
  gst_pad_set_chain_function (mprtcpsender->mprtcp_sinkpad,
        GST_DEBUG_FUNCPTR(gst_mprtcpsender_rtcp_sink_chain));
  gst_element_add_pad (GST_ELEMENT(mprtcpsender), mprtcpsender->mprtcp_sinkpad);

  mprtcpsender->iterator = NULL;
  g_rw_lock_init(&mprtcpsender->rwmutex);
}

void
gst_mprtcpsender_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMprtcpsender *mprtcpsender = GST_MPRTCPSENDER (object);
  GST_DEBUG_OBJECT (mprtcpsender, "set_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_mprtcpsender_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstMprtcpsender *mprtcpsender = GST_MPRTCPSENDER (object);

  GST_DEBUG_OBJECT (mprtcpsender, "get_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_mprtcpsender_dispose (GObject * object)
{
  GstMprtcpsender *mprtcpsender = GST_MPRTCPSENDER (object);

  GST_DEBUG_OBJECT (mprtcpsender, "dispose");

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (gst_mprtcpsender_parent_class)->dispose (object);
}

void
gst_mprtcpsender_finalize (GObject * object)
{
  GstMprtcpsender *mprtcpsender = GST_MPRTCPSENDER (object);

  GST_DEBUG_OBJECT (mprtcpsender, "finalize");

  /* clean up object here */

  G_OBJECT_CLASS (gst_mprtcpsender_parent_class)->finalize (object);
}



static GstPad *
gst_mprtcpsender_request_new_pad (GstElement * element, GstPadTemplate * templ,
	    const gchar * name, const GstCaps* caps)
{

	GstPad *srcpad;
	GstMprtcpsender *mprtcps;
	GList *it;
	guint16 subflow_id;
	Subflow* subflow;

	mprtcps = GST_MPRTCPSENDER (element);
	GST_DEBUG_OBJECT (mprtcps, "requesting pad");

	sscanf(name, "src_%u", &subflow_id);

	SUBFLOW_WRITELOCK(mprtcps);
	subflow = (Subflow*) g_malloc0(sizeof(Subflow));

	srcpad = gst_pad_new_from_template (templ, name);

	gst_pad_set_link_function (srcpad,
	            GST_DEBUG_FUNCPTR(gst_mprtcpsender_src_link));
	gst_pad_set_unlink_function (srcpad,
	            GST_DEBUG_FUNCPTR(gst_mprtcpsender_src_unlink));
	subflow->id = subflow_id;
	subflow->outpad = srcpad;
	mprtcps->subflows = g_list_prepend(mprtcps->subflows, subflow);
	SUBFLOW_WRITEUNLOCK(mprtcps);

	gst_element_add_pad (GST_ELEMENT (mprtcps), srcpad);

	gst_pad_set_active (srcpad, TRUE);

	return srcpad;
}

static void
gst_mprtcpsender_release_pad (GstElement * element, GstPad * pad)
{

}

static GstStateChangeReturn
gst_mprtcpsender_change_state (GstElement * element, GstStateChange transition)
{
  GstMprtcpsender *mprtcpsender;
  GstStateChangeReturn ret;
  g_return_val_if_fail (GST_IS_MPRTCPSENDER (element), GST_STATE_CHANGE_FAILURE);
  mprtcpsender = GST_MPRTCPSENDER (element);

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

  ret = GST_ELEMENT_CLASS (gst_mprtcpsender_parent_class)->change_state (element, transition);

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
gst_mprtcpsender_query (GstElement * element, GstQuery * query)
{
  GstMprtcpsender *mprtcpsender = GST_MPRTCPSENDER (element);
  gboolean ret;

  GST_DEBUG_OBJECT (mprtcpsender, "query");
  switch (GST_QUERY_TYPE (query)) {
    default:
      ret = GST_ELEMENT_CLASS (gst_mprtcpsender_parent_class)->query (element, query);
      break;
  }

  return ret;
}


static GstPadLinkReturn
gst_mprtcpsender_src_link (GstPad *pad, GstObject *parent, GstPad *peer)
{
  GstMprtcpsender *mprtcpsender;
  mprtcpsender = GST_MPRTCPSENDER (parent);
  GST_DEBUG_OBJECT(mprtcpsender, "link");

  return GST_PAD_LINK_OK;
}

static void
gst_mprtcpsender_src_unlink (GstPad *pad, GstObject *parent)
{
  GstMprtcpsender *mprtcpsender;
  mprtcpsender = GST_MPRTCPSENDER (parent);
  GST_DEBUG_OBJECT(mprtcpsender, "unlink");

}

static GstFlowReturn
gst_mprtcpsender_rtcp_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buf)
{
  GstMprtcpsender *mprtcps;
  GstMapInfo info;
  GstPad *outpad = NULL;
  guint8 *data;
  guint16 subflow_id;
  GstFlowReturn result;
  Subflow *subflow;
  GList **it,*it2;
  GstRTCPBuffer rtcp = {NULL, };

  mprtcps = GST_MPRTCPSENDER (parent);
  GST_DEBUG_OBJECT(mprtcps, "RTCP/MPRTCP sink");
  SUBFLOW_READLOCK(mprtcps);

  //if rtcp_srcpad is linked all traffic flows there
  if (gst_pad_is_linked (mprtcps->rtcp_srcpad)) {
	  result = gst_pad_push(mprtcps->rtcp_srcpad, buf);
	  goto gst_mprtcpsender_sink_chain_done;
  }

  if(!gst_buffer_map(buf, &info, GST_MAP_READ)){
  	GST_WARNING("Buffer is not readable");
  	result = GST_FLOW_ERROR;
  	goto gst_mprtcpsender_sink_chain_done;
  }
  data = info.data + 1;
  subflow_id = g_ntohs(*((guint16*)(info.data +
		  8 /*RTCP Header*/ +
		  6 /*first block info until subflow id*/)));
  gst_buffer_unmap(buf, &info);
  if(*data == MPRTCP_PACKET_TYPE_IDENTIFIER){
	result = GST_FLOW_CUSTOM_ERROR;
    for(it2 = mprtcps->subflows; it2 != NULL; it2 = it2->next){
      subflow = it2->data;
      if(subflow->id == subflow_id){
    	result = gst_pad_push(subflow->outpad, buf);
      }
    }
    goto gst_mprtcpsender_sink_chain_done;
  }

  it = &mprtcps->iterator;
  if(*it == NULL || !gst_pad_is_linked(((Subflow*)(*it)->data)->outpad)){
	  *it = mprtcps->subflows;
  }
  if(*it == NULL){
    GST_ERROR_OBJECT(mprtcps, "No subflow");
    goto gst_mprtcpsender_sink_chain_done;
  }
  subflow = (Subflow*)(*it)->data;
  result = gst_pad_push(subflow->outpad, buf);
  *it = (*it)->next;

  //MPRTCP packet to send on appropiate subflow;

gst_mprtcpsender_sink_chain_done:
  SUBFLOW_READUNLOCK(mprtcps);
  return result;
}


#undef MPRTCP_PACKET_TYPE_IDENTIFIER
#undef SUBFLOW_WRITELOCK
#undef SUBFLOW_WRITEUNLOCK
#undef SUBFLOW_READLOCK
#undef SUBFLOW_READUNLOCK
