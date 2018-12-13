#ifndef TESTS_SOURCE_H_
#define TESTS_SOURCE_H_

#include <gst/gst.h>
#include <string.h>

#include "pipeline.h"

typedef struct{
  GstElement*    element;
  gchar          bin_name[256];
  ObjectsHolder* objects_holder;

  Subscriber     on_playing;
  Subscriber     on_destroy;

  GstClockTime pts;
  GstClockTime dts;
  GstClockTime dpts;
  GstClockTime ddts;
  gboolean dinit;
  guint64 offset;
  guint64 doffset;

  union {
    struct{
      GQueue* buffers;
      gint cached_bytes;
      GstElement* appsrc;
      GstPipeline* readerPipe;
      gint interval_in_ms;
    }livesource;
  };
}Source;


Source* source_ctor(void);
void source_dtor(Source* this);
Source* make_source(SourceParams *params, SinkParams* sink_params);

#endif /* TESTS_SINK_H_ */
