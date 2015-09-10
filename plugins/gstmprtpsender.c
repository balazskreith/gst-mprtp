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

#include <gst/gst.h>
#include <gst/gst.h>
#include "gstmprtpsender.h"
#include "mprtpssubflow.h"
#include "gstmprtcpbuffer.h"

GST_DEBUG_CATEGORY_STATIC (gst_mprtpsender_debug_category);
#define GST_CAT_DEFAULT gst_mprtpsender_debug_category

#define MPRTP_SENDER_DEFAULT_EXTENSION_HEADER_ID 3

#define THIS_WRITELOCK(mprtcp_ptr) (g_rw_lock_writer_lock(&mprtcp_ptr->rwmutex))
#define THIS_WRITEUNLOCK(mprtcp_ptr) (g_rw_lock_writer_unlock(&mprtcp_ptr->rwmutex))

#define THIS_READLOCK(mprtcp_ptr) (g_rw_lock_reader_lock(&mprtcp_ptr->rwmutex))
#define THIS_READUNLOCK(mprtcp_ptr) (g_rw_lock_reader_unlock(&mprtcp_ptr->rwmutex))

#define PACKET_IS_RTP(b) (b > 0x7f && b < 0xc0)
#define PACKET_IS_DTLS(b) (b > 0x13 && b < 0x40)
#define PACKET_IS_RTCP(b) (b > 192 && b < 223)



static void gst_mprtpsender_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_mprtpsender_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_mprtpsender_dispose (GObject * object);
static void gst_mprtpsender_finalize (GObject * object);

static GstPad *gst_mprtpsender_request_new_pad(GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps* caps);
static void gst_mprtpsender_release_pad (GstElement * element, GstPad * pad);
static GstStateChangeReturn
gst_mprtpsender_change_state (GstElement * element, GstStateChange transition);
//static void gst_mprtpsender_eventing_run (void *data);
static gboolean gst_mprtpsender_query (GstElement * element, GstQuery * query);

static GstPadLinkReturn gst_mprtpsender_src_link (GstPad *pad, GstObject *parent, GstPad *peer);
static void gst_mprtpsender_src_unlink (GstPad *pad, GstObject *parent);
static GstFlowReturn gst_mprtpsender_mprtcp_sink_chain (GstPad *pad,
  GstObject *parent, GstBuffer *buffer);

static GstFlowReturn gst_mprtpsender_mprtp_sink_chain (GstPad *pad,
  GstObject *parent, GstBuffer *buffer);

static GstFlowReturn
gst_mprtpsender_rtcp_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buf);

static gboolean gst_mprtpsender_acceptcaps_default (GstMprtpsender * mprtps,
	    GstPadDirection direction, GstCaps * caps);
static gboolean gst_mprtpsender_pad_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static GstCaps *gst_mprtpsender_query_caps (GstMprtpsender * mprtps, GstPad * pad,
    GstCaps * filter);
static GstCaps *gst_mprtpsender_transform_caps (GstMprtpsender * mprtps,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_mprtpsender_rtcp_sink_eventfunc (GstPad *pad,
	GstObject *parent, GstEvent * event);
static gboolean gst_mprtpsender_rtcp_src_eventfunc (GstPad *pad,
	GstObject *parent, GstEvent * event);
static gboolean gst_mprtpsender_setcaps (GstMprtpsender * mprtps,
	GstPad * pad, GstCaps * incaps);
static GstCaps *gst_mprtpsender_find_transform (GstMprtpsender * mprtps,
	GstPad * pad, GstCaps * caps);
static gboolean gst_mprtpsender_default_decide_allocation (
   GstMprtpsender * trans, GstQuery * query);
static gboolean gst_mprtpsender_do_bufferpool (GstMprtpsender * mprtps,
	GstCaps * outcaps);
static gboolean gst_mprtpsender_set_allocation (GstMprtpsender * mprtps,
    GstBufferPool * pool, GstAllocator * allocator,
    GstAllocationParams * params, GstQuery * query);



typedef struct{
  GstPad* outpad;
  guint8 state;
  guint16 id;
}Subflow;


static GstPad* _otherpad_for_rtcp(GstMprtpsender * this, GstPad * actual);

static gboolean
gst_mprtpsender_mprtp_sink_event_handler (GstPad * pad, GstObject * parent, GstEvent * event);

static gboolean
_select_subflow(GstMprtpsender *this, guint16 id, Subflow **result);

static GstPad* _otherpad_for_rtcp(GstMprtpsender * this, GstPad * actual);
static Subflow* _get_subflow(GstMprtpsender *this);

enum
{
  PROP_0,
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
    GST_STATIC_CAPS ("ANY")
    );


static GstStaticPadTemplate gst_mprtpsender_mprtcp_rr_sink_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_rr_sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
    );

static GstStaticPadTemplate gst_mprtpsender_mprtcp_sr_sink_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_sr_sink",
	GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
    );


static GstStaticPadTemplate gst_mprtpsender_rtcp_sink_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_sink",
	GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
    );


static GstStaticPadTemplate gst_mprtpsender_rtcp_src_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_src",
	GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
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
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpsender_rtcp_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpsender_rtcp_src_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS(klass),
      "FIXME Long name", "Generic", "FIXME Description",
      "FIXME <fixme@example.com>");

  gobject_class->set_property = gst_mprtpsender_set_property;
  gobject_class->get_property = gst_mprtpsender_get_property;
  gobject_class->dispose = gst_mprtpsender_dispose;
  gobject_class->finalize = gst_mprtpsender_finalize;
  element_class->request_new_pad = GST_DEBUG_FUNCPTR (gst_mprtpsender_request_new_pad);
  element_class->release_pad = GST_DEBUG_FUNCPTR (gst_mprtpsender_release_pad);
  element_class->change_state = GST_DEBUG_FUNCPTR (gst_mprtpsender_change_state);
  element_class->query = GST_DEBUG_FUNCPTR (gst_mprtpsender_query);
}



