#include "statsrelayer.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include "mapper.h"
#include "source.h"
#include "sink.h"
#include "buffer.h"

typedef void (*SprintfElement)(gpointer component, gchar* info);

typedef struct{
  gpointer component;
  PushPort **output;
  PushPort *input;
  GFreeFunc dtor;
  SprintfElement sprintf_cmp;
  Process* flush;
  Process* start_process;
  Process* stop_process;
  Process* reset_process;
}Element;

typedef struct{
  GSList* elements;
  GSList* threads;
  gchar name[255];
  gboolean started;
}Pipeline;

typedef void (*ExecuteOnPipelineCb)(StatsRelayer* this, Pipeline* pipeline);
static Element* _make_element(gchar* string);
static void _statsrelayer_execute_on_pipeline(StatsRelayer* this, const gchar* name, ExecuteOnPipelineCb callback);
static void _start_pipeline(StatsRelayer* this, Pipeline* pipeline);
static void _stop_pipeline(StatsRelayer* this, Pipeline* pipeline);

StatsRelayer* make_statsrelayer(void) {
  StatsRelayer* this = g_malloc0(sizeof(StatsRelayer));
  return this;
}

void statsrelayer_add_pipeline(StatsRelayer* this, const gchar* string) {
  Pipeline* result = g_malloc0(sizeof(Pipeline));
  gchar* last_chr_in_name = strchr(string, ' ');
  gchar **tokens;
  gint i, len;
  Element *prev_element, *element;
  gchar info_string[1024];

  memcpy(result->name, string, last_chr_in_name - string);
  tokens = g_strsplit(last_chr_in_name + 1, "!", -1);
  len = g_strv_length(tokens);
  fprintf(stdout, "Add pipeline %s\n", result->name);
  for (i = 0; i < len; ++i) {
    element = _make_element(tokens[i]);
    memset(info_string, 0, 1024);
    fprintf(stdout, "Parse string %s for an element\n", tokens[i]);
    element->sprintf_cmp(element->component, info_string);
    fprintf(stdout, "Added element: %s\n", info_string);
    if (!element) {
      continue;
    }
    if (i == 0 || !element->input) {
      result->elements = g_slist_append(result->elements, element);
      prev_element = element;
      continue;
    }
    *prev_element->output = element->input;
    result->elements = g_slist_append(result->elements, element);
    prev_element = element;
  }
  _start_pipeline(this, result);
  this->pipelines = g_slist_append(this->pipelines, result);
  g_strfreev(tokens);
}


static void _list_pipeline(StatsRelayer* this, Pipeline* pipeline) {
  GSList* it;
  gchar info_string[1024];
  fprintf(stdout, "- - - - - - - - - - - P i p e l i n e - - - - - - - - - - - - \n");
  fprintf(stdout, "Name: %s\n", pipeline->name);
  for (it = pipeline->elements; it; it = it->next) {
    Element* element = it->data;
    memset(info_string, 0, 1024);
    element->sprintf_cmp(element->component, info_string);
    fprintf(stdout, "%s", info_string);
  }
}

void statsrelayer_list_pipeline(StatsRelayer* this, const gchar* name) {
  _statsrelayer_execute_on_pipeline(this, name, (ExecuteOnPipelineCb)_list_pipeline);
}

static void _reset_pipeline(StatsRelayer* this, Pipeline* pipeline) {
  GSList* it;
  fprintf(stdout, "reset: %s\n", pipeline->name);
  for (it = pipeline->elements; it; it = it->next) {
    Element* element = it->data;
    if (!element->reset_process) {
      continue;
    }
    process_call(element->reset_process);
  }
}

void statsrelayer_reset_pipeline(StatsRelayer* this, const gchar* name) {
  fprintf(stdout, "Reset pipeline: %s\n", name);
  _statsrelayer_execute_on_pipeline(this, name, (ExecuteOnPipelineCb)_reset_pipeline);
}

static void _rem_pipeline(StatsRelayer* this, Pipeline* pipeline) {
  GSList* it;
  for (it = pipeline->elements; it; it = it->next) {
    Element* element = it->data;
    element->dtor(element);
  }
  this->pipelines = g_slist_remove(it, pipeline);
}

void statsrelayer_rem_pipeline(StatsRelayer* this, const gchar* name) {
  _statsrelayer_execute_on_pipeline(this, name, (ExecuteOnPipelineCb)_rem_pipeline);
}

static void _flush_pipeline(StatsRelayer* this, Pipeline* pipeline) {
  GSList* it;
  g_print("Pipeline %s ordered to be flushed.\n", pipeline->name);
  for (it = pipeline->elements; it; it = it->next) {
    Element* element = it->data;
    if (!element->flush) continue;
    process_call(element->flush);
  }
}

void statsrelayer_flush_pipeline(StatsRelayer* this, const gchar* name) {
  _statsrelayer_execute_on_pipeline(this, name, (ExecuteOnPipelineCb)_flush_pipeline);
}


