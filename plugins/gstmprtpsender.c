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
#include <stdlib.h>
#include <gst/gst.h>
#include "gstmprtpsender.h"
#include "mprtpssubflow.h"
#include "schtree.h"
#include "gstmprtcpbuffer.h"

GST_DEBUG_CATEGORY_STATIC (gst_mprtpsender_debug_category);
#define GST_CAT_DEFAULT gst_mprtpsender_debug_category

#define MPRTP_SENDER_MUTEX_PTR(mprtps_ptr) &mprtps_ptr->base_object.object.lock
#define SUBFLOW_LOCK(mprtps_ptr) g_mutex_lock(&mprtps_ptr->subflows_mutex)
#define SUBFLOW_UNLOCK(mprtps_ptr) g_mutex_unlock(&mprtps_ptr->subflows_mutex)

#define GST_MPRTCP_BUFFER_FOR_PACKETS(b,buffer,packet) \
  for ((b) = gst_rtcp_buffer_get_first_packet ((buffer), (packet)); (b); \
          (b) = gst_rtcp_packet_move_to_next ((packet)))

#define MPRTP_SENDER_DEFAULT_CHARGE_VALUE 1.0
#define MPRTP_SENDER_DEFAULT_ALPHA_VALUE 0.5
#define MPRTP_SENDER_DEFAULT_BETA_VALUE 0.1
#define MPRTP_SENDER_DEFAULT_GAMMA_VALUE 0.2
#define MPRTP_SENDER_DEFAULT_EXTENSION_HEADER_ID 3
#define MPRTP_SENDER_DEFAULT_MPRTCP_MTU 1400


static void gst_mprtpsender_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_mprtpsender_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_mprtpsender_dispose (GObject * object);
static void gst_mprtpsender_finalize (GObject * object);

static GstPad *gst_mprtpsender_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps* caps);
static void gst_mprtpsender_release_pad (GstElement * element, GstPad * pad);
static GstStateChangeReturn
gst_mprtpsender_change_state (GstElement * element, GstStateChange transition);
static gboolean gst_mprtpsender_send_event (GstElement * element, GstEvent * event);
static gboolean gst_mprtpsender_query (GstElement * element, GstQuery * query);

static GstFlowReturn gst_mprtpsender_rtp_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buffer);
static GstFlowReturn gst_mprtpsender_rtp_sink_chainlist (GstPad *pad, GstObject *parent, GstBufferList *list);
static gboolean gst_mprtpsender_rtp_event_handler (GstPad *pad, GstObject *parent, GstEvent * event);

static GstFlowReturn gst_mprtpsender_rtcp_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *outbuf);
static GstFlowReturn gst_mprtpsender_mprtcp_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *outbuf);

static GstPadLinkReturn gst_mprtpsender_src_link (GstPad *pad, GstObject *parent, GstPad *peer);
static void gst_mprtpsender_src_unlink (GstPad *pad, GstObject *parent);

static GstPadLinkReturn gst_mprtpsender_rtcp_src_link (GstPad *pad, GstObject *parent, GstPad *peer);
static void gst_mprtpsender_rtcp_src_unlink (GstPad *pad, GstObject *parent);

static void gst_mprtp_sender_scheduler_run (void *data);

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
static void
gst_mprtp_sender_mprtcp_riporter_run (void *data);

static MPRTPSSubflow*
_get_next_subflow(GstMprtpsender *this, GstRTPBuffer *rtp);
static void _delete_subflow(GstMprtpsender* this, MPRTPSSubflow *subflow);


enum
{
  PROP_0,
  PROP_CHARGE,
  PROP_ALPHA,
  PROP_BETA,
  PROP_GAMMA,
  PROP_MPRTCP_MTU,
  PROP_EXT_HEADER_ID,
};

/* pad templates */

static GstStaticPadTemplate gst_mprtpsender_rtp_sink_template =
GST_STATIC_PAD_TEMPLATE ("rtp_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate gst_mprtpsender_rtcp_sink_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate gst_mprtpsender_mprtcp_sink_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
	GST_STATIC_CAPS ("ANY")
    );


static GstStaticPadTemplate gst_mprtpsender_src_factory =
GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
	GST_STATIC_CAPS ("application/x-rtp;application/x-rtcp;application/x-srtcp")
	);


static GstStaticPadTemplate gst_mprtpsender_rtcp_src_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_src",
	GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
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
      gst_static_pad_template_get (&gst_mprtpsender_rtp_sink_template));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpsender_rtcp_sink_template));

  gst_element_class_add_pad_template (element_class,
          gst_static_pad_template_get (&gst_mprtpsender_rtcp_src_template));

  gst_element_class_add_pad_template (element_class,
          gst_static_pad_template_get (&gst_mprtpsender_mprtcp_sink_template));

  gst_element_class_add_pad_template (element_class,
          gst_static_pad_template_get (&gst_mprtpsender_src_factory));

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
  element_class->send_event = GST_DEBUG_FUNCPTR (gst_mprtpsender_send_event);
  element_class->query = GST_DEBUG_FUNCPTR (gst_mprtpsender_query);

}

static gboolean
gst_mprtpsender_rtp_event_handler (GstPad *pad, GstObject *parent, GstEvent * event)
{

  GstMprtpsender *this = GST_MPRTPSENDER(parent);
  MPRTPSSubflow *subflow;
  GList *it;
  SUBFLOW_LOCK(this);

  for(it = this->subflows; it != NULL; it = it->next){
    subflow = it->data;
    subflow->push_event(subflow, event);
  }

gst_mprtpsender_rtp_event_handler_done:
  SUBFLOW_UNLOCK(this);
  return TRUE;
}

