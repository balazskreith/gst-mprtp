#include "source.h"
#include "transceiver.h"

#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

static GstElement* _make_file_source(SourceParams *params);
static GstElement* _make_testvideo_source(SourceParams *params);
//static void _setup_rawproxy_source(GstBin* encoderBin, SourceParams *params);
GstElement* _make_livefile_source(SourceParams *params);

static GstFlowReturn _on_new_sample_from_receiver (GstElement * sink, GstElement* source);

Source* source_ctor(void)
{
  Source* this;

  this = g_malloc0(sizeof(Source));

  return this;
}

void source_dtor(Source* this)
{
  g_free(this);
}


Source* make_source(SourceParams *params)
{
  GstElement* source;
  Source* this = source_ctor();
  GstBin* sourceBin     = GST_BIN(gst_bin_new(NULL));

  switch(params->type){
    case SOURCE_TYPE_TESTVIDEO:
      source = _make_testvideo_source(params);
      break;
    case SOURCE_TYPE_RAWPROXY:
//      _setup_rawproxy_source(sourceBin, params);
      break;
    case SOURCE_TYPE_FILE:
      source = _make_file_source(params);
      break;
    case SOURCE_TYPE_LIVEFILE:
      source = _make_livefile_source(params);
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

GstElement* _make_testvideo_source(SourceParams *params)
{
  GstElement *source = gst_element_factory_make ("videotestsrc", NULL);

  g_object_set (source,
      "is-live", TRUE,
      "horizontal-speed", 15,
      NULL);

  return source;
}


GstElement* _make_file_source(SourceParams *params)
{
  GstElement* fileSrc          = gst_bin_new(NULL);
  GstElement* multifilesrc     = gst_element_factory_make ("multifilesrc", NULL);
  GstElement* videoparse       = gst_element_factory_make ("videoparse", NULL);
  GstElement* autovideoconvert = gst_element_factory_make ("autovideoconvert", NULL);

  gst_bin_add_many(GST_BIN(fileSrc),

      multifilesrc,
      videoparse,
      autovideoconvert,
      NULL

  );

  g_object_set(multifilesrc,
      "location", params->file.location,
      "loop",     params->file.loop,
      NULL
  );

  g_object_set(videoparse,
      "width", atoi(params->file.width),
      "height", atoi(params->file.height),
      "framerate", params->file.framerate.numerator, params->file.framerate.divider,
      "format", params->file.format,
      NULL
  );

  gst_element_link_many(multifilesrc, videoparse, autovideoconvert, NULL);

  setup_ghost_src(autovideoconvert, fileSrc);

  return GST_ELEMENT(fileSrc);
}

static void _on_playing(GstPipeline *readerPipe, gpointer user_data)
{
  gst_element_set_state (GST_ELEMENT (readerPipe), GST_STATE_PLAYING);
  g_print("Livefile source called to play\n");
}

static void _on_destroy(GstPipeline *readerPipe, gpointer user_data)
{
  g_print("Livefile source called to destroy\n");
  gst_element_set_state (GST_ELEMENT (readerPipe), GST_STATE_NULL);
  gst_object_unref (readerPipe);
}


GstElement* _make_livefile_source(SourceParams *params)
{
  GstPipeline* readerPipe      = gst_pipeline_new("readerPipe");
  GstElement* fileSrc          = _make_file_source(params);
  GstElement* appsink    = gst_element_factory_make ("appsink", NULL);

  GstElement *appsrc = gst_element_factory_make ("appsrc", NULL);

  gst_bin_add_many(GST_BIN(readerPipe),

      fileSrc,
      appsink,
      NULL

  );

  g_object_set (G_OBJECT (appsink), "emit-signals", TRUE,
      //"sync", FALSE, "async", FALSE,
      NULL);

  g_signal_connect (appsink, "new-sample", G_CALLBACK (_on_new_sample_from_receiver), appsrc);

  gst_element_link_many(fileSrc, appsink, NULL);

  notifier_add_listener(get_sender_eventers()->on_playing, (listener) _on_playing, readerPipe);
  notifier_add_listener(get_sender_eventers()->on_destroy, (listener) _on_destroy, readerPipe);

  g_object_set (appsrc,
      "is-live", TRUE,
      "format", GST_FORMAT_TIME,
      NULL);

  return appsrc;
}
//
//GstElement* _make_livefile_source(SourceParams *params)
//{
//  GstPipeline* readerPipe      = gst_pipeline_new("readerPipe");
//  GstElement* multifilesrc     = gst_element_factory_make ("multifilesrc", NULL);
//  GstElement* videoparse       = gst_element_factory_make ("videoparse", NULL);
//  GstElement* autovideoconvert = gst_element_factory_make ("autovideoconvert", NULL);
//  GstElement* appsink    = gst_element_factory_make ("appsink", NULL);
//
//  GstElement *appsrc = gst_element_factory_make ("appsrc", NULL);
//
//  gst_bin_add_many(GST_BIN(readerPipe),
//
//      multifilesrc,
//      videoparse,
//      autovideoconvert,
//      appsink,
//      NULL
//
//  );
//
//  g_object_set(multifilesrc,
//      "location", params->file.location,
//      "loop",     params->file.loop,
//      NULL
//  );
//
//  g_object_set(videoparse,
//      "width", atoi(params->file.width),
//      "height", atoi(params->file.height),
//      "framerate", params->file.framerate.numerator, params->file.framerate.divider,
//      "format", params->file.format,
//      NULL
//  );
//
//  g_object_set (G_OBJECT (appsink), "emit-signals", TRUE,
//      //"sync", FALSE, "async", FALSE,
//      NULL);
//
//  g_signal_connect (appsink, "new-sample", G_CALLBACK (_on_new_sample_from_receiver), appsrc);
//
//  gst_element_link_many(multifilesrc, videoparse, autovideoconvert, appsink, NULL);
//
//  notifier_add_listener(get_sender_eventers()->on_playing, (listener) _on_playing, readerPipe);
//  notifier_add_listener(get_sender_eventers()->on_destroy, (listener) _on_destroy, readerPipe);
//
//  g_object_set (appsrc,
//      "is-live", TRUE,
//      "format", GST_FORMAT_TIME,
//      NULL);
//
//  return appsrc;
//}

//
//void _setup_rawproxy_source(GstBin* sourceBin, SourceParams *params)
//{
//  GstElement* receiver    = gst_element_factory_make("udpsrc", NULL);
//  GstElement* rawDepay    = gst_element_factory_make("rtpvrawdepay", NULL);
//  GstElement* transceiver = make_transceiver();
//  GstElement* videoParse  = gst_element_factory_make("videoparse", NULL);
//
//  GstElement* tee         = gst_element_factory_make("tee", NULL);
//  GstElement* queue       = gst_element_factory_make("queue", NULL);
//  GstElement* source        = gst_element_factory_make("autovideosource", NULL);
//
//  const GstCaps* caps        = gst_caps_new_simple ("application/x-rtp",
//      "media", G_TYPE_STRING, "video",
//      "clock-rate", G_TYPE_INT, params->clock_rate,
//      "width", G_TYPE_STRING, "352",
//      "height", G_TYPE_STRING, "288",
//      "sampling", G_TYPE_STRING, "YCbCr-4:2:0",
//      "framerate", GST_TYPE_FRACTION, params->framerate.numerator, proxy_params->framerate.divider,
//      "encoding-name", G_TYPE_STRING, "RAW", NULL
//      );
//
//  g_print("Caps: %s\n", gst_caps_to_string(caps));
//
//  g_object_set(G_OBJECT(receiver),
//      "port", params->port,
//      "caps", caps,
//      NULL);
//
//  g_object_set(G_OBJECT(videoParse),
//      "format", 2,
//      "width", 352,
//      "height", 288,
//      "framerate", 25, 1,
//      NULL
//  );
//
//  gst_bin_add_many (sourceBin, receiver, rawDepay, transceiver, videoParse,
//      tee, queue, source,
//      NULL);
//
//
//  gst_element_link_pads(receiver, "src", rawDepay, "source");
//  gst_element_link_pads(rawDepay, "src", tee, "source");
//  gst_element_link_pads(tee, "src_1", transceiver, "source");
//
//  gst_element_link_pads(tee, "src_2", queue, "source");
//  gst_element_link_pads(queue, "src", videoParse, "source");
//  gst_element_link_pads(videoParse, "src", source, "source");
//
//  //  gst_element_link_many(receiver, rawDepay, transceiver, videoParse, NULL);
//
//  g_print("CAPS!!!: %s\n",
//      gst_caps_to_string(gst_pad_get_current_caps(gst_element_get_static_pad(videoParse, "src"))));
//
//  setup_ghost_src (transceiver, sourceBin);
//}



static GstFlowReturn
_on_new_sample_from_receiver (GstElement * sink, GstElement* source)
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


