/* GStreamer
 * Copyright (C) 2016 FIXME <fixme@example.com>
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
 * SECTION:element-gsttransceiver
 *
 * The transceiver element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v fakesrc ! transceiver ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include "gsttransceiver.h"

GST_DEBUG_CATEGORY_STATIC (gst_transceiver_debug_category);
#define GST_CAT_DEFAULT gst_transceiver_debug_category

/* prototypes */


static void gst_transceiver_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_transceiver_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_transceiver_dispose (GObject * object);
static void gst_transceiver_finalize (GObject * object);

static GstCaps *gst_transceiver_get_caps (GstBaseSink * sink, GstCaps * filter);
static gboolean gst_transceiver_set_caps (GstBaseSink * sink, GstCaps * caps);
static GstCaps *gst_transceiver_fixate (GstBaseSink * sink, GstCaps * caps);
static gboolean gst_transceiver_activate_pull (GstBaseSink * sink, gboolean active);
static void gst_transceiver_get_times (GstBaseSink * sink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end);
static gboolean gst_transceiver_propose_allocation (GstBaseSink * sink,
    GstQuery * query);
static gboolean gst_transceiver_start (GstBaseSink * sink);
static gboolean gst_transceiver_stop (GstBaseSink * sink);
static gboolean gst_transceiver_unlock (GstBaseSink * sink);
static gboolean gst_transceiver_unlock_stop (GstBaseSink * sink);
static gboolean gst_transceiver_query (GstBaseSink * sink, GstQuery * query);
static gboolean gst_transceiver_event (GstBaseSink * sink, GstEvent * event);
static GstFlowReturn gst_transceiver_wait_event (GstBaseSink * sink,
    GstEvent * event);
static GstFlowReturn gst_transceiver_prepare (GstBaseSink * sink,
    GstBuffer * buffer);
static GstFlowReturn gst_transceiver_prepare_list (GstBaseSink * sink,
    GstBufferList * buffer_list);
static GstFlowReturn gst_transceiver_preroll (GstBaseSink * sink,
    GstBuffer * buffer);
static GstFlowReturn gst_transceiver_render (GstBaseSink * sink,
    GstBuffer * buffer);
static GstFlowReturn gst_transceiver_render_list (GstBaseSink * sink,
    GstBufferList * buffer_list);

enum
{
  PROP_0
};

/* pad templates */

static GstStaticPadTemplate gst_transceiver_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
	GST_STATIC_CAPS ("ANY")
    );


static GstStaticPadTemplate gst_transceiver_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
	GST_STATIC_CAPS ("ANY")
    );

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstTransceiver, gst_transceiver, GST_TYPE_BASE_SINK,
  GST_DEBUG_CATEGORY_INIT (gst_transceiver_debug_category, "transceiver", 0,
  "debug category for transceiver element"));

static void
gst_transceiver_class_init (GstTransceiverClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *base_sink_class = GST_BASE_SINK_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS(klass),
      &gst_transceiver_sink_template);


  gst_element_class_add_pad_template (GST_ELEMENT_CLASS(klass),
      gst_static_pad_template_get (&gst_transceiver_src_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS(klass),
      "FIXME Long name", "Generic", "FIXME Description",
      "FIXME <fixme@example.com>");

  gobject_class->set_property = gst_transceiver_set_property;
  gobject_class->get_property = gst_transceiver_get_property;
  gobject_class->dispose = gst_transceiver_dispose;
  gobject_class->finalize = gst_transceiver_finalize;
  base_sink_class->get_caps = GST_DEBUG_FUNCPTR (gst_transceiver_get_caps);
  base_sink_class->set_caps = GST_DEBUG_FUNCPTR (gst_transceiver_set_caps);
  base_sink_class->fixate = GST_DEBUG_FUNCPTR (gst_transceiver_fixate);
  base_sink_class->activate_pull = GST_DEBUG_FUNCPTR (gst_transceiver_activate_pull);
  base_sink_class->get_times = GST_DEBUG_FUNCPTR (gst_transceiver_get_times);
  base_sink_class->propose_allocation = GST_DEBUG_FUNCPTR (gst_transceiver_propose_allocation);
  base_sink_class->start = GST_DEBUG_FUNCPTR (gst_transceiver_start);
  base_sink_class->stop = GST_DEBUG_FUNCPTR (gst_transceiver_stop);
  base_sink_class->unlock = GST_DEBUG_FUNCPTR (gst_transceiver_unlock);
  base_sink_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_transceiver_unlock_stop);
  base_sink_class->query = GST_DEBUG_FUNCPTR (gst_transceiver_query);
  base_sink_class->event = GST_DEBUG_FUNCPTR (gst_transceiver_event);
  base_sink_class->wait_event = GST_DEBUG_FUNCPTR (gst_transceiver_wait_event);
  base_sink_class->prepare = GST_DEBUG_FUNCPTR (gst_transceiver_prepare);
  base_sink_class->prepare_list = GST_DEBUG_FUNCPTR (gst_transceiver_prepare_list);
  base_sink_class->preroll = GST_DEBUG_FUNCPTR (gst_transceiver_preroll);
  base_sink_class->render = GST_DEBUG_FUNCPTR (gst_transceiver_render);
  base_sink_class->render_list = GST_DEBUG_FUNCPTR (gst_transceiver_render_list);

}

static void
gst_transceiver_init (GstTransceiver *this)
{
  this->src = gst_pad_new_from_static_template (&gst_transceiver_src_template, "src");
}