static void
gst_mprtpsender_init (GstMprtpsender *mprtpsender)
{

	mprtpsender->rtp_sinkpad = gst_pad_new_from_static_template (
			  &gst_mprtpsender_rtp_sink_template, "rtp_sink");

    gst_pad_set_chain_function (mprtpsender->rtp_sinkpad,
			GST_DEBUG_FUNCPTR(gst_mprtpsender_rtp_sink_chain));
    gst_pad_set_chain_list_function (mprtpsender->rtp_sinkpad,
			GST_DEBUG_FUNCPTR(gst_mprtpsender_rtp_sink_chainlist));
    gst_pad_set_event_function(mprtpsender->rtp_sinkpad,
    		GST_DEBUG_FUNCPTR(gst_mprtpsender_rtp_event_handler));

    gst_element_add_pad (GST_ELEMENT(mprtpsender), mprtpsender->rtp_sinkpad);

	mprtpsender->mprtcp_sinkpad = gst_pad_new_from_static_template (
			  &gst_mprtpsender_mprtcp_sink_template, "mprtcp_sink");

    gst_pad_set_chain_function (mprtpsender->mprtcp_sinkpad,
			GST_DEBUG_FUNCPTR(gst_mprtpsender_mprtcp_sink_chain));

    gst_element_add_pad (GST_ELEMENT(mprtpsender), mprtpsender->mprtcp_sinkpad);

    mprtpsender->rtcp_sinkpad = gst_pad_new_from_static_template (
    		  &gst_mprtpsender_rtcp_sink_template, "rtcp_sink");

    	gst_pad_set_chain_function (mprtpsender->rtcp_sinkpad,
    			  GST_DEBUG_FUNCPTR(gst_mprtpsender_rtcp_sink_chain));

	gst_pad_set_event_function(mprtpsender->rtcp_sinkpad,
		    		  GST_DEBUG_FUNCPTR(gst_mprtpsender_rtcp_sink_eventfunc));
	gst_pad_set_query_function(mprtpsender->rtcp_sinkpad,
		    		  GST_DEBUG_FUNCPTR(gst_mprtpsender_pad_query));

	gst_element_add_pad (GST_ELEMENT(mprtpsender), mprtpsender->rtcp_sinkpad);

	mprtpsender->rtcp_srcpad = gst_pad_new_from_static_template (
		&gst_mprtpsender_rtcp_src_template, "rtcp_src");

	gst_pad_set_event_function(mprtpsender->rtcp_srcpad,
	    		  GST_DEBUG_FUNCPTR(gst_mprtpsender_rtcp_src_eventfunc));
	gst_pad_set_query_function (mprtpsender->rtcp_srcpad,
	      GST_DEBUG_FUNCPTR (gst_mprtpsender_pad_query));
    gst_pad_set_link_function (mprtpsender->rtcp_srcpad,
			GST_DEBUG_FUNCPTR(gst_mprtpsender_rtcp_src_link));
    gst_pad_set_unlink_function (mprtpsender->rtcp_srcpad,
			GST_DEBUG_FUNCPTR(gst_mprtpsender_rtcp_src_unlink));

	gst_element_add_pad (GST_ELEMENT(mprtpsender), mprtpsender->rtcp_srcpad);

  mprtpsender->schtree = g_object_new (SCHTREE_TYPE, NULL);
  mprtpsender->charge_value = MPRTP_SENDER_DEFAULT_CHARGE_VALUE;
  mprtpsender->alpha_value = MPRTP_SENDER_DEFAULT_ALPHA_VALUE;
  mprtpsender->beta_value = MPRTP_SENDER_DEFAULT_BETA_VALUE;
  mprtpsender->gamma_value = MPRTP_SENDER_DEFAULT_GAMMA_VALUE;
  g_cond_init(&mprtpsender->scheduler_cond);
  g_mutex_init(&mprtpsender->subflows_mutex);
  mprtpsender->scheduler_last_run = 0;
  mprtpsender->no_active_subflows = TRUE;
  mprtpsender->scheduler_state = 0;
  mprtpsender->mprtcp_mtu = MPRTP_SENDER_DEFAULT_MPRTCP_MTU;
  mprtpsender->ext_header_id = MPRTP_SENDER_DEFAULT_EXTENSION_HEADER_ID;
  mprtpsender->rtcp_send_item = NULL;
  mprtpsender->ssrc = g_random_int();

  gst_segment_init (&mprtpsender->segment, GST_FORMAT_UNDEFINED);
  mprtpsender->priv = GST_MPRTPSENDER_GET_PRIVATE(mprtpsender);
}

