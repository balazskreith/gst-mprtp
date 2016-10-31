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
 * SECTION:element-gstbufferpacer
 *
 * The bufferpacer element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v fakesrc ! bufferpacer ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gst/gst.h>
#include "gstbufferpacer.h"


GST_DEBUG_CATEGORY_STATIC (gst_bufferpacer_debug_category);
#define GST_CAT_DEFAULT gst_bufferpacer_debug_category

#define PACKET_IS_RTP_OR_RTCP(b) (b > 0x7f && b < 0xc0)
#define PACKET_IS_RTCP(b) (b > 192 && b < 223)
#define THIS_LOCK(this) (g_mutex_lock(&this->mutex))
#define THIS_UNLOCK(this) (g_mutex_unlock(&this->mutex))

#define _now(this) gst_clock_get_time (this->sysclock)

static void gst_bufferpacer_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_bufferpacer_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_bufferpacer_dispose (GObject * object);
static void gst_bufferpacer_finalize (GObject * object);

static GstStateChangeReturn
gst_bufferpacer_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_bufferpacer_query (GstElement * element,
    GstQuery * query);
static GstFlowReturn gst_bufferpacer_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static GstFlowReturn gst_bufferpacer_sink_chainlist (GstPad * pad,
    GstObject * parent, GstBufferList * list);

static gboolean gst_bufferpacer_src_query (GstPad * srckpad, GstObject * parent,
    GstQuery * query);

//static gboolean gst_bufferpacer_mprtp_src_event (GstPad * pad, GstObject * parent, GstEvent * event);
//static gboolean gst_bufferpacer_sink_eventfunc (GstPad * srckpad, GstObject * parent, GstEvent * event);
static gboolean gst_bufferpacer_src_event (GstPad * srckpad, GstObject * parent, GstEvent * event);
static gboolean gst_bufferpacer_sink_event (GstPad * srckpad, GstObject * parent, GstEvent * event);


static void bufferpacer_pacing_process(GstBufferPacer *this);


enum
{
  PROP_0,
};

/* signals and args */
enum
{
  LAST_SIGNAL
};



/* pad templates */
static GstStaticPadTemplate gst_bufferpacer_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );


static GstStaticPadTemplate gst_bufferpacer_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

/* class initialization */
G_DEFINE_TYPE_WITH_CODE (GstBufferPacer, gst_bufferpacer,
    GST_TYPE_ELEMENT,
    GST_DEBUG_CATEGORY_INIT (gst_bufferpacer_debug_category,
        "bufferpacer", 0, "debug category for bufferpacer element"));

#define GST_BUFFERPACER_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_BUFFERPACER, GstBufferPacerPrivate))

struct _GstBufferPacerPrivate
{

};

static void
gst_bufferpacer_class_init (GstBufferPacerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_bufferpacer_sink_template));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_bufferpacer_src_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "MPRTP Scheduler", "Generic", "MPRTP scheduler FIXME LONG DESC",
      "Balázs Kreith <balazs.kreith@gmail.com>");

  gobject_class->set_property = gst_bufferpacer_set_property;
  gobject_class->get_property = gst_bufferpacer_get_property;
  gobject_class->dispose = gst_bufferpacer_dispose;
  gobject_class->finalize = gst_bufferpacer_finalize;
  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_bufferpacer_change_state);
  element_class->query = GST_DEBUG_FUNCPTR (gst_bufferpacer_query);

}

static void
gst_bufferpacer_init (GstBufferPacer * this)
{

  this->sink = gst_pad_new_from_static_template (&gst_bufferpacer_sink_template, "sink");

  gst_pad_set_chain_function (this->sink,
      GST_DEBUG_FUNCPTR (gst_bufferpacer_sink_chain));
  gst_pad_set_chain_list_function (this->sink,
      GST_DEBUG_FUNCPTR (gst_bufferpacer_sink_chainlist));
  gst_pad_set_event_function (this->sink,
        GST_DEBUG_FUNCPTR (gst_bufferpacer_sink_event));

  GST_PAD_SET_PROXY_CAPS (this->sink);
  GST_PAD_SET_PROXY_ALLOCATION (this->sink);

  gst_element_add_pad (GST_ELEMENT (this), this->sink);

  this->src = gst_pad_new_from_static_template (&gst_bufferpacer_src_template, "src");

  gst_pad_set_event_function (this->src,
      GST_DEBUG_FUNCPTR (gst_bufferpacer_src_event));
  gst_pad_use_fixed_caps (this->src);
  GST_PAD_SET_PROXY_CAPS (this->src);
  GST_PAD_SET_PROXY_ALLOCATION (this->src);

  gst_pad_set_query_function(this->src,
    GST_DEBUG_FUNCPTR(gst_bufferpacer_src_query));

  gst_element_add_pad (GST_ELEMENT (this), this->src);

  this->sysclock = gst_system_clock_obtain ();
  g_mutex_init (&this->mutex);
  g_cond_init(&this->receiving_signal);
  g_cond_init(&this->waiting_signal);

  this->packetsq      = g_queue_new();

}


void
gst_bufferpacer_dispose (GObject * object)
{
  GstBufferPacer *bufferpacer = GST_BUFFERPACER (object);

  GST_DEBUG_OBJECT (bufferpacer, "dispose");

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (gst_bufferpacer_parent_class)->dispose (object);
}

