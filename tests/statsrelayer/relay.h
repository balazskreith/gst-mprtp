#ifndef TESTS_STATSRELAYER_RELAY_H_
#define TESTS_STATSRELAYER_RELAY_H_
#include <gst/gst.h>
#include "common.h"
#include "sink.h"
#include "source.h"
#include "buffer.h"
#include "mapper.h"

typedef struct {
  Sink* sink;
  Mapper* mapper;
  Buffer* buffer;
  Source* source;
  Process* on_first_received;
}Relay;

Relay* make_relay(const gchar* string, guint item_size);
void relay_start(Relay* this);
void relay_flush(Relay* this);
void relay_prepare(Relay* this, gint prepare_num);
void relay_stop(Relay* this);
void relay_dtor(Relay* this);

#endif /* TESTS_STATSRELAYER_RELAY_H_ */