void
gst_mprtpsender_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMprtpsender *mprtpsender = GST_MPRTPSENDER (object);
  GList *it;
  MPRTPSSubflow *subflow;
  GST_DEBUG_OBJECT (mprtpsender, "set_property");

  switch (property_id) {
    case PROP_CHARGE:
      SUBFLOW_LOCK(mprtpsender);
  	  mprtpsender->charge_value = g_value_get_float(value);
  	  SUBFLOW_UNLOCK(mprtpsender);
  	  break;
    case PROP_ALPHA:
      SUBFLOW_LOCK(mprtpsender);
      mprtpsender->alpha_value = g_value_get_float(value);
      SUBFLOW_UNLOCK(mprtpsender);
      break;
    case PROP_BETA:
      SUBFLOW_LOCK(mprtpsender);
      mprtpsender->beta_value = g_value_get_float(value);
      SUBFLOW_UNLOCK(mprtpsender);
      break;
    case PROP_GAMMA:
      SUBFLOW_LOCK(mprtpsender);
      mprtpsender->gamma_value = g_value_get_float(value);
      SUBFLOW_UNLOCK(mprtpsender);
      break;
    case PROP_EXT_HEADER_ID:
	  SUBFLOW_LOCK(mprtpsender);
	  mprtpsender->ext_header_id = g_value_get_int(value);
	  SUBFLOW_UNLOCK(mprtpsender);
	  break;
    case PROP_MPRTCP_MTU:
	  SUBFLOW_LOCK(mprtpsender);
	  mprtpsender->mprtcp_mtu = g_value_get_uint(value);
	  SUBFLOW_UNLOCK(mprtpsender);
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
  case PROP_CHARGE:
	  SUBFLOW_LOCK(mprtpsender);
 	  g_value_set_float(value, mprtpsender->charge_value);
 	  SUBFLOW_UNLOCK(mprtpsender);
 	break;
  case PROP_ALPHA:
	  SUBFLOW_LOCK(mprtpsender);
 	  g_value_set_float(value, mprtpsender->alpha_value);
 	  SUBFLOW_UNLOCK(mprtpsender);
 	break;
  case PROP_BETA:
	  SUBFLOW_LOCK(mprtpsender);
 	  g_value_set_float(value, mprtpsender->beta_value);
 	  SUBFLOW_UNLOCK(mprtpsender);
 	break;
  case PROP_GAMMA:
	  SUBFLOW_LOCK(mprtpsender);
 	  g_value_set_float(value, mprtpsender->gamma_value);
   	  SUBFLOW_UNLOCK(mprtpsender);
 	break;
  case PROP_EXT_HEADER_ID:
	  SUBFLOW_LOCK(mprtpsender);
	  g_value_set_int(value, mprtpsender->ext_header_id);
	  SUBFLOW_UNLOCK(mprtpsender);
	break;
  case PROP_MPRTCP_MTU:
  	  SUBFLOW_LOCK(mprtpsender);
  	  g_value_set_uint(value, mprtpsender->mprtcp_mtu);
  	  SUBFLOW_UNLOCK(mprtpsender);
  	  break;
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
	GstMprtpsender *mprtps;
	MPRTPSSubflow *subflow;
	GList *it;
	guint16 subflow_id;

	mprtps = GST_MPRTPSENDER (element);
	GST_DEBUG_OBJECT (mprtps, "requesting pad");

	sscanf(name, "src_%u", &subflow_id);
	for(it = mprtps->subflows; it != NULL; it = it->next){
	  subflow = (MPRTPSSubflow*) it->data;
	  if(subflow->id == subflow_id){
		GST_WARNING_OBJECT(mprtps, "Requested Pad subflow id is not unique");
		return NULL;
	  }
	}

	SUBFLOW_LOCK(mprtps);
	srcpad = gst_pad_new_from_template (templ, name);
    gst_pad_set_link_function (srcpad,
			GST_DEBUG_FUNCPTR(gst_mprtpsender_src_link));
    gst_pad_set_unlink_function (srcpad,
			GST_DEBUG_FUNCPTR(gst_mprtpsender_src_unlink));

    subflow = make_mprtps_subflow(subflow_id, srcpad);
    mprtps->subflows = g_list_prepend(mprtps->subflows, subflow);
    SUBFLOW_UNLOCK(mprtps);

	gst_element_add_pad (GST_ELEMENT (mprtps), srcpad);

	gst_pad_set_active (srcpad, TRUE);

	return srcpad;
}

static void
gst_mprtpsender_release_pad (GstElement * element, GstPad * pad)
{
	GstMprtpsender *mprtps;
	MPRTPSSubflow *subflow = NULL;
	GList *it;

	mprtps = GST_MPRTPSENDER (element);
	GST_DEBUG_OBJECT (mprtps, "releasing pad");
	gst_pad_set_active(pad, FALSE);

	SUBFLOW_LOCK(mprtps);
    for(it = mprtps->subflows; it != NULL; it = it->next){
      subflow = it->data;
      if(subflow->outpad == pad){
        break;
      }
      subflow = NULL;
    }
    if(subflow == NULL){
      GST_WARNING_OBJECT(mprtps, "The requested pad has no subflow");
      SUBFLOW_UNLOCK(mprtps);
      return;
    }
    //mark subflow to delete
	SUBFLOW_UNLOCK(mprtps);
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
	  mprtpsender->scheduler = gst_task_new (gst_mprtp_sender_scheduler_run, mprtpsender, NULL);
	  g_rec_mutex_init (&mprtpsender->scheduler_mutex);
	  gst_task_set_lock (mprtpsender->scheduler, &mprtpsender->scheduler_mutex);
	  mprtpsender->riporter = gst_task_new (gst_mprtp_sender_mprtcp_riporter_run, mprtpsender, NULL);
	  g_rec_mutex_init (&mprtpsender->riporter_mutex);
	  gst_task_set_lock (mprtpsender->riporter, &mprtpsender->riporter_mutex);
	  mprtpsender->scheduler_state = 1;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      gst_task_start (mprtpsender->scheduler);
      gst_task_start (mprtpsender->riporter);
      mprtpsender->scheduler_state = 2;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (gst_mprtpsender_parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
        gst_task_stop (mprtpsender->scheduler);
        gst_task_stop (mprtpsender->riporter);
        gst_task_join (mprtpsender->scheduler);
        gst_task_join (mprtpsender->riporter);
      mprtpsender->scheduler_state = 1;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (gst_task_get_state (mprtpsender->scheduler) != GST_TASK_STOPPED)
	    GST_ERROR ("task %p should be stopped by now", mprtpsender->scheduler);
		gst_object_unref (mprtpsender->scheduler);
		mprtpsender->scheduler = NULL;
		g_rec_mutex_clear (&mprtpsender->scheduler_mutex);
		gst_object_unref (mprtpsender->riporter);
		mprtpsender->riporter = NULL;
		g_rec_mutex_clear (&mprtpsender->riporter_mutex);
	  mprtpsender->scheduler_state = 0;
      break;
    default:
      break;
  }

  return ret;
}