static void
gst_mprtpsender_init (GstMprtpsender *mprtpsender)
{

  mprtpsender->mprtcp_rr_sinkpad = gst_pad_new_from_static_template (
    &gst_mprtpsender_mprtcp_rr_sink_template, "mprtcp_rr_sink"
  );
  gst_pad_set_chain_function (mprtpsender->mprtcp_rr_sinkpad,
      GST_DEBUG_FUNCPTR(gst_mprtpsender_mprtcp_sink_chain));
  gst_element_add_pad (GST_ELEMENT(mprtpsender), mprtpsender->mprtcp_rr_sinkpad);

  mprtpsender->mprtcp_sr_sinkpad = gst_pad_new_from_static_template (
    &gst_mprtpsender_mprtcp_sr_sink_template, "mprtcp_sr_sink"
  );
  gst_pad_set_chain_function (mprtpsender->mprtcp_sr_sinkpad,
        GST_DEBUG_FUNCPTR(gst_mprtpsender_mprtcp_sink_chain));
  gst_element_add_pad (GST_ELEMENT(mprtpsender), mprtpsender->mprtcp_sr_sinkpad);

  mprtpsender->mprtp_sinkpad = gst_pad_new_from_static_template (
    &gst_mprtpsender_mprtp_sink_template, "mprtp_sink");
  gst_pad_set_chain_function (mprtpsender->mprtp_sinkpad,
        GST_DEBUG_FUNCPTR(gst_mprtpsender_mprtp_sink_chain));
  gst_pad_set_event_function(mprtpsender->mprtp_sinkpad,
		GST_DEBUG_FUNCPTR(gst_mprtpsender_mprtp_sink_event_handler));

  gst_element_add_pad (GST_ELEMENT(mprtpsender), mprtpsender->mprtp_sinkpad);

  mprtpsender->rtcp_srcpad = gst_pad_new_from_static_template (
        &gst_mprtpsender_rtcp_src_template, "rtcp_src");

  gst_pad_set_event_function(mprtpsender->rtcp_srcpad,
    	    		  GST_DEBUG_FUNCPTR(gst_mprtpsender_rtcp_src_eventfunc));
  gst_pad_set_query_function (mprtpsender->rtcp_srcpad,
    	      GST_DEBUG_FUNCPTR (gst_mprtpsender_pad_query));

  gst_element_add_pad (GST_ELEMENT(mprtpsender), mprtpsender->rtcp_srcpad);

  mprtpsender->rtcp_sinkpad = gst_pad_new_from_static_template (
          &gst_mprtpsender_rtcp_src_template, "rtcp_sinkpad");
  gst_pad_set_chain_function (mprtpsender->mprtcp_sr_sinkpad,
          GST_DEBUG_FUNCPTR(gst_mprtpsender_rtcp_sink_chain));

  gst_pad_set_event_function(mprtpsender->rtcp_sinkpad,
  		    		  GST_DEBUG_FUNCPTR(gst_mprtpsender_rtcp_sink_eventfunc));
  	gst_pad_set_query_function(mprtpsender->rtcp_sinkpad,
  		    		  GST_DEBUG_FUNCPTR(gst_mprtpsender_pad_query));

  gst_element_add_pad (GST_ELEMENT(mprtpsender), mprtpsender->rtcp_sinkpad);

  mprtpsender->iterator = NULL;
  mprtpsender->ext_header_id = MPRTP_SENDER_DEFAULT_EXTENSION_HEADER_ID;
  //mprtpsender->events = g_queue_new();
  g_rw_lock_init(&mprtpsender->rwmutex);
}

void
gst_mprtpsender_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMprtpsender *mprtpsender = GST_MPRTPSENDER (object);
  GST_DEBUG_OBJECT (mprtpsender, "set_property");

  switch (property_id) {
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
	    const gchar * name, const GstCaps* caps)
{

	GstPad *srcpad;
	GstMprtpsender *mprtcps;
	GList *it;
	guint16 subflow_id;
	Subflow* subflow;

	mprtcps = GST_MPRTPSENDER (element);
	GST_DEBUG_OBJECT (mprtcps, "requesting pad");

	sscanf(name, "src_%u", &subflow_id);
	THIS_WRITELOCK(mprtcps);
	subflow = (Subflow*) g_malloc0(sizeof(Subflow));

	srcpad = gst_pad_new_from_template (templ, name);

	gst_pad_set_link_function (srcpad,
	            GST_DEBUG_FUNCPTR(gst_mprtpsender_src_link));
	gst_pad_set_unlink_function (srcpad,
	            GST_DEBUG_FUNCPTR(gst_mprtpsender_src_unlink));
	subflow->id = subflow_id;
	subflow->outpad = srcpad;
	subflow->state = 0;
	mprtcps->subflows = g_list_prepend(mprtcps->subflows, subflow);
	THIS_WRITEUNLOCK(mprtcps);

	gst_element_add_pad (GST_ELEMENT (mprtcps), srcpad);

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
  GstMprtpsender *mprtpsender;
  GstStateChangeReturn ret;
  g_return_val_if_fail (GST_IS_MPRTPSENDER (element), GST_STATE_CHANGE_FAILURE);
  mprtpsender = GST_MPRTPSENDER (element);

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

  ret = GST_ELEMENT_CLASS (gst_mprtpsender_parent_class)->change_state (element, transition);

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
      ret = GST_ELEMENT_CLASS (gst_mprtpsender_parent_class)->query (element, query);
      break;
  }

  return ret;
}


static GstPadLinkReturn
gst_mprtpsender_src_link (GstPad *pad, GstObject *parent, GstPad *peer)
{
  GstMprtpsender *mprtpsender;
  GList *it;
  Subflow *subflow;
  GstPadLinkReturn result = GST_PAD_LINK_OK;

  mprtpsender = GST_MPRTPSENDER (parent);
  GST_DEBUG_OBJECT(mprtpsender, "link");
  THIS_READLOCK(mprtpsender);

  for(subflow = NULL, it = mprtpsender->subflows; it != NULL; it = it->next){
    subflow = it->data;
    if(subflow->outpad == pad){
      break;
    }
    subflow = NULL;
  }
  if(subflow == NULL){
    goto gst_mprtpsender_src_link_done;
  }
  subflow->state = 0;

gst_mprtpsender_src_link_done:
  THIS_READUNLOCK(mprtpsender);
  return result;
}

static void
gst_mprtpsender_src_unlink (GstPad *pad, GstObject *parent)
{
  GstMprtpsender *mprtpsender;
  GList *it;
  Subflow *subflow;
  GstPadLinkReturn result = GST_PAD_LINK_OK;

  mprtpsender = GST_MPRTPSENDER (parent);
  GST_DEBUG_OBJECT(mprtpsender, "unlink");
  THIS_WRITELOCK(mprtpsender);

    for(subflow = NULL, it = mprtpsender->subflows; it != NULL; it = it->next){
      subflow = it->data;
      if(subflow->outpad == pad){
        break;
      }
      subflow = NULL;
    }
    if(subflow == NULL){
      goto gst_mprtpsender_src_unlink_done;
    }

    mprtpsender->subflows = g_list_remove(mprtpsender->subflows, subflow);
gst_mprtpsender_src_unlink_done:
    THIS_WRITEUNLOCK(mprtpsender);
}