void _start_pipeline(StatsRelayer* this, Pipeline* pipeline) {
  GSList* it;
  if (pipeline->started) {
    return;
  }
  for (it = pipeline->elements; it; it = it->next) {
    Element* element = it->data;
    GThread* thread;
    gchar thread_name[255];
    if (!element->start_process) continue;
    memset(thread_name, 0, 255);
    sprintf(thread_name, "%s-%p", pipeline->name, element);
    thread = g_thread_new(thread_name, (GThreadFunc) process_call, element->start_process);
    pipeline->threads = g_slist_append(pipeline->threads, thread);
  }
  pipeline->started = TRUE;
}

void statsrelayer_start_pipeline(StatsRelayer* this, const gchar* name) {
  _statsrelayer_execute_on_pipeline(this, name, (ExecuteOnPipelineCb)_start_pipeline);
}

void _stop_pipeline(StatsRelayer* this, Pipeline* pipeline) {
  GSList* it;
  if (!pipeline->started) {
    return;
  }
  for (it = pipeline->elements; it; it = it->next) {
    Element* element = it->data;
    if (!element->stop_process) continue;
    process_call(element->stop_process);
  }

  for (it = pipeline->threads; it; it = it->next) {
    GThread* thread = it->data;
    g_thread_join(thread);
  }
  g_slist_free(pipeline->threads);
  pipeline->threads = NULL;
  pipeline->started = FALSE;
}

void statsrelayer_stop_pipeline(StatsRelayer* this, const gchar* name) {
  _statsrelayer_execute_on_pipeline(this, name, (ExecuteOnPipelineCb)_stop_pipeline);
}


void statsrelayer_dtor(StatsRelayer* this) {
  statsrelayer_rem_pipeline(this, "*");
  g_free(this);
}

Element* _make_element(gchar* string) {
  Element* result = g_malloc0(sizeof(Element));
  gchar *last_char_in_name;
  gchar name[255];
  last_char_in_name = strchr(string, ':');
  memset(name, 0, 255);
  if (last_char_in_name) {
    memcpy(name, string, last_char_in_name - string);
    fprintf(stdout, "Parse |%s|, add element: |%s| params: |%s|\n", string, name, last_char_in_name + 1);
  }else {
    strcpy(name, string);
    fprintf(stdout, "Parse |%s|, add element: |%s|\n", string, name);
  }

  if(strcmp(name, MAPPER_ELEMENT_NAME) == 0) {
    Mapper* mapper = make_mapper(last_char_in_name + 1, sizeof(RTPStatPacket));
    result->component = mapper;
    result->dtor = (GFreeFunc) mapper_dtor;
    result->output = &mapper->output;
    result->input = mapper->input;
    result->sprintf_cmp = (SprintfElement) mapper_sprintf;
    result->start_process = result->stop_process = NULL;
    result->reset_process = mapper->reset_process;
  }else if(strcmp(name, SOURCE_ELEMENT_NAME) == 0) {
    Source* source = make_source(last_char_in_name + 1, sizeof(RTPStatPacket));
    result->component = source;
    result->dtor = (GFreeFunc) source_dtor;
    result->output = &source->output;
    result->input = NULL;
    result->sprintf_cmp = (SprintfElement) source_sprintf;
    result->start_process = source->start_process;
    result->stop_process = source->stop_process;
    result->reset_process = source->reset_process;
  }else if(strcmp(name, BUFFER_ELEMENT_NAME) == 0) {
    Buffer* buffer = make_buffer(sizeof(RTPStatPacket));
    result->component = buffer;
    result->dtor = (GFreeFunc) buffer_dtor;
    result->input = buffer->input;
    result->output = &buffer->output;
    result->sprintf_cmp = (SprintfElement) buffer_sprintf;
    result->flush = make_process((ProcessCb) buffer_flush, buffer);
    result->start_process = result->stop_process = NULL;
    result->reset_process = buffer->reset_process;
  } else if(strcmp(name, SINK_ELEMENT_NAME) == 0) {
    Sink* sink = make_sink(last_char_in_name + 1);
    result->component = sink;
    result->dtor = (GFreeFunc) sink_dtor;
    result->input = sink->input;
    result->sprintf_cmp = (SprintfElement) sink_sprintf;
    result->start_process = NULL;
    result->stop_process = sink->stop_process;
    result->reset_process = sink->reset_process;
  } else {
    result->reset_process = NULL;
    g_free(result);
    result = NULL;
  }
  return result;
}


void _statsrelayer_execute_on_pipeline(StatsRelayer* this, const gchar* name, ExecuteOnPipelineCb callback) {
  GSList* it;
  GQueue* subjects = g_queue_new();
  for (it = this->pipelines; it; it = it->next) {
    Pipeline* pipeline = it->data;
    if (strcmp(name, "*") != 0 && strcmp(name, pipeline->name) != 0) {
      continue;
    }
    g_queue_push_tail(subjects, pipeline);
  }

  while(!g_queue_is_empty(subjects)) {
    Pipeline* pipeline = g_queue_pop_head(subjects);
    callback(this, pipeline);
  }
  g_queue_free(subjects);
}


