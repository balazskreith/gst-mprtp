/*
 * sink.h
 *
 *  Created on: 13 Sep 2017
 *      Author: balazskreith
 */

#ifndef TESTS_STATSRELAYER_SOURCE_H_
#define TESTS_STATSRELAYER_SOURCE_H_
#include <gst/gst.h>
#include "common.h"

typedef enum {
  SOURCE_TYPE_FILE = 1,
  SOURCE_TYPE_MKFIFO = 2,
  SOURCE_TYPE_UNIX_DGRAM_SOCKET = 3,
  SOURCE_TYPE_STDIN = 4,
}SourceType;

typedef struct {
  SourceType type;
  gchar path[1024];
  PushPort* output;
  guint item_size;
  gpointer databed;
  Process* start_process;
  Process* stop_process;
  volatile gboolean stop;
  gchar* type_in_string;
  gint socket;
}Source;

Source* make_source(const gchar* string, guint item_size);
const gchar* source_get_type_in_string(Source* this);
const gchar* source_get_path(Source* this);
void source_dtor(Source* this) ;

#endif /* TESTS_STATSRELAYER_SOURCE_H_ */