static gboolean
gst_mprtpsender_send_event (GstElement * element, GstEvent * event)
{
  return TRUE;
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

static GstFlowReturn
gst_mprtpsender_rtp_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
  MPRTPSSubflow *subflow;
  GstMprtpsender *mprtpsender;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  GstBuffer *outbuf;
  SchTree *schtree;
  GstFlowReturn result;

  mprtpsender = GST_MPRTPSENDER (parent);
  schtree = mprtpsender->schtree;
  GST_DEBUG_OBJECT(mprtpsender, "chain function started");
  outbuf = gst_buffer_make_writable (buffer);
  if (G_UNLIKELY (!gst_rtp_buffer_map(outbuf, GST_MAP_READWRITE, &rtp))){
	GST_WARNING_OBJECT(mprtpsender, "The RTP packet is not writeable");
	return GST_FLOW_ERROR;
  }
  SUBFLOW_LOCK(mprtpsender);

  if(mprtpsender->no_active_subflows){
    g_cond_wait(&mprtpsender->scheduler_cond, &mprtpsender->subflows_mutex);
  }


  //subflow = schtree->get_next(schtree);
  subflow = _get_next_subflow(mprtpsender, &rtp);
  subflow->process_rtpbuf_out(subflow, mprtpsender->ext_header_id, &rtp);
  //g_print("sent out on %d\n",subflow->get_id(subflow));
  GST_DEBUG_OBJECT(mprtpsender, "selected subflow is %d", subflow->get_id(subflow));

  //gst_print_rtp_packet_info(&rtp);
  gst_rtp_buffer_unmap (&rtp);
  //result = gst_pad_push(subflow->get_outpad(subflow), outbuf);
  result = subflow->push_buffer(subflow, outbuf);
  GST_DEBUG_OBJECT(mprtpsender, "packet sent out on %d, chain is going to be unlocked", subflow->get_id(subflow));

  SUBFLOW_UNLOCK(mprtpsender);

  GST_DEBUG_OBJECT(mprtpsender, "chain is unlocked");

  return GST_FLOW_OK;
}

MPRTPSSubflow* _get_next_subflow(GstMprtpsender *this, GstRTPBuffer *rtp)
{
  SchTree *schtree = this->schtree;
  MPRTPSSubflow *result;
  /*
  if(!gst_rtp_buffer_get_marker(rtp)){
    return schtree->get_actual(schtree);
  }
/**/
  //gst_print_rtp_packet_info(rtp);
  result = schtree->get_next(schtree);
  /*
  if(!GST_BUFFER_FLAG_IS_SET(rtp->buffer, GST_BUFFER_FLAG_DELTA_UNIT)){
	while(result->get_state(result) != MPRTP_SENDER_SUBFLOW_STATE_NON_CONGESTED){
	  result = schtree->get_next(schtree);
    }
  }
  /**/
  return result;

}
static GstFlowReturn
gst_mprtpsender_rtp_sink_chainlist (GstPad *pad, GstObject *parent, GstBufferList *list)
{
  GstMprtpsender *mprtpsender;
  mprtpsender = GST_MPRTPSENDER (parent);
  GST_WARNING_OBJECT(mprtpsender, "mprtp chainlist sender is not implemented in this version");

  return GST_FLOW_CUSTOM_ERROR;
}

static GstPadLinkReturn
gst_mprtpsender_src_link (GstPad *pad, GstObject *parent, GstPad *peer)
{
  GstMprtpsender *this;
  GList *it;
  MPRTPSSubflow *subflow, *selected = NULL;
  this = GST_MPRTPSENDER (parent);
  GST_DEBUG_OBJECT(this, "Link the source pad");

  SUBFLOW_LOCK(this);
  for(it = this->subflows; it != NULL; it = it->next){
    subflow = it->data;
    if(subflow->get_outpad(subflow) == pad){
      selected = subflow;
    }
  }
  if(G_UNLIKELY(selected == NULL)){
	SUBFLOW_UNLOCK (this);
    GST_WARNING_OBJECT(this, "Can not find subflow for the recently linked pad.");
    return GST_PAD_LINK_OK;
  }
  selected->set_event(selected, MPRTP_SENDER_SUBFLOW_EVENT_JOINED);
  SUBFLOW_UNLOCK(this);
  return GST_PAD_LINK_OK;
}

static void
gst_mprtpsender_src_unlink (GstPad *pad, GstObject *parent)
{
  GstMprtpsender *mprtpsender;
  GList *it;
  MPRTPSSubflow *subflow, *selected = NULL;
  mprtpsender = GST_MPRTPSENDER (parent);
  GST_DEBUG_OBJECT(mprtpsender, "Unlink the source pad");

  SUBFLOW_LOCK(mprtpsender);
  for(it = mprtpsender->subflows; it != NULL; it = it->next){
    subflow = it->data;
    if(subflow->outpad == pad){
      selected = subflow;
    }
  }
  if(G_UNLIKELY(selected == NULL)){
	SUBFLOW_UNLOCK(mprtpsender);
    GST_WARNING_OBJECT(mprtpsender, "Can not find subflow for the recently unlinked pad.");
    return;
  }
  selected->set_event(selected, MPRTP_SENDER_SUBFLOW_EVENT_DETACHED);
  SUBFLOW_UNLOCK(mprtpsender);
}



static GstPadLinkReturn
gst_mprtpsender_rtcp_src_link (GstPad *pad, GstObject *parent, GstPad *peer)
{
  GstMprtpsender *this;
  GList *it;
  MPRTPSSubflow *subflow;
  this = GST_MPRTPSENDER (parent);
  GST_DEBUG_OBJECT(this, "Link the source rtcp pad");

  return GST_PAD_LINK_OK;
}

static void
gst_mprtpsender_rtcp_src_unlink (GstPad *pad, GstObject *parent)
{
  GstMprtpsender *mprtpsender;
  GList *it;
  MPRTPSSubflow *subflow;
  mprtpsender = GST_MPRTPSENDER (parent);
  GST_DEBUG_OBJECT(mprtpsender, "Unlink the source pad");

}


