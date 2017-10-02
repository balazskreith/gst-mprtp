#include "buffer.h"
#include <string.h>
#include <stdio.h>

static void _process(Buffer* this, gpointer item);

Buffer* make_buffer(guint item_size) {
  Buffer* this = g_malloc0(sizeof(Buffer));
  fprintf(stdout, "Create Buffer\n");
  this->items = g_queue_new();
  this->recycle = g_queue_new();
  this->input = make_pushport((PushCb)_process, this);
  this->item_size = item_size;
  return this;
}

void buffer_dtor(Buffer* this) {
  pushport_dtor(this->input);
  g_queue_free_full(this->recycle, g_free);
  g_queue_free_full(this->items, g_free);
  g_free(this);
}


void buffer_flush(Buffer* this) {
  while(!g_queue_is_empty(this->items)) {
    gpointer item = g_queue_pop_head(this->items);
    pushport_send(this->output, item);
    g_queue_push_tail(this->recycle, item);
    ++this->sent_packets;
    this->sent_bytes += this->item_size;
  }
}

void buffer_sprintf(Buffer* this, gchar* string) {
  sprintf(string, "Buffer number of transcieved items: %d, amount of bytes: %d->%d\n",
      this->rcved_packets, this->rcved_bytes, this->sent_bytes);
}

void buffer_reset_metrics(Buffer* this) {
  this->rcved_packets = 0;
  this->sent_packets = 0;
  this->rcved_bytes = 0;
  this->sent_bytes = 0;
}

void buffer_prepare(Buffer* this, guint num) {
  gint i;
  for (i = 0; i < num; ++i) {
    g_queue_push_tail(this->recycle, g_malloc0(this->item_size));
  }
}

void _process(Buffer* this, gpointer item) {
  gpointer new_item = item;
  if (!g_queue_is_empty(this->recycle)) {
    new_item = g_queue_pop_head(this->recycle);
  } else {
    new_item = g_malloc0(this->item_size);
  }
  memcpy(new_item, item, this->item_size);
  g_queue_push_tail(this->items, new_item);
  ++this->rcved_packets;
  this->rcved_bytes += this->item_size;
}
