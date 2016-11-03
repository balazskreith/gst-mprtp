#include "sink.h"

static GstElement* _make_autovideo_sink(SinkParams *params);
static void _setup_rawproxy_sink(GstBin* encoderBin, SinkParams *params);

Sink* sink_ctor(void)
{
  Sink* this;

  this = g_malloc0(sizeof(Sink));

  return this;
}

void sink_dtor(Sink* this)
{
  g_free(this);
}


Sink* make_sink(SinkParams *params)
{
  Sink* this = sink_ctor();
  GstBin* sinkBin     = GST_BIN(gst_bin_new(NULL));
  GstElement* sink = NULL;

  switch(params->type){
    case SINK_TYPE_AUTOVIDEO:
      sink = _make_autovideo_sink(params);
      break;
    case SINK_TYPE_RAWPROXY:
      _setup_rawproxy_sink(sinkBin, params);
      break;
  };

  gst_bin_add_many(sinkBin,
      sink,
      NULL
  );

  setup_ghost_sink(sink, sinkBin);

  this->element = GST_ELEMENT(sinkBin);

  return this;
}

GstElement* _make_autovideo_sink(SinkParams *params)
{
  GstElement* autovideosink = gst_element_factory_make("autovideosink", NULL);

  g_object_set (autovideosink, "sync", FALSE, NULL);

  return autovideosink;
}

void _setup_rawproxy_sink(GstBin* sinkBin, SinkParams *params)
{

}


