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
 * SECTION:element-gsttry
 *
 * The try element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v fakesrc ! try ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include "gsttry.h"

GST_DEBUG_CATEGORY_STATIC (gst_try_debug_category);
#define GST_CAT_DEFAULT gst_try_debug_category

/* prototypes */


static void gst_try_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_try_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_try_dispose (GObject * object);
static void gst_try_finalize (GObject * object);

static gboolean gst_try_accept_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps);

static GstFlowReturn gst_try_transform (GstBaseTransform * trans,
		GstBuffer * inbuf, GstBuffer * outbuf);

static GstFlowReturn
gst_try_prepare_output_buffer (GstBaseTransform * trans, GstBuffer * input,
    GstBuffer ** outbuf);
enum
{
  PROP_0
};

/* pad templates */

static GstStaticPadTemplate gst_try_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
    );

static GstStaticPadTemplate gst_try_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp;application/x-srtcp")
    );


/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstTry, gst_try, GST_TYPE_BASE_TRANSFORM,
  GST_DEBUG_CATEGORY_INIT (gst_try_debug_category, "try", 0,
  "debug category for try element"));

static void
gst_try_class_init (GstTryClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class = GST_BASE_TRANSFORM_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS(klass),
      gst_static_pad_template_get (&gst_try_src_template));
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS(klass),
      gst_static_pad_template_get (&gst_try_sink_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS(klass),
      "FIXME Long name", "Generic", "FIXME Description",
      "FIXME <fixme@example.com>");

  gobject_class->set_property = gst_try_set_property;
  gobject_class->get_property = gst_try_get_property;
  gobject_class->dispose = gst_try_dispose;
  gobject_class->finalize = gst_try_finalize;
  base_transform_class->accept_caps = GST_DEBUG_FUNCPTR (gst_try_accept_caps);
  base_transform_class->transform = GST_DEBUG_FUNCPTR (gst_try_transform);
  base_transform_class->prepare_output_buffer = GST_DEBUG_FUNCPTR(gst_try_prepare_output_buffer);
  base_transform_class->transform_ip_on_passthrough = FALSE;
  base_transform_class->passthrough_on_same_caps = FALSE;
}

static void
gst_try_init (GstTry *try)
{

}

void
gst_try_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTry *try = GST_TRY (object);

  GST_DEBUG_OBJECT (try, "set_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_try_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstTry *try = GST_TRY (object);

  GST_DEBUG_OBJECT (try, "get_property");

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_try_dispose (GObject * object)
{
  GstTry *try = GST_TRY (object);

  GST_DEBUG_OBJECT (try, "dispose");

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (gst_try_parent_class)->dispose (object);
}

void
gst_try_finalize (GObject * object)
{
  GstTry *try = GST_TRY (object);

  GST_DEBUG_OBJECT (try, "finalize");

  /* clean up object here */

  G_OBJECT_CLASS (gst_try_parent_class)->finalize (object);
}


static gboolean
gst_try_accept_caps (GstBaseTransform * trans, GstPadDirection direction,
    GstCaps * caps)
{
  GstTry *try = GST_TRY (trans);

  GST_DEBUG_OBJECT (try, "accept_caps");

  return TRUE;
}


GstFlowReturn
gst_try_prepare_output_buffer (GstBaseTransform * trans, GstBuffer * input,
    GstBuffer ** outbuf)
{
  GstTry *try = GST_TRY (trans);
  *outbuf = gst_buffer_make_writable(input);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_try_transform (GstBaseTransform * trans, GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstTry *try = GST_TRY (trans);
  GST_DEBUG_OBJECT (try, "transform");
g_print("--->RTCP\n");
  return GST_FLOW_OK;
}

