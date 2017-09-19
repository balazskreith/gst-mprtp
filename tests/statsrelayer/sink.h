/*
 * sink.h
 *
 *  Created on: 13 Sep 2017
 *      Author: balazskreith
 */

#ifndef TESTS_STATSRELAYER_SINK_H_
#define TESTS_STATSRELAYER_SINK_H_
#include <gst/gst.h>
#include <stdio.h>
#include "common.h"

typedef enum {
  SINK_TYPE_FILE = 1,
  SINK_TYPE_MKFIFO = 2,
  SINK_TYPE_UNIX_DGRAM_SOCKET = 3
}SinkType;

typedef enum {
  WRITE_FORMAT_BINARY = 1,
  WRITE_FORMAT_PACKET_CSV = 2,
}WriteFormat;

typedef struct _Sink Sink;
typedef void (*SinkWriterProcess)(Sink* this, gpointer item, gint item_length);

struct _Sink{
  SinkType type;
  WriteFormat format;
  gchar* format_in_string;
  SinkWriterProcess writer_process;
  gchar path[1024];
  gchar* type_in_string;
  union {
    FILE* fp;
    gint socket;
  };
  PushPort* input;
  guint item_size;
  Process* stop_process;
  union { // some utils merged for different type of sink
    struct {
      gboolean file_opened;
      gchar file_open_mode[256];
    };
  };
};

Sink* make_sink(const gchar* string, guint item_size);
const gchar* sink_get_type_in_string(Sink* this);
const gchar* sink_get_format_in_string(Sink* this);
const gchar* sink_get_path(Sink* this);
void sink_dtor(Sink* this);

#endif /* TESTS_STATSRELAYER_SINK_H_ */
