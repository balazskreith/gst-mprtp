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

typedef struct _Sink{
  SinkType type;
  gchar path[1024];
  union {
    FILE* fp;
    gint socket;
  };
  PushPort* input;
  guint item_size;
  Process* stop_process;
  volatile gboolean stop;
}Sink;

Sink* make_sink(const gchar* string, guint item_size);
void sink_dtor(Sink* this);

#endif /* TESTS_STATSRELAYER_SINK_H_ */
