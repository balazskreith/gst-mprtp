#include "buffer.h"
#include <string.h>

static void _process(Buffer* this, gpointer item);

Buffer* make_buffer(guint item_size) {
  Buffer* this = g_malloc0(sizeof(Buffer));
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

void buffer_collect(Buffer* this) {
  this->collect = TRUE;
}

void buffer_set_on_first_received(Buffer* this, Process* on_first_received) {
  this->on_first_received = on_first_received;
}

void buffer_flush(Buffer* this) {
  while(!g_queue_is_empty(this->items)) {
    gpointer item = g_queue_pop_head(this->items);
    pushport_send(this->output, item);
    g_queue_push_tail(this->recycle, item);
  }
}

void buffer_prepare(Buffer* this, guint num) {
  gint i;
  for (i = 0; i < num; ++i) {
    g_queue_push_tail(this->recycle, g_malloc0(this->item_size));
  }
}

void _process(Buffer* this, gpointer item) {
  gpointer new_item = item;
  if (!this->first_received) {
    if (this->on_first_received) {
      process_call(this->on_first_received);
    } else {
      this->collect = TRUE;
    }
    this->first_received = TRUE;
  }
  if (!this->collect) {
    return;
  }

  if (!g_queue_is_empty(this->recycle)) {
    new_item = g_queue_pop_head(this->recycle);
  } else {
    new_item = g_malloc0(this->item_size);
  }
  memcpy(new_item, item, this->item_size);
  g_queue_push_tail(this->items, new_item);
}