void
gst_bufferpacer_finalize (GObject * object)
{
  GstBufferPacer *this = GST_BUFFERPACER (object);

  GST_DEBUG_OBJECT (this, "finalize");

  /* clean up object here */

  g_object_unref (this->sysclock);
  g_object_unref(this->packetsq);

  G_OBJECT_CLASS (gst_bufferpacer_parent_class)->finalize (object);
}

void
gst_bufferpacer_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBufferPacer *this = GST_BUFFERPACER (object);

  GST_DEBUG_OBJECT (this, "set_property");
  THIS_LOCK(this);
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  THIS_UNLOCK(this);
}


void
gst_bufferpacer_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstBufferPacer *this = GST_BUFFERPACER (object);

  GST_DEBUG_OBJECT (this, "get_property");
  THIS_LOCK(this);
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  THIS_UNLOCK(this);

}


static GstStateChangeReturn
gst_bufferpacer_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstBufferPacer * this;

  this = GST_BUFFERPACER (element);
  g_return_val_if_fail (GST_IS_BUFFERPACER (element),
      GST_STATE_CHANGE_FAILURE);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
       gst_pad_start_task(this->src, (GstTaskFunction)bufferpacer_pacing_process,
         this, NULL);
       break;
     default:
       break;
   }

   ret =
       GST_ELEMENT_CLASS (gst_bufferpacer_parent_class)->change_state
       (element, transition);

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
gst_bufferpacer_query (GstElement * element, GstQuery * query)
{
  GstBufferPacer *this = GST_BUFFERPACER (element);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (this, "query");
  switch (GST_QUERY_TYPE (query)) {
    default:
      ret =
          GST_ELEMENT_CLASS (gst_bufferpacer_parent_class)->query (element,
          query);
      break;
  }

  return ret;
}


static void _wait(GstBufferPacer *this, GstClockTime end, gint64 step_in_microseconds)
{
  gint64 end_time;
  while(_now(this) < end){
    end_time = g_get_monotonic_time() + step_in_microseconds;
    g_cond_wait_until(&this->waiting_signal, &this->mutex, end_time);
  }
}

static GstFlowReturn
gst_bufferpacer_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstBufferPacer *this;
  GstFlowReturn result;

  this = GST_BUFFERPACER (parent);

  if (GST_PAD_IS_FLUSHING(pad)) {
    result = GST_FLOW_FLUSHING;
    goto done;
  }

  result = GST_FLOW_OK;

  THIS_LOCK(this);

  g_queue_push_tail(this->packetsq, gst_buffer_ref(buffer));
  g_cond_signal(&this->receiving_signal);

  THIS_UNLOCK(this);

done:
  return result;
}


static GstFlowReturn
gst_bufferpacer_sink_chainlist (GstPad * pad, GstObject * parent,
    GstBufferList * list)
{
  GstBuffer *buffer;
  gint i, len;
  GstFlowReturn result;

  result = GST_FLOW_OK;

  /* chain each buffer in list individually */
  len = gst_buffer_list_length (list);

  if (len == 0)
    goto done;

  for (i = 0; i < len; i++) {
    buffer = gst_buffer_list_get (list, i);

    result = gst_bufferpacer_sink_chain (pad, parent, buffer);
    if (result != GST_FLOW_OK)
      break;
  }

done:
  return result;
}


static gboolean gst_bufferpacer_sink_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
    gboolean ret;

    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_CAPS:
        case GST_EVENT_FLUSH_STOP:
        case GST_EVENT_STREAM_START:
        case GST_EVENT_SEGMENT:
        default:
            ret = gst_pad_event_default(pad, parent, event);
            break;
    }

    return ret;
}

static gboolean gst_bufferpacer_src_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
    gboolean ret;

    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_FLUSH_START:
        case GST_EVENT_RECONFIGURE:
        case GST_EVENT_FLUSH_STOP:
        default:
            ret = gst_pad_event_default(pad, parent, event);
            break;
    }

    return ret;
}


static gboolean
gst_bufferpacer_src_query (GstPad * srckpad, GstObject * parent,
    GstQuery * query)
{
  GstBufferPacer *this = GST_BUFFERPACER (parent);
  gboolean result;

  GST_DEBUG_OBJECT (this, "query");
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      gboolean live;
      GstClockTime min, max;
      GstPad *peer;
      peer = gst_pad_get_peer (this->sink);
      if ((result = gst_pad_query (peer, query))) {
          gst_query_parse_latency (query, &live, &min, &max);
          min= GST_MSECOND;
          max = -1;
          gst_query_set_latency (query, live, min, max);
      }
      gst_object_unref (peer);
    }
    break;
    default:
      result = gst_pad_query_default(srckpad, parent, query);
      break;
  }

  return result;
}



static void
bufferpacer_pacing_process (GstBufferPacer *this)
{
  GstBuffer *buffer;
  GstClockTime now, next_time;

  THIS_LOCK(this);

  if(g_queue_is_empty(this->packetsq)){
    g_cond_wait(&this->receiving_signal, &this->mutex);
  }
  buffer = (GstBuffer*) g_queue_pop_head(this->packetsq);
  if(!this->last_pts){
    gst_pad_push(this->src, buffer);
    this->last_pts = GST_BUFFER_PTS(buffer);
    goto done;
  }

  now       = _now(this);
  next_time = now + GST_BUFFER_PTS(buffer) - this->last_pts;
  if(now < next_time - GST_MSECOND){
    _wait(this, next_time, 1000);
  }

  gst_pad_push(this->src, buffer);
  this->last_pts = GST_BUFFER_PTS(buffer);

done:
  THIS_UNLOCK(this);
  return;
}

