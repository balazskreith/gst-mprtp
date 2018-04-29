#include "source.h"
#include "receiver.h"
#include "sink.h"

#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

static GstElement* _make_testvideo_source(Source* this,SourceParams *params);
static GstElement* _make_raw_source(Source* this,      SourceParams *params);
static GstElement* _make_file_source(Source* this,     SourceParams *params);
static GstElement* _make_v4l2_source(Source* this,     SourceParams *params);
static int _instance_counter = 0;

Source* source_ctor(void)
{
  Source* this;

  this = g_malloc0(sizeof(Source));
  this->objects_holder = objects_holder_ctor();
  sprintf(this->bin_name, "SourceBin_%d", _instance_counter++);

  this->on_destroy.subscriber_func = on_fi_called;
  this->on_playing.subscriber_func = on_fi_called;
  return this;
}

void source_dtor(Source* this)
{
  object_holder_dtor(this->objects_holder);
  g_free(this);
}


Source* make_source(SourceParams *params, SinkParams* sink_params)
{

  Source* this = source_ctor();
  GstBin* sourceBin     = GST_BIN(gst_bin_new(this->bin_name));
  GstElement* source = NULL;
  GstElement* src = NULL;

  switch(params->type){
    case SOURCE_TYPE_TESTVIDEO:
      src = source = _make_testvideo_source(this, params);
      break;
    case SOURCE_TYPE_RAWPROXY:
      src = source = _make_raw_source(this, params);
      break;
    case SOURCE_TYPE_FILE:
      src = source = _make_file_source(this, params);
      break;
    case SOURCE_TYPE_V4L2:
      src = source = _make_v4l2_source(this, params);
      break;
  };


  gst_bin_add_many (sourceBin,
      source,
      NULL
  );

  if(sink_params){
      GstElement* tee     = gst_element_factory_make("tee", NULL);
      GstElement* q1      = gst_element_factory_make("queue", NULL);
      GstElement* q2      = gst_element_factory_make("queue", NULL);
      Sink*       sink    = make_sink(sink_params);

      objects_holder_add(this->objects_holder, sink, (GDestroyNotify)sink_dtor);

      gst_bin_add_many(sourceBin, tee, q1, q2, sink->element, NULL);

      gst_element_link(source, tee);
      gst_element_link_pads(tee, "src_1", q1, "sink");
      src = q1;

      gst_element_link_pads(tee, "src_2", q2, "sink");
      gst_element_link_many(q2, sink->element, NULL);
    }

  setup_ghost_src(src,  sourceBin);

  this->element = GST_ELEMENT(sourceBin);
  return this;
}

GstElement* _make_testvideo_source(Source* this, SourceParams *params)
{
  GstBin*     srcBin     = gst_bin_new(NULL);
  GstElement* source     = gst_element_factory_make ("videotestsrc", NULL);
  GstElement* capsfilter = gst_element_factory_make ("capsfilter", NULL);


  g_object_set (source,
      "is-live", TRUE,
      "horizontal-speed", 15,
      NULL);

  g_object_set(capsfilter, "caps",
      gst_caps_new_simple("video/x-raw",
          "framerate", GST_TYPE_FRACTION, 100, 1,
          "width", G_TYPE_INT, 352,
          "height",G_TYPE_INT,  288, NULL),
          NULL);

  gst_bin_add_many(srcBin, source, capsfilter, NULL);
  gst_element_link(source, capsfilter);
  setup_ghost_src(capsfilter, srcBin);
//  return source;
  return GST_ELEMENT(srcBin);
}


static void _on_playing(GstPipeline *readerPipe, gpointer user_data)
{
  g_print("Livefile source called to play\n");
  gst_element_set_state (GST_ELEMENT (readerPipe), GST_STATE_PLAYING);
}

static void _on_destroy_readerpipeline(GstPipeline *readerPipe, gpointer user_data)
{
  g_print("Livefile source called to destroy\n");
  gst_element_set_state (GST_ELEMENT (readerPipe), GST_STATE_NULL);
  gst_object_unref (readerPipe);
}

static gboolean _push_frames(gpointer user_data) {
  Source* this = user_data;
  GstBuffer* buffer = g_queue_pop_head(this->livesource.buffers);
  GstFlowReturn result = gst_app_src_push_buffer (GST_APP_SRC (this->livesource.appsrc), gst_buffer_ref(buffer));
  g_queue_push_tail(this->livesource.buffers, gst_buffer_ref(buffer));
  return result == GST_FLOW_OK;
}

static void _on_destroy_frames_pusher(GstPipeline *readerPipe, gpointer user_data)
{
//  Source* this = user_data;

}