static GstFlowReturn
gst_mprtpsender_mprtcp_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buf)
{
  GstMprtpsender *mprtcps;
  GstMapInfo info;
  GstPad *outpad = NULL;
  guint8 *data;
  guint16 subflow_id;
  GstFlowReturn result;
  Subflow *subflow;
  GList **it,*it2;
  GstRTCPBuffer rtcp = {NULL, };

  mprtcps = GST_MPRTPSENDER (parent);
  GST_DEBUG_OBJECT(mprtcps, "RTCP/MPRTCP sink");
  THIS_READLOCK(mprtcps);

  if(!gst_buffer_map(buf, &info, GST_MAP_READ)){
  	GST_WARNING("Buffer is not readable");
  	result = GST_FLOW_ERROR;
  	goto gst_mprtpsender_sink_chain_done;
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
    	break;
      }
    }
    goto gst_mprtpsender_sink_chain_done;
  }

gst_mprtpsender_sink_chain_done:
  THIS_READUNLOCK(mprtcps);
  return result;
}



static GstFlowReturn
gst_mprtpsender_mprtp_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buf)
{
  GstMprtpsender *this;
  GstFlowReturn result;
  Subflow *subflow;
  guint8 first_byte, second_byte;
  GstRTPBuffer rtp = GST_RTCP_BUFFER_INIT;
  MPRTPSubflowHeaderExtension *subflow_infos = NULL;
  guint size;
  gpointer pointer;

  this = GST_MPRTPSENDER (parent);
  GST_DEBUG_OBJECT(this, "RTP/MPRTP/OTHER sink");
  if (gst_buffer_extract (buf, 0, &first_byte, 1) != 1 ||
	  gst_buffer_extract (buf, 1, &second_byte, 1) != 1) {
    GST_WARNING_OBJECT (this, "could not extract first byte from buffer");
    gst_buffer_unref (buf);
    return GST_FLOW_OK;
  }

  THIS_READLOCK(this);
  if(PACKET_IS_DTLS(first_byte)){
	  subflow = _get_subflow(this);
	  if(subflow == NULL){
		gst_buffer_unref(buf);
	    result = GST_FLOW_CUSTOM_ERROR;
	    goto gst_mprtpsender_sink_chain_done;
	  }
	  result = gst_pad_push(subflow->outpad, buf);
	  goto gst_mprtpsender_sink_chain_done;
  }

  if(PACKET_IS_RTCP(second_byte)){
    subflow = _otherpad_for_rtcp(this, this->rtcp_sinkpad);
    if(subflow == NULL){
      gst_buffer_unref(buf);
	  result = GST_FLOW_CUSTOM_ERROR;
	  goto gst_mprtpsender_sink_chain_done;
	}
   	result = gst_pad_push(subflow->outpad, buf);
	goto gst_mprtpsender_sink_chain_done;
  }

  if(!PACKET_IS_RTP(first_byte)){
	 GST_WARNING_OBJECT(this, "Not recognized RTP packet");
	 gst_buffer_unref(buf);
	 result = GST_FLOW_OK;
	 goto gst_mprtpsender_sink_chain_done;
  }

  if (G_UNLIKELY (!gst_rtp_buffer_map(buf, GST_MAP_READ, &rtp))){
  	GST_WARNING_OBJECT(this, "The RTP packet is not readable");
  	result = GST_FLOW_OK;
  	goto gst_mprtpsender_sink_chain_done;
  }

  if(!gst_rtp_buffer_get_extension(&rtp)){
  	//Backward compatibility
  	GST_WARNING_OBJECT(this, "The received buffer extension bit is 0 thus it is not an MPRTP packet.");
  	result = gst_pad_push(subflow->outpad, buf);
  	gst_rtp_buffer_unmap(&rtp);
  	result = GST_FLOW_OK;
    goto gst_mprtpsender_sink_chain_done;
  }

  if(!gst_rtp_buffer_get_extension_onebyte_header(&rtp, this->ext_header_id, 0, &pointer, &size)){
  	GST_WARNING_OBJECT(this, "The received buffer extension is not processable");
  	gst_rtp_buffer_unmap(&rtp);
  	result = GST_FLOW_OK;
	goto gst_mprtpsender_sink_chain_done;
  }

  subflow_infos = (MPRTPSubflowHeaderExtension*) pointer;
  if(_select_subflow(this, subflow_infos->id, &subflow) == FALSE){
	GST_ERROR_OBJECT(this, "The subflow lookup was not successful");
	gst_rtp_buffer_unmap(&rtp);
	subflow = _get_subflow(this);
	if(subflow == NULL){
	  gst_buffer_unref(buf);
	  result = GST_FLOW_CUSTOM_ERROR;
	  goto gst_mprtpsender_sink_chain_done;
	}

	result = gst_pad_push(subflow->outpad, buf);
	goto gst_mprtpsender_sink_chain_done;
  }

  gst_rtp_buffer_unmap(&rtp);
  result = gst_pad_push(subflow->outpad, buf);

gst_mprtpsender_sink_chain_done:
  THIS_READUNLOCK(this);
  return result;
}

Subflow* _get_subflow(GstMprtpsender *this)
{
  GList *it;
  Subflow *result = NULL, *subflow;
  guint max_state = 0;
  for(it = this->subflows; it != NULL; it = it->next){
	subflow = it->data;
	if(result == NULL){
	  result = subflow;
	}
	if(subflow->state > max_state){
	  result = subflow;
	  max_state = subflow->state;
	}
  }
  return result;
}

static GstPad* _otherpad_for_rtcp(GstMprtpsender * this, GstPad * actual)
{
  GstPad* result;
  Subflow *subflow;
  if(actual != this->rtcp_sinkpad){
    return this->rtcp_sinkpad;
  }
  if(gst_pad_is_linked(this->rtcp_srcpad)){
    return this->rtcp_srcpad;
  }
  subflow = _get_subflow(this);
  //subflow = _get_actual_iterated_subflow(this);
  if(subflow == NULL){
    return NULL;
  }
  return subflow->outpad;
}


