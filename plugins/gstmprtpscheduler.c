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
#include "mprtpssubflow.h"
#include "schtree.h"
#include "gstmprtcpbuffer.h"

GST_DEBUG_CATEGORY_STATIC (gst_mprtpscheduler_debug_category);
#define GST_CAT_DEFAULT gst_mprtpscheduler_debug_category

#define PACKET_IS_RTP(b) (b > 0x7f && b < 0xc0)

#define MPRTP_SENDER_MUTEX_PTR(mprtps_ptr) &mprtps_ptr->base_object.object.lock
#define THIS_LOCK(mprtps_ptr) g_mutex_lock(&mprtps_ptr->subflows_mutex)
#define THIS_UNLOCK(mprtps_ptr) g_mutex_unlock(&mprtps_ptr->subflows_mutex)

#define GST_MPRTCP_BUFFER_FOR_PACKETS(b,buffer,packet) \
  for ((b) = gst_rtcp_buffer_get_first_packet ((buffer), (packet)); (b); \
          (b) = gst_rtcp_packet_move_to_next ((packet)))

#define MPRTP_SENDER_DEFAULT_CHARGE_VALUE 1.0
#define MPRTP_SENDER_DEFAULT_ALPHA_VALUE 0.5
#define MPRTP_SENDER_DEFAULT_BETA_VALUE 0.1
#define MPRTP_SENDER_DEFAULT_GAMMA_VALUE 0.2
#define MPRTP_SENDER_DEFAULT_EXTENSION_HEADER_ID 3
#define MPRTP_SENDER_DEFAULT_MPRTCP_MTU 1400


static void gst_mprtpscheduler_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_mprtpscheduler_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_mprtpscheduler_dispose (GObject * object);
static void gst_mprtpscheduler_finalize (GObject * object);

static GstStateChangeReturn
gst_mprtpscheduler_change_state (GstElement * element, GstStateChange transition);
static gboolean gst_mprtpscheduler_query (GstElement * element, GstQuery * query);

static GstFlowReturn gst_mprtpscheduler_rtp_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buffer);
static GstFlowReturn gst_mprtpscheduler_rtp_sink_chainlist (GstPad *pad, GstObject *parent, GstBufferList *list);

static GstFlowReturn gst_mprtpscheduler_mprtcp_rr_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *outbuf);
static gboolean		 gst_mprtpscheduler_rtp_sink	(GstPad *pad, GstObject *parent,
                                                                 GstEvent *event);
static void gst_mprtp_sender_scheduler_run (void *data);
static gboolean gst_mprtpscheduler_sink_query(GstPad *pad, GstObject *parent,
        GstQuery *query);
static void
gst_mprtp_sender_mprtcp_riporter_run (void *data);


static void
_join_subflow(GstMprtpscheduler* this, guint subflow_id);

static void
_detach_subflow(GstMprtpscheduler* this, guint subflow_id);

static MPRTPSSubflow*
_get_next_subflow(GstMprtpscheduler *this, GstRTPBuffer *rtp);
static void _delete_subflow(GstMprtpscheduler* this, MPRTPSSubflow *subflow);


enum
{
  PROP_0,
  PROP_CHARGE,
  PROP_ALPHA,
  PROP_BETA,
  PROP_GAMMA,
  PROP_MPRTCP_MTU,
  PROP_EXT_HEADER_ID,
  PROP_JOIN_SUBFLOW,
  PROP_DETACH_SUBFLOW,
  PROP_SUBFLOW_RIPORTS_ENABLED,
  PROP_SUBFLOW_MANUAL_SENDING_RATE_ENABLED,
  PROP_SUBFLOW_RATE,
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
    GST_STATIC_CAPS ("ANY")
    );


static GstStaticPadTemplate gst_mprtpscheduler_mprtcp_rr_sink_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_rr_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
	GST_STATIC_CAPS ("ANY")
    );


static GstStaticPadTemplate gst_mprtpscheduler_mprtcp_sr_src_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_sr_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
	GST_STATIC_CAPS ("ANY")
    );


