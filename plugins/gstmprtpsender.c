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

#define PACKET_IS_RTP_OR_RTCP(b) (b > 0x7f && b < 0xc0)
#define PACKET_IS_DTLS(b) (b > 0x13 && b < 0x40)
#define PACKET_IS_RTCP(b) (b > 192 && b < 223)


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
static gboolean
gst_mprtpsender_src_query (GstPad * sinkpad, GstObject * parent,
    GstQuery * query);
static GstFlowReturn gst_mprtpsender_mprtp_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);

static GstFlowReturn
gst_mprtpsender_mprtcp_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);

static gboolean gst_mprtpsender_mprtp_sink_event_handler (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean
gst_mprtpsender_mprtp_sink_query_handler (GstPad * pad, GstObject * parent,
                                          GstQuery * query);

typedef struct
{
  GstPad *outpad;
  guint8 state;
  guint8 id;
  gboolean initialized;

  GstClock *sysclock;
} Subflow;


static GstBuffer *_assemble_report (Subflow * this, GstBuffer * blocks);
static Subflow *_get_subflow_from_blocks (GstMprtpsender * this,
    GstBuffer * blocks);
static gboolean _select_subflow (GstMprtpsender * this, guint8 id,
    Subflow ** result);

enum
{
  PROP_0,
  PROP_MPRTP_EXT_HEADER_ID,
  PROP_PIVOT_OUTPAD,
};

/* pad templates */

static GstStaticPadTemplate gst_mprtpsender_src_template =
GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);


static GstStaticPadTemplate gst_mprtpsender_mprtp_sink_template =
GST_STATIC_PAD_TEMPLATE ("mprtp_sink",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);


static GstStaticPadTemplate gst_mprtpsender_mprtcp_rr_sink_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_rr_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
//    GST_STATIC_CAPS ("application/x-rtcp")
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_mprtpsender_mprtcp_sr_sink_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_sr_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
//    GST_STATIC_CAPS ("application/x-rtcp")
    GST_STATIC_CAPS_ANY);

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


//
//gboolean
//gst_mprtpsender_mprtp_sink_event_handler (GstPad * pad, GstObject * parent,
//    GstEvent * event)
//{
//  GstMprtpsender *this;
//  gboolean result = TRUE;
//  GList *it;
//  Subflow *subflow;
//  const gchar *stream_id;
//  const GstSegment *segment;
//  GstCaps *caps;
//
////  g_print ("SND EVENT to the sink: %s", GST_EVENT_TYPE_NAME (event));
//
//  this = GST_MPRTPSENDER (parent);
//  THIS_WRITELOCK (this);
//  switch (GST_EVENT_TYPE (event)) {
//    case GST_EVENT_STREAM_START:
//      if (this->event_stream_start != NULL) {
//        gst_event_unref (this->event_stream_start);
//      }
//      gst_event_parse_stream_start (event, &stream_id);
//      this->event_stream_start = gst_event_new_stream_start (stream_id);
//      goto sending;
//    case GST_EVENT_SEGMENT:
//      if (this->event_segment != NULL) {
//        gst_event_unref (this->event_segment);
//      }
//      gst_event_parse_segment (event, &segment);
//      this->event_segment = gst_event_new_segment (segment);
//      goto sending;
//    case GST_EVENT_CAPS:
//      if (this->event_caps != NULL) {
//        gst_event_unref (this->event_caps);
//      }
//      gst_event_parse_caps (event, &caps);
//      this->event_caps = gst_event_new_caps (caps);
//      goto sending;
//    default:
//      sending:
//      for (subflow = NULL, it = this->subflows; it != NULL; it = it->next) {
//        subflow = it->data;
//        result &= gst_pad_push_event (subflow->outpad, gst_event_copy (event));
//      }
//      result &= gst_pad_event_default (pad, parent, event);
//      //result = gst_pad_event_default (pad, parent, event);
//  }
//
//  THIS_WRITEUNLOCK (this);
//  return result;
//}

static void _iterate_subflows(GstMprtpsender *this, void (*process)(Subflow*,gpointer),gpointer data)
{
  GList *it;
  Subflow *subflow;
  for(it = this->subflows; it; it = it->next)
  {
    subflow = it->data;
    process(subflow, data);
  }
}

static void _forward_event(Subflow *subflow, gpointer data)
{
  GstEvent *ev = data;
//  gst_pad_push_event(subflow->outpad, ev);
  gst_event_ref(ev);
  gst_element_send_event (GST_ELEMENT(subflow->outpad), ev);
}