static GstFlowReturn
gst_mprtpsender_rtcp_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buf)
{
  GstMprtpsender *this;
  GstFlowReturn result;
  Subflow *subflow;
  guint8 second_byte;
  GstRTPBuffer rtp = GST_RTCP_BUFFER_INIT;
  MPRTPSubflowHeaderExtension *subflow_infos = NULL;
  guint size;
  gpointer pointer;

  this = GST_MPRTPSENDER (parent);
  GST_DEBUG_OBJECT(this, "RTCP");


  if (gst_buffer_extract (buf, 1, &second_byte, 1) != 1) {
      GST_WARNING_OBJECT (this, "could not extract first byte from buffer");
      gst_buffer_unref (buf);
      return GST_FLOW_OK;
    }

  THIS_READLOCK(this);
  if(!PACKET_IS_RTCP(second_byte)){
	 GST_WARNING_OBJECT(this, "Not recognized RTCP packet");
	 result = GST_FLOW_OK;
	 gst_buffer_unref (buf);
	 goto gst_mprtpsender_rtcp_sink_chain_done;
  }

  result = gst_pad_push(_otherpad_for_rtcp(this, this->rtcp_sinkpad), buf);
gst_mprtpsender_rtcp_sink_chain_done:
  THIS_READUNLOCK(this);
  return result;
}

gboolean
gst_mprtpsender_mprtp_sink_event_handler (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstMprtpsender *this;
  const GstStructure *s;
  gboolean result = TRUE;
  GList *it;
  Subflow *subflow;
  guint subflow_id, state_value;

  this = GST_MPRTPSENDER (parent);
  THIS_WRITELOCK(this);

  switch(GST_EVENT_TYPE(event))
  {
    gst_pad_event_default(pad, parent, event);
    g_print("gst_mprtpsender_mprtp_sink_event_handler: %s\n", GST_EVENT_TYPE_NAME(event));
    case GST_EVENT_CUSTOM_DOWNSTREAM:
      result = TRUE;
      s = gst_event_get_structure (event);
      if (!s || !gst_structure_has_name(s, "GstMpRtpSubflowState")){
        break;
      }
      gst_structure_get_uint(s, "state", &state_value);
	  gst_structure_get_uint(s, "subflow_id", &subflow_id);

	  for(subflow = NULL, it = this->subflows; it != NULL; it = it->next){
	    subflow = it->data;
		if(subflow->id == subflow_id){
		  break;
		}
		subflow = NULL;
      }
      if(subflow == NULL){
        break;
      }
      subflow->state = state_value;
	break;
    case GST_EVENT_SEGMENT:
    case GST_EVENT_CAPS:
      for(subflow = NULL, it = this->subflows; it != NULL; it = it->next){
	    subflow = it->data;
	    result &= gst_pad_push_event (subflow->outpad, gst_event_copy(event));
      }
      result &= gst_pad_event_default(pad, parent, event);
      break;
    default:
    	result = gst_pad_event_default(pad, parent, event);
  }

  THIS_WRITEUNLOCK(this);
  return result;
}


gboolean _select_subflow(GstMprtpsender *this, guint16 id, Subflow **result)
{
  GList *it;
  Subflow *subflow;

  for(it = this->subflows; it != NULL; it = it->next){
	subflow = it->data;
    if(subflow->id == id){
      *result = subflow;
      return TRUE;
    }
  }
  *result = NULL;
  return FALSE;
}


//Rohadt capsok és eventek.


/* given @caps on the src or sink pad (given by @direction)
 * calculate the possible caps on the other pad.
 *
 * Returns new caps, unref after usage.
 */
static GstCaps *
gst_mprtpsender_transform_caps (GstMprtpsender * mprtps,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *ret = NULL;

  if (caps == NULL)
    return NULL;

  GST_DEBUG_OBJECT (mprtps, "transform caps (direction = %d)", direction);
  GST_LOG_OBJECT (mprtps, "from: %" GST_PTR_FORMAT, caps);
  if (filter) {
    ret = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
  } else {
    ret = gst_caps_ref (caps);
  }
  GST_LOG_OBJECT (mprtps, "  to: %" GST_PTR_FORMAT, ret);

#ifndef G_DISABLE_ASSERT
    if (filter) {
      if (!gst_caps_is_subset (ret, filter)) {
        GstCaps *intersection;

        GST_ERROR_OBJECT (mprtps,
            "transform_caps returned caps %" GST_PTR_FORMAT
            " which are not a real subset of the filter caps %"
            GST_PTR_FORMAT, ret, filter);
        g_warning ("%s: transform_caps returned caps which are not a real "
            "subset of the filter caps", GST_ELEMENT_NAME (mprtps));

        intersection =
            gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (ret);
        ret = intersection;
      }
    }
#endif

  GST_DEBUG_OBJECT (mprtps, "to: %" GST_PTR_FORMAT, ret);
  return ret;
}


/* get the caps that can be handled by @pad. We perform:
 *
 *  - take the caps of peer of otherpad,
 *  - filter against the padtemplate of otherpad,
 *  - calculate all transforms of remaining caps
 *  - filter against template of @pad
 *
 * If there is no peer, we simply return the caps of the padtemplate of pad.
 */