static GstFlowReturn
gst_mprtpsender_rtcp_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
	GstMprtpsender *mprtps;
	GstPad *outpad;
	GstFlowReturn result;
	SchTree *schtree;
	MPRTPSSubflow *subflow;
	GList *it;

	mprtps = GST_MPRTPSENDER (parent);
	SUBFLOW_LOCK(mprtps);
	schtree = mprtps->schtree;

	outpad = gst_pad_is_linked (mprtps->rtcp_srcpad) ? mprtps->rtcp_srcpad
			: schtree->get_actual(schtree)->outpad;
	result = gst_pad_push (outpad, buffer);

    SUBFLOW_UNLOCK(mprtps);
	return result;
}



static GstFlowReturn
gst_mprtpsender_mprtcp_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
	GstMprtpsender *this;
	GstFlowReturn result = GST_FLOW_OK;
	MPRTPSSubflow *subflow;
	GList *it;
    GstRTCPBuffer rtcp = {NULL, };
    GstRTCPHeader *header;
    GstMPRTCPSubflowRiport *riport;
    GstMPRTCPSubflowBlock *block;
    guint16 subflow_id;
    guint8 pt;

	this = GST_MPRTPSENDER (parent);
	SUBFLOW_LOCK(this);

    if(!gst_rtcp_buffer_map(buffer, GST_MAP_READ, &rtcp)){
      GST_ERROR_OBJECT(this,"mprtcp buffer is not readable");
      goto gst_mprtpsender_mprtcp_sink_chain_done;
    }

    for(header = gst_rtcp_get_first_header(&rtcp);
    	header != NULL;
    	header = gst_rtcp_get_next_header(&rtcp, header))
    {
      gst_rtcp_header_getdown(header, NULL, NULL, NULL, &pt, NULL, NULL);
      if(pt != MPRTCP_PACKET_TYPE_IDENTIFIER){
        continue;
      }
      //gst_print_rtcp(header);
      riport = (GstMPRTCPSubflowRiport*) header;
      for(block = gst_mprtcp_get_first_block(riport);
    	  block != NULL;
    	  block = gst_mprtcp_get_next_block(riport, block))
      {
        gst_mprtcp_block_getdown(&block->info, NULL, NULL, &subflow_id);
    	for(it = this->subflows; it != NULL; it = it->next)
    	{
          subflow = it->data;
          if(subflow->get_id(subflow) != subflow_id){
            continue;
          }
          subflow->process_mprtcp_block(subflow, block);
        }
      }
    }
	gst_rtcp_buffer_unmap(&rtcp);

gst_mprtpsender_mprtcp_sink_chain_done:
    SUBFLOW_UNLOCK(this);
	return result;
}


