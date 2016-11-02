#include "source.h"
#include "transceiver.h"

static void _setup_testvideo_source(GstBin* encoderBin, SourceParams *params);
//static void _setup_rawproxy_source(GstBin* encoderBin, SourceParams *params);


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
  Source* this = g_malloc0(sizeof(Source));
  GstBin* sourceBin     = GST_BIN(gst_bin_new(NULL));

  switch(params->type){
    case SOURCE_TYPE_TESTVIDEO:
      _setup_testvideo_source(sourceBin, params);
      break;
    case SOURCE_TYPE_RAWPROXY:
      _setup_rawproxy_source(sourceBin, params);
      break;
  };

  this->element = GST_ELEMENT(sourceBin);
  return this;
}

void _setup_testvideo_source(GstBin* sourceBin, SourceParams *params)
{
  GstElement *source = gst_element_factory_make ("videotestsrc", NULL);
  g_object_set (source,
      "is-live", TRUE,
      "horizontal-speed", 1,
      NULL);

  gst_bin_add_many (sourceBin, source,
      NULL);

  setup_ghost_src(source,  sourceBin);
}

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



