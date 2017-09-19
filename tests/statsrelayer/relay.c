#include "relay.h"

Relay* make_relay(const gchar* string, guint item_size) {
  Relay* this = g_malloc0(sizeof(Relay));
  gchar **tokens = g_strsplit(string, "|", -1);
  Source* source = make_source(tokens[0], item_size);
  Buffer* buffer = make_buffer(item_size);
  Sink* sink = make_sink(tokens[1], item_size);

  source->output = buffer->input;
  buffer->output = sink->input;

  g_strfreev(tokens);
  this->source = source;
  this->buffer = buffer;
  this->sink = sink;
  return this;
}

void relay_start(Relay* this) {
  process_call(this->source->start_process);
}

void relay_flush(Relay* this) {
  buffer_flush(this->buffer);
}

void relay_stop(Relay* this) {
  process_call(this->source->stop_process);
  process_call(this->sink->stop_process);
}

void relay_dtor(Relay* this) {
  source_dtor(this->source);
  buffer_dtor(this->buffer);
  sink_dtor(this->sink);
  g_free(this);
}