void
gst_mprtp_sender_scheduler_run (void *data)
{
  GstMprtpsender* mprtps = (GstMprtpsender*) data;
  SchTree* schtree = mprtps->schtree;
  GstClockTime now;
  GstClockID clock_id;
  GstClockTime next_scheduler_time;
  GList *it;
  gfloat subflow_sending_rate;
  gfloat charge_value = 0.0;
  MPRTPSSubflow *subflow;
  MPRTPSubflowEvent subflow_event;
  gboolean has_active_subflow, had_active_subflows = !mprtps->no_active_subflows;
  gboolean commit_schtree_changes = FALSE;
  guint32 non_congested_path_num = 0, congested_path_num = 0;
  guint consecutive_keeps;

  GST_DEBUG_OBJECT(mprtps, "Scheduler task is started");
  SUBFLOW_LOCK(mprtps);

  //determine the charge_value
  {
    gfloat charge_start_value = 1.0, charge_nc_value = 0.0, charge_c_value = 0.0;
    MPRTPSubflowStates state;
	for(it = mprtps->subflows; it != NULL; it = it->next){
	  subflow = (MPRTPSSubflow*) it->data;
      state = subflow->get_state(subflow);
	  if(state == MPRTP_SENDER_SUBFLOW_STATE_NON_CONGESTED){
	    charge_nc_value += subflow->get_sending_rate(subflow);
	    ++non_congested_path_num;
	  }else if(state == MPRTP_SENDER_SUBFLOW_STATE_CONGESTED){
		charge_c_value += subflow->get_sending_rate(subflow);
	    ++congested_path_num;
	  }
    }
	if(non_congested_path_num > 0){
	  charge_value = charge_nc_value / (non_congested_path_num*2.0);
	}else if(congested_path_num > 0){
	  charge_value = charge_c_value*2.0;
	}

	if(charge_value < 1.0){
	  charge_value = charge_start_value;
	}
	//g_print("charge_value: %f\n", charge_value);
  }

  //Fire
  has_active_subflow = FALSE;
  for(it = mprtps->subflows; it != NULL; it = it->next)
  {
    subflow = (MPRTPSSubflow*) it->data;
	subflow_event = subflow->check(subflow);
	subflow_sending_rate = subflow->get_sending_rate(subflow);
	if(subflow_event != MPRTP_SENDER_SUBFLOW_EVENT_KEEP){
      //g_print("subflow: %d event: %d sending rate: %f\n"
    	//	, subflow->get_id(subflow), subflow_event, subflow_sending_rate);
	}
    switch(subflow->get_state(subflow)){
      case MPRTP_SENDER_SUBFLOW_STATE_NON_CONGESTED:
        switch(subflow_event){
          case MPRTP_SENDER_SUBFLOW_EVENT_JOINED:
			schtree->setup_sending_rate(schtree, subflow, charge_value);
			subflow->set_state(subflow, MPRTP_SENDER_SUBFLOW_STATE_NON_CONGESTED);
			commit_schtree_changes = TRUE;
		  break;
          case MPRTP_SENDER_SUBFLOW_EVENT_DISTORTION:
            subflow_sending_rate *= (1.0 - mprtps->alpha_value);
            schtree->setup_sending_rate(schtree, subflow, subflow_sending_rate);
            commit_schtree_changes = TRUE;
          break;
          case MPRTP_SENDER_SUBFLOW_EVENT_CONGESTION:
  		    subflow_sending_rate *= (1.0 - mprtps->beta_value);
  		    schtree->setup_sending_rate(schtree, subflow, subflow_sending_rate);
  		    subflow->set_state(subflow, MPRTP_SENDER_SUBFLOW_STATE_CONGESTED);
		    commit_schtree_changes = TRUE;
		  break;

          case MPRTP_SENDER_SUBFLOW_EVENT_LATE:
			subflow->set_state(subflow, MPRTP_SENDER_SUBFLOW_STATE_PASSIVE);
			schtree->setup_sending_rate(schtree, subflow, 0.0);
			commit_schtree_changes = TRUE;
		  break;
          case MPRTP_SENDER_SUBFLOW_EVENT_DETACHED:
            schtree->delete_path(schtree, subflow);
            commit_schtree_changes = TRUE;
            _delete_subflow(mprtps, subflow);
          break;
          case MPRTP_SENDER_SUBFLOW_EVENT_KEEP:
        	consecutive_keeps = subflow->get_consecutive_keeps_num(subflow);
        	if(consecutive_keeps > 2){
              subflow->save_sending_rate(subflow);
              schtree->setup_sending_rate(schtree, subflow, subflow_sending_rate);
        	}
          break;
          case MPRTP_SENDER_SUBFLOW_EVENT_REFRESH:
        	  schtree->setup_sending_rate(schtree, subflow, subflow_sending_rate);
        	  commit_schtree_changes = TRUE;
          break;
          default:
          break;
        }
      break;
      case MPRTP_SENDER_SUBFLOW_STATE_CONGESTED:
        switch(subflow_event){
          case MPRTP_SENDER_SUBFLOW_EVENT_LATE:
			subflow->set_state(subflow, MPRTP_SENDER_SUBFLOW_STATE_PASSIVE);
			schtree->setup_sending_rate(schtree, subflow, 0.0);
			commit_schtree_changes = TRUE;
		  break;
          case MPRTP_SENDER_SUBFLOW_EVENT_SETTLED:
        	subflow->set_state(subflow, MPRTP_SENDER_SUBFLOW_STATE_NON_CONGESTED);
			subflow_sending_rate = subflow->load_sending_rate(subflow);
			if(subflow_sending_rate == 0.0){
			  subflow_sending_rate = charge_value;
			}
			schtree->setup_sending_rate(schtree, subflow, subflow_sending_rate);
			commit_schtree_changes = TRUE;
		  break;
          case MPRTP_SENDER_SUBFLOW_EVENT_DETACHED:
        	schtree->delete_path(schtree, subflow);
		    commit_schtree_changes = TRUE;
		    _delete_subflow(mprtps, subflow);
		  break;
		  default:
			//g_print("CONGESTED state got KEEP/DISTORTION/OTHER event\n");
		  break;
	    }
	  break;
      case MPRTP_SENDER_SUBFLOW_STATE_PASSIVE:
        switch(subflow_event){
          case MPRTP_SENDER_SUBFLOW_EVENT_SETTLED:
			subflow->set_state(subflow, MPRTP_SENDER_SUBFLOW_STATE_NON_CONGESTED);
			subflow_sending_rate = subflow->load_sending_rate(subflow);
			if(subflow_sending_rate == 0.0){
			  subflow_sending_rate = charge_value;
			}
			schtree->setup_sending_rate(schtree, subflow, subflow_sending_rate);
			commit_schtree_changes = TRUE;
		  break;
          case MPRTP_SENDER_SUBFLOW_EVENT_DETACHED:
        	schtree->delete_path(schtree, subflow);
			commit_schtree_changes = TRUE;
			_delete_subflow(mprtps, subflow);
		  break;
          case MPRTP_SENDER_SUBFLOW_EVENT_KEEP:
          default:
          break;
        }
	  break;
      default:
      break;
    }
    if(subflow->is_active(subflow)){
	  has_active_subflow = TRUE;
	}
  }
  now = gst_clock_get_time(GST_ELEMENT_CLOCK(mprtps));
  if(mprtps->last_schtree_commit < now - 30 * GST_SECOND){
    commit_schtree_changes = TRUE;
  }
  if(commit_schtree_changes){
	  for(it = mprtps->subflows; it != NULL; it = it->next){
		  subflow = it->data;
		  g_print("Subflow %d, sent packet num: %d\n", subflow->get_id(subflow),
		  	          		subflow->get_sent_packet_num(subflow));
	  }
    schtree->commit_changes(schtree);
    mprtps->last_schtree_commit = now;
  }

  mprtps->no_active_subflows = !has_active_subflow;
  //g_print("mprtps->no_active_subflows: %d had_active_subflows: %d\n",
	//	  mprtps->no_active_subflows, had_active_subflows);
  if(mprtps->no_active_subflows == FALSE && had_active_subflows == FALSE){
	 g_cond_signal(&mprtps->scheduler_cond);
  }

  mprtps->scheduler_last_run = now;


  gdouble rand = g_random_double();
  next_scheduler_time = now + GST_SECOND * (0.5 + rand);
  GST_DEBUG_OBJECT(mprtps, "Next scheduling interval time is %u", next_scheduler_time);
  clock_id = gst_clock_new_single_shot_id (GST_ELEMENT_CLOCK(mprtps), next_scheduler_time);

  SUBFLOW_UNLOCK(mprtps);

  if(gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED){
    GST_WARNING_OBJECT(mprtps, "The scheduler clock wait is interrupted");
  }
  gst_clock_id_unref (clock_id);
  //g_print("+%lu*%f=%lu\n", rr_max_time_dif, 0.5 + rand, next_scheduler_time);
  //g_print("2:%lu\n",gst_clock_get_time(GST_ELEMENT_CLOCK(mprtps)));

}


