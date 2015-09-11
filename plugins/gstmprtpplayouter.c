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
#include "mprtprsubflow.h"
#include "mprtpssubflow.h"

GST_DEBUG_CATEGORY_STATIC (gst_mprtpplayouter_debug_category);
#define GST_CAT_DEFAULT gst_mprtpplayouter_debug_category

#define THIS_LOCK(mprtpr_ptr) g_mutex_lock(&mprtpr_ptr->mutex)
#define THIS_UNLOCK(mprtpr_ptr) g_mutex_unlock(&mprtpr_ptr->mutex)

#define MPRTP_PLAYOUTER_DEFAULT_EXTENSION_HEADER_ID 3
#define MPRTP_PLAYOUTER_DEFAULT_SSRC 0
/* prototypes */

typedef struct _MPRTPRSubflowHeaderExtension{
  guint16 id;
  guint16 sequence;
}MPRTPRSubflowHeaderExtension;

static void gst_mprtpplayouter_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_mprtpplayouter_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_mprtpplayouter_dispose (GObject * object);
static void gst_mprtpplayouter_finalize (GObject * object);

static GstStateChangeReturn
gst_mprtpplayouter_change_state (GstElement * element, GstStateChange transition);
static gboolean gst_mprtpplayouter_query (GstElement * element, GstQuery * query);
static GstFlowReturn gst_mprtpplayouter_mprtp_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buffer);
static GstFlowReturn gst_mprtpplayouter_mprtcp_sr_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buffer);
static void gst_mprtpplayouter_mprtcp_riporter_run (void *data);
static void gst_mprtpplayouter_playouter_run (void *data);

static gboolean _select_subflow(GstMprtpplayouter *this, guint16 id,
		MPRTPRSubflow **result);
static void _processing_mprtcp_packet(GstMprtpplayouter *mprtpr, GstBuffer *buf);
static void _processing_mprtp_packet(GstMprtpplayouter *mprtpr, GstBuffer *buf);
static GList* _merge_lists(GList *F, GList *L);
static gint _cmp_seq(guint16 x, guint16 y);
static void _join_subflow(GstMprtpplayouter* this, guint subflow_id);
static void _detach_subflow(GstMprtpplayouter* this, guint subflow_id);

enum
{
  PROP_0,
  PROP_EXT_HEADER_ID,
  PROP_PIVOT_SSRC,
  PROP_JOIN_SUBFLOW,
  PROP_DETACH_SUBFLOW,
  PROP_PIVOT_CLOCK_RATE
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
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
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
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
    );


/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstMprtpplayouter, gst_mprtpplayouter, GST_TYPE_ELEMENT,
  GST_DEBUG_CATEGORY_INIT (gst_mprtpplayouter_debug_category, "mprtpplayouter", 0,
  "debug category for mprtpplayouter element"));

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
      gst_static_pad_template_get (&gst_mprtpplayouter_mprtcp_sr_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpplayouter_mprtp_src_template));
  gst_element_class_add_pad_template (element_class,
       gst_static_pad_template_get (&gst_mprtpplayouter_mprtcp_rr_src_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS(klass),
      "MPRTP Playouter", "Generic",
	  "MPRTP Playouter FIXME",
      "Balázs Kreith <balazs.kreith@gmail.com>");

  gobject_class->set_property = gst_mprtpplayouter_set_property;
  gobject_class->get_property = gst_mprtpplayouter_get_property;
  gobject_class->dispose = gst_mprtpplayouter_dispose;
  gobject_class->finalize = gst_mprtpplayouter_finalize;

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

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_mprtpplayouter_change_state);
  element_class->query = GST_DEBUG_FUNCPTR (gst_mprtpplayouter_query);
}