static GstCaps *
gst_mprtpsender_query_caps (GstMprtpsender * mprtps, GstPad * pad,
    GstCaps * filter)
{
  GstPad *otherpad;
  GstCaps *peercaps = NULL, *caps, *temp, *peerfilter = NULL;
  GstCaps *templ, *otempl;

  //otherpad = (pad == mprtps->rtcp_srcpad) ? mprtps->rtcp_sinkpad : mprtps->rtcp_srcpad;
  otherpad = _otherpad_for_rtcp(mprtps, pad);
  GST_DEBUG_OBJECT(mprtps,"A otherpad: %s", gst_pad_get_name(otherpad));
  templ = gst_pad_get_pad_template_caps (pad);
  otempl = gst_pad_get_pad_template_caps (otherpad);

  /* first prepare the filter to be send onwards. We need to filter and
   * transform it to valid caps for the otherpad. */
  if (filter) {
    GST_DEBUG_OBJECT (pad, "filter caps  %" GST_PTR_FORMAT, filter);

    /* filtered against our padtemplate of this pad */
    GST_DEBUG_OBJECT (pad, "our template  %" GST_PTR_FORMAT, templ);
    temp = gst_caps_intersect_full (filter, templ, GST_CAPS_INTERSECT_FIRST);
    GST_DEBUG_OBJECT (pad, "intersected %" GST_PTR_FORMAT, temp);

    /* then see what we can transform this to */
    peerfilter = gst_mprtpsender_transform_caps (mprtps,
        GST_PAD_DIRECTION (pad), temp, NULL);
    GST_DEBUG_OBJECT (pad, "transformed  %" GST_PTR_FORMAT, peerfilter);
    gst_caps_unref (temp);

    if (!gst_caps_is_empty (peerfilter)) {
      /* and filter against the template of the other pad */
      GST_DEBUG_OBJECT (pad, "our template  %" GST_PTR_FORMAT, otempl);
      /* We keep the caps sorted like the returned caps */
      temp =
          gst_caps_intersect_full (peerfilter, otempl,
          GST_CAPS_INTERSECT_FIRST);
      GST_DEBUG_OBJECT (pad, "intersected %" GST_PTR_FORMAT, temp);
      gst_caps_unref (peerfilter);
      peerfilter = temp;
    }
  }

  GST_DEBUG_OBJECT (pad, "peer filter caps %" GST_PTR_FORMAT, peerfilter);

  if (peerfilter && gst_caps_is_empty (peerfilter)) {
    GST_DEBUG_OBJECT (pad, "peer filter caps are empty");
    caps = peerfilter;
    peerfilter = NULL;
    goto done;
  }

  /* query the peer with the transformed filter */
  peercaps = gst_pad_peer_query_caps (otherpad, peerfilter);

  if (peerfilter)
    gst_caps_unref (peerfilter);

  if (peercaps) {
    GST_DEBUG_OBJECT (pad, "peer caps  %" GST_PTR_FORMAT, peercaps);

    /* filtered against our padtemplate on the other side */
    GST_DEBUG_OBJECT (pad, "our template  %" GST_PTR_FORMAT, otempl);
    temp = gst_caps_intersect_full (peercaps, otempl, GST_CAPS_INTERSECT_FIRST);
    GST_DEBUG_OBJECT (pad, "intersected %" GST_PTR_FORMAT, temp);
  } else {
    temp = gst_caps_ref (otempl);
  }

  /* then see what we can transform this to */
  caps = gst_mprtpsender_transform_caps (mprtps,
      GST_PAD_DIRECTION (otherpad), temp, filter);
  GST_DEBUG_OBJECT (pad, "transformed  %" GST_PTR_FORMAT, caps);
  gst_caps_unref (temp);
  if (caps == NULL || gst_caps_is_empty (caps))
    goto done;

  if (peercaps) {
    /* and filter against the template of this pad */
    GST_DEBUG_OBJECT (pad, "our template  %" GST_PTR_FORMAT, templ);
    /* We keep the caps sorted like the returned caps */
    temp = gst_caps_intersect_full (caps, templ, GST_CAPS_INTERSECT_FIRST);
    GST_DEBUG_OBJECT (pad, "intersected %" GST_PTR_FORMAT, temp);
    gst_caps_unref (caps);
    caps = temp;
  } else {
    gst_caps_unref (caps);
    /* no peer or the peer can do anything, our padtemplate is enough then */
    if (filter) {
      caps = gst_caps_intersect_full (filter, templ, GST_CAPS_INTERSECT_FIRST);
    } else {
      caps = gst_caps_ref (templ);
    }
  }

done:
  GST_DEBUG_OBJECT (mprtps, "returning  %" GST_PTR_FORMAT, caps);

  if (peercaps)
    gst_caps_unref (peercaps);

  gst_caps_unref (templ);
  gst_caps_unref (otempl);

  return caps;
}


static gboolean
gst_mprtpsender_acceptcaps_default (GstMprtpsender * mprtps,
    GstPadDirection direction, GstCaps * caps)
{
  gboolean ret = TRUE;
  {
    GstCaps *allowed;
    GST_DEBUG_OBJECT (mprtps, "accept caps %" GST_PTR_FORMAT, caps);

    /* get all the formats we can handle on this pad */
    if (direction == GST_PAD_SRC)
      if(gst_pad_is_linked(mprtps->rtcp_srcpad))
        allowed = gst_pad_query_caps (mprtps->rtcp_srcpad, caps);
      else
    	allowed = _otherpad_for_rtcp(mprtps, mprtps->rtcp_sinkpad);
    else
      allowed = gst_pad_query_caps (mprtps->rtcp_sinkpad, caps);

    if (!allowed) {
      GST_DEBUG_OBJECT (mprtps, "gst_pad_query_caps() failed");
      goto no_transform_possible;
    }

    GST_DEBUG_OBJECT (mprtps, "allowed caps %" GST_PTR_FORMAT, allowed);

    /* intersect with the requested format */
    ret = gst_caps_is_subset (caps, allowed);
    gst_caps_unref (allowed);

    if (!ret)
      goto no_transform_possible;
  }

done:

  return ret;

  /* ERRORS */
no_transform_possible:
  {
    GST_DEBUG_OBJECT (mprtps,
        "transform could not transform %" GST_PTR_FORMAT
        " in anything we support", caps);
    ret = FALSE;
    goto done;
  }
}


static gboolean
gst_mprtpsender_pad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{

  GstMprtpsender * mprtps = GST_MPRTPSENDER (parent);
  GstPadDirection direction = GST_PAD_DIRECTION(pad);
  gboolean ret = FALSE;
  GstPad *otherpad = NULL;

  otherpad = _otherpad_for_rtcp(mprtps, pad);
  //otherpad = (pad == mprtps->rtcp_srcpad) ? mprtps->rtcp_sinkpad : mprtps->rtcp_srcpad;
  GST_DEBUG_OBJECT(mprtps,"A otherpad: %s", gst_pad_get_name(otherpad));
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps;
      gst_query_parse_accept_caps (query, &caps);
      ret = gst_mprtpsender_acceptcaps_default(mprtps, direction, caps);
      gst_query_set_accept_caps_result (query, ret);
	  /* return TRUE, we answered the query */
	  ret = TRUE;
      break;
    }
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_mprtpsender_query_caps (mprtps, pad, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      ret = TRUE;
      break;
    }
    default:
      ret = gst_pad_peer_query (otherpad, query);
      break;
  }

done:
  return ret;
}