/* class initialization */
G_DEFINE_TYPE_WITH_CODE (GstMprtpscheduler, gst_mprtpscheduler, GST_TYPE_ELEMENT,
  GST_DEBUG_CATEGORY_INIT (gst_mprtpscheduler_debug_category, "mprtpscheduler", 0,
  "debug category for mprtpscheduler element"));

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

  g_type_class_add_private (klass, sizeof (GstMprtpschedulerPrivate));
  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpscheduler_rtp_sink_template));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpscheduler_mprtp_src_template));

  gst_element_class_add_pad_template (element_class,
          gst_static_pad_template_get (&gst_mprtpscheduler_mprtcp_sr_src_template));

  gst_element_class_add_pad_template (element_class,
          gst_static_pad_template_get (&gst_mprtpscheduler_mprtcp_rr_sink_template));


  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS(klass),
      "MPRTP Scheduler", "Generic", "MPRTP scheduler FIXME LONG DESC",
      "Balázs Kreith <balazs.kreith@gmail.com>");

  gobject_class->set_property = gst_mprtpscheduler_set_property;
  gobject_class->get_property = gst_mprtpscheduler_get_property;
  gobject_class->dispose = gst_mprtpscheduler_dispose;
  gobject_class->finalize = gst_mprtpscheduler_finalize;
  element_class->change_state = GST_DEBUG_FUNCPTR (gst_mprtpscheduler_change_state);
  element_class->query = GST_DEBUG_FUNCPTR (gst_mprtpscheduler_query);


  g_object_class_install_property (gobject_class, PROP_JOIN_SUBFLOW,
              g_param_spec_uint ("join-subflow", "the subflow id requested to join",
                  "Join a subflow with a given id.", 0,
                  255, 0,
                  G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DETACH_SUBFLOW,
            g_param_spec_uint ("detach-subflow", "the subflow id requested to detach",
                "Detach a subflow with a given id.", 0,
                255, 0,
				G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SUBFLOW_RIPORTS_ENABLED,
            g_param_spec_boolean ("subflow-riports-enabled", "enable or disable the subflow riports",
                "Enable or Disable sending subflow SR riports", TRUE,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SUBFLOW_MANUAL_SENDING_RATE_ENABLED,
            g_param_spec_boolean ("subflow-manual-rate-enabled", "Enable or disable the subflow rate manual changes",
                "Enable or disable the subflow rate manual changes", FALSE,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SUBFLOW_RATE,
        g_param_spec_value_array ("subflow-rate", "Set subflow rate",
            "Setup the sending rate for a specific subflow",
            g_param_spec_uint ("rates", "subflow id and rate ",
                "Filter coefficient", 0, 255, 0.0,
                G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS),
			G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
}
/*
static gboolean
gst_mprtpscheduler_rtp_event_handler (GstPad *pad, GstObject *parent, GstEvent * event)
{

  GstMprtpscheduler *this = GST_MPRTPSCHEDULER(parent);
  MPRTPSSubflow *subflow;
  GList *it;
  gboolean result;
  THIS_LOCK(this);


  result = gst_pad_event_p
  for(it = this->subflows; it != NULL; it = it->next){
    subflow = it->data;
    subflow->push_event(subflow, event);
  }

gst_mprtpscheduler_rtp_event_handler_done:
  THIS_UNLOCK(this);
  return result;
}
*/


static gboolean
gst_mprtpscheduler_mprtp_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstMprtpscheduler *this;
  gboolean result;
//  const GstStructure *s;

//  GList *it;
//  MPRTPSSubflow *subflow;
//  guint subflow_id, event_value;

  this = GST_MPRTPSCHEDULER (parent);
  THIS_LOCK(this);
  switch(GST_EVENT_TYPE(event))
  {
//    case GST_EVENT_CUSTOM_UPSTREAM:
//      result = TRUE;
//      s = gst_event_get_structure (event);
//      if (!s || !gst_structure_has_name(s, "GstMpRtpSubflowEvent")){
//        break;
//      }
//      gst_structure_get_uint(s, "event", &event_value);
//	  gst_structure_get_uint(s, "subflow_id", &subflow_id);
//	  if(event_value == MPRTP_SENDER_SUBFLOW_EVENT_JOINED){
//	    subflow = make_mprtps_subflow(subflow_id);
//	    this->subflows = g_list_prepend(this->subflows, subflow);
//		subflow->set_event(subflow, MPRTP_SENDER_SUBFLOW_EVENT_JOINED);
//
//		break;
//	  }
//
//	  for(subflow = NULL, it = this->subflows; it != NULL; it = it->next){
//	    subflow = it->data;
//		if(subflow->get_id(subflow) == subflow_id){
//		  break;
//		}
//		subflow = NULL;
//      }
//      if(subflow == NULL){
//        break;
//      }
//      if(event_value == MPRTP_SENDER_SUBFLOW_EVENT_DETACHED){
//        subflow->set_event(subflow, MPRTP_SENDER_SUBFLOW_EVENT_DETACHED);
//      }
//	break;
    default:
      result = gst_pad_event_default (pad, parent, event);
  }
  THIS_UNLOCK(this);
  return result;
}

void _join_subflow(GstMprtpscheduler* this, guint subflow_id)
{
  MPRTPSSubflow *subflow;
  subflow = make_mprtps_subflow(subflow_id);
  this->subflows = g_list_prepend(this->subflows, subflow);

}

void _detach_subflow(GstMprtpscheduler* this, guint subflow_id)
{
  MPRTPSSubflow *subflow;
  GList *it;
  for(subflow = NULL, it = this->subflows; it != NULL; it = it->next){
  	subflow = it->data;
  	if(subflow->get_id(subflow) == subflow_id){
  	  break;
  	}
  	  subflow = NULL;
  }
  if(subflow == NULL){
	 GST_WARNING_OBJECT(this, "The requested subflow with an id (%d)"
			 " to detach was not found.", subflow_id);
     return;
  }
  subflow->set_detached(subflow);

}



static void
gst_mprtpscheduler_init (GstMprtpscheduler *mprtpscheduler)
{
    SchTree *schtree;
	mprtpscheduler->rtp_sinkpad = gst_pad_new_from_static_template (
			  &gst_mprtpscheduler_rtp_sink_template, "rtp_sink");

    gst_pad_set_chain_function (mprtpscheduler->rtp_sinkpad,
			GST_DEBUG_FUNCPTR(gst_mprtpscheduler_rtp_sink_chain));
    gst_pad_set_chain_list_function (mprtpscheduler->rtp_sinkpad,
			GST_DEBUG_FUNCPTR(gst_mprtpscheduler_rtp_sink_chainlist));
    gst_pad_set_event_function(mprtpscheduler->rtp_sinkpad,
    		gst_mprtpscheduler_rtp_sink);

    gst_element_add_pad (GST_ELEMENT(mprtpscheduler), mprtpscheduler->rtp_sinkpad);

	mprtpscheduler->mprtcp_rr_sinkpad = gst_pad_new_from_static_template (
			  &gst_mprtpscheduler_mprtcp_rr_sink_template, "mprtcp_rr_sink");

    gst_pad_set_chain_function (mprtpscheduler->mprtcp_rr_sinkpad,
			GST_DEBUG_FUNCPTR(gst_mprtpscheduler_mprtcp_rr_sink_chain));

    gst_pad_set_query_function(mprtpscheduler->rtp_sinkpad,
    		GST_DEBUG_FUNCPTR(gst_mprtpscheduler_sink_query));

    gst_element_add_pad (GST_ELEMENT(mprtpscheduler),
    		mprtpscheduler->mprtcp_rr_sinkpad);

	mprtpscheduler->mprtcp_sr_srcpad = gst_pad_new_from_static_template (
			  &gst_mprtpscheduler_mprtcp_sr_src_template, "mprtcp_sr_src");

    gst_element_add_pad (GST_ELEMENT(mprtpscheduler), mprtpscheduler->mprtcp_sr_srcpad);

    mprtpscheduler->mprtp_srcpad = gst_pad_new_from_static_template (
    		  &gst_mprtpscheduler_mprtp_src_template, "mprtp_src");
    gst_pad_set_event_function(mprtpscheduler->mprtp_srcpad,
    		GST_DEBUG_FUNCPTR(gst_mprtpscheduler_mprtp_src_event));

	gst_element_add_pad (GST_ELEMENT(mprtpscheduler), mprtpscheduler->mprtp_srcpad);

  mprtpscheduler->schtree = schtree = g_object_new (SCHTREE_TYPE, NULL);
  mprtpscheduler->charge_value = MPRTP_SENDER_DEFAULT_CHARGE_VALUE;
  mprtpscheduler->alpha_value = MPRTP_SENDER_DEFAULT_ALPHA_VALUE;
  mprtpscheduler->beta_value = MPRTP_SENDER_DEFAULT_BETA_VALUE;
  mprtpscheduler->gamma_value = MPRTP_SENDER_DEFAULT_GAMMA_VALUE;
  g_cond_init(&mprtpscheduler->scheduler_cond);
  g_mutex_init(&mprtpscheduler->subflows_mutex);
  mprtpscheduler->scheduler_last_run = 0;
  mprtpscheduler->no_active_subflows = TRUE;
  mprtpscheduler->scheduler_state = 0;
  mprtpscheduler->mprtcp_mtu = MPRTP_SENDER_DEFAULT_MPRTCP_MTU;
  mprtpscheduler->ext_header_id = MPRTP_SENDER_DEFAULT_EXTENSION_HEADER_ID;
  mprtpscheduler->ssrc = g_random_int();
  mprtpscheduler->flowable = FALSE;
  mprtpscheduler->subflows = NULL;
  mprtpscheduler->subflow_riports_enabled = TRUE;
  mprtpscheduler->manual_sending_rates_enabled = FALSE;

  schtree->setup_bid_values(schtree, 0.675);
/*
  mprtpscheduler->subflows = g_list_prepend(mprtpscheduler->subflows,
		  make_mprtps_subflow(1));
  mprtpscheduler->subflows = g_list_prepend(mprtpscheduler->subflows,
		  make_mprtps_subflow(2));
*/
  gst_segment_init (&mprtpscheduler->segment, GST_FORMAT_UNDEFINED);
  mprtpscheduler->priv = GST_MPRTPSCHEDULER_GET_PRIVATE(mprtpscheduler);
}

void
gst_mprtpscheduler_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMprtpscheduler *mprtpscheduler = GST_MPRTPSCHEDULER (object);
  GValueArray *array;
  GValue *v;
  guint subflow_id, rate;
  MPRTPSSubflow *subflow;
  GList *it;
  SchTree *schtree;
  GST_DEBUG_OBJECT (mprtpscheduler, "set_property");

  switch (property_id) {
    case PROP_CHARGE:
      THIS_LOCK(mprtpscheduler);
  	  mprtpscheduler->charge_value = g_value_get_float(value);
  	  THIS_UNLOCK(mprtpscheduler);
  	  break;
    case PROP_ALPHA:
      THIS_LOCK(mprtpscheduler);
      mprtpscheduler->alpha_value = g_value_get_float(value);
      THIS_UNLOCK(mprtpscheduler);
      break;
    case PROP_BETA:
      THIS_LOCK(mprtpscheduler);
      mprtpscheduler->beta_value = g_value_get_float(value);
      THIS_UNLOCK(mprtpscheduler);
      break;
    case PROP_GAMMA:
      THIS_LOCK(mprtpscheduler);
      mprtpscheduler->gamma_value = g_value_get_float(value);
      THIS_UNLOCK(mprtpscheduler);
      break;
    case PROP_EXT_HEADER_ID:
	  THIS_LOCK(mprtpscheduler);
	  mprtpscheduler->ext_header_id = g_value_get_int(value);
	  THIS_UNLOCK(mprtpscheduler);
	  break;
    case PROP_MPRTCP_MTU:
	  THIS_LOCK(mprtpscheduler);
	  mprtpscheduler->mprtcp_mtu = g_value_get_uint(value);
	  THIS_UNLOCK(mprtpscheduler);
	  break;
    case PROP_JOIN_SUBFLOW:
      THIS_LOCK(mprtpscheduler);
      _join_subflow(mprtpscheduler, g_value_get_uint(value));
      THIS_UNLOCK(mprtpscheduler);
    break;
    case PROP_DETACH_SUBFLOW:
	  THIS_LOCK(mprtpscheduler);
	  _detach_subflow(mprtpscheduler, g_value_get_uint(value));
	  THIS_UNLOCK(mprtpscheduler);
	break;
    case PROP_SUBFLOW_MANUAL_SENDING_RATE_ENABLED:
	  THIS_LOCK(mprtpscheduler);
	  mprtpscheduler->manual_sending_rates_enabled = g_value_get_boolean(value);
	  THIS_UNLOCK(mprtpscheduler);
	break;
    case PROP_SUBFLOW_RIPORTS_ENABLED:
	  THIS_LOCK(mprtpscheduler);
	  mprtpscheduler->subflow_riports_enabled = g_value_get_boolean(value);
	  THIS_UNLOCK(mprtpscheduler);
	break;
    case PROP_SUBFLOW_RATE:
      THIS_LOCK(mprtpscheduler);
      schtree = mprtpscheduler->schtree;
      array = g_value_dup_boxed (value);
      v = g_value_array_get_nth (array, 0);
      subflow_id = g_value_get_uint (v);
      v = g_value_array_get_nth (array, 1);
      rate = g_value_get_uint (v);
      for(it = mprtpscheduler->subflows; it != NULL; it = it->next){
    	subflow = it->data;
    	if(subflow->get_id(subflow) == subflow_id){
    	  schtree->setup_sending_rate(schtree, subflow, (gfloat)rate);
    	}
      }
      THIS_UNLOCK(mprtpscheduler);
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
  GstMprtpscheduler *mprtpscheduler = GST_MPRTPSCHEDULER (object);

  GST_DEBUG_OBJECT (mprtpscheduler, "get_property");

  switch (property_id) {
  case PROP_CHARGE:
	  THIS_LOCK(mprtpscheduler);
 	  g_value_set_float(value, mprtpscheduler->charge_value);
 	  THIS_UNLOCK(mprtpscheduler);
 	break;
  case PROP_ALPHA:
	  THIS_LOCK(mprtpscheduler);
 	  g_value_set_float(value, mprtpscheduler->alpha_value);
 	  THIS_UNLOCK(mprtpscheduler);
 	break;
  case PROP_BETA:
	  THIS_LOCK(mprtpscheduler);
 	  g_value_set_float(value, mprtpscheduler->beta_value);
 	  THIS_UNLOCK(mprtpscheduler);
 	break;
  case PROP_GAMMA:
	  THIS_LOCK(mprtpscheduler);
 	  g_value_set_float(value, mprtpscheduler->gamma_value);
   	  THIS_UNLOCK(mprtpscheduler);
 	break;
  case PROP_EXT_HEADER_ID:
	  THIS_LOCK(mprtpscheduler);
	  g_value_set_int(value, mprtpscheduler->ext_header_id);
	  THIS_UNLOCK(mprtpscheduler);
	break;
  case PROP_MPRTCP_MTU:
  	  THIS_LOCK(mprtpscheduler);
  	  g_value_set_uint(value, mprtpscheduler->mprtcp_mtu);
  	  THIS_UNLOCK(mprtpscheduler);
  	  break;
  case PROP_SUBFLOW_RIPORTS_ENABLED:
 	  THIS_LOCK(mprtpscheduler);
 	  g_value_set_boolean(value, mprtpscheduler->subflow_riports_enabled);
 	  THIS_UNLOCK(mprtpscheduler);
 	break;
  case PROP_SUBFLOW_MANUAL_SENDING_RATE_ENABLED:
 	  THIS_LOCK(mprtpscheduler);
 	  g_value_set_boolean(value, mprtpscheduler->manual_sending_rates_enabled);
 	  THIS_UNLOCK(mprtpscheduler);
 	break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  break;
  }
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





static GstStateChangeReturn
gst_mprtpscheduler_change_state (GstElement * element, GstStateChange transition)
{
  GstMprtpscheduler *mprtpscheduler;
  GstStateChangeReturn ret;

  g_return_val_if_fail (GST_IS_MPRTPSCHEDULER (element), GST_STATE_CHANGE_FAILURE);
  mprtpscheduler = GST_MPRTPSCHEDULER (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
	  mprtpscheduler->scheduler = gst_task_new (gst_mprtp_sender_scheduler_run, mprtpscheduler, NULL);
	  g_rec_mutex_init (&mprtpscheduler->scheduler_mutex);
	  gst_task_set_lock (mprtpscheduler->scheduler, &mprtpscheduler->scheduler_mutex);
	  mprtpscheduler->riporter = gst_task_new (gst_mprtp_sender_mprtcp_riporter_run, mprtpscheduler, NULL);
	  g_rec_mutex_init (&mprtpscheduler->riporter_mutex);
	  gst_task_set_lock (mprtpscheduler->riporter, &mprtpscheduler->riporter_mutex);
	  mprtpscheduler->scheduler_state = 1;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      gst_task_start (mprtpscheduler->scheduler);
      gst_task_start (mprtpscheduler->riporter);
      mprtpscheduler->scheduler_state = 2;
      break;
    default:
      break;
  }


  ret = GST_ELEMENT_CLASS (gst_mprtpscheduler_parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
        gst_task_stop (mprtpscheduler->scheduler);
        gst_task_stop (mprtpscheduler->riporter);
        gst_task_join (mprtpscheduler->scheduler);
        gst_task_join (mprtpscheduler->riporter);
      mprtpscheduler->scheduler_state = 1;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (gst_task_get_state (mprtpscheduler->scheduler) != GST_TASK_STOPPED)
	    GST_ERROR ("task %p should be stopped by now", mprtpscheduler->scheduler);
		gst_object_unref (mprtpscheduler->scheduler);
		mprtpscheduler->scheduler = NULL;
		g_rec_mutex_clear (&mprtpscheduler->scheduler_mutex);
		gst_object_unref (mprtpscheduler->riporter);
		mprtpscheduler->riporter = NULL;
		g_rec_mutex_clear (&mprtpscheduler->riporter_mutex);
	  mprtpscheduler->scheduler_state = 0;
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_mprtpscheduler_rtp_sink	(GstPad *pad, GstObject *parent, GstEvent *event)
{
  gboolean result;
  GstMprtpscheduler *this = GST_MPRTPSCHEDULER(parent);
  if(GST_EVENT_TYPE(event) == GST_EVENT_STREAM_START &&
     gst_task_get_state (this->scheduler) != GST_TASK_STARTED)
  {
    gst_task_start (this->scheduler);
	gst_task_start (this->riporter);
  }

  if(GST_EVENT_TYPE(event) == GST_EVENT_EOS &&
     gst_task_get_state (this->scheduler) != GST_TASK_STOPPED)
  {
    gst_task_stop (this->scheduler);
    gst_task_stop (this->riporter);
  }

  GstPad *peer = gst_pad_get_peer(this->mprtp_srcpad);
  result = gst_pad_send_event(peer, event);
  return result;
}



static gboolean
gst_mprtpscheduler_query (GstElement * element, GstQuery * query)
{
  GstMprtpscheduler *this = GST_MPRTPSCHEDULER (element);
  gboolean ret;
  GstStructure *s;

  GST_DEBUG_OBJECT (this, "query");

  switch (GST_QUERY_TYPE (query)) {
  case GST_QUERY_CUSTOM:
      	if(!gst_structure_has_name(s, GST_MPRTCP_SCHEDULER_SENT_BYTES_STRUCTURE_NAME)){
      		ret = GST_ELEMENT_CLASS (gst_mprtpscheduler_parent_class)->query (element, query);
      		break;
      	}
      	THIS_LOCK(this);
      	s = gst_query_writable_structure (query);
      	gst_structure_set (s,
      		GST_MPRTCP_SCHEDULER_SENT_OCTET_SUM_FIELD,
  			G_TYPE_UINT,
  			this->rtcp_sent_octet_sum,
  			NULL);
      	this->rtcp_sent_octet_sum = 0;
      	THIS_UNLOCK(this);
      	break;
    default:
      ret = GST_ELEMENT_CLASS (gst_mprtpscheduler_parent_class)->query (element, query);
      break;
  }

  return ret;
}

static GstFlowReturn
gst_mprtpscheduler_rtp_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
  MPRTPSSubflow *subflow;
  GstMprtpscheduler *this;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  GstBuffer *outbuf;
  GstFlowReturn result;
  guint8 first_byte;
  this = GST_MPRTPSCHEDULER (parent);
  GST_DEBUG_OBJECT(this, "chain function started");
  if (gst_buffer_extract (buffer, 0, &first_byte, 1) != 1) {
     GST_WARNING_OBJECT (this, "could not extract first byte from buffer");
     gst_buffer_unref (buffer);
     return GST_FLOW_OK;
  }

  if(!PACKET_IS_RTP(first_byte)){
    GST_WARNING_OBJECT(this, "Not RTP Packet arrived ar rtp_sink");
    return gst_pad_push(this->mprtp_srcpad, buffer);
  }

  //the packet is rtp
  outbuf = gst_buffer_make_writable (buffer);
  if (G_UNLIKELY (!gst_rtp_buffer_map(outbuf, GST_MAP_READWRITE, &rtp))){
	GST_WARNING_OBJECT(this, "The RTP packet is not writeable");
	return GST_FLOW_ERROR;
  }
  THIS_LOCK(this);

  if(this->no_active_subflows){
	  g_print("no active subflow");
    g_cond_wait(&this->scheduler_cond, &this->subflows_mutex);
  }
  //subflow = schtree->get_next(schtree);
  subflow = _get_next_subflow(this, &rtp);
  subflow->process_rtpbuf_out(subflow, this->ext_header_id, &rtp);
  //g_print("sent out on %d\n",subflow->get_id(subflow));
  GST_DEBUG_OBJECT(this, "selected subflow is %d", subflow->get_id(subflow));

  //gst_print_rtp_packet_info(&rtp);
  gst_rtp_buffer_unmap (&rtp);
  if(!this->flowable){
	  this->flowable = TRUE;
  }
  result = gst_pad_push(this->mprtp_srcpad, outbuf);

  //result = subflow->push_buffer(subflow, outbuf);
  GST_DEBUG_OBJECT(this, "packet sent out on %d, chain is going to be unlocked", subflow->get_id(subflow));

  THIS_UNLOCK(this);

  GST_DEBUG_OBJECT(this, "chain is unlocked");

  return result;
}

MPRTPSSubflow* _get_next_subflow(GstMprtpscheduler *this, GstRTPBuffer *rtp)
{
  SchTree *schtree = this->schtree;
  MPRTPSSubflow *result;

  result = schtree->get_next(schtree);
  if(!GST_BUFFER_FLAG_IS_SET(rtp->buffer, GST_BUFFER_FLAG_DELTA_UNIT)
	 // || gst_rtp_buffer_get_marker(rtp)
	)
  {
    if(schtree->has_nc_nl_path(schtree)){
      gboolean is_nc = result->is_non_congested(result);
      gboolean is_nl = result->is_non_lossy(result);
      while(!is_nc || !is_nl){
        result = schtree->get_next(schtree);
        is_nc = result->is_non_congested(result);
        is_nl = result->is_non_lossy(result);
      }
    }
  }

  return result;

}
static GstFlowReturn
gst_mprtpscheduler_rtp_sink_chainlist (GstPad *pad, GstObject *parent, GstBufferList *list)
{
  GstMprtpscheduler *mprtpscheduler;
  GstBuffer *buffer;
  gint i,len;
  GstFlowReturn result;
  mprtpscheduler = GST_MPRTPSCHEDULER (parent);

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

   //gst_buffer_list_unref (list);

   return result;
}


static GstFlowReturn
gst_mprtpscheduler_mprtcp_rr_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
	GstMprtpscheduler *this;
	GstFlowReturn result = GST_FLOW_OK;
	MPRTPSSubflow *subflow;
	GList *it;
    GstRTCPBuffer rtcp = {NULL, };
    GstRTCPHeader *header;
    GstMPRTCPSubflowRiport *riport;
    GstMPRTCPSubflowBlock *block;
    guint16 subflow_id;
    guint8 pt;

	this = GST_MPRTPSCHEDULER (parent);
	THIS_LOCK(this);

    if(!gst_rtcp_buffer_map(buffer, GST_MAP_READ, &rtcp)){
      GST_ERROR_OBJECT(this,"mprtcp buffer is not readable");
      goto gst_mprtpscheduler_mprtcp_sink_chain_done;
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

gst_mprtpscheduler_mprtcp_sink_chain_done:
    THIS_UNLOCK(this);
	return result;
}


static gboolean gst_mprtpscheduler_sink_query(GstPad *sinkpad, GstObject *parent,
        GstQuery *query)
{
	GstMprtpscheduler *this = GST_MPRTPSCHEDULER (parent);
	gboolean result;
	GST_DEBUG_OBJECT (this, "query");
	switch (GST_QUERY_TYPE (query)) {

	default:
	    result = gst_pad_peer_query(this->mprtp_srcpad, query);
	  break;
	}

	return result;
}


static void _set_charge_value(GstMprtpscheduler* this, gfloat *charge_value)
{
  GList *it;
  MPRTPSSubflow *subflow;
  guint non_congested_paths_num = 0, other_paths_num = 0;
  gfloat charge_start_value = 1.0;
  gfloat sending_bid;
  gfloat non_congested_sending_bid_sum = 0.0, other_sending_bid_sum = 0.0;

  if(*charge_value > 0.0){
    return;
  }

  //determine the charge_value

  for(it = this->subflows; it != NULL; it = it->next){
    subflow = (MPRTPSSubflow*) it->data;
    sending_bid = subflow->get_sending_bid(subflow);
    if(subflow->is_non_congested(subflow)){
      ++non_congested_paths_num;
      non_congested_sending_bid_sum += sending_bid;
    }else{
      ++other_paths_num;
      other_sending_bid_sum += sending_bid;
    }
  }

  if(non_congested_paths_num > 0){
    *charge_value = non_congested_sending_bid_sum / (non_congested_paths_num*2.0);
  }else if(other_paths_num > 0){
    *charge_value = other_sending_bid_sum * 2.0;
  }

  if(*charge_value < 1.0){
	*charge_value = charge_start_value;
  }
  //g_print("charge_value: %f\n", charge_value);
}

static void _print_event(char *text, guint16 id, MPRTPSubflowEvent event)
{
  char result[255];
  switch(event)
  {
  case MPRTP_SENDER_SUBFLOW_EVENT_BID:
      sprintf(result, "BID");
      break;
  case MPRTP_SENDER_SUBFLOW_EVENT_CONGESTION:
      sprintf(result, "CONGESTION");
      break;
  case MPRTP_SENDER_SUBFLOW_EVENT_DISTORTION:
      sprintf(result, "DISTORTION");
      break;
  case MPRTP_SENDER_SUBFLOW_EVENT_KEEP:
      sprintf(result, "KEEP");
      break;
  case MPRTP_SENDER_SUBFLOW_EVENT_LATE:
      sprintf(result, "LATE");
      break;
  case MPRTP_SENDER_SUBFLOW_EVENT_LOSTS:
      sprintf(result, "LOSTS");
      break;
  case MPRTP_SENDER_SUBFLOW_EVENT_REFRESH:
      sprintf(result, "REFRESH");
      break;
  case MPRTP_SENDER_SUBFLOW_EVENT_SETTLED:
      sprintf(result, "SETTLED");
      break;
  case MPRTP_SENDER_SUBFLOW_EVENT_SETTLEMENT:
      sprintf(result, "SETTLEMENT");
      break;
  }
  if(event != MPRTP_SENDER_SUBFLOW_EVENT_KEEP){
    g_print("%s (%d): %s\n", text, id, result);
  }
}

void
gst_mprtp_sender_scheduler_run (void *data)
{
  GstMprtpscheduler* this = (GstMprtpscheduler*) data;
  SchTree* schtree = this->schtree;
  GstClockTime now;
  GstClockID clock_id;
  GstClockTime next_scheduler_time;
  GList *it;
  gfloat subflow_sending_bid;
  guint16 subflow_id;
  gfloat subflow_reload_value, charge_value;
  MPRTPSSubflow *subflow;
  MPRTPSubflowEvent subflow_event;
  gboolean has_active_subflow, had_active_subflows;
  gboolean commit_schtree_changes = FALSE;
  gdouble rand;
  GstEvent *event = NULL;
  MPRTPSubflowFlags state_before, state_after;
  has_active_subflow = FALSE;
  gboolean subflow_is_non_congested, subflow_is_non_lossy, subflow_is_active;
  guint congested_subflow_num = 0, lossy_subflow_num = 0;
  guint non_congested_non_lossy_subflow_num = 0;
  guint active_subflow_num = 0;
  GST_DEBUG_OBJECT(this, "Scheduler task is started");
  THIS_LOCK(this);
  had_active_subflows = !this->no_active_subflows;
  for(it = this->subflows; it != NULL; it = it->next)
  {
	subflow = it->data;
    //check existance
	if(subflow->is_new(subflow)){
		_set_charge_value(this, &charge_value);
		subflow->set_active(subflow);
		subflow->set_non_congested(subflow);
		subflow->set_non_lossy(subflow);
    	schtree->setup_sending_rate(schtree, subflow, charge_value);
		subflow->set_joined(subflow);
		has_active_subflow = TRUE;
		commit_schtree_changes = TRUE;
		continue;
    }
    if(subflow->is_detached(subflow)){
    	schtree->delete_path(schtree, subflow);
		commit_schtree_changes = TRUE;
		_delete_subflow(this, subflow);
		continue;
    }
    if(this->manual_sending_rates_enabled){
    	continue;
    }
    subflow_id = subflow->get_id(subflow);
    //check latency
	state_before = subflow->get_state(subflow);
    subflow_event = subflow->check_latency(subflow);
    _print_event("subflow->check_latency", subflow_id, subflow_event);
    switch(subflow_event)
    {
      case MPRTP_SENDER_SUBFLOW_EVENT_LATE:
    	  subflow->set_passive(subflow);
    	  subflow_sending_bid = 0.0;
		  commit_schtree_changes = TRUE;
		  goto setup_subflow_sending_rate;
      break;
      case MPRTP_SENDER_SUBFLOW_EVENT_SETTLED:
    	  subflow->set_active(subflow);
    	  if(!subflow->try_reload_from_passive(subflow, &subflow_sending_bid)){
		   _set_charge_value(this, &charge_value);
		   subflow_sending_bid = charge_value;
		 }
    	  commit_schtree_changes = TRUE;
    	  goto setup_subflow_sending_rate;
      break;
      default:
      break;
    }

    //check congestion
    subflow_event = subflow->check_congestion(subflow);
    _print_event("subflow->check_congestion", subflow_id, subflow_event);
    switch(subflow_event)
    {
	  case MPRTP_SENDER_SUBFLOW_EVENT_CONGESTION:
		 subflow->set_congested(subflow);
		 subflow_sending_bid = subflow->get_sending_bid(subflow) * this->beta_value;
		 commit_schtree_changes = TRUE;
		 goto setup_subflow_sending_rate;
	  break;
	  case MPRTP_SENDER_SUBFLOW_EVENT_SETTLED:
		 subflow->set_non_congested(subflow);
		 if(!subflow->try_reload_from_congested(subflow, &subflow_sending_bid)){
		   _set_charge_value(this, &charge_value);
		   subflow_sending_bid = charge_value;
		 }
		 commit_schtree_changes = TRUE;
		 goto setup_subflow_sending_rate;
	  break;
//	  case MPRTP_SENDER_SUBFLOW_EVENT_DISTORTION:
//		  subflow_sending_bid = subflow->get_goodput(subflow) *
//		                        (1.0 - this->alpha_value);
//		  //this->consecutive_non_distortions = 0;
//		  commit_schtree_changes = TRUE;
//		  goto setup_subflow_sending_rate;
//	  break;
	  default:
		  //++this->consecutive_non_distortions;
	  break;
	}

    //check lossy
	subflow_event = subflow->check_lossy(subflow);
	_print_event("subflow->check_lossy", subflow_id, subflow_event);
	switch(subflow_event)
	{
	  case MPRTP_SENDER_SUBFLOW_EVENT_LOSTS:
		 subflow->set_lossy(subflow);
		 schtree->setup_sending_rate(schtree, subflow, subflow_sending_bid);
		 commit_schtree_changes = TRUE;
		 goto setup_subflow_sending_rate;
	  break;
	  case MPRTP_SENDER_SUBFLOW_EVENT_SETTLED:
		 subflow->set_non_lossy(subflow);
		 if(!subflow->try_reload_from_lossy(subflow, &subflow_sending_bid)){
		   subflow_sending_bid = subflow->get_sending_bid(subflow);
		 }
		 commit_schtree_changes = TRUE;
		 goto setup_subflow_sending_rate;
	  break;
	  default:
	  break;
	}

//	subflow_event = subflow->check_monotocity(subflow);
//	switch(subflow_event)
//	{
//	  case MPRTP_SENDER_SUBFLOW_EVENT_REFRESH:
//		  commit_schtree_changes = TRUE;
//	  break;
//	  default:
//	  break;
//	}

	subflow_sending_bid = subflow->get_sending_bid(subflow);

	setup_subflow_sending_rate:
	subflow_is_active = subflow->is_active(subflow);
	subflow_is_non_congested = !subflow->is_non_congested(subflow);
	subflow_is_non_lossy = !subflow->is_non_lossy(subflow);
	if(subflow_is_active){
//	  if(subflow_is_non_congested && subflow_is_non_lossy){
//		++non_congested_non_lossy_subflow_num;
//	  }else if(subflow_is_non_congested){
//		++lossy_subflow_num;
//	  }else{
//		++congested_subflow_num;
//	  }
//	  ++active_subflow_num;
	  has_active_subflow = TRUE;
	}
	state_after = subflow->get_state(subflow);
    schtree->setup_sending_rate(schtree, subflow, subflow_sending_bid);

//    g_print("subflow %d is active: %d; non-congested: %d; non-lossy: %d\n",
//    		subflow->get_id(subflow), subflow->is_active(subflow),
//			subflow->is_non_congested(subflow), subflow->is_non_lossy(subflow));
	if(state_before != state_after){
	  event = gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM,
			gst_structure_new ("GstMpRtpSubflowState",
				"subflow_id", G_TYPE_UINT, subflow->get_id(subflow),
				"state", G_TYPE_UINT, state_after, NULL));
	  gst_pad_push_event (this->mprtp_srcpad, event);
	}
  }

  if(non_congested_non_lossy_subflow_num < active_subflow_num){
    //increasing decreasing ratio!;
  }
  GstClock *sysclock = gst_system_clock_obtain();
  now = gst_clock_get_time(sysclock);
  if(this->last_schtree_commit < now - 60 * GST_SECOND){
    commit_schtree_changes = TRUE;
  }
  for(it = this->subflows; it != NULL; it = it->next){
  		  subflow = it->data;
  		  g_print("%lu,%p,subflow %d,%d\n", now, this, subflow->get_id(subflow),
  		  	          		subflow->get_sent_packet_num(subflow));
  	  }

  if(commit_schtree_changes){
    schtree->commit_changes(schtree);
    this->last_schtree_commit = now;
  }

  this->no_active_subflows = !has_active_subflow;
//
//  g_print("this->no_active_subflows: %d had_active_subflows: %d has_active_subflows: %d\n",
//		  this->no_active_subflows, had_active_subflows, has_active_subflow);
  if(this->no_active_subflows == FALSE && had_active_subflows == FALSE){
	 g_cond_signal(&this->scheduler_cond);
  }

  this->scheduler_last_run = now;

gst_mprtp_sender_scheduler_run_done:
  rand = g_random_double();
  next_scheduler_time = now + GST_SECOND * (0.5 + rand);
  GST_DEBUG_OBJECT(this, "Next scheduling interval time is %u", next_scheduler_time);
  clock_id = gst_clock_new_single_shot_id (sysclock, next_scheduler_time);

  THIS_UNLOCK(this);

  if(gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED){
    GST_WARNING_OBJECT(this, "The scheduler clock wait is interrupted");
  }
  g_object_unref (sysclock);
  gst_clock_id_unref (clock_id);

}



void
gst_mprtp_sender_mprtcp_riporter_run (void *data)
{
  GstMprtpscheduler* this = (GstMprtpscheduler*) data;
  GstClockTime now;
  GstClockTimeDiff clock_jitter;
  GstClockID clock_id;
  GstClockTime next_scheduler_time;
  GList *it;
  MPRTPSSubflow *subflow;
  GstBuffer *outbuf;
  GstRTCPBuffer rtcp = {NULL, };
  guint64 next_riport_time;
  GstRTCPHeader *header = NULL;
  GstMPRTCPSubflowRiport *riport;
  gboolean compound_sending, first = TRUE;
  guint rtcp_packet_size, rtcp_packet_size_compound = 0;


  GstClock *sysclock = gst_system_clock_obtain();
  now = gst_clock_get_time(sysclock);
  THIS_LOCK(this);
  if(!this->subflow_riports_enabled){
	  goto gst_mprtpreceiver_mprtcp_riporter_run_end;
  }

  compound_sending = FALSE; //this->mprtcp_riport_compound_sending;
  for(first = TRUE, it = this->subflows; it != NULL; it = it->next){
    subflow = it->data;
    if(!subflow->do_riport_now(subflow, &next_scheduler_time)){
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

    rtcp_packet_size = rtcp.map.size + (28<<3) /*UDP header size with IP header*/;
    subflow->set_avg_rtcp_size(subflow, rtcp_packet_size);
    rtcp_packet_size_compound+=rtcp_packet_size;


    if(compound_sending){
      continue;
    }
    gst_rtcp_buffer_unmap(&rtcp);
    if(this->flowable){
      this->rtcp_sent_octet_sum += rtcp_packet_size>>3;
      gst_pad_push(this->mprtcp_sr_srcpad, outbuf);
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
	this->rtcp_sent_octet_sum += rtcp_packet_size_compound>>3;
    gst_pad_push(this->mprtcp_sr_srcpad, outbuf);
  }

gst_mprtpreceiver_mprtcp_riporter_run_end:
  THIS_UNLOCK(this);

  next_scheduler_time = now + GST_MSECOND * 100;
  clock_id = gst_clock_new_single_shot_id (sysclock, next_scheduler_time);
  if(gst_clock_id_wait (clock_id, &clock_jitter) == GST_CLOCK_UNSCHEDULED){
    GST_WARNING_OBJECT(this, "The scheduler clock wait is interrupted");
  }
  gst_clock_id_unref (clock_id);
  g_object_unref (sysclock);

}

void _delete_subflow(GstMprtpscheduler* this, MPRTPSSubflow *subflow)
{
  this->subflows = g_list_remove(this->subflows, subflow);
  g_object_unref((GObject*)subflow);
}


#undef THIS_LOCK
#undef THIS_UNLOCK
#undef MPRTP_SENDER_DEFAULT_CHARGE_VALUE
#undef MPRTP_SENDER_DEFAULT_ALPHA_VALUE
#undef MPRTP_SENDER_DEFAULT_BETA_VALUE
#undef MPRTP_SENDER_DEFAULT_GAMMA_VALUE
#undef MPRTP_SENDER_DEFAULT_EXTENSION_HEADER_ID
#undef MPRTP_SENDER_DEFAULT_MPRTCP_MTU
#undef PACKET_IS_RTP
