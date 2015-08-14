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
#include <gst/gst.h>
#include "gstmprtpreceiver.h"
#include "gstmprtcpbuffer.h"
#include "mprtprsubflow.h"

GST_DEBUG_CATEGORY_STATIC (gst_mprtpreceiver_debug_category);
#define GST_CAT_DEFAULT gst_mprtpreceiver_debug_category

#define MPRTPR_LOCK(mprtpr_ptr) g_mutex_lock(&mprtpr_ptr->mutex)
#define MPRTPR_UNLOCK(mprtpr_ptr) g_mutex_unlock(&mprtpr_ptr->mutex)

#define MPRTP_RECEIVER_DEFAULT_EXTENSION_HEADER_ID 3
#define MPRTP_RECEIVER_DEFAULT_SSRC 0
/* prototypes */

typedef struct _MPRTPRSubflowHeaderExtension{
  guint16 id;
  guint16 sequence;
}MPRTPRSubflowHeaderExtension;

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

static GstPadLinkReturn gst_mprtpreceiver_sink_link (GstPad *pad, GstObject *parent, GstPad *peer);
static void gst_mprtpreceiver_sink_unlink (GstPad *pad, GstObject *parent);
static GstFlowReturn gst_mprtpreceiver_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buffer);
static GstFlowReturn gst_mprtpreceiver_rtcp_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buffer);
static void gst_mprtpreceiver_mprtcp_riporter_run (void *data);
static void gst_mprtpreceiver_playouter_run (void *data);

static gboolean _select_subflow(GstMprtpreceiver *this, guint16 id,
		MPRTPRSubflow **result);
static void _processing_mprtcp_packet(GstMprtpreceiver *mprtpr, GstBuffer *buf);
static void _processing_mprtp_packet(GstMprtpreceiver *mprtpr, GstBuffer *buf);
static GList* _merge_lists(GList *F, GList *L);
static gint _cmp_seq(guint16 x, guint16 y);


enum
{
  PROP_0,
  PROP_EXT_HEADER_ID,
  PROP_PIVOT_SSRC,
  PROP_PIVOT_CLOCK_RATE
};

/* pad templates */

static GstStaticPadTemplate gst_mprtpreceiver_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
	GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtp;application/x-rtcp;application/x-srtcp")
    );


static GstStaticPadTemplate gst_mprtpreceiver_rtcp_sink_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_sink",
    GST_PAD_SINK,
	GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
    );

