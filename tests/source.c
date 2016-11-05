#include "source.h"
#include "transceiver.h"
#include "receiver.h"

#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

static GstElement* _make_testvideo_source(Source* this,SourceParams *params);
static GstElement* _make_raw_source(Source* this,      SourceParams *params);
static GstElement* _make_file_source(Source* this,     SourceParams *params);
static GstElement* _make_v4l2_source(Source* this,     SourceParams *params);

Source* source_ctor(void)
{
  Source* this;

  this = g_malloc0(sizeof(Source));
  this->on_destroy.listener_func = on_fi_called;
  this->on_playing.listener_func = on_fi_called;
  return this;
}

void source_dtor(Source* this)
{
  g_free(this);
}


Source* make_source(SourceParams *params)
{

  Source* this = source_ctor();
  GstBin* sourceBin     = GST_BIN(gst_bin_new(NULL));
  GstElement* source = NULL;

  switch(params->type){
    case SOURCE_TYPE_TESTVIDEO:
      source = _make_testvideo_source(this, params);
      break;
    case SOURCE_TYPE_RAWPROXY:
      source = _make_raw_source(this, params);
      break;
    case SOURCE_TYPE_FILE:
      source = _make_file_source(this, params);
      break;
    case SOURCE_TYPE_V4L2:
      source = _make_v4l2_source(this, params);
      break;
  };


  gst_bin_add_many (sourceBin,
      source,
      NULL
  );

  setup_ghost_src(source,  sourceBin);

  this->element = GST_ELEMENT(sourceBin);
  return this;
}

GstElement* _make_testvideo_source(Source* this, SourceParams *params)
{
  GstElement *source = gst_element_factory_make ("videotestsrc", NULL);

  g_object_set (source,
      "is-live", TRUE,
      "horizontal-speed", 15,
      NULL);

  return source;
}


static void _on_playing(GstPipeline *readerPipe, gpointer user_data)
{
  g_print("Livefile source called to play\n");
  gst_element_set_state (GST_ELEMENT (readerPipe), GST_STATE_PLAYING);
}

static void _on_destroy(GstPipeline *readerPipe, gpointer user_data)
{
  g_print("Livefile source called to destroy\n");
  gst_element_set_state (GST_ELEMENT (readerPipe), GST_STATE_NULL);
  gst_object_unref (readerPipe);
}


static GstFlowReturn _on_new_sample_from_sink (GstElement * sink, GstElement* source)
{
  GstSample *sample;
  GstFlowReturn result;
  GstBuffer* buffer;

  sample = gst_app_sink_pull_sample (GST_APP_SINK (sink));
  buffer = gst_sample_get_buffer(sample);
  result = gst_app_src_push_buffer (GST_APP_SRC (source), gst_buffer_ref(buffer));
  gst_sample_unref (sample);

  return result;
}

GstElement* _make_file_source(Source* this, SourceParams *params)
{
  GstPipeline* readerPipe      = gst_pipeline_new("readerPipe");
  GstBin* sourceBin            = gst_bin_new(NULL);
  GstElement* multifilesrc     = gst_element_factory_make ("multifilesrc", NULL);
  GstElement* sink_videoparse  = gst_element_factory_make ("videoparse", NULL);
  GstElement* src_videoparse   = gst_element_factory_make ("videoparse", NULL);
  GstElement* autovideoconvert = gst_element_factory_make ("autovideoconvert", NULL);

  GstElement* appsink    = gst_element_factory_make ("appsink", NULL);
  GstElement *appsrc     = gst_element_factory_make ("appsrc", NULL);

  gst_bin_add_many(GST_BIN(readerPipe),

      multifilesrc,
      sink_videoparse,
      autovideoconvert,
      appsink,
      NULL

  );

  g_object_set(multifilesrc,
      "location", params->file.location,
      "loop",     params->file.loop,
      NULL
  );

  g_object_set(sink_videoparse,
      "width", atoi(params->file.width),
      "height", atoi(params->file.height),
      "framerate", params->file.framerate.numerator, params->file.framerate.divider,
      "format", params->file.format,
      NULL
  );

  g_object_set(src_videoparse,
      "width", atoi(params->file.width),
      "height", atoi(params->file.height),
      "framerate", params->file.framerate.numerator, params->file.framerate.divider,
      "format", params->file.format,
      NULL
  );

  g_object_set (G_OBJECT (appsink), "emit-signals", TRUE,
//      "sync", FALSE, "async", FALSE,
      NULL);

  g_signal_connect (appsink, "new-sample", G_CALLBACK (_on_new_sample_from_sink), appsrc);

  gst_element_link_many(multifilesrc, sink_videoparse, autovideoconvert, appsink, NULL);

  this->on_playing.listener_obj  = readerPipe;
  this->on_playing.listener_func = (listener) _on_playing;

  this->on_destroy.listener_obj  = readerPipe;
  this->on_destroy.listener_func = (listener) _on_destroy;

  gst_bin_add_many(sourceBin,
      appsrc,
      src_videoparse,
      NULL);

  g_object_set (appsrc,
      "is-live", TRUE,
      "format", GST_FORMAT_TIME,
      NULL);

  gst_element_link(appsrc, src_videoparse);
  setup_ghost_src(src_videoparse, sourceBin);
  return GST_ELEMENT(sourceBin);
}


GstElement* _make_v4l2_source(Source* this, SourceParams *params)
{
  GstElement* v4l2src      = gst_element_factory_make ("v4l2src", NULL);
  return v4l2src;
}

GstElement* _make_raw_source(Source* this, SourceParams *params)
{
  GstBin*     rawBin      = gst_bin_new(NULL);
  Receiver*   receiver    = make_receiver(NULL, NULL, params->rawproxy.rcv_transfer_params);
  GstElement* rawDepay    = gst_element_factory_make("rtpvrawdepay", NULL);

  const GstCaps* caps        = gst_caps_new_simple ("application/x-rtp",
        "media", G_TYPE_STRING, "video",
        "clock-rate", G_TYPE_INT, params->rawproxy.clock_rate,
        "width", G_TYPE_STRING, params->rawproxy.width,
        "height", G_TYPE_STRING, params->rawproxy.width,
        "payload",G_TYPE_INT, 96,
        "sampling", G_TYPE_STRING, "YCbCr-4:2:0",
        "framerate", GST_TYPE_FRACTION, params->rawproxy.framerate.numerator, params->rawproxy.framerate.divider,
        "encoding-name", G_TYPE_STRING, "RAW", NULL
        );


  gst_bin_add_many(rawBin,

      receiver->element,
      rawDepay,

      NULL
  );

  receiver_on_caps_change(receiver, caps);


  gst_element_link_many(receiver->element, rawDepay, NULL);

  setup_ghost_src(rawDepay, rawBin);
  return GST_ELEMENT(rawBin);
}