static gboolean
gst_mprtpsender_rtcp_sink_eventfunc (GstPad *pad, GstObject *parent,
	GstEvent * event)
{
  GstMprtpsender * mprtps = GST_MPRTPSENDER(parent);
  gboolean ret = TRUE, forward = TRUE;
  GstPad *otherpad = NULL;
  otherpad = _otherpad_for_rtcp(mprtps, pad);
  //if(!gst_pad_is_linked(mprtps->rtcp_srcpad)){
  	//if(mprtps->subflows != NULL){
      //  otherpad = ((MPRTPSSubflow*)mprtps->subflows->data)->outpad;
  	//}
  //}else{
	//otherpad = mprtps->rtcp_srcpad;
  //}
  GST_DEBUG_OBJECT(mprtps,"otherpad: %s", gst_pad_get_name(otherpad));
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      break;
    case GST_EVENT_FLUSH_STOP:
      /* we need new segment info after the flush. */
      //mprtps->have_segment = FALSE;
      //gst_segment_init (&mprtps->segment, GST_FORMAT_UNDEFINED);
      //priv->position_out = GST_CLOCK_TIME_NONE;
      break;
    case GST_EVENT_EOS:
      break;
    case GST_EVENT_TAG:
      break;
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      /* clear any pending reconfigure flag */
      gst_pad_check_reconfigure (otherpad);
      ret = gst_mprtpsender_setcaps (mprtps, pad, caps);

      forward = FALSE;
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      //g_print("GST_EVENT_SEGMENT\n");
      //gst_event_copy_segment (event, &mprtps->segment);
      //mprtps->have_segment = TRUE;

      //GST_DEBUG_OBJECT (mprtps, "received SEGMENT %" GST_SEGMENT_FORMAT,
      //    &mprtps->segment);
      break;
    }
    default:
      break;
  }

  if (ret && forward)
    ret = gst_pad_push_event (otherpad, event);
  else
    gst_event_unref (event);

  return ret;
}

static gboolean
gst_mprtpsender_rtcp_src_eventfunc (GstPad *pad, GstObject *parent,
		GstEvent * event)
{
  GstMprtpsender *mprtps = GST_MPRTPSENDER(parent);
  gboolean ret;

  GST_DEBUG_OBJECT (mprtps, "handling event %p %" GST_PTR_FORMAT, event, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      break;
    case GST_EVENT_NAVIGATION:
      break;
    case GST_EVENT_QOS:
    {
      //g_print("GST_EVENT_QOS\n");
      //gdouble proportion;
      //GstClockTimeDiff diff;
      //GstClockTime timestamp;

      //gst_event_parse_qos (event, NULL, &proportion, &diff, &timestamp);
      //gst_base_transform_update_qos (mprtps, proportion, diff, timestamp);
      break;
    }
    default:
      break;
  }

  ret = gst_pad_push_event (mprtps->rtcp_sinkpad, event);

  return ret;
}

static gboolean
gst_mprtpsender_setcaps (GstMprtpsender * mprtps, GstPad * pad,
    GstCaps * incaps)
{
  GstCaps *outcaps, *prev_incaps = NULL, *prev_outcaps = NULL;
  gboolean ret = TRUE;
  GstPad *otherpad = NULL;
  otherpad = _otherpad_for_rtcp(mprtps, pad);
  //if(!gst_pad_is_linked(mprtps->rtcp_srcpad)){
    //if(mprtps->subflows != NULL){
      //  otherpad = ((MPRTPSSubflow*)mprtps->subflows->data)->outpad;
    //}
  //}else{
  	//otherpad = mprtps->rtcp_srcpad;
  //}
  GST_DEBUG_OBJECT(mprtps,"Otherpad: %s", gst_pad_get_name(otherpad));
  GST_DEBUG_OBJECT (pad, "have new caps %p %" GST_PTR_FORMAT, incaps, incaps);

  /* find best possible caps for the other pad */
  outcaps = gst_mprtpsender_find_transform (mprtps, pad, incaps);
  if (!outcaps || gst_caps_is_empty (outcaps))
    goto no_transform_possible;

  /* configure the element now */

  /* if we have the same caps, we can optimize and reuse the input caps */
  if (gst_caps_is_equal (incaps, outcaps)) {
    GST_INFO_OBJECT (mprtps, "reuse caps");
    gst_caps_unref (outcaps);
    outcaps = gst_caps_ref (incaps);
  }

  prev_incaps = gst_pad_get_current_caps (otherpad);
  prev_outcaps = gst_pad_get_current_caps (otherpad);
  if (prev_incaps && prev_outcaps && gst_caps_is_equal (prev_incaps, incaps)
      && gst_caps_is_equal (prev_outcaps, outcaps)) {
    GST_DEBUG_OBJECT (mprtps,
        "New caps equal to old ones: %" GST_PTR_FORMAT " -> %" GST_PTR_FORMAT,
        incaps, outcaps);
    ret = TRUE;
  } else {
    if (!prev_outcaps || !gst_caps_is_equal (outcaps, prev_outcaps))
      /* let downstream know about our caps */
      ret = gst_pad_set_caps (otherpad, outcaps);
  }

  if (ret) {
    /* try to get a pool when needed */
    ret = gst_mprtpsender_do_bufferpool (mprtps, outcaps);
  }

done:
  if (outcaps)
    gst_caps_unref (outcaps);
  if (prev_incaps)
    gst_caps_unref (prev_incaps);
  if (prev_outcaps)
    gst_caps_unref (prev_outcaps);

  return ret;

  /* ERRORS */
no_transform_possible:
  {
    GST_WARNING_OBJECT (mprtps,
        "transform could not transform %" GST_PTR_FORMAT
        " in anything we support", incaps);
    ret = FALSE;
    goto done;
  }
failed_configure:
  {
    GST_WARNING_OBJECT (mprtps, "FAILED to configure incaps %" GST_PTR_FORMAT
        " and outcaps %" GST_PTR_FORMAT, incaps, outcaps);
    ret = FALSE;
    goto done;
  }
}

/* given a fixed @caps on @pad, create the best possible caps for the
 * other pad.
 * @caps must be fixed when calling this function.
 *
 * This function calls the transform caps vmethod of the basetransform to figure
 * out the possible target formats. It then tries to select the best format from
 * this list by:
 *
 * - attempt passthrough if the target caps is a superset of the input caps
 * - fixating by using peer caps
 * - fixating with transform fixate function
 * - fixating with pad fixate functions.
 *
 * this function returns a caps that can be transformed into and is accepted by
 * the peer element.
 */