void
gst_mprtp_sender_mprtcp_riporter_run (void *data)
{
  GstMprtpsender* this = (GstMprtpsender*) data;
  GstClockTime now;
  GstClockTimeDiff rr_max_time_dif = 0, clock_jitter;
  GstClockID clock_id;
  GstClockTime next_scheduler_time;
  GList *it;
  MPRTPSSubflow *subflow;
  gboolean rtcp_static_is_linked;
  GstPad *outpad;
  GstBuffer *outbuf;
  GstRTCPBuffer rtcp = {NULL, };
  guint64 next_riport_time;
  GstRTCPHeader *header = NULL;
  GstMPRTCPSubflowRiport *riport;
  gboolean compound_sending, first = TRUE;

  now = gst_clock_get_time(GST_ELEMENT_CLOCK(this));
  SUBFLOW_LOCK(this);

  compound_sending = gst_pad_is_linked(this->rtcp_srcpad);
  for(first = TRUE, it = this->subflows; it != NULL; it = it->next){
    subflow = it->data;
    if(now < subflow->get_sr_riport_time(subflow)){
	  continue;
	}
    if(first == TRUE){
	  outbuf = gst_rtcp_buffer_new(1400);
	  gst_rtcp_buffer_map (outbuf, GST_MAP_READWRITE, &rtcp);
      first = FALSE;
    }
    header = gst_rtcp_add_begin(&rtcp);
    riport = gst_mprtcp_add_riport(header);
    next_riport_time = now + GST_SECOND * 3;
    subflow->setup_sr_riport(subflow, riport);
    gst_rtcp_add_end(&rtcp, header);

    subflow->set_sr_riport_time(subflow, next_riport_time);

    if(compound_sending){
      continue;
    }
    gst_rtcp_buffer_unmap(&rtcp);
    if(subflow->is_flowable(subflow)){
      subflow->push_buffer(subflow, outbuf);
    }else{
      gst_buffer_unref(outbuf);
    }

    if(it->next != NULL){
      outbuf = gst_rtcp_buffer_new(1400);
      gst_rtcp_buffer_map (outbuf, GST_MAP_READWRITE, &rtcp);
    }

  }
  if(first == FALSE && compound_sending){
    gst_rtcp_buffer_unmap(&rtcp);
  }

  if(compound_sending){
    gst_pad_push(this->rtcp_srcpad, outbuf);
  }

gst_mprtpreceiver_mprtcp_riporter_run_end:
  SUBFLOW_UNLOCK(this);

  next_scheduler_time = now + GST_MSECOND * 100;
  clock_id = gst_clock_new_single_shot_id (GST_ELEMENT_CLOCK(this), next_scheduler_time);
  if(gst_clock_id_wait (clock_id, &clock_jitter) == GST_CLOCK_UNSCHEDULED){
    GST_WARNING_OBJECT(this, "The scheduler clock wait is interrupted");
  }
  gst_clock_id_unref (clock_id);

}

void _delete_subflow(GstMprtpsender* this, MPRTPSSubflow *subflow)
{
  GList *it;
  this->subflows = g_list_remove(this->subflows, subflow);
  g_object_unref((GObject*)subflow);
}

/*

    if(subflow->delay > mprtps->max_delay){
      subflow->fire(subflow, MPRTP_SENDER_SUBFLOW_EVENT_LATE, NULL);
      continue;
    }
    rr_arrived = GST_CLOCK_DIFF(scheduler_last_run, subflow->RRT) > 0?TRUE:FALSE;
    db_arrived = GST_CLOCK_DIFF(scheduler_last_run, subflow->DPRT) > 0?TRUE:FALSE;
    if(rr_arrived == FALSE){
      ++subflow->consequent_RR_missing;
    }else{
      subflow->consequent_RR_missing = 0;
      subflow->receive_rate = (gfloat)((gfloat)subflow->UB * (1-subflow->LostRate) - (gfloat)subflow->DB) /
    		        (gfloat) (subflow->RR_time_dif);
    }
    if(subflow->consequent_RR_missing > 10){
      subflow->fire(subflow, MPRTP_SENDER_SUBFLOW_EVENT_DEAD, NULL);
      continue;
    }
    if(rr_max_time_dif < subflow->RR_time_dif){
    	rr_max_time_dif = subflow->RR_time_dif;
    }
    if(db_arrived){
      subflow->distortions |= 1;
    }
    if(rr_arrived && subflow->lost_packets_num > 0){
      subflow->distortions |= 2;
    }
    subflow->distortions<<=2;

    //two consequtive lost and at least one discarded riport or three consequtive lost
    if(subflow->distortions > 40 && (subflow->distortions & 0x28) > 0){
      ++subflow->consequent_distortions;
      subflow->consequent_settlements = 0;
    }else if(subflow->distortions == 0){
      subflow->consequent_distortions = 0;
      ++subflow->consequent_settlements;
    }

    if(subflow->consequent_distortions > 2){
      rate = MIN(1.0/(gfloat)subflow_num, (gfloat)UB_sum * mprtps->alpha_discharge);
      subflow->fire(subflow, MPRTP_SENDER_SUBFLOW_EVENT_CONGESTION, &rate);
      subflow->consequent_distortions = 0;
      subflow->consequent_settlements = 0;
    }else if(subflow->consequent_distortions > 0){
      rate = subflow->receive_rate*(1-mprtps->beta);
      subflow->fire(subflow, MPRTP_SENDER_SUBFLOW_EVENT_DISTORTION, &rate);
    }else if(subflow->consequent_settlements > 2){
      subflow->fire(subflow, MPRTP_SENDER_SUBFLOW_EVENT_SETTLED, NULL);
      subflow->consequent_settlements = 0;
    }else if(subflow->increasement_request){
      rate = subflow->receive_rate*(1+mprtps->gamma);
      subflow->fire(subflow, MPRTP_SENDER_SUBFLOW_EVENT_BID, &rate);
    }else{
      subflow->fire(subflow, MPRTP_SENDER_SUBFLOW_EVENT_fi, NULL);
    }
*/