static gboolean
gst_mprtpsender_mprtp_sink_event_handler (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res;
  GstMprtpsender *this;

  this = GST_MPRTPSENDER (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
    {
      gst_event_copy_segment (event, &this->segment);
      GST_DEBUG_OBJECT (this, "configured SEGMENT %" GST_SEGMENT_FORMAT,
          &this->segment);
      /* fall through */
      g_print("Segment event\n");
      DISABLE_LINE _iterate_subflows(this, _forward_event, event);
    }
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }
  return res;
}

static gboolean
gst_mprtpsender_mprtp_sink_query_handler (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res;

  switch (GST_QUERY_TYPE (query)) {
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }
  return res;
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

  g_object_class_install_property (gobject_class, PROP_MPRTP_EXT_HEADER_ID,
      g_param_spec_uint ("mprtp-ext-header-id",
          "Set or get the id for the RTP extension",
          "Sets or gets the id for the extension header the MpRTP based on. The default is 3",
          0, 15, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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
  gst_pad_set_event_function (mprtpsender->mprtcp_sr_sinkpad,
       GST_DEBUG_FUNCPTR (gst_mprtpsender_mprtp_sink_event_handler));
   gst_pad_set_query_function (mprtpsender->mprtcp_sr_sinkpad,
       GST_DEBUG_FUNCPTR (gst_mprtpsender_mprtp_sink_query_handler));
  gst_element_add_pad (GST_ELEMENT (mprtpsender),
      mprtpsender->mprtcp_sr_sinkpad);

  mprtpsender->mprtp_sinkpad =
      gst_pad_new_from_static_template (&gst_mprtpsender_mprtp_sink_template,
      "mprtp_sink");
  gst_pad_set_chain_function (mprtpsender->mprtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpsender_mprtp_sink_chain));
  gst_pad_set_event_function (mprtpsender->mprtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpsender_mprtp_sink_event_handler));
  gst_pad_set_query_function (mprtpsender->mprtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpsender_mprtp_sink_query_handler));
  //GST_OBJECT_FLAG_SET (mprtpsender->mprtp_sinkpad, GST_PAD_FLAG_PROXY_CAPS);

  gst_element_add_pad (GST_ELEMENT (mprtpsender), mprtpsender->mprtp_sinkpad);

//  GST_PAD_SET_PROXY_CAPS (mprtpsender->mprtp_sinkpad);
//  GST_PAD_SET_PROXY_ALLOCATION (mprtpsender->mprtp_sinkpad);

  gst_segment_init (&mprtpsender->segment, GST_FORMAT_UNDEFINED);
  mprtpsender->mprtp_ext_header_id = MPRTP_DEFAULT_EXTENSION_HEADER_ID;
  mprtpsender->pivot_outpad = NULL;
  mprtpsender->event_segment = NULL;
  mprtpsender->event_caps = NULL;
  mprtpsender->event_stream_start = NULL;
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
    case PROP_MPRTP_EXT_HEADER_ID:
      THIS_WRITELOCK (this);
      this->mprtp_ext_header_id = (guint8) g_value_get_uint (value);
      THIS_WRITEUNLOCK (this);
      break;
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
  GstMprtpsender *this = GST_MPRTPSENDER (object);

  GST_DEBUG_OBJECT (this, "get_property");

  switch (property_id) {
    case PROP_MPRTP_EXT_HEADER_ID:
      THIS_READLOCK (this);
      g_value_set_uint (value, (guint) this->mprtp_ext_header_id);
      THIS_READUNLOCK (this);
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


static gboolean
forward_sticky_events (GstPad * pad, GstEvent ** event, gpointer user_data)
{
  GstPad *srcpad = GST_PAD_CAST (user_data);
  GstFlowReturn ret;

  ret = gst_pad_store_sticky_event (srcpad, *event);
  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (srcpad, "storing sticky event %p (%s) failed: %s", *event,
        GST_EVENT_TYPE_NAME (*event), gst_flow_get_name (ret));
  }

  return TRUE;
}


