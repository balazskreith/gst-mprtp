#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gsttransceiver.h"
#include <string.h>

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_transceiver_debug);
#define GST_CAT_DEFAULT gst_transceiver_debug

/* FakeSink signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_SYNC FALSE

#define DEFAULT_SILENT TRUE
#define DEFAULT_DUMP FALSE
#define DEFAULT_SIGNAL_HANDOFFS FALSE
#define DEFAULT_LAST_MESSAGE NULL
#define DEFAULT_CAN_ACTIVATE_PUSH TRUE
#define DEFAULT_CAN_ACTIVATE_PULL FALSE
#define DEFAULT_NUM_BUFFERS -1

enum
{
  PROP_0,
};


#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_transceiver_debug, "transceiver", 0, "transceiver element");
#define gst_transceiver_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstTransceiver, gst_transceiver, GST_TYPE_BASE_SINK,
    _do_init);

static void gst_transceiver_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_transceiver_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_transceiver_finalize (GObject * obj);

static GstStateChangeReturn gst_transceiver_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_transceiver_preroll (GstBaseSink * bsink,
    GstBuffer * buffer);
static GstFlowReturn gst_transceiver_render (GstBaseSink * bsink,
    GstBuffer * buffer);
static gboolean gst_transceiver_event (GstBaseSink * bsink, GstEvent * event);
static gboolean gst_transceiver_query (GstBaseSink * bsink, GstQuery * query);

static void
gst_transceiver_class_init (GstTransceiverClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbase_sink_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbase_sink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->set_property = gst_transceiver_set_property;
  gobject_class->get_property = gst_transceiver_get_property;
  gobject_class->finalize = gst_transceiver_finalize;


  gst_element_class_set_static_metadata (gstelement_class,
      "Transceiver Element",
      "Sink & Source",
      "Proxy for buffers",
      "Balazs Kreith <balazs.kreith@gmail.com>, ");

  gst_element_class_add_static_pad_template (gstelement_class, &sinktemplate);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_transceiver_change_state);

  gstbase_sink_class->event = GST_DEBUG_FUNCPTR (gst_transceiver_event);
  gstbase_sink_class->preroll = GST_DEBUG_FUNCPTR (gst_transceiver_preroll);
  gstbase_sink_class->render = GST_DEBUG_FUNCPTR (gst_transceiver_render);
  gstbase_sink_class->query = GST_DEBUG_FUNCPTR (gst_transceiver_query);
}

static void
gst_transceiver_init (GstTransceiver * transceiver)
{
  gst_base_sink_set_sync (GST_BASE_SINK (transceiver), DEFAULT_SYNC);

  transceiver->receiver = gst_element_factory_make("appsrc", NULL);
}

static void
gst_transceiver_finalize (GObject * obj)
{
  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_transceiver_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
//  GstTransceiver *sink;
//
//  sink = GST_TRANSCEIVER (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_transceiver_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
//  GstTransceiver *sink;
//
//  sink = GST_TRANSCEIVER (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
gst_transceiver_event (GstBaseSink * bsink, GstEvent * event)
{

  return GST_BASE_SINK_CLASS (parent_class)->event (bsink, event);
}

static GstFlowReturn
gst_transceiver_preroll (GstBaseSink * bsink, GstBuffer * buffer)
{
//  GstTransceiver *sink = GST_TRANSCEIVER (bsink);

  return GST_FLOW_OK;

}

static GstFlowReturn
gst_transceiver_render (GstBaseSink * bsink, GstBuffer * buf)
{
//  GstTransceiver *sink = GST_TRANSCEIVER_CAST (bsink);

  return GST_FLOW_OK;

}

static gboolean
gst_transceiver_query (GstBaseSink * bsink, GstQuery * query)
{
  gboolean ret;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_SEEKING:{
      GstFormat fmt;

      /* we don't supporting seeking */
      gst_query_parse_seeking (query, &fmt, NULL, NULL, NULL);
      gst_query_set_seeking (query, fmt, FALSE, 0, -1);
      ret = TRUE;
      break;
    }
    default:
      ret = GST_BASE_SINK_CLASS (parent_class)->query (bsink, query);
      break;
  }

  return ret;
}

static GstStateChangeReturn
gst_transceiver_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstTransceiver *transceiver = GST_TRANSCEIVER (element);

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

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      GST_OBJECT_LOCK (transceiver);
      GST_OBJECT_UNLOCK (transceiver);
      break;
    default:
      break;
  }

  return ret;
}