static gboolean
_readerpipe_bus_callback (GstBus *bus,
         GstMessage *message,
         gpointer    data)
{
  Source* this = data;
  gint message_type = GST_MESSAGE_TYPE (message);

  if (message_type != GST_MESSAGE_EOS) {
    if (message_type == GST_MESSAGE_ERROR) {
      GError *err;
      gchar *debug;
      gst_message_parse_error (message, &err, &debug);
      g_print ("Error: %s\n", err->message);
      g_error_free (err);
      g_free (debug);
    }
    /* we want to be notified again the next time there is a message
     * on the bus, so returning TRUE (FALSE means we want to stop watching
     * for messages on the bus and our callback should not be called again)
     */
    return TRUE;
  }

  // Print information about the queue
  g_print("The file has been read. Number of frames read: %d, Source frequency from now: %d, The collected bytes: %d\n",
      g_queue_get_length(this->livesource.buffers),
      this->livesource.interval_in_ms,
      this->livesource.cached_bytes);

  _on_destroy_readerpipeline(this->livesource.readerPipe, NULL);
  g_queue_pop_tail(this->livesource.buffers);

  this->on_destroy.subscriber_obj  = this;
  this->on_destroy.subscriber_func = (subscriber) _on_destroy_frames_pusher;

  g_timeout_add(this->livesource.interval_in_ms, _push_frames, this);
  // we destroy readerpipeline and stop receiving messages
  return FALSE;
}

static GstFlowReturn _on_new_sample_from_sink (GstElement * sink, Source* this)
{
  GstSample *sample;
  GstFlowReturn result;
  GstBuffer* buffer;

  sample = gst_app_sink_pull_sample (GST_APP_SINK (sink));
  buffer = gst_sample_get_buffer(sample);
  g_queue_push_tail(this->livesource.buffers, gst_buffer_ref(buffer));
  result = gst_app_src_push_buffer (GST_APP_SRC (this->livesource.appsrc), gst_buffer_ref(buffer));
  gst_sample_unref (sample);
  this->livesource.cached_bytes += gst_buffer_get_size(buffer);
  return result;
}

GstElement* _make_file_source(Source* this, SourceParams *params)
{
  GstPipeline* readerPipe      = gst_pipeline_new(NULL);
  GstBin* sourceBin            = gst_bin_new(NULL);
//  GstElement* multifilesrc     = gst_element_factory_make ("multifilesrc", NULL);
  GstElement* multifilesrc     = gst_element_factory_make ("filesrc", NULL);
  GstElement* sink_videoparse  = gst_element_factory_make ("videoparse", NULL);
  GstElement* src_videoparse   = gst_element_factory_make ("videoparse", NULL);
  GstElement* autovideoconvert = gst_element_factory_make ("autovideoconvert", NULL);

  GstElement* appsink    = gst_element_factory_make ("appsink", NULL);
  GstElement* appsrc = gst_element_factory_make ("appsrc", NULL);

  this->livesource.appsrc = appsrc;
  this->livesource.buffers = g_queue_new();
  this->livesource.readerPipe = readerPipe;

  gst_bin_add_many(GST_BIN(readerPipe),

      multifilesrc,
      sink_videoparse,
      autovideoconvert,
      appsink,
      NULL

  );

  g_object_set(multifilesrc,
      "location", params->file.location,
//      "loop",     params->file.loop,
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

  {
    GstBus* bus = gst_element_get_bus (GST_ELEMENT (readerPipe));
    gint bus_watch_id = gst_bus_add_watch (bus, _readerpipe_bus_callback, this);
    gst_object_unref (bus);
  }

  // calculate the frequency
  {
    this->livesource.interval_in_ms = 1000 / params->file.framerate.numerator;
  }


  g_object_set (G_OBJECT (appsink), "emit-signals", TRUE,
//      "sync", FALSE, "async", FALSE,
      NULL);

  g_signal_connect (appsink, "new-sample", G_CALLBACK (_on_new_sample_from_sink), this);

  gst_element_link_many(multifilesrc, sink_videoparse, autovideoconvert, appsink, NULL);

  this->on_playing.subscriber_obj  = readerPipe;
  this->on_playing.subscriber_func = (subscriber) _on_playing;

  this->on_destroy.subscriber_obj  = readerPipe;
  this->on_destroy.subscriber_func = (subscriber) _on_destroy_readerpipeline;

  gst_bin_add_many(sourceBin,
      this->livesource.appsrc,
      src_videoparse,
      NULL);

  g_object_set (this->livesource.appsrc,
      "is-live", TRUE,
      "format", GST_FORMAT_TIME,
      NULL);

  gst_element_link(this->livesource.appsrc, src_videoparse);
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
  Receiver*   receiver    = make_receiver(params->rawproxy.rcv_transfer_params, NULL, NULL, NULL);
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





