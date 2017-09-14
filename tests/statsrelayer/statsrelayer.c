#include "statsrelayer.h"
#include "common.h"
#include "relay.h"
#include <stdio.h>

typedef struct {
  gchar name[255];
  GSList* threads;
  volatile guint first_arrived;
  Process* on_first_arrived;
}RelaysGroup;

StatsRelayer* make_statsrelayer(void) {
  StatsRelayer* this = g_malloc0(sizeof(StatsRelayer));
  return this;
}

static void _on_first_arrived(RelaysGroup* this) {
  GSList* it;
  if (++this->first_arrived < g_slist_length(this->threads)) {
    return;
  }
  for (it = this->threads; it; it = it->next) {
    GThread* thread = it->data;
    Relay* relay = thread->data;
    relay_collect(relay);
  }
  fprintf(stdout, "Relays group %s is ordered to collect\n", this->name);
}

void statsrelayer_add_group(StatsRelayer* this, const gchar* string) {
  gchar *relays = NULL;
  gchar *name = NULL;
  RelaysGroup* relays_group = g_malloc0(sizeof(RelaysGroup));
  gchar **tokens = g_strsplit(string, " ", -1);
  name = tokens[0];
  relays = tokens[1];
  relays_group->on_first_arrived = make_process((ProcessCb) _on_first_arrived, relays_group);

  if (relays) {
    gchar **tokens = g_strsplit(relays, "!", -1);
    gint i, len = g_strv_length(tokens);
    for (i = 0; i < len; ++i) {
      Relay* relay = make_relay(tokens[i], sizeof(Packet));
      GThread* thread;
      relay_set_on_first_received(relay, relays_group->on_first_arrived);
      thread = g_thread_new(name, (GThreadFunc) relay_start, relay);
      relays_group->threads = g_slist_prepend(relays_group->threads, thread);
    }

    g_strfreev(tokens);
  }

  if (name) {
    strcpy(relays_group->name, name);
  }

  this->groups = g_slist_prepend(this->groups, relays_group);
  g_strfreev(tokens);
  fprintf(stdout, "Relays group %s is added\n", relays_group->name);
}

static void _rem_group(StatsRelayer* this, RelaysGroup* relays_group) {
  GSList* it;
  this->groups = g_slist_remove(this->groups, relays_group);
  for (it = relays_group->threads; it; it = it->next) {
    GThread* thread = it->data;
    Relay* relay = thread->data;
    relay_stop(relay);
    g_thread_join(thread);
    relay_dtor(relay);
  }
  fprintf(stdout, "Relays group %s is removed\n", relays_group->name);
  g_free(relays_group);
}

void statsrelayer_rem_group(StatsRelayer* this, const gchar* name) {
  GSList* it;
  GQueue* subjects = g_queue_new();
  for (it = this->groups; it; it = it->next) {
    RelaysGroup* relays_group = it->data;
    if (strcmp(name, "*") != 0 && strcmp(name, relays_group->name) != 0) {
      continue;
    }
    g_queue_push_tail(subjects, relays_group);
  }

  while(!g_queue_is_empty(subjects)) {
    RelaysGroup* relays_group = g_queue_pop_head(subjects);
    _rem_group(this, relays_group);
  }
  g_queue_free(subjects);
}


static void _flush_group(StatsRelayer* this, RelaysGroup* relays_group) {
  GSList* it;
  fprintf(stdout, "Relays group %s is ordered to flush\n", relays_group->name);
  for (it = relays_group->threads; it; it = it->next) {
    GThread* thread = it->data;
    Relay* relay = thread->data;
    relay_flush(relay);
  }
}

void statsrelayer_flush_group(StatsRelayer* this, const gchar* name) {
  GSList* it;
  GQueue* subjects = g_queue_new();
  for (it = this->groups; it; it = it->next) {
    RelaysGroup* relays_group = it->data;
    if (strcmp(name, "*") != 0 && strcmp(name, relays_group->name) != 0) {
      continue;
    }
    g_queue_push_tail(subjects, relays_group);
  }

  while(!g_queue_is_empty(subjects)) {
    RelaysGroup* relays_group = g_queue_pop_head(subjects);
    _flush_group(this, relays_group);
  }
  g_queue_free(subjects);
}

void statsrelayer_dtor(StatsRelayer* this) {
  g_free(this);
}