static void
gst_mprtpplayouter_init (GstMprtpplayouter *mprtpplayouter)
{

	mprtpplayouter->mprtp_sinkpad = gst_pad_new_from_static_template (&gst_mprtpplayouter_mprtp_sink_template,"mprtp_sink");
	gst_element_add_pad (GST_ELEMENT(mprtpplayouter), mprtpplayouter->mprtp_sinkpad);

	mprtpplayouter->mprtp_srcpad = gst_pad_new_from_static_template (&gst_mprtpplayouter_mprtp_src_template,"mprtp_src");
	  gst_element_add_pad (GST_ELEMENT(mprtpplayouter), mprtpplayouter->mprtp_srcpad);

    mprtpplayouter->mprtcp_sr_sinkpad = gst_pad_new_from_static_template (&gst_mprtpplayouter_mprtcp_sr_sink_template,"mprtcp_sr_sink");
        gst_element_add_pad (GST_ELEMENT(mprtpplayouter), mprtpplayouter->mprtcp_sr_sinkpad);

	mprtpplayouter->mprtcp_rr_srcpad = gst_pad_new_from_static_template (&gst_mprtpplayouter_mprtcp_rr_src_template,"mprtcp_rr_src");
		gst_element_add_pad (GST_ELEMENT(mprtpplayouter), mprtpplayouter->mprtcp_rr_srcpad);

		gst_pad_set_chain_function (mprtpplayouter->mprtcp_sr_sinkpad,
		    GST_DEBUG_FUNCPTR(gst_mprtpplayouter_mprtcp_sr_sink_chain));
		gst_pad_set_chain_function (mprtpplayouter->mprtp_sinkpad,
		    GST_DEBUG_FUNCPTR(gst_mprtpplayouter_mprtp_sink_chain));


    mprtpplayouter->ext_header_id = MPRTP_PLAYOUTER_DEFAULT_EXTENSION_HEADER_ID;
    mprtpplayouter->playout_delay = 0.0;
    mprtpplayouter->pivot_clock_rate = 0;
    mprtpplayouter->pivot_ssrc = MPRTP_PLAYOUTER_DEFAULT_SSRC;
    mprtpplayouter->path_skew_counter = 0;
    mprtpplayouter->ext_rtptime = -1;
    mprtpplayouter->path_skew_index = 0;
    mprtpplayouter->compound_sending = FALSE;
    mprtpplayouter->flowable = FALSE;
    mprtpplayouter->rtcp_sent_octet_sum = 0;
    g_mutex_init(&mprtpplayouter->mutex);
}

