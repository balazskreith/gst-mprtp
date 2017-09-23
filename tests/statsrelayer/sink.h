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

typedef struct _Sink Sink;

struct _Sink{
  SinkType type;
  gchar path[1024];
  gchar* type_in_string;
  union {
    FILE* fp;
    gint socket;
  };
  PushPort* input;
  Process* stop_process;
  union { // some utils merged for different type of sink
    struct {
      gboolean file_opened;
      gchar file_open_mode[256];
    };
  };
};

Sink* make_sink(const gchar* string);
const gchar* sink_get_type_in_string(Sink* this);
const gchar* sink_get_path(Sink* this);
void sink_dtor(Sink* this);

#endif /* TESTS_STATSRELAYER_SINK_H_ */