static GstCaps *
gst_mprtpsender_find_transform (GstMprtpsender * mprtps, GstPad * pad,
    GstCaps * caps)
{
  //GstBaseTransformClass *klass;
  GstPad *otherpad, *otherpeer;
  GstCaps *othercaps;
  gboolean is_fixed;

  /* caps must be fixed here, this is a programming error if it's not */
  g_return_val_if_fail (gst_caps_is_fixed (caps), NULL);

  //klass = GST_BASE_TRANSFORM_GET_CLASS (mprtps);


  otherpad = _otherpad_for_rtcp(mprtps, pad);
  GST_DEBUG_OBJECT(mprtps,"Otherpad: %s", gst_pad_get_name(otherpad));
  otherpeer = gst_pad_get_peer (otherpad);

  othercaps = gst_mprtpsender_transform_caps (mprtps,
      GST_PAD_DIRECTION (pad), caps, NULL);

  /* The caps we can actually output is the intersection of the transformed
   * caps with the pad template for the pad */
  if (othercaps && !gst_caps_is_empty (othercaps)) {
    GstCaps *intersect, *templ_caps;

    templ_caps = gst_pad_get_pad_template_caps (otherpad);
    GST_DEBUG_OBJECT (mprtps,
        "intersecting against padtemplate %" GST_PTR_FORMAT, templ_caps);

    intersect =
        gst_caps_intersect_full (othercaps, templ_caps,
        GST_CAPS_INTERSECT_FIRST);

    gst_caps_unref (othercaps);
    gst_caps_unref (templ_caps);
    othercaps = intersect;
  }

  /* check if transform is empty */
  if (!othercaps || gst_caps_is_empty (othercaps))
    goto no_transform;

  /* if the othercaps are not fixed, we need to fixate them, first attempt
   * is by attempting passthrough if the othercaps are a superset of caps. */
  /* FIXME. maybe the caps is not fixed because it has multiple structures of
   * fixed caps */
  is_fixed = gst_caps_is_fixed (othercaps);
  if (!is_fixed) {
    GST_DEBUG_OBJECT (mprtps,
        "transform returned non fixed  %" GST_PTR_FORMAT, othercaps);

    /* Now let's see what the peer suggests based on our transformed caps */
    if (otherpeer) {
      GstCaps *peercaps, *intersection, *templ_caps;

      GST_DEBUG_OBJECT (mprtps,
          "Checking peer caps with filter %" GST_PTR_FORMAT, othercaps);

      peercaps = gst_pad_query_caps (otherpeer, othercaps);
      GST_DEBUG_OBJECT (mprtps, "Resulted in %" GST_PTR_FORMAT, peercaps);
      if (!gst_caps_is_empty (peercaps)) {
        templ_caps = gst_pad_get_pad_template_caps (otherpad);

        GST_DEBUG_OBJECT (mprtps,
            "Intersecting with template caps %" GST_PTR_FORMAT, templ_caps);

        intersection =
            gst_caps_intersect_full (peercaps, templ_caps,
            GST_CAPS_INTERSECT_FIRST);
        GST_DEBUG_OBJECT (mprtps, "Intersection: %" GST_PTR_FORMAT,
            intersection);
        gst_caps_unref (peercaps);
        gst_caps_unref (templ_caps);
        peercaps = intersection;

        GST_DEBUG_OBJECT (mprtps,
            "Intersecting with transformed caps %" GST_PTR_FORMAT, othercaps);
        intersection =
            gst_caps_intersect_full (peercaps, othercaps,
            GST_CAPS_INTERSECT_FIRST);
        GST_DEBUG_OBJECT (mprtps, "Intersection: %" GST_PTR_FORMAT,
            intersection);
        gst_caps_unref (peercaps);
        gst_caps_unref (othercaps);
        othercaps = intersection;
      } else {
        gst_caps_unref (othercaps);
        othercaps = peercaps;
      }

      is_fixed = gst_caps_is_fixed (othercaps);
    } else {
      GST_DEBUG_OBJECT (mprtps, "no peer, doing passthrough");
      gst_caps_unref (othercaps);
      othercaps = gst_caps_ref (caps);
      is_fixed = TRUE;
    }
  }
  if (gst_caps_is_empty (othercaps))
    goto no_transform_possible;

  GST_DEBUG ("have %sfixed caps %" GST_PTR_FORMAT, (is_fixed ? "" : "non-"),
      othercaps);

   othercaps = gst_caps_fixate (othercaps);
   is_fixed = gst_caps_is_fixed (othercaps);

  /* caps should be fixed now, if not we have to fail. */
  if (!is_fixed)
    goto could_not_fixate;

  /* and peer should accept */
  if (otherpeer && !gst_pad_query_accept_caps (otherpeer, othercaps))
    goto peer_no_accept;

  GST_DEBUG_OBJECT (mprtps, "Input caps were %" GST_PTR_FORMAT
      ", and got final caps %" GST_PTR_FORMAT, caps, othercaps);

  if (otherpeer)
    gst_object_unref (otherpeer);

  return othercaps;

  /* ERRORS */
no_transform:
  {
    GST_DEBUG_OBJECT (mprtps,
        "transform returned useless  %" GST_PTR_FORMAT, othercaps);
    goto error_cleanup;
  }
no_transform_possible:
  {
    GST_DEBUG_OBJECT (mprtps,
        "transform could not transform %" GST_PTR_FORMAT
        " in anything we support", caps);
    goto error_cleanup;
  }
could_not_fixate:
  {
    GST_DEBUG_OBJECT (mprtps, "FAILED to fixate %" GST_PTR_FORMAT, othercaps);
    goto error_cleanup;
  }
peer_no_accept:
  {
    GST_DEBUG_OBJECT (mprtps, "FAILED to get peer of %" GST_PTR_FORMAT
        " to accept %" GST_PTR_FORMAT, otherpad, othercaps);
    goto error_cleanup;
  }
error_cleanup:
  {
    if (otherpeer)
      gst_object_unref (otherpeer);
    if (othercaps)
      gst_caps_unref (othercaps);
    return NULL;
  }
}


