#include "statsrelayer.h"
#include "common.h"
#include "relay.h"
#include <stdio.h>
#include <stdlib.h>

typedef enum {
  STATE_WAITING = 0,
  STATE_COLLECTING = 1,
  STATE_FLUSHING = 2
}RelaysGroupState;

typedef struct {
  gchar name[255];
  GSList* threads;
  volatile guint first_arrived;
  Process* on_item_rcved;
  guint flushed_count;
  RelaysGroupState state;
  guint item_size;
}RelaysGroup;



StatsRelayer* make_statsrelayer(void) {
  StatsRelayer* this = g_malloc0(sizeof(StatsRelayer));
  return this;
}

typedef void (*ExecuteOnGroupCb)(StatsRelayer* this, RelaysGroup* relays_group);

static void _statsrelayer_execute_on_group(StatsRelayer* this, const gchar* name, ExecuteOnGroupCb callback) {
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
    callback(this, relays_group);
  }
  g_queue_free(subjects);
}

static gint _gslist_find_relays_group_by_name_helper(gconstpointer  a, gconstpointer  b) {
  const RelaysGroup* relays_group_a = a;
  const RelaysGroup* relays_group_b = b;
  return strcmp(relays_group_a->name, relays_group_b->name);
}

void statsrelayer_add_group(StatsRelayer* this, const gchar* string) {
  gchar *relays = NULL;
  gchar *name = NULL;
  RelaysGroup* relays_group = g_malloc0(sizeof(RelaysGroup));
  gchar **tokens = g_strsplit(string, " ", -1);
  relays_group->item_size = sizeof(RTPStatPacket);
  name = tokens[0];
  relays = tokens[1];
  if (2 < g_strv_length(tokens)) {
    relays_group->item_size = atoi(tokens[2]);
  }

  if (g_slist_find_custom(this->groups, name, (GCompareFunc)_gslist_find_relays_group_by_name_helper)) {
    fprintf(stderr, "Group %s already exists\n", name);
    return;
  }

//  fprintf(stdout, "%s -> add group name: %s (%s)\n", string, name, relays);
  if (relays) {
    gchar **tokens = g_strsplit(relays, "|", -1);
    gint i, len = g_strv_length(tokens);
    for (i = 0; i < len; ++i) {
      Relay* relay;
      GThread* thread;
      gchar thread_name[255];
      sprintf(thread_name, "%s-relay-%d", name, i);
      fprintf(stdout, "Create a relay based on string: %s\n", tokens[i]);
      relay = make_relay(tokens[i], relays_group->item_size);
      thread = g_thread_new(thread_name, (GThreadFunc) relay_start, relay);
      relays_group->threads = g_slist_prepend(relays_group->threads, thread);
    }

    g_strfreev(tokens);
  }

  if (name) {
    strcpy(relays_group->name, name);
  }
  relays_group->state = STATE_WAITING;
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
  _statsrelayer_execute_on_group(this, name, (ExecuteOnGroupCb)_rem_group);
}


static void _flush_group(StatsRelayer* this, RelaysGroup* relays_group) {
  GSList* it;
  relays_group->state = STATE_FLUSHING;
  fprintf(stdout, "Relays group %s is ordered to flush\n", relays_group->name);
  for (it = relays_group->threads; it; it = it->next) {
    GThread* thread = it->data;
    Relay* relay = thread->data;
    relay_flush(relay);
  }
  ++relays_group->flushed_count;
  relays_group->state = STATE_COLLECTING;
  fprintf(stdout, "Relays group %s is flushed\n", relays_group->name);
}

void statsrelayer_flush_group(StatsRelayer* this, const gchar* name) {
  _statsrelayer_execute_on_group(this, name, (ExecuteOnGroupCb)_flush_group);
}


static void _prepare_group(StatsRelayer* this, RelaysGroup* relays_group) {
  GSList* it;
  gint prepare_num = 500000;
  for (it = relays_group->threads; it; it = it->next) {
    GThread* thread = it->data;
    Relay* relay = thread->data;
    relay_prepare(relay, prepare_num);
  }
  fprintf(stdout, "Relays group %s is prepared %d item to accept items\n", relays_group->name, prepare_num);
}

void statsrelayer_prepare_group(StatsRelayer* this, const gchar* name) {
  _statsrelayer_execute_on_group(this, name, (ExecuteOnGroupCb)_prepare_group);
}

void statsrelayer_dtor(StatsRelayer* this) {
  g_free(this);
}

static void _set_state_string(RelaysGroup* relays_group, gchar* result) {
  switch(relays_group->state) {
  case STATE_WAITING:
      sprintf(result, "Waiting");
      break;
  case STATE_COLLECTING:
      sprintf(result, "Collecting");
      break;
  case STATE_FLUSHING:
      sprintf(result, "Flushing");
      break;
  }
}

static void _list_group(StatsRelayer* this, RelaysGroup* relays_group) {
  GSList* it;
  gchar state[256];
  memset(state, 0, 256);
  _set_state_string(relays_group, state);
  fprintf(stdout, "- - - - - - - - - - - R e l a y s G r o u p - - - - - - - - - - - - \n");
  fprintf(stdout, "NAME:      %s\n", relays_group->name);
  fprintf(stdout, "STATE:     %s\n", state);
  fprintf(stdout, "FLUSHED:   %d\n", relays_group->flushed_count);
  fprintf(stdout, "ITEM SIZE: %d\n", relays_group->item_size);
  //                        10        20        30        40        50        60
  //               123456789012345678901234567890123456789012345678901234567890
  fprintf(stdout, "SOURCES                                                    MAP:SINKS\n");
  for (it = relays_group->threads; it; it = it->next) {
    GThread* thread = it->data;
    Relay* relay = thread->data;
    gchar source[256];
    gchar sink[256];
    sprintf(source, "%s:%s", source_get_type_in_string(relay->source), source_get_path(relay->source));
    sprintf(sink, "%s:%s", sink_get_type_in_string(relay->sink), sink_get_path(relay->sink));
    fprintf(stdout, "%-59s%s:%s\n", source, mapper_get_type_in_string(relay->mapper), sink);
  }
}

void statsrelayer_list_group(StatsRelayer* this, const gchar* name) {
  _statsrelayer_execute_on_group(this, name, (ExecuteOnGroupCb)_list_group);
}