static gboolean
gst_mprtpsender_src_activate_mode (GstPad * pad, GstObject * parent, GstPadMode mode,
    gboolean active)
{
  GstMprtpsender *this;
  gboolean res;

  this = GST_MPRTPSENDER (parent);

  switch (mode) {
    case GST_PAD_MODE_PULL:
    {
      goto not_supported;
      break;
    }
    default:
      res = TRUE;
      break;
  }

  return res;

  /* ERRORS */
not_supported:
  {
    GST_INFO_OBJECT (this, "PULL mode is not supported");
    return FALSE;
  }
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
  gst_pad_set_query_function(srcpad,
      GST_DEBUG_FUNCPTR(gst_mprtpsender_src_query));
  gst_pad_sticky_events_foreach (this->mprtp_sinkpad,
                                 forward_sticky_events, srcpad);
  subflow->id = subflow_id;
  subflow->outpad = srcpad;
  subflow->state = 0;
  subflow->sysclock = gst_system_clock_obtain ();
  this->subflows = g_list_prepend (this->subflows, subflow);
  this->dirty = TRUE;
  THIS_WRITEUNLOCK (this);
  GST_OBJECT_FLAG_SET (srcpad, GST_PAD_FLAG_PROXY_CAPS);

  gst_pad_set_activatemode_function (srcpad,
      GST_DEBUG_FUNCPTR (gst_mprtpsender_src_activate_mode));
//  gst_pad_set_active (srcpad, TRUE);

  gst_element_add_pad (GST_ELEMENT (this), srcpad);

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
  g_print("Change state: %d\n", ret);

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
//  g_print ("SND QUERY to the element: %s\n", GST_QUERY_TYPE_NAME (query));
  switch (GST_QUERY_TYPE (query)) {
    default:
      ret =
          GST_ELEMENT_CLASS (gst_mprtpsender_parent_class)->query (element,
          query);
      break;
  }

  return ret;
}


gboolean
gst_mprtpsender_src_query (GstPad * srcpad, GstObject * parent,
    GstQuery * query)
{
  GstMprtpsender *this = GST_MPRTPSENDER(parent);
  gboolean result = FALSE;
  GST_DEBUG_OBJECT (this, "query");
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
      {
        gboolean live;
        GstClockTime min, max;
        GstPad *peer;
        peer = gst_pad_get_peer (this->mprtp_sinkpad);
        if(!peer) goto default_query;
        if ((result = gst_pad_query (peer, query))) {
            gst_query_parse_latency (query, &live, &min, &max);
            //don't have any latency here. normally... who knows... I think I need to rest...
//            min+= GST_MSECOND;
//            if(max != -1) max+=min;
            gst_query_set_latency (query, live, min, max);
        }
        gst_object_unref (peer);
      }
      break;
    default:
    default_query:
      result = gst_pad_query_default (srcpad, parent, query);
      break;
  }
  return result;
}


static GstPadLinkReturn
gst_mprtpsender_src_link (GstPad * pad, GstObject * parent, GstPad * peer)
{
  GstMprtpsender *this;
  GList *it;
  Subflow *subflow;
  GstPadLinkReturn result = GST_PAD_LINK_OK;

  this = GST_MPRTPSENDER (parent);
  GST_DEBUG_OBJECT (this, "link");
  THIS_READLOCK (this);

  for (subflow = NULL, it = this->subflows; it != NULL; it = it->next) {
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
  if (this->event_stream_start != NULL) {
    gst_pad_push_event (subflow->outpad,
        gst_event_copy (this->event_stream_start));
  }
  if (this->event_caps != NULL) {
    gst_pad_push_event (subflow->outpad, gst_event_copy (this->event_caps));
  }
  if (this->event_segment != NULL) {
    gst_pad_push_event (subflow->outpad, gst_event_copy (this->event_segment));
  }

gst_mprtpsender_src_link_done:
  THIS_READUNLOCK (this);
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

      //SRTP validation - it must be fail
//      gst_rtp_buffer_add_extension_onebyte_header (&rtp, 2,
//            (gpointer) subflow_infos, sizeof (*subflow_infos));
    }
    gst_rtp_buffer_unmap (&rtp);
    result = PACKET_IS_MPRTP;
    goto done;
  }


done:
  return result;
}