void
gst_transceiver_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (object);

  GST_DEBUG_OBJECT (transceiver, "set_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_transceiver_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (object);

  GST_DEBUG_OBJECT (transceiver, "get_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_transceiver_dispose (GObject * object)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (object);

  GST_DEBUG_OBJECT (transceiver, "dispose");

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (gst_transceiver_parent_class)->dispose (object);
}

void
gst_transceiver_finalize (GObject * object)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (object);

  GST_DEBUG_OBJECT (transceiver, "finalize");

  /* clean up object here */

  G_OBJECT_CLASS (gst_transceiver_parent_class)->finalize (object);
}

static GstCaps *
gst_transceiver_get_caps (GstBaseSink * sink, GstCaps * filter)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (sink);

  GST_DEBUG_OBJECT (transceiver, "get_caps");

  return NULL;
}

/* notify subclass of new caps */
static gboolean
gst_transceiver_set_caps (GstBaseSink * sink, GstCaps * caps)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (sink);

  GST_DEBUG_OBJECT (transceiver, "set_caps");

  return TRUE;
}

/* fixate sink caps during pull-mode negotiation */
static GstCaps *
gst_transceiver_fixate (GstBaseSink * sink, GstCaps * caps)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (sink);

  GST_DEBUG_OBJECT (transceiver, "fixate");

  return NULL;
}

/* start or stop a pulling thread */
static gboolean
gst_transceiver_activate_pull (GstBaseSink * sink, gboolean active)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (sink);

  GST_DEBUG_OBJECT (transceiver, "activate_pull");

  return TRUE;
}

/* get the start and end times for syncing on this buffer */
static void
gst_transceiver_get_times (GstBaseSink * sink, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (sink);

  GST_DEBUG_OBJECT (transceiver, "get_times");

}

/* propose allocation parameters for upstream */
static gboolean
gst_transceiver_propose_allocation (GstBaseSink * sink, GstQuery * query)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (sink);

  GST_DEBUG_OBJECT (transceiver, "propose_allocation");

  return TRUE;
}

/* start and stop processing, ideal for opening/closing the resource */
static gboolean
gst_transceiver_start (GstBaseSink * sink)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (sink);

  GST_DEBUG_OBJECT (transceiver, "start");

  return TRUE;
}

static gboolean
gst_transceiver_stop (GstBaseSink * sink)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (sink);

  GST_DEBUG_OBJECT (transceiver, "stop");

  return TRUE;
}

/* unlock any pending access to the resource. subclasses should unlock
 * any function ASAP. */
static gboolean
gst_transceiver_unlock (GstBaseSink * sink)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (sink);

  GST_DEBUG_OBJECT (transceiver, "unlock");

  return TRUE;
}

/* Clear a previously indicated unlock request not that unlocking is
 * complete. Sub-classes should clear any command queue or indicator they
 * set during unlock */
static gboolean
gst_transceiver_unlock_stop (GstBaseSink * sink)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (sink);

  GST_DEBUG_OBJECT (transceiver, "unlock_stop");

  return TRUE;
}

/* notify subclass of query */
static gboolean
gst_transceiver_query (GstBaseSink * sink, GstQuery * query)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (sink);

  GST_DEBUG_OBJECT (transceiver, "query");

  return TRUE;
}

/* notify subclass of event */
static gboolean
gst_transceiver_event (GstBaseSink * sink, GstEvent * event)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (sink);

  GST_DEBUG_OBJECT (transceiver, "event");

  return TRUE;
}

/* wait for eos or gap, subclasses should chain up to parent first */
static GstFlowReturn
gst_transceiver_wait_event (GstBaseSink * sink, GstEvent * event)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (sink);

  GST_DEBUG_OBJECT (transceiver, "wait_event");

  return GST_FLOW_OK;
}

/* notify subclass of buffer or list before doing sync */
static GstFlowReturn
gst_transceiver_prepare (GstBaseSink * sink, GstBuffer * buffer)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (sink);

  GST_DEBUG_OBJECT (transceiver, "prepare");

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_transceiver_prepare_list (GstBaseSink * sink, GstBufferList * buffer_list)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (sink);

  GST_DEBUG_OBJECT (transceiver, "prepare_list");

  return GST_FLOW_OK;
}

/* notify subclass of preroll buffer or real buffer */
static GstFlowReturn
gst_transceiver_preroll (GstBaseSink * sink, GstBuffer * buffer)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (sink);

  GST_DEBUG_OBJECT (transceiver, "preroll");

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_transceiver_render (GstBaseSink * sink, GstBuffer * buffer)
{
  GstTransceiver *this = GST_TRANSCEIVER (sink);

  GST_DEBUG_OBJECT (this, "render");
  gst_pad_push(this->src, buffer);

  return GST_FLOW_OK;
}

/* Render a BufferList */
static GstFlowReturn
gst_transceiver_render_list (GstBaseSink * sink, GstBufferList * list)
{
  GstTransceiver *transceiver = GST_TRANSCEIVER (sink);
  gint i, len;
  GstFlowReturn result;
  GST_DEBUG_OBJECT (transceiver, "render_list");

  result = GST_FLOW_OK;

  /* chain each buffer in list individually */
  len = gst_buffer_list_length (list);

  if (len == 0)
    goto done;

  for (i = 0; i < len; i++) {
    GstBuffer *buffer = gst_buffer_list_get (list, i);

    result = gst_transceiver_render (sink, buffer);
    if (result != GST_FLOW_OK)
      break;
  }

done:
  return result;
}