/*
GstPad*
gst_mprtp_sender_get_rtcp_outpad(GstMprtpsender *mprtps)
{
	MPRTPSSubflow *subflow = NULL;
	GList *item = NULL;
	gint count;
	//static srcpad for rtcp rr and sr packets
	if(mprtps->rtcp_out_subflow_changed == FALSE &&
	   mprtps->rtcp_out_subflow_id != MPRTP_SENDER_RTCP_OUT_ROUNDROBIN){
	   return mprtps->rtcp_sinkpad;
	}
	//round robin srcpad for rtcp rr and sr
	if(mprtps->rtcp_out_subflow_changed == FALSE){
	  item = mprtps->rtcp_out_subflow_item;

	  for(count = 0; item != NULL || count < 2; item = item->next){
		if(item == NULL){
	      ++count;
	      item = mprtps->subflows;
		}
		subflow = (MPRTPSSubflow*)item->data;
		if(subflow->state != MPRTP_SENDER_SUBFLOW_STATE_NON_CONGESTED){
		  subflow = NULL;
		  continue;
		}
		break;
	  }
	  if(subflow == NULL){
		  GST_WARNING_OBJECT(mprtps, "No Non-congested RTCP outpad was found.");
		  return NULL;
	  }
	  return subflow->outpad;
	}

	//the mprtps rtcp out subflow value is changed.
	mprtps->rtcp_out_subflow_changed = FALSE;
	mprtps->rtcp_out_subflow_item = NULL;
	if(mprtps->rtcp_out_subflow_id == MPRTP_SENDER_RTCP_OUT_ROUNDROBIN){
	  return gst_mprtp_sender_get_rtcp_outpad(mprtps);
	}

	//set rtcp out to a specific srcpad
	for(item = mprtps->subflows; item != NULL; item = item->next){
	  subflow = (MPRTPSSubflow*)item->data;
	  if(subflow->id != (guint16)mprtps->rtcp_out_subflow_id){
		  continue;
	  }
	  mprtps->rtcp_out_subflow_item = item;
	  break;
	}
	if(mprtps->rtcp_out_subflow_item == NULL){
		GST_WARNING_OBJECT(mprtps, "Static RTCP outpad can not be set, because there is no subflow with ID: %d", mprtps->rtcp_out_subflow_id);
	  return NULL;
	}
	return subflow->outpad;
}
*/

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

  otherpad = (pad == mprtps->rtcp_srcpad) ? mprtps->rtcp_sinkpad : mprtps->rtcp_srcpad;

  GST_DEBUG_OBJECT(mprtps,"B otherpad: %s", gst_pad_get_name(otherpad));
  if(otherpad == mprtps->rtcp_srcpad && !gst_pad_is_linked(mprtps->rtcp_srcpad)){
	otherpad = NULL;
	if(mprtps->subflows != NULL){
	  otherpad = ((MPRTPSSubflow*)mprtps->subflows->data)->outpad;
    }
  }
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
    	allowed = gst_pad_query_caps(((MPRTPSSubflow*)mprtps->subflows->data)->outpad, caps);
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

  otherpad = (pad == mprtps->rtcp_srcpad) ? mprtps->rtcp_sinkpad : mprtps->rtcp_srcpad;
  GST_DEBUG_OBJECT(mprtps,"B otherpad: %s", gst_pad_get_name(otherpad));
  if(otherpad == mprtps->rtcp_srcpad && !gst_pad_is_linked(mprtps->rtcp_srcpad)){
	otherpad = NULL;
	if(mprtps->subflows != NULL){
      otherpad = ((MPRTPSSubflow*)mprtps->subflows->data)->outpad;
	}
  }
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
  if(!gst_pad_is_linked(mprtps->rtcp_srcpad)){
  	if(mprtps->subflows != NULL){
        otherpad = ((MPRTPSSubflow*)mprtps->subflows->data)->outpad;
  	}
  }else{
	otherpad = mprtps->rtcp_srcpad;
  }
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
  if(!gst_pad_is_linked(mprtps->rtcp_srcpad)){
    if(mprtps->subflows != NULL){
        otherpad = ((MPRTPSSubflow*)mprtps->subflows->data)->outpad;
    }
  }else{
  	otherpad = mprtps->rtcp_srcpad;
  }
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

  otherpad = (pad == mprtps->rtcp_srcpad) ? mprtps->rtcp_sinkpad : mprtps->rtcp_srcpad;
  if(!gst_pad_is_linked(mprtps->rtcp_srcpad)){
    if(mprtps->subflows != NULL){
	  otherpad = ((MPRTPSSubflow*)mprtps->subflows->data)->outpad;
    }
  }
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
  if(!gst_pad_is_linked(mprtps->rtcp_srcpad)){
    if(mprtps->subflows != NULL){
  	  otherpad = ((MPRTPSSubflow*)mprtps->subflows->data)->outpad;
    }
  }else{
  	otherpad = mprtps->rtcp_srcpad;
  }
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



#undef SUBFLOW_LOCK
#undef SUBFLOW_UNLOCK
#undef MPRTP_SENDER_DEFAULT_CHARGE_VALUE
#undef MPRTP_SENDER_DEFAULT_ALPHA_VALUE
#undef MPRTP_SENDER_DEFAULT_BETA_VALUE
#undef MPRTP_SENDER_DEFAULT_GAMMA_VALUE
#undef MPRTP_SENDER_DEFAULT_EXTENSION_HEADER_ID
#undef MPRTP_SENDER_DEFAULT_MPRTCP_MTU