static void _init_all_subflows(GstMprtpsender *this, GstBuffer *buf)
{
  GList *it;
  Subflow *subflow;
  for(it = this->subflows; it; it = it->next)
  {
    GstSegment *seg;
    GstEvent *ev;
    subflow = it->data;
    if(subflow->initialized) continue;
    subflow->initialized = TRUE;
    seg = &this->segment;
    /* If resending then mark segment start and position accordingly */
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
      seg->position = GST_BUFFER_TIMESTAMP (buf);
    }
    ev = gst_event_new_segment (seg);
    if (!gst_pad_push_event (subflow->outpad, ev)) {
      GST_WARNING_OBJECT (this,
          "newsegment handling failed in %" GST_PTR_FORMAT,
          subflow->outpad);
    }

    //also push an empty buffer so for the prerolling not blocking everything
    //-----------------------------------------------------------------------
    //if you are a gstreamer developer
    //you might have some idea how many hours I spent
    //to figure out why it doesn't change the state,
    //figure out how the gstreamer creature under the hood really works,
    //what kind of messages it sends and I can continue it, not
    //mentioned about the forums on the internet I digged out to get an answer,
    //so I can continue it infinitively but I just simply almost crying
    //when it finally changed the state to PLAYING.
    //Anyway, if you are not a gstreamer developer you have probably no idea
    //how much it hurts me that one possible solution for that problem
    //is so simple that this anecdote is longer in lines.
    {
      GstBuffer *result;
      GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

      result = gst_rtp_buffer_new_allocate (1400, 0, 0);
      gst_rtp_buffer_map(result, GST_MAP_READWRITE, &rtp);
      gst_rtp_buffer_set_payload_type(&rtp, MONITOR_PAYLOAD_DEFAULT_ID);
      gst_rtp_buffer_unmap(&rtp);
      gst_pad_push(subflow->outpad, result);
    }
  }
  return;
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
  GstClockTime position, duration;


  this = GST_MPRTPSENDER (parent);
  GST_DEBUG_OBJECT (this, "RTP/MPRTP/OTHER sink");
  if (!gst_buffer_map (buf, &map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (this, "Buffer is not readable");
    result = GST_FLOW_CUSTOM_ERROR;
    goto exit;
  }
  THIS_READLOCK (this);
  if(this->dirty) {
    _init_all_subflows(this, buf);
    this->dirty = FALSE;
  }
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
//      g_print("%d->%p|", subflow_id, subflow->outpad);
    outpad = subflow->outpad;
  } else if (this->pivot_outpad != NULL &&
      gst_pad_is_active (this->pivot_outpad) &&
      gst_pad_is_linked (this->pivot_outpad)) {
    outpad = this->pivot_outpad;
  } else {
    if (n > 1) {
      r = g_random_int_range (0, n);
      subflow = g_list_nth (this->subflows, r)->data;
      outpad = subflow->outpad;
    } else {
      subflow = this->subflows->data;
      outpad = subflow->outpad;
    }
  }
  gst_buffer_unmap (buf, &map);

  /* Keep track of last stop and use it in SEGMENT start after
     switching to a new src pad */
  position = GST_BUFFER_TIMESTAMP (buf);
  if (GST_CLOCK_TIME_IS_VALID (position)) {
    duration = GST_BUFFER_DURATION (buf);
    if (GST_CLOCK_TIME_IS_VALID (duration)) {
      position += duration;
    }
    GST_LOG_OBJECT (this, "setting last stop %" GST_TIME_FORMAT,
        GST_TIME_ARGS (position));
    this->segment.position = position;
  }
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
  GstFlowReturn result = GST_FLOW_OK;
  Subflow *subflow = NULL;
  this = GST_MPRTPSENDER (parent);
  THIS_READLOCK (this);
  subflow = _get_subflow_from_blocks (this, buf);
  if (!subflow) {
    goto done;
  }
//  g_print("############################ SENT (%lu)################################\n", GST_TIME_AS_MSECONDS(gst_clock_get_time(subflow->sysclock)));
  result = gst_pad_push (subflow->outpad, _assemble_report (subflow, buf));

done:
  THIS_READUNLOCK (this);
  return result;
}