void
gst_mprtpplayouter_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMprtpplayouter *mprtpplayouter = GST_MPRTPPLAYOUTER (object);
  GList *it;
  MPRTPRSubflow *subflow;
  GST_DEBUG_OBJECT (mprtpplayouter, "set_property");

  switch (property_id) {
  case PROP_EXT_HEADER_ID:
	  THIS_LOCK(mprtpplayouter);
	  mprtpplayouter->ext_header_id = g_value_get_int(value);
	  for(it = mprtpplayouter->subflows; it != NULL; it = it->next){
		subflow = it->data;
		subflow->ext_header_id = mprtpplayouter->ext_header_id;
	  }
	  THIS_UNLOCK(mprtpplayouter);
	  break;
  case PROP_PIVOT_SSRC:
	  THIS_LOCK(mprtpplayouter);
	  mprtpplayouter->pivot_ssrc = g_value_get_uint(value);
	  THIS_UNLOCK(mprtpplayouter);
	  break;
  case PROP_PIVOT_CLOCK_RATE:
  	  THIS_LOCK(mprtpplayouter);
  	  mprtpplayouter->pivot_clock_rate = g_value_get_uint(value);
  	  THIS_UNLOCK(mprtpplayouter);
  	  break;
  case PROP_JOIN_SUBFLOW:
  	  THIS_LOCK(mprtpplayouter);
  	  _join_subflow(mprtpplayouter, g_value_get_uint(value));
  	  THIS_UNLOCK(mprtpplayouter);
  	  break;
  case PROP_DETACH_SUBFLOW:
  	  THIS_LOCK(mprtpplayouter);
  	  _detach_subflow(mprtpplayouter, g_value_get_uint(value));
  	  THIS_UNLOCK(mprtpplayouter);
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
  GstMprtpplayouter *mprtpplayouter = GST_MPRTPPLAYOUTER (object);

  GST_DEBUG_OBJECT (mprtpplayouter, "get_property");

  switch (property_id) {
  case PROP_EXT_HEADER_ID:
    	  THIS_LOCK(mprtpplayouter);
    	  g_value_set_int(value, mprtpplayouter->ext_header_id);
    	  THIS_UNLOCK(mprtpplayouter);
    	break;
  case PROP_PIVOT_CLOCK_RATE:
    	  THIS_LOCK(mprtpplayouter);
    	  g_value_set_uint(value, mprtpplayouter->pivot_clock_rate);
    	  THIS_UNLOCK(mprtpplayouter);
    	break;
  case PROP_PIVOT_SSRC:
    	  THIS_LOCK(mprtpplayouter);
    	  g_value_set_uint(value, mprtpplayouter->pivot_ssrc);
    	  THIS_UNLOCK(mprtpplayouter);
    	break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
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
gst_mprtpplayouter_finalize (GObject * object)
{
  GstMprtpplayouter *mprtpplayouter = GST_MPRTPPLAYOUTER (object);

  GST_DEBUG_OBJECT (mprtpplayouter, "finalize");

  /* clean up object here */

  G_OBJECT_CLASS (gst_mprtpplayouter_parent_class)->finalize (object);
}



void _join_subflow(GstMprtpplayouter* this, guint subflow_id)
{
	MPRTPRSubflow *subflow;
	subflow = make_mprtpr_subflow(subflow_id, this->ext_header_id);
	this->subflows = g_list_prepend(this->subflows, subflow);
}

void _detach_subflow(GstMprtpplayouter* this, guint subflow_id)
{
	MPRTPRSubflow *subflow;
	GList *it;
	for(it = this->subflows; it != NULL; it = it->next){
	  subflow = it->data;
	  if(subflow->id == subflow_id){
		  break;
	  }
	  subflow = NULL;
	}
	if(subflow == NULL){
		GST_WARNING_OBJECT(this, "The requested subflow id (%d) "
				"was not found to detach", subflow_id);
	  return;
	}
    this->subflows = g_list_remove(this->subflows, subflow);
}



static GstStateChangeReturn
gst_mprtpplayouter_change_state (GstElement * element, GstStateChange transition)
{
  GstMprtpplayouter *mprtpplayouter;
  GstStateChangeReturn ret;
  g_return_val_if_fail (GST_IS_MPRTPPLAYOUTER (element), GST_STATE_CHANGE_FAILURE);
  mprtpplayouter = GST_MPRTPPLAYOUTER (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
	  mprtpplayouter->riporter = gst_task_new (gst_mprtpplayouter_mprtcp_riporter_run, mprtpplayouter, NULL);
	  g_rec_mutex_init (&mprtpplayouter->riporter_mutex);
	  gst_task_set_lock (mprtpplayouter->riporter, &mprtpplayouter->riporter_mutex);

	  mprtpplayouter->playouter = gst_task_new (gst_mprtpplayouter_playouter_run, mprtpplayouter, NULL);
	  g_rec_mutex_init (&mprtpplayouter->playouter_mutex);
	  gst_task_set_lock (mprtpplayouter->playouter, &mprtpplayouter->playouter_mutex);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
		gst_task_start (mprtpplayouter->playouter);
		gst_task_start (mprtpplayouter->riporter);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (gst_mprtpplayouter_parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
        gst_task_stop (mprtpplayouter->playouter);
        gst_task_stop (mprtpplayouter->riporter);
        gst_task_join (mprtpplayouter->playouter);
        gst_task_join (mprtpplayouter->riporter);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
    	gst_object_unref (mprtpplayouter->playouter);
    	mprtpplayouter->playouter = NULL;
		g_rec_mutex_clear (&mprtpplayouter->playouter_mutex);
		gst_object_unref (mprtpplayouter->riporter);
		mprtpplayouter->riporter = NULL;
		g_rec_mutex_clear (&mprtpplayouter->riporter_mutex);
      break;
    default:
      break;
  }

  return ret;
}

void
gst_mprtpplayouter_playouter_run (void *data)
{
	GstMprtpplayouter *this = data;
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

	THIS_LOCK(this);

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
	  gst_pad_push(this->mprtp_srcpad, buf);
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
	THIS_UNLOCK(this);

	clock_id = gst_clock_new_single_shot_id (GST_ELEMENT_CLOCK(this), next_scheduler_time);
	if(gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED){
	  GST_WARNING_OBJECT(this, "The playout clock wait is interrupted");
	}
	gst_clock_id_unref (clock_id);

}

void
gst_mprtpplayouter_mprtcp_riporter_run (void *data)
{
  GstMprtpplayouter *this = data;
  GstClockID clock_id;
  GstClockTime next_scheduler_time;
  GstClockTime now;
  gboolean compound_sending, first;
  GList *it;
  MPRTPRSubflow *subflow;
  GstBuffer *outbuf;
  GstRTCPBuffer rtcp = {NULL,};
  GstRTCPHeader *header;
  GstMPRTCPSubflowRiport *riport;
  gsize rtcp_packet_size;

  now = gst_clock_get_time(GST_ELEMENT_CLOCK(this));
  next_scheduler_time = 0;
  THIS_LOCK(this);
  if(!this->flowable){
    goto gst_mprtpplayouter_mprtcp_riporter_run_done;
  }
  compound_sending = this->compound_sending;
  for(first = TRUE, it = this->subflows; it != NULL; it = it->next){
	GstClockTime next_time;
    subflow = it->data;
    if(!subflow->do_riport_now(subflow, &next_time) || !subflow->is_active(subflow)){
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

    rtcp_packet_size = rtcp.map.size + (28<<3) /*UDP header size with IP header*/;
    subflow->set_avg_rtcp_size(subflow, rtcp_packet_size);

    if(compound_sending){
	  continue;
    }

    this->rtcp_sent_octet_sum += rtcp_packet_size>>3;
    gst_rtcp_buffer_unmap(&rtcp);
    //g_print("sending %d\n", subflow->get_id(subflow));
    gst_pad_push(this->mprtcp_rr_srcpad, outbuf);

    outbuf = gst_rtcp_buffer_new(1400);
    gst_rtcp_buffer_map (outbuf, GST_MAP_READWRITE, &rtcp);
  }
  if(first == FALSE){
	rtcp_packet_size = (rtcp.map.size>>3) + 28 /*UDP header octet size*/;
    gst_rtcp_buffer_unmap(&rtcp);
  }

  if(compound_sending && first == FALSE){
	if(gst_pad_is_linked(this->mprtcp_rr_srcpad)){
	  this->rtcp_sent_octet_sum += rtcp_packet_size;
      gst_pad_push(this->mprtcp_rr_srcpad, outbuf);
	}else{
	  GST_ERROR_OBJECT(this, "MPRTP Source is not linked");
	}
  }

gst_mprtpplayouter_mprtcp_riporter_run_done:
  THIS_UNLOCK(this);

  next_scheduler_time = now + GST_MSECOND * 100;
  clock_id = gst_clock_new_single_shot_id (GST_ELEMENT_CLOCK(this),
	next_scheduler_time);
  if(gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED){
    GST_WARNING_OBJECT(this, "The riporter clock wait is interrupted");
  }
  gst_clock_id_unref (clock_id);
}

static gboolean
gst_mprtpplayouter_query (GstElement * element, GstQuery * query)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (element);
  gboolean ret;
  GstStructure *s;
  GST_DEBUG_OBJECT (this, "query");
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CUSTOM:
    	if(!gst_structure_has_name(s, GST_MPRTCP_PLAYOUTER_SENT_BYTES_STRUCTURE_NAME)){
    		ret = GST_ELEMENT_CLASS (gst_mprtpplayouter_parent_class)->query (element, query);
    		break;
    	}
    	THIS_LOCK(this);
    	s = gst_query_writable_structure (query);
    	gst_structure_set (s,
    		GST_MPRTCP_PLAYOUTER_SENT_OCTET_SUM_FIELD,
			G_TYPE_UINT,
			this->rtcp_sent_octet_sum,
			NULL);
    	this->rtcp_sent_octet_sum = 0;
    	THIS_UNLOCK(this);
    	break;
    default:
      ret = GST_ELEMENT_CLASS (gst_mprtpplayouter_parent_class)->query (element, query);
      break;
  }

  return ret;
}


static GstFlowReturn
gst_mprtpplayouter_mprtp_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buf)
{
  GstMprtpplayouter *this;
  GstMapInfo info;
  guint8 *data;

  this = GST_MPRTPPLAYOUTER (parent);
  GST_DEBUG_OBJECT(this, "RTP/RTCP/MPRTP/MPRTCP sink");
  THIS_LOCK(this);


  if(!gst_buffer_map(buf, &info, GST_MAP_READ)){
  	GST_WARNING("Buffer is not readable");
  	THIS_UNLOCK(this);
  	return GST_FLOW_ERROR;
  }
  data = info.data + 1;
  gst_buffer_unmap(buf, &info);
  //demultiplexing based on RFC5761
  if(*data == MPRTCP_PACKET_TYPE_IDENTIFIER){
	_processing_mprtcp_packet(this, buf);
	THIS_UNLOCK(this);
	return GST_FLOW_OK;
  }

  //the packet is either rtcp or mprtp
  if(*data > 192 && *data < 223){
    THIS_UNLOCK(this);
    return gst_pad_push(this->mprtp_srcpad, buf);
  }

  if(!this->flowable){
    this->flowable = TRUE;
  }
  _processing_mprtp_packet(this, buf);
  THIS_UNLOCK(this);
  return GST_FLOW_OK;

}


static GstFlowReturn
gst_mprtpplayouter_mprtcp_sr_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buf)
{
	GstMprtpplayouter *this;
	GstMapInfo info;
	guint8 *data;

  this = GST_MPRTPPLAYOUTER (parent);
  GST_DEBUG_OBJECT(this, "RTCP/MPRTCP sink");
  if(!gst_buffer_map(buf, &info, GST_MAP_READ)){
   	GST_WARNING("Buffer is not readable");
   	THIS_UNLOCK(this);
   	return GST_FLOW_ERROR;
  }
  data = info.data + 1;
  gst_buffer_unmap(buf, &info);
  //demultiplexing based on RFC5761
  if(*data != MPRTCP_PACKET_TYPE_IDENTIFIER){
	GST_WARNING_OBJECT(this, "mprtcp_sr_sink process only MPRTCP packets");
  	THIS_UNLOCK(this);
  	return GST_FLOW_OK;
  }
  _processing_mprtcp_packet(this, buf);
  THIS_UNLOCK(this);
  //g_print("RTCP packet is forwarded: %p\n", this->rtcp_srcpad);
  return GST_FLOW_OK;
}