static GstStaticPadTemplate gst_mprtpreceiver_rtp_src_template =
GST_STATIC_PAD_TEMPLATE ("rtp_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate gst_mprtpreceiver_rtcp_src_template =
GST_STATIC_PAD_TEMPLATE ("rtcp_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
    );


static GstStaticPadTemplate gst_mprtpreceiver_mprtcp_src_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
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
      gst_static_pad_template_get (&gst_mprtpreceiver_rtp_src_template));
  gst_element_class_add_pad_template (element_class,
       gst_static_pad_template_get (&gst_mprtpreceiver_rtcp_src_template));
  gst_element_class_add_pad_template (element_class,
       gst_static_pad_template_get (&gst_mprtpreceiver_mprtcp_src_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS(klass),
      "FIXME Long name", "Generic", "FIXME Description",
      "FIXME <fixme@example.com>");

  gobject_class->set_property = gst_mprtpreceiver_set_property;
  gobject_class->get_property = gst_mprtpreceiver_get_property;
  gobject_class->dispose = gst_mprtpreceiver_dispose;
  gobject_class->finalize = gst_mprtpreceiver_finalize;

  g_object_class_install_property (gobject_class, PROP_PIVOT_CLOCK_RATE,
          g_param_spec_uint ("pivot-clock-rate", "Clock rate of the pivot stream",
              "Sets the clock rate of the pivot stream used for calculating "
              "skew and playout delay at the receiver", 0,
              G_MAXUINT, 0,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_PIVOT_SSRC,
          g_param_spec_uint ("pivot-ssrc", "SSRC of the pivot stream",
              "Sets the ssrc of the pivot stream used selecting MPRTP packets "
              "for playout delay at the receiver", 0,
              G_MAXUINT, 0,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  element_class->request_new_pad = GST_DEBUG_FUNCPTR (gst_mprtpreceiver_request_new_pad);
  element_class->release_pad = GST_DEBUG_FUNCPTR (gst_mprtpreceiver_release_pad);
  element_class->change_state = GST_DEBUG_FUNCPTR (gst_mprtpreceiver_change_state);
  element_class->query = GST_DEBUG_FUNCPTR (gst_mprtpreceiver_query);
}

static void
gst_mprtpreceiver_init (GstMprtpreceiver *mprtpreceiver)
{

  mprtpreceiver->rtp_srcpad = gst_pad_new_from_static_template (&gst_mprtpreceiver_rtp_src_template,"rtp_src");
    gst_element_add_pad (GST_ELEMENT(mprtpreceiver), mprtpreceiver->rtp_srcpad);

  mprtpreceiver->rtcp_srcpad = gst_pad_new_from_static_template (&gst_mprtpreceiver_rtcp_src_template,"rtcp_src");
    gst_element_add_pad (GST_ELEMENT(mprtpreceiver), mprtpreceiver->rtcp_srcpad);

    mprtpreceiver->rtcp_sinkpad = gst_pad_new_from_static_template (&gst_mprtpreceiver_rtcp_sink_template,"rtcp_sink");
        gst_element_add_pad (GST_ELEMENT(mprtpreceiver), mprtpreceiver->rtcp_sinkpad);

	mprtpreceiver->mprtcp_srcpad = gst_pad_new_from_static_template (&gst_mprtpreceiver_mprtcp_src_template,"mprtcp_src");
		gst_element_add_pad (GST_ELEMENT(mprtpreceiver), mprtpreceiver->mprtcp_srcpad);

  gst_pad_set_chain_function (mprtpreceiver->rtcp_sinkpad,
    GST_DEBUG_FUNCPTR(gst_mprtpreceiver_rtcp_sink_chain));
    mprtpreceiver->ext_header_id = MPRTP_RECEIVER_DEFAULT_EXTENSION_HEADER_ID;
    mprtpreceiver->playout_delay = 0.0;
    mprtpreceiver->pivot_clock_rate = 0;
    mprtpreceiver->pivot_ssrc = MPRTP_RECEIVER_DEFAULT_SSRC;
    mprtpreceiver->path_skew_counter = 0;
    mprtpreceiver->ext_rtptime = -1;
    mprtpreceiver->path_skew_index = 0;
    g_mutex_init(&mprtpreceiver->mutex);
}

void
gst_mprtpreceiver_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMprtpreceiver *mprtpreceiver = GST_MPRTPRECEIVER (object);
  GList *it;
  MPRTPRSubflow *subflow;
  GST_DEBUG_OBJECT (mprtpreceiver, "set_property");

  switch (property_id) {
  case PROP_EXT_HEADER_ID:
	  MPRTPR_LOCK(mprtpreceiver);
	  mprtpreceiver->ext_header_id = g_value_get_int(value);
	  for(it = mprtpreceiver->subflows; it != NULL; it = it->next){
		subflow = it->data;
		subflow->ext_header_id = mprtpreceiver->ext_header_id;
	  }
	  MPRTPR_UNLOCK(mprtpreceiver);
	  break;
  case PROP_PIVOT_SSRC:
	  MPRTPR_LOCK(mprtpreceiver);
	  mprtpreceiver->pivot_ssrc = g_value_get_uint(value);
	  MPRTPR_UNLOCK(mprtpreceiver);
	  break;
  case PROP_PIVOT_CLOCK_RATE:
	  MPRTPR_LOCK(mprtpreceiver);
	  mprtpreceiver->pivot_clock_rate = g_value_get_uint(value);
	  MPRTPR_UNLOCK(mprtpreceiver);
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
  GstMprtpreceiver *mprtpreceiver = GST_MPRTPRECEIVER (object);

  GST_DEBUG_OBJECT (mprtpreceiver, "get_property");

  switch (property_id) {
  case PROP_EXT_HEADER_ID:
    	  MPRTPR_LOCK(mprtpreceiver);
    	  g_value_set_int(value, mprtpreceiver->ext_header_id);
    	  MPRTPR_UNLOCK(mprtpreceiver);
    	break;
  case PROP_PIVOT_CLOCK_RATE:
    	  MPRTPR_LOCK(mprtpreceiver);
    	  g_value_set_uint(value, mprtpreceiver->pivot_clock_rate);
    	  MPRTPR_UNLOCK(mprtpreceiver);
    	break;
  case PROP_PIVOT_SSRC:
    	  MPRTPR_LOCK(mprtpreceiver);
    	  g_value_set_uint(value, mprtpreceiver->pivot_ssrc);
    	  MPRTPR_UNLOCK(mprtpreceiver);
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
	    const gchar * name, const GstCaps* caps)
{

	GstPad *sinkpad;
	GstMprtpreceiver *mprtpr;
	MPRTPRSubflow *subflow;
	GList *it;
	guint16 subflow_id;
	mprtpr = GST_MPRTPRECEIVER (element);
	GST_DEBUG_OBJECT (mprtpr, "requesting pad");

	sscanf(name, "sink_%u", &subflow_id);

	MPRTPR_LOCK(mprtpr);

	for(it = mprtpr->subflows; it != NULL; it = it->next){
	  subflow = (MPRTPRSubflow*) it->data;
	  if(subflow->id == subflow_id){
		GST_WARNING_OBJECT(mprtpr, "Requested Pad subflow id(%d) is not unique", subflow_id);
		return NULL;
	  }
	}

	sinkpad = gst_pad_new_from_template (templ, name);
	gst_pad_set_link_function (sinkpad,
	            GST_DEBUG_FUNCPTR(gst_mprtpreceiver_sink_link));
	gst_pad_set_unlink_function (sinkpad,
	            GST_DEBUG_FUNCPTR(gst_mprtpreceiver_sink_unlink));
	gst_pad_set_chain_function (sinkpad,
	            GST_DEBUG_FUNCPTR(gst_mprtpreceiver_sink_chain));
	subflow = make_mprtpr_subflow(subflow_id, sinkpad, mprtpr->ext_header_id);
	mprtpr->subflows = g_list_prepend(mprtpr->subflows, subflow);
	MPRTPR_UNLOCK(mprtpr);

	gst_element_add_pad (GST_ELEMENT (mprtpr), sinkpad);

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
	  mprtpreceiver->riporter = gst_task_new (gst_mprtpreceiver_mprtcp_riporter_run, mprtpreceiver, NULL);
	  g_rec_mutex_init (&mprtpreceiver->riporter_mutex);
	  gst_task_set_lock (mprtpreceiver->riporter, &mprtpreceiver->riporter_mutex);

	  mprtpreceiver->playouter = gst_task_new (gst_mprtpreceiver_playouter_run, mprtpreceiver, NULL);
	  g_rec_mutex_init (&mprtpreceiver->playouter_mutex);
	  gst_task_set_lock (mprtpreceiver->playouter, &mprtpreceiver->playouter_mutex);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
		gst_task_start (mprtpreceiver->playouter);
		gst_task_start (mprtpreceiver->riporter);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (gst_mprtpreceiver_parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
        gst_task_stop (mprtpreceiver->playouter);
        gst_task_stop (mprtpreceiver->riporter);
        gst_task_join (mprtpreceiver->playouter);
        gst_task_join (mprtpreceiver->riporter);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
    	gst_object_unref (mprtpreceiver->playouter);
    	mprtpreceiver->playouter = NULL;
		g_rec_mutex_clear (&mprtpreceiver->playouter_mutex);
		gst_object_unref (mprtpreceiver->riporter);
		mprtpreceiver->riporter = NULL;
		g_rec_mutex_clear (&mprtpreceiver->riporter_mutex);
      break;
    default:
      break;
  }

  return ret;
}

void
gst_mprtpreceiver_playouter_run (void *data)
{
	GstMprtpreceiver *this = data;
	GstClockID clock_id;
	GstClockTime next_scheduler_time;
	GstClockTime now;
	GList *it;
	MPRTPRSubflow *subflow;
	gint i;
	guint64 path_skew;
	guint32 max_path_skew = 0;
	GList *F = NULL,*L = NULL, *P = NULL;
	GstBuffer *buf;

	MPRTPR_LOCK(this);

	now = gst_clock_get_time(GST_ELEMENT_CLOCK(this));
    for(it = this->subflows; it != NULL; it = it->next){
      subflow = it->data;
      if(!subflow->is_active(subflow)){
        continue;
      }
      F = subflow->get_packets(subflow);
      F = g_list_reverse(F);

      L = _merge_lists(F, L);
      path_skew = subflow->get_skews_median(subflow);
      //g_print("Median: %llu ", path_skew);
      this->path_skews[this->path_skew_index++] = path_skew;
      if(this->path_skew_index == 256){
        this->path_skew_index = 0;
      }
      ++this->path_skew_counter;
    }
    //F = g_list_reverse(L);
    /*
    P = L;
    if(P != NULL) g_print("\nMerged list: ");
    while(P != NULL){
      GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
      buf = P->data;
      gst_rtp_buffer_map(buf, GST_MAP_READ, &rtp);
      g_print("%d->",gst_rtp_buffer_get_seq(&rtp));
      gst_rtp_buffer_unmap(&rtp);
      //gst_pad_push(this->rtp_srcpad, buf);
      P = P->next;
    }
    if(L != NULL) g_print("\n");
    /**/
    while(L != NULL){
	  buf = L->data;
	  gst_pad_push(this->rtp_srcpad, buf);
	  L = L->next;
	}
    for(i=0; i < 256 && i < this->path_skew_counter; ++i){
    	if(max_path_skew < this->path_skews[i]){
    	  max_path_skew = this->path_skews[i];
    	}
    }

    this->playout_delay =
      ((gfloat)max_path_skew + 124.0 * this->playout_delay) / 125.0;

	next_scheduler_time = now + (guint64)this->playout_delay;
	MPRTPR_UNLOCK(this);

	clock_id = gst_clock_new_single_shot_id (GST_ELEMENT_CLOCK(this), next_scheduler_time);
	if(gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED){
	  GST_WARNING_OBJECT(this, "The playout clock wait is interrupted");
	}
	gst_clock_id_unref (clock_id);

}

void
gst_mprtpreceiver_mprtcp_riporter_run (void *data)
{
  GstMprtpreceiver *this = data;
  GstClockID clock_id;
  GstClockTime next_scheduler_time, next_riport_time;
  GstClockTime now;
  gboolean compound_sending, first;
  GList *it;
  MPRTPRSubflow *subflow;
  GstBuffer *outbuf;
  GstRTCPBuffer rtcp = {NULL,};
  GstRTCPHeader *header;
  GstMPRTCPSubflowRiport *riport;


  now = gst_clock_get_time(GST_ELEMENT_CLOCK(this));

  MPRTPR_LOCK(this);
  compound_sending = FALSE;
  for(first = TRUE, it = this->subflows; it != NULL; it = it->next){
    subflow = it->data;
    if(now < subflow->get_rr_riport_time(subflow) ||
     !subflow->is_active(subflow)){
     continue;
    }

    if(first == TRUE){
      outbuf = gst_rtcp_buffer_new(1400);
      gst_rtcp_buffer_map (outbuf, GST_MAP_READWRITE, &rtcp);
	  first = FALSE;
    }
    header = gst_rtcp_add_begin(&rtcp);
    riport = gst_mprtcp_add_riport(header);
    subflow->setup_rr_riport(subflow, riport);
    subflow->setup_xr_rfc2743_late_discarded_riport(subflow, riport);
    gst_rtcp_add_end(&rtcp, header);

    subflow->setup_rr_riport_time(subflow);

    if(compound_sending){
	  continue;
    }
    gst_rtcp_buffer_unmap(&rtcp);
    //g_print("sending %d\n", subflow->get_id(subflow));
    gst_pad_push(this->mprtcp_srcpad, outbuf);

    outbuf = gst_rtcp_buffer_new(1400);
    gst_rtcp_buffer_map (outbuf, GST_MAP_READWRITE, &rtcp);
  }
  if(first == FALSE){
    gst_rtcp_buffer_unmap(&rtcp);
  }

  if(compound_sending && first == FALSE){
	if(gst_pad_is_linked(this->mprtcp_srcpad)){
      gst_pad_push(this->mprtcp_srcpad, outbuf);
	}else{
	  GST_ERROR_OBJECT(this, "MPRTP Source is not linked");
	}
  }
  MPRTPR_UNLOCK(this);

  next_scheduler_time = now + GST_MSECOND * 100;
  clock_id = gst_clock_new_single_shot_id (GST_ELEMENT_CLOCK(this),
	next_scheduler_time);
  if(gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED){
  GST_WARNING_OBJECT(this, "The riporter clock wait is interrupted");
}
gst_clock_id_unref (clock_id);
}

static gboolean
gst_mprtpreceiver_query (GstElement * element, GstQuery * query)
{
  GstMprtpreceiver *mprtpreceiver = GST_MPRTPRECEIVER (element);
  gboolean ret;

  GST_DEBUG_OBJECT (mprtpreceiver, "query");
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
  GstMprtpreceiver *mprtpr;
  GstMapInfo info;
  GstPad *outpad = NULL;
  guint8 *data;

  mprtpr = GST_MPRTPRECEIVER (parent);
  GST_DEBUG_OBJECT(mprtpr, "RTP/RTCP/MPRTP/MPRTCP sink");
  MPRTPR_LOCK(mprtpr);

  if(!gst_buffer_map(buf, &info, GST_MAP_READ)){
  	GST_WARNING("Buffer is not readable");
  	MPRTPR_UNLOCK(mprtpr);
  	return GST_FLOW_ERROR;
  }
  data = info.data + 1;
  gst_buffer_unmap(buf, &info);
  //demultiplexing based on RFC5761
  if(*data == MPRTCP_PACKET_TYPE_IDENTIFIER){
	_processing_mprtcp_packet(mprtpr, buf);
	MPRTPR_UNLOCK(mprtpr);
	return GST_FLOW_OK;
  }

  //the packet is either rtcp or mprtp
  if(*data < 192 || *data > 223){
    _processing_mprtp_packet(mprtpr, buf);
    MPRTPR_UNLOCK(mprtpr);
    //return gst_pad_push(mprtpr->rtp_srcpad, buf);
    return GST_FLOW_OK;
  }

  //the packet is rtcp
  if(mprtpr->rtcp_srcpad == NULL){
	GST_ERROR_OBJECT(mprtpr, "No outpad");
	MPRTPR_UNLOCK(mprtpr);
	return GST_FLOW_ERROR;
  }
  if (!gst_pad_is_linked (mprtpr->rtcp_srcpad)) {
    GST_ERROR_OBJECT(mprtpr, "The rtcp src is not connected");
    MPRTPR_UNLOCK(mprtpr);
    return GST_FLOW_ERROR;
  }
  MPRTPR_UNLOCK(mprtpr);
  return gst_pad_push (mprtpr->rtcp_srcpad, buf);
}


static GstFlowReturn
gst_mprtpreceiver_rtcp_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buf)
{
  GstMprtpreceiver *mprtpr;

  mprtpr = GST_MPRTPRECEIVER (parent);
  GST_DEBUG_OBJECT(mprtpr, "RTCP/MPRTCP sink");
  MPRTPR_LOCK(mprtpr);

  //the packet is rtcp
  if(mprtpr->rtcp_srcpad == NULL){
	GST_ERROR_OBJECT(mprtpr, "No outpad");
	MPRTPR_UNLOCK(mprtpr);
	return GST_FLOW_ERROR;
  }
  if (!gst_pad_is_linked (mprtpr->rtcp_srcpad)) {
    GST_ERROR_OBJECT(mprtpr, "The rtcp src is not connected");
    MPRTPR_UNLOCK(mprtpr);
    return GST_FLOW_ERROR;
  }
  MPRTPR_UNLOCK(mprtpr);
  //g_print("RTCP packet is forwarded: %p\n", mprtpr->rtcp_srcpad);
  return gst_pad_push (mprtpr->rtcp_srcpad, buf);
}


void
_processing_mprtp_packet(GstMprtpreceiver *mprtpr, GstBuffer *buf)
{
	gpointer pointer = NULL;
	MPRTPRSubflowHeaderExtension *subflow_infos = NULL;
	MPRTPRSubflow *subflow;
	guint size;
	GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

	if (G_UNLIKELY (!gst_rtp_buffer_map(buf, GST_MAP_READ, &rtp))){
	  GST_WARNING_OBJECT(mprtpr, "The received Buffer is not readable");
	  gst_rtp_buffer_unmap(&rtp);
	  return;
	}

    //gst_print_rtp_packet_info(&rtp);

	if(!gst_rtp_buffer_get_extension(&rtp)){

		//Backward compatibility in a way to process rtp packet must be implemented here!!!

		GST_WARNING_OBJECT(mprtpr, "The received buffer extension bit is 0 thus it is not an MPRTP packet.");
		gst_rtp_buffer_unmap(&rtp);
		return;
	}
	//_print_rtp_packet_info(rtp);

	if(!gst_rtp_buffer_get_extension_onebyte_header(&rtp, mprtpr->ext_header_id, 0, &pointer, &size)){
		GST_WARNING_OBJECT(mprtpr, "The received buffer extension is not processable");
		gst_rtp_buffer_unmap(&rtp);
		return;
	}
	subflow_infos = (MPRTPRSubflowHeaderExtension*) pointer;
	if(_select_subflow(mprtpr, subflow_infos->id, &subflow) == FALSE){
		GST_ERROR_OBJECT(mprtpr, "The subflow lookup was not successful");
		gst_rtp_buffer_unmap(&rtp);
		return;
	}

	if(gst_rtp_buffer_get_ssrc(&rtp) == mprtpr->pivot_ssrc ||
	  mprtpr->pivot_ssrc == MPRTP_RECEIVER_DEFAULT_SSRC){
	  guint32 rtptime;
	  rtptime = gst_rtp_buffer_get_timestamp(&rtp);
	  //ext_rtptime = gst_rtp_buffer_ext_timestamp (&mprtpr->ext_rtptime, rtptime);
	  //sent = gst_util_uint64_scale_int (ext_rtptime-last_arrived,
		//GST_SECOND, mprtpr->pivot_clock_rate);

	  subflow->add_packet_skew(subflow, rtptime, mprtpr->pivot_clock_rate);
	}

	subflow->process_mprtp_packets(subflow, buf, subflow_infos->sequence);
}

gboolean _select_subflow(GstMprtpreceiver *this, guint16 id, MPRTPRSubflow **result)
{
  GList *it;
  MPRTPRSubflow *subflow;

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


void
_processing_mprtcp_packet(GstMprtpreceiver *this, GstBuffer *buf)
{
  GstPad *outpad;
  GstBuffer *outbuf;
  GstRTCPBuffer rtcp = {NULL, };
  GstRTCPHeader *header;
  GstMPRTCPSubflowRiport *riport;
  GstMPRTCPSubflowBlock *block;
  GstMPRTCPSubflowInfo *info;
  guint8 info_type;
  guint16 subflow_id;
  GList *it;
  MPRTPRSubflow *subflow;

  outbuf = gst_buffer_make_writable(buf);
  if (G_UNLIKELY (!gst_rtcp_buffer_map(outbuf, GST_MAP_READWRITE, &rtcp))){
    GST_WARNING_OBJECT(this, "The RTP packet is not writeable");
    return;
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
    for(it = this->subflows; it != NULL; it = it->next){
      subflow = it->data;
      if(subflow->id == subflow_id){
        subflow->proc_mprtcpblock(subflow, block);
      }
    }

  }
}


GList* _merge_lists(GList *F, GList *L)
{
  GList *head = NULL,*tail = NULL,*p = NULL,**s = NULL;
  GstRTPBuffer F_rtp = GST_RTP_BUFFER_INIT, L_rtp = GST_RTP_BUFFER_INIT;
  GstBuffer *F_buf, *L_buf;
  guint16 s1,s2;

  while(F != NULL && L != NULL){
	F_buf = F->data;
	L_buf = L->data;
	gst_rtp_buffer_map(F_buf, GST_MAP_READ, &F_rtp);
	gst_rtp_buffer_map(L_buf, GST_MAP_READ, &L_rtp);
	s1 = gst_rtp_buffer_get_seq(&F_rtp);
	s2 = gst_rtp_buffer_get_seq(&L_rtp);
	//g_print("S1: %d S2: %d cmp(s1,s2): %d\n", s1,s2, _cmp_seq(s1, s2));
	if(_cmp_seq(s1, s2) < 0){
	  s = &F;
	}else{
	  s = &L;
	}
	gst_rtp_buffer_unmap(&F_rtp);
	gst_rtp_buffer_unmap(&L_rtp);
	//s = (((packet_t*)F->data)->absolute_sequence > ((packet_t*)L->data)->absolute_sequence) ? &F : &L;
    if(head == NULL){
      head = *s;
      p = NULL;
    }else{
      tail->next = *s;
      tail->prev = p;
      p = tail;
    }
    tail = *s;
    *s = (*s)->next;
  }
  if(head != NULL){
    if(F != NULL){
	  tail->next = F;
      F->prev = tail;
	}else if(L != NULL){
	  tail->next = L;
	  L->prev = tail;
	}
  }else{
    head = (F != NULL)?F:L;
  }
/*
  GList *K = head;
  while(K != NULL){
	L_buf = K->data;
	gst_rtp_buffer_map(L_buf, GST_MAP_READ, &L_rtp);
    g_print("%p:%d->", &L_rtp, gst_rtp_buffer_get_seq(&L_rtp));
    gst_rtp_buffer_unmap(&L_rtp);
	K = K->next;
  }
/**/
  return head;
}

gint _cmp_seq(guint16 x, guint16 y)
{
  if(x == y){
	  return 0;
  }
  if(x < y || (0x8000 < x && y < 0x8000)){
	  return -1;
  }
  return 1;

  //return ((gint16) (x - y)) < 0 ? -1 : 1;
}