Subflow *
_get_subflow_from_blocks (GstMprtpsender * this, GstBuffer * blocks)
{
  GstMapInfo map = GST_MAP_INFO_INIT;
  Subflow *result = NULL;
  guint16 subflow_id;
  GstMPRTCPSubflowBlock *block;
  if (!gst_buffer_map (blocks, &map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (this, "Buffer is not readable");
    goto done;
  }
  block = (GstMPRTCPSubflowBlock *) map.data;
  gst_mprtcp_block_getdown (&block->info, NULL, NULL, &subflow_id);
  if (!_select_subflow (this, subflow_id, &result)) {
    result = NULL;
  }
done:
  return result;
}

GstBuffer *
_assemble_report (Subflow * this, GstBuffer * blocks)
{
  GstBuffer *result = NULL;
  gsize report_header_size = 0;
  gsize blocks_length = 0;
  GstMPRTCPSubflowReport *report;
  GstMPRTCPSubflowBlock *block;
  guint16 length;
  guint16 offset = 0;
  guint8 block_length = 0;
  guint16 subflow_id, prev_subflow_id = 0;
  GstMapInfo map = GST_MAP_INFO_INIT;
  guint8 src = 0;

  if (!gst_buffer_map (blocks, &map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (this, "Buffer is not readable");
    goto exit;
  }
  report_header_size = sizeof(GstRTCPHeader) + sizeof(guint32);
  block = (GstMPRTCPSubflowBlock *) (map.data + offset);
  for (; offset < map.size; offset += (block_length + 1) << 2, ++src) {
//      {
//            guint8 pt;
//            gst_rtcp_header_getdown(&block->block_header, NULL, NULL, NULL, &pt, NULL, NULL);
//            if(pt == GST_RTCP_TYPE_SR){
//                guint64 ntptime;
//                GstRTCPSR *sr;
//                sr = &block->sender_riport;
//                gst_rtcp_srb_getdown(&sr->sender_block, &ntptime, NULL, NULL, NULL);
//                g_print("Created NTP time for subflow %d is %lu, but it sent at: "
//                    "%lu (%lu)\n", this->id, ntptime, NTP_NOW>>32,
//                    get_epoch_time_from_ntp_in_ns(NTP_NOW - ntptime));
//            }
//          }
    gst_mprtcp_block_getdown (&block->info, NULL, &block_length, &subflow_id);
    if (prev_subflow_id > 0 && subflow_id != prev_subflow_id) {
      GST_WARNING ("MPRTCP block comes from multiple subflow");
    }
    blocks_length += (block_length + 1) << 2;
    block = (GstMPRTCPSubflowBlock *) (map.data + blocks_length);
  }
  report = (GstMPRTCPSubflowReport*) g_malloc0(report_header_size + blocks_length);
  gst_mprtcp_report_init (report);
  memcpy((gpointer) &report->blocks, (gpointer) map.data, blocks_length);
  length = (report_header_size + blocks_length - 4)>>2;

  gst_rtcp_header_change(&report->header, NULL, NULL,
                         &src, NULL, &length, NULL);
  gst_buffer_unmap(blocks, &map);
//  gst_print_rtcp(&report->header);
  result = gst_buffer_new_wrapped ((gpointer)report, (length + 1)<<2);

exit:
  return result;
}

//
//GstBuffer *
//_assemble_report (Subflow * this, GstBuffer * blocks)
//{
//  GstBuffer *result;
//  gpointer dataptr;
//  GstMPRTCPSubflowReport report;
//  GstMPRTCPSubflowBlock *block;
//  guint16 length;
//  guint16 offset = 0;
//  guint8 block_length = 0;
//  guint16 subflow_id, prev_subflow_id = 0;
//  guint8 src = 0;
//  GstMapInfo map = GST_MAP_INFO_INIT;
//
//  gst_mprtcp_report_init (&report);
//  gst_rtcp_header_getdown (&report.header, NULL, NULL, NULL, NULL, &length,
//      NULL);
//  length = (length + 1) << 2;
//  dataptr = g_malloc0 (length);
//  memcpy (dataptr, &report, length);
//  result = gst_buffer_new_wrapped (dataptr, length);
//
//  if (!gst_buffer_map (blocks, &map, GST_MAP_READ)) {
//    GST_ERROR_OBJECT (this, "Buffer is not readable");
//    goto exit;
//  }
//  for (offset = 0, src = 0,
//      block = (GstMPRTCPSubflowBlock *) (map.data + offset);
//      offset < map.size;
//      offset += (block_length + 1) << 2,
//          ++src,
//          block = (GstMPRTCPSubflowBlock *) (map.data + offset)) {
//    length += (block_length + 1) << 2;
//    gst_mprtcp_block_getdown (&block->info, NULL, &block_length, &subflow_id);
//    if (prev_subflow_id > 0 && subflow_id != prev_subflow_id) {
//      GST_WARNING ("MPRTCP block comes from multiple subflow");
//    }
//    //gst_print_mprtcp_block(block, NULL);
//  }
//
//  gst_buffer_unmap (blocks, &map);
//  result = gst_buffer_append (result, blocks);
//  gst_buffer_map (result, &map, GST_MAP_WRITE);
//  length>>=2;
//  gst_rtcp_header_change ((GstRTCPHeader *) map.data, NULL, NULL, &src,
//      NULL, &length, NULL);
//  g_print("map-size:%lu\n", map.size);
//  gst_print_rtcp((GstRTCPHeader *) map.data);
//  gst_buffer_unmap (result, &map);
//exit:
//  return result;
//}

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



#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef PACKET_IS_RTP_OR_RTCP
#undef PACKET_IS_DTLS