static gboolean
gst_mprtpsender_do_bufferpool (GstMprtpsender * mprtps, GstCaps * outcaps)
{
  GstQuery *query;
  gboolean result = TRUE;
  GstBufferPool *pool = NULL;
  //GstBaseTransformClass *klass;
  GstAllocator *allocator;
  GstAllocationParams params;
  GstPad *otherpad;
  otherpad = _otherpad_for_rtcp(mprtps, mprtps->rtcp_sinkpad);
//  if(!gst_pad_is_linked(mprtps->rtcp_srcpad)){
//    if(mprtps->subflows != NULL){
//  	  otherpad = ((MPRTPSSubflow*)mprtps->subflows->data)->outpad;
//    }
//  }else{
//  	otherpad = mprtps->rtcp_srcpad;
//  }
  GST_DEBUG_OBJECT(mprtps,"Otherpad: %s", gst_pad_get_name(otherpad));
  GST_DEBUG_OBJECT (mprtps, "doing allocation query");
  query = gst_query_new_allocation (outcaps, TRUE);
  if (!gst_pad_peer_query (otherpad, query)) {
    /* not a problem, just debug a little */
    GST_DEBUG_OBJECT (mprtps, "peer ALLOCATION query failed");
  }

  GST_DEBUG_OBJECT (mprtps, "calling decide_allocation");
  result = gst_mprtpsender_default_decide_allocation(mprtps, query);

  GST_DEBUG_OBJECT (mprtps, "ALLOCATION (%d) params: %" GST_PTR_FORMAT, result,
      query);

  if (!result)
    goto no_decide_allocation;

  /* we got configuration from our peer or the decide_allocation method,
   * parse them */
  if (gst_query_get_n_allocation_params (query) > 0) {
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
  } else {
    allocator = NULL;
    gst_allocation_params_init (&params);
  }

  if (gst_query_get_n_allocation_pools (query) > 0)
    gst_query_parse_nth_allocation_pool (query, 0, &pool, NULL, NULL, NULL);

  /* now store */
  result =
	  gst_mprtpsender_set_allocation (mprtps, pool, allocator, &params,
      query);

  return result;

  /* Errors */
no_decide_allocation:
  {
    GST_WARNING_OBJECT (mprtps, "Subclass failed to decide allocation");
    gst_query_unref (query);

    return result;
  }
}


/* takes ownership of the pool, allocator and query */
static gboolean
gst_mprtpsender_set_allocation (GstMprtpsender * mprtps,
    GstBufferPool * pool, GstAllocator * allocator,
    GstAllocationParams * params, GstQuery * query)
{
  GstAllocator *oldalloc;
  GstBufferPool *oldpool;
  GstQuery *oldquery;
  GstMprtpsenderPrivate *priv = mprtps->priv;

  GST_OBJECT_LOCK (mprtps);
  oldpool = priv->pool;
  priv->pool = pool;
  priv->pool_active = FALSE;

  oldalloc = priv->allocator;
  priv->allocator = allocator;

  oldquery = priv->query;
  priv->query = query;

  if (params)
    priv->params = *params;
  else
    gst_allocation_params_init (&priv->params);
  GST_OBJECT_UNLOCK (mprtps);

  if (oldpool) {
    GST_DEBUG_OBJECT (mprtps, "deactivating old pool %p", oldpool);
    gst_buffer_pool_set_active (oldpool, FALSE);
    gst_object_unref (oldpool);
  }
  if (oldalloc) {
    gst_object_unref (oldalloc);
  }
  if (oldquery) {
    gst_query_unref (oldquery);
  }
  return TRUE;
}


static gboolean
gst_mprtpsender_default_decide_allocation (GstMprtpsender * trans,
    GstQuery * query)
{
  guint i, n_metas;
  //GstBaseTransformClass *klass;
  GstCaps *outcaps;
  GstBufferPool *pool;
  guint size, min, max;
  GstAllocator *allocator;
  GstAllocationParams params;
  GstStructure *config;
  gboolean update_allocator;

  n_metas = gst_query_get_n_allocation_metas (query);
  for (i = 0; i < n_metas; i++) {
    GType api;
    const GstStructure *params;
    gboolean remove;

    api = gst_query_parse_nth_allocation_meta (query, i, &params);

    /* by default we remove all metadata, subclasses should implement a
     * filter_meta function */
    if (gst_meta_api_type_has_tag (api, _gst_meta_tag_memory)) {
      /* remove all memory dependent metadata because we are going to have to
       * allocate different memory for input and output. */
      GST_LOG_OBJECT (trans, "removing memory specific metadata %s",
          g_type_name (api));
      remove = TRUE;
    } else {
      GST_LOG_OBJECT (trans, "removing metadata %s", g_type_name (api));
      remove = TRUE;
    }

    if (remove) {
      gst_query_remove_nth_allocation_meta (query, i);
      i--;
      n_metas--;
    }
  }

  gst_query_parse_allocation (query, &outcaps, NULL);

  /* we got configuration from our peer or the decide_allocation method,
   * parse them */
  if (gst_query_get_n_allocation_params (query) > 0) {
    /* try the allocator */
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    update_allocator = TRUE;
  } else {
    allocator = NULL;
    gst_allocation_params_init (&params);
    update_allocator = FALSE;
  }

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

    if (pool == NULL) {
      /* no pool, we can make our own */
      GST_DEBUG_OBJECT (trans, "no pool, making new pool");
      pool = gst_buffer_pool_new ();
    }
  } else {
    pool = NULL;
    size = min = max = 0;
  }

  /* now configure */
  if (pool) {
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
    gst_buffer_pool_config_set_allocator (config, allocator, &params);

    /* buffer pool may have to do some changes */
    if (!gst_buffer_pool_set_config (pool, config)) {
      config = gst_buffer_pool_get_config (pool);

      /* If change are not acceptable, fallback to generic pool */
      if (!gst_buffer_pool_config_validate_params (config, outcaps, size, min,
              max)) {
        GST_DEBUG_OBJECT (trans, "unsuported pool, making new pool");

        gst_object_unref (pool);
        pool = gst_buffer_pool_new ();
        gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
        gst_buffer_pool_config_set_allocator (config, allocator, &params);
      }

      if (!gst_buffer_pool_set_config (pool, config))
        goto config_failed;
    }
  }

  if (update_allocator)
    gst_query_set_nth_allocation_param (query, 0, allocator, &params);
  else
    gst_query_add_allocation_param (query, allocator, &params);
  if (allocator)
    gst_object_unref (allocator);

  if (pool) {
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
    gst_object_unref (pool);
  }

  return TRUE;

config_failed:
  GST_ELEMENT_ERROR (trans, RESOURCE, SETTINGS,
      ("Failed to configure the buffer pool"),
      ("Configuration is most likely invalid, please report this issue."));
  return FALSE;
}



#undef MPRTCP_PACKET_TYPE_IDENTIFIER
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef PACKET_IS_RTP
#undef PACKET_IS_DTLS
