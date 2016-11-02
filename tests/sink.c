#include "sink.h"

static void _setup_autovideo_sink(GstBin* encoderBin, SinkParams *params);
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

  switch(params->type){
    case SINK_TYPE_AUTOVIDEO:
      _setup_autovideo_sink(sinkBin, params);
      break;
    case SINK_TYPE_RAWPROXY:
      _setup_rawproxy_sink(sinkBin, params);
      break;
  };

  this->element = GST_ELEMENT(sinkBin);
  return this;
}

void _setup_autovideo_sink(GstBin* sinkBin, SinkParams *params)
{


}

void _setup_rawproxy_sink(GstBin* sinkBin, SinkParams *params)
{

}