void
_processing_mprtp_packet(GstMprtpplayouter *this, GstBuffer *buf)
{
	gpointer pointer = NULL;
	MPRTPRSubflowHeaderExtension *subflow_infos = NULL;
	MPRTPRSubflow *subflow;
	guint size;
	GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

	if (G_UNLIKELY (!gst_rtp_buffer_map(buf, GST_MAP_READ, &rtp))){
	  GST_WARNING_OBJECT(this, "The received Buffer is not readable");
	  gst_rtp_buffer_unmap(&rtp);
	  return;
	}

    //gst_print_rtp_packet_info(&rtp);

	if(!gst_rtp_buffer_get_extension(&rtp)){

		//Backward compatibility in a way to process rtp packet must be implemented here!!!

		GST_WARNING_OBJECT(this, "The received buffer extension bit is 0 thus it is not an MPRTP packet.");
		gst_rtp_buffer_unmap(&rtp);
		return;
	}
	//_print_rtp_packet_info(rtp);

	if(!gst_rtp_buffer_get_extension_onebyte_header(&rtp, this->ext_header_id, 0, &pointer, &size)){
		GST_WARNING_OBJECT(this, "The received buffer extension is not processable");
		gst_rtp_buffer_unmap(&rtp);
		return;
	}
	subflow_infos = (MPRTPRSubflowHeaderExtension*) pointer;
	if(_select_subflow(this, subflow_infos->id, &subflow) == FALSE){
		subflow = make_mprtpr_subflow(subflow_infos->id, this->ext_header_id);
		this->subflows = g_list_prepend(this->subflows, subflow);
		GST_ERROR_OBJECT(this, "The subflow lookup was not successful");
		gst_rtp_buffer_unmap(&rtp);
		return;
	}

	if(gst_rtp_buffer_get_ssrc(&rtp) == this->pivot_ssrc ||
	  this->pivot_ssrc == MPRTP_PLAYOUTER_DEFAULT_SSRC){
	  guint32 rtptime;
	  rtptime = gst_rtp_buffer_get_timestamp(&rtp);
	  //ext_rtptime = gst_rtp_buffer_ext_timestamp (&this->ext_rtptime, rtptime);
	  //sent = gst_util_uint64_scale_int (ext_rtptime-last_arrived,
		//GST_SECOND, this->pivot_clock_rate);

	  subflow->add_packet_skew(subflow, rtptime, this->pivot_clock_rate);
	}

	subflow->process_mprtp_packets(subflow, buf, subflow_infos->sequence);
}

gboolean _select_subflow(GstMprtpplayouter *this, guint16 id, MPRTPRSubflow **result)
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
_processing_mprtcp_packet(GstMprtpplayouter *this, GstBuffer *buf)
{
  GstRTCPBuffer rtcp = {NULL, };
  GstRTCPHeader *header;
  GstMPRTCPSubflowRiport *riport;
  GstMPRTCPSubflowBlock *block;
  GstMPRTCPSubflowInfo *info;
  guint8 info_type;
  guint16 subflow_id;
  GList *it;
  MPRTPRSubflow *subflow;

  if (G_UNLIKELY (!gst_rtcp_buffer_map(buf, GST_MAP_READ, &rtcp))){
    GST_WARNING_OBJECT(this, "The RTP packet is not readable");
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
