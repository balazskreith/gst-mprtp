#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <netinet/if_ether.h>

#include <pcap.h>

#define current_unix_time_in_us g_get_real_time ()
#define current_unix_time_in_ms (current_unix_time_in_us / 1000L)
#define current_unix_time_in_s  (current_unix_time_in_ms / 1000L)
#define epoch_now_in_ns ((guint64)((current_unix_time_in_us + 2208988800000000LL) * 1000))
#define get_ntp_from_epoch_ns(epoch_in_ns) gst_util_uint64_scale (epoch_in_ns, (1LL << 32), GST_SECOND)
#define get_epoch_time_from_ntp_in_ns(ntp_time) gst_util_uint64_scale (ntp_time, GST_SECOND, (1LL << 32))
#define NTP_NOW get_ntp_from_epoch_ns(epoch_now_in_ns)
#define _now(this) gst_clock_get_time (this->sysclock)


#define _define_recycle_pop_method(name, return_type, recycle) \
  static volatile return_type* name##_pop() { \
    return_type* result = g_async_queue_try_pop(recycle); \
    if(!result) { \
      result = g_malloc0(sizeof(return_type)); \
    } \
    return result; \
  } \

#define _define_recycle_pop_and_set_method(name, return_type, recycle) \
  static volatile return_type* name##_pop_and_set(gpointer data) { \
    return_type* result = g_async_queue_try_pop(recycle); \
    if(!result) { \
      result = g_malloc0(sizeof(return_type)); \
    } \
    memcpy(result, data, sizeof(return_type)); \
    return result; \
  } \

#define _define_recycle_push_method(name, return_type, recycle) \
  static volatile void name##_push(gpointer value) { \
    g_async_queue_push(recycle, value); \
  } \

#define _define_recycle_ctor(name, recycle) \
  static void name##_ctor() { \
    recycle = g_async_queue_new_full(g_free); \
  } \

#define _define_recycle_dtor(name, recycle) \
  static void name##_dtor() { \
    gst_object_unref(recycle); \
  } \

#define _define_recycle(return_type, name, recycle) \
    _define_recycle_ctor(name, recycle) \
    _define_recycle_dtor(name, recycle) \
    _define_recycle_push_method(name, return_type, recycle) \
    _define_recycle_pop_method(name, return_type, recycle) \
    _define_recycle_pop_and_set_method(name, return_type, recycle) \


typedef void (*RefTracker)(gpointer);

typedef struct{
  volatile gint ref;
  GFreeFunc dtor;
}Object;

static void _init_object(Object* object, GFreeFunc dtor) {
  object->ref = 1;
  object->dtor = dtor;
}

static void _object_set_dtor(Object* this, GFreeFunc dtor) {
  this->dtor = dtor;
}

static void _object_ref(Object* object) {
  ++object->ref;
}

static void _object_unref(Object* object) {
  if(0 < --object->ref) return;
  object->dtor(object);
}

#define _define_primitive_object_accessor(object_type, covered_type, name) \
  typedef struct { \
    Object base; \
    covered_type value; \
  }object_type; \
  static object_type* _##make_##name(covered_type* value) { \
    object_type* result = g_malloc0(sizeof(object_type)); \
    _init_object(result, g_free); \
    memcpy(&result->value, value, sizeof(covered_type)); \
    return result; \
  } \

_define_primitive_object_accessor(Int32, gint32, int32)


#define FEC_PAYLOAD_TYPE 126

#define TUPLE_MAX_ITEMS_NUM 32
typedef struct{
  Object   base;
  gpointer values[TUPLE_MAX_ITEMS_NUM];
  gint     length;
}Tuple;


static volatile Tuple* _make_tuple(gpointer arg1, ...) {
  Tuple* this = tuple_recycle_pop();
  va_list arguments;
  gpointer value = NULL;
  _init_object(this, tuple_recycle_push);
  this->length = 0;
  this->values[this->length++] = arg1;
  va_start ( arguments, arg1 );
  for(value = va_arg( arguments, gpointer); value; value = va_arg(arguments, gpointer)){
    this->values[this->length++] = value;
  }
  va_end ( arguments );
  return this;
}

static gpointer _tuple_get(Tuple* this, gint index) {
  if(MIN(TUPLE_MAX_ITEMS_NUM, this->length) < index) {
    return NULL;
  }
  return this->values[index];
}

static void _tuple_set(Tuple* this, gint index, gpointer value) {
  if(MIN(TUPLE_MAX_ITEMS_NUM, this->length) < index) {
    return;
  }
  this->values[index] = value;
}

static void _tuple_add(Tuple* this, gpointer value) {
  if(MIN(TUPLE_MAX_ITEMS_NUM, this->length) < index) {
    return;
  }
  this->values[this->length++] = value;
}


typedef struct _Packet
{
  Object               base;

  guint64              tracked_ntp;
  guint16              seq_num;
  guint32              ssrc;
  guint8               subflow_id;
  guint16              subflow_seq;

  gboolean             marker;
  guint8               payload_type;
  guint32              timestamp;

  guint                header_size;
  guint                payload_size;

  guint16              protect_begin;
  guint16              protect_end;
}Packet;


static _setup_packet(Packet* packet, gchar* line){
  sscanf(line, "%lu,%hu,%u,%u,%d,%u,%d,%d,%d,%hu,%hu,%d",
        &packet->tracked_ntp,
        &packet->seq_num,
        &packet->timestamp,
        &packet->ssrc,
        &packet->payload_type,
        &packet->payload_size,
        &packet->subflow_id,
        &packet->subflow_seq,
        &packet->header_size,
        &packet->protect_begin,
        &packet->protect_end,
        &packet->marker
  );
}

static Packet* _make_packet(gchar* line){
//  Packet* packet = g_malloc0(sizeof(Packet));
  Packet* packet = packet_recycle_pop();
  _init_object(packet, packet_recycle_push);
  _setup_packet(packet, line);
  return packet;
}


static gint
_cmp_seq (guint16 x, guint16 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 32768) return -1;
  if(x > y && x - y > 32768) return -1;
  if(x < y && y - x > 32768) return 1;
  if(x > y && x - y < 32768) return 1;
  return 0;
}


#define COMPONENT_MAX_CONNECTION_NUM 16
typedef struct _Component Component;
typedef void (*PusherIO)(Component*, gpointer);

typedef struct{
  Component* dst;
  PusherIO target;
}PusherConnection;

typedef struct _Component{
  PusherConnection connections[COMPONENT_MAX_CONNECTION_NUM];
};

static void _pushconnect_cmp(Component* src, gint io, Component* dst, PusherIO target){
  PusherConnection* connection = src->connections + io;
  connection->dst = dst;
  connection->target = target;
}

static void _transmit(Component* src, gint io, gpointer data) {
  PusherConnection* connection = src->connections + io;
  if(!connection->target) {
    g_print("Not connected output\n");
    return;
  }
  connection->target(connection->dst, data);
}

static void _optional_transmit(Component* src, gint io, gpointer data) {
  PusherConnection* connection = src->connections + io;
  if(!connection->target) {
    return;
  }
  connection->target(connection->dst, data);
}


/*----------------------- FileReader ---------------------------*/
typedef void (*toStruct)(gpointer, gchar*);

typedef struct{
  Component base;
  gchar path[256];
  toStruct toStruct;
  gint linesNum;
  GQueue* recycle;
}FileReader;

typedef enum{
  FILE_READER_OUTPUT = 1,
}FileReaderIO;

static void _logfile_reader_process(FileReader* this) {
  gchar line[1024];
  gpointer data;
  FILE* fp = fopen(this->path, "r");
  while (fgets(line, 1024, fp)){
    data = this->toStruct(line);
    if(!data) {
      g_print("A generator process can not produce null while reading!\n");
      continue;
    }
    _transmit(&this->base, FILE_READER_OUTPUT, data);
    ++this->linesNum;
  }
  g_print("FILE READING DONE At %s Lines: %d\n", this->path, this->linesNum);
  _transmit(&this->base, FILE_READER_OUTPUT, NULL);
  fclose(fp);
}

static FileReader* _make_reader(gchar* path, toStruct toStruct) {
  FileReader* this = g_malloc0(sizeof(FileReader));
  this->toStruct = toStruct;
  this->recycle = g_queue_new();
  strcpy(this->path, path);
  return this;
}

static void _dispose_reader(FileReader* this) {
  g_free(this);
}

/*----------------------- FileWriter ---------------------------*/

typedef void (*toString)(gchar*, gpointer);

typedef struct{
  Component base;
  gchar path[256];
  toString toString;
  GQueue* values;
  gboolean started;
  gint linesNum;
  gint processedNum;
}FileWriter;

typedef enum{
  FILE_WRITER_TRASH_OUTPUT = 1,
}FileWriterIO;

static void _file_writer_process(FileWriter* this, gpointer data) {
  gchar* line;
  if(!data){
    g_print("Flush signal at File Writer for %s written lines: %d, processed data: %d\n",
        this->path, this->linesNum + g_queue_get_length(this->values), this->processedNum);
    goto flush;
  }
  ++this->processedNum;
  line = line_recycle_pop();
  this->toString(line, data);
  g_queue_push_tail(this->values, line);
  if(g_queue_get_length(this->lines) < 1000){
    return;
  }

flush:
  {
    FILE* fp;
    fp = fopen(this->path, this->started ? "a" : "w");
    this->started = TRUE;
    while(!g_queue_is_empty(this->lines)) {
      gchar* line = g_queue_pop_head(this->lines);
      fprintf(fp, "%s\n", line);
      line_recycle_push(line);
      ++this->linesNum;
    }
    fclose(fp);
  }

}

static FileWriter* _make_writer(gchar* path, toString toString) {
  FileWriter* this = g_malloc0(sizeof(FileWriter));
  this->lines = g_queue_new();
  strcpy(this->path, path);
  this->toString = toString;
  return this;
}

static void _dispose_writer(FileWriter* this) {
  g_queue_clear(this->lines);
  g_queue_free(this->lines);
  g_free(this);
}

/*----------------------- Sorter ---------------------------*/

typedef struct{
  Component base;
  GQueue*   items;
  GCompareDataFunc cmp;
  gpointer         cmp_udata;
}Sorter;

typedef enum{
  SORTER_OUTPUT = 1,
}SorterIO;

static void _sorter_process(Sorter* this, gpointer data) {
  if (!data) {
    g_print("Flush signal at Sorter\n");
    goto flush;
  }
  g_queue_insert_sorted(this->items, data, this->cmp, this->cmp_udata);
  if (32000 < g_queue_get_length(this->items)) {
    _transmit(&this->base, SORTER_OUTPUT, g_queue_pop_head(this->items));
  }
  return;

flush:
  while(!g_queue_is_empty(this->items)) {
    _transmit(&this->base, SORTER_OUTPUT, g_queue_pop_head(this->items));
  }
  _transmit(&this->base, SORTER_OUTPUT, NULL);
}

static Sorter* _make_sorter(GCompareDataFunc cmp, gpointer cmp_udata) {
  Sorter* this = g_malloc0(sizeof(Sorter));
  this->cmp = cmp;
  this->cmp_udata = cmp_udata;
  this->items = g_queue_new();
  return this;
}

static void _dispose_sorter(Sorter* this) {
  g_queue_free(this->items);
  g_free(this);
}

/*----------------------- Mapper ---------------------------*/
typedef gpointer (*MapperProcess)(gpointer);
typedef struct{
  Component base;
  MapperProcess process;
}Mapper;

typedef enum{
  MAPPER_OUTPUT = 1,
}MapperIO;

static void _mapper_process(Mapper* this, gpointer data) {
  gpointer result;
  if (!data) {
    _transmit(&this->base, MAPPER_OUTPUT, NULL);
    return;
  }
  result = this->process(data);
  _transmit(&this->base, MAPPER_OUTPUT, data);
}

static Mapper* _make_mapper(MapperProcess process) {
  Mapper* this = g_malloc0(sizeof(Mapper));
  this->process = process;
  return this;
}

static void _dispose_mapper(Mapper* this) {
  g_free(this);
}


/*----------------------- Reducer ---------------------------*/
typedef void (*ReducerProcess)(gpointer result, gpointer value);
typedef struct{
  Component base;
  gpointer result;
  ReducerProcess process;
}Reducer;

typedef enum{
  REDUCER_OUTPUT = 1,
}ReducerIO;

static void _reducer_process(Reducer* this, gpointer data) {
  gpointer result;
  if (!data) {
    _transmit(&this->base, REDUCER_OUTPUT, this->result);
    return;
  }
  this->process(result, data);
}

static Reducer* _make_reducer(ReducerProcess process, gpointer result) {
  Reducer* this = g_malloc0(sizeof(Reducer));
  this->process = process;
  this->result = result;
  return this;
}

static void _dispose_reducer(Reducer* this) {
  g_free(this);
}

/*----------------------- Merger ---------------------------*/
typedef gpointer (*MergeProcess)(gpointer,gpointer);
typedef struct{
  Component base;
  GCond* cond;
  GMutex *mutex;
  gpointer input_x;
  gpointer input_y;
  gboolean flushed_x;
  gboolean flushed_y;
  GCompareFunc comparator;
  RefTracker unref;
  MergeProcess merge;
  gint32 processed_num;
  gint32 invoked_x;
  gint32 invoked_y;
}Merger;

typedef enum{
  MERGER_OUTPUT = 1,
}MergerIO;


static void _merger_unref_data(Merger* this, gpointer data) {
  if(this->unref && data) {
    this->unref(data);
  }
}

static void _merger_process_input_x(Merger* this, gpointer data) {
  gint cmp;
//  g_print("_merger_process_input_x\n");
  g_mutex_lock(this->mutex);
  if (!data) {
    this->flushed_x = TRUE;
    _merger_unref_data(this, this->input_x);
    this->input_x = NULL;
    if(this->flushed_y) {
      g_print("Flush siangl at Merger, number of merged object: %d | Invokes: x: %d y:%d\n",
                this->processed_num, this->invoked_x, this->invoked_y);
      _transmit(&this->base, MERGER_OUTPUT, NULL);
    }
    g_cond_signal(this->cond);
    goto exit;
  } else if (this->flushed_y) {
    _merger_unref_data(this, data);
    goto exit;
  }

  ++this->invoked_x;

  while (this->input_x) {
    if(this->flushed_y) {
      _merger_unref_data(this, data);
      _merger_unref_data(this, this->input_x);
      this->input_x = NULL;
      goto exit;
    }
    g_cond_wait(this->cond, this->mutex);
  }

  if (!this->input_y) {
    this->input_x = data;
    goto exit;
  }

  g_cond_signal(this->cond);

  // Both are exists
  if (data) {
    cmp = this->comparator(data, this->input_y);
  } else {
    this->input_x = NULL;
    goto exit;
  }

  if (cmp < 0) { // data is smaller than data in input_y
    _merger_unref_data(this, data);
  } else if (0 < cmp) { // data is larger than data in input y
    _merger_unref_data(this, this->input_y);
    this->input_x = data;
    this->input_y = NULL;
  } else { // match
    gpointer result = this->merge(data, this->input_y);
    _transmit(&this->base, MERGER_OUTPUT, result);
    this->input_y = NULL;
    ++this->processed_num;
  }


exit:
  g_mutex_unlock(this->mutex);
}

static void _merger_process_input_y(Merger* this, gpointer data) {
  gint cmp;
  g_mutex_lock(this->mutex);
  if (!data) {
    this->flushed_y = TRUE;
    _merger_unref_data(this, this->input_y);
    this->input_y = NULL;
    if(this->flushed_x) {
      g_print("Flush siangl at Merger, number of merged object: %d | Invokes: x: %d y:%d\n",
                this->processed_num, this->invoked_x, this->invoked_y);
      _transmit(&this->base, MERGER_OUTPUT, NULL);
    }
    g_cond_signal(this->cond);
    goto exit;
  } else if (this->flushed_x) {
    _merger_unref_data(this, data);
    goto exit;
  }

  ++this->invoked_y;

  while (this->input_y) {
    if(this->flushed_x) {
      _merger_unref_data(this, data);
      _merger_unref_data(this, this->input_y);
      this->input_y = NULL;
      goto exit;
    }
    g_cond_wait(this->cond, this->mutex);
  }

  if (!this->input_x) {
    this->input_y = data;
    goto exit;
  }

  g_cond_signal(this->cond);

  if (data) {
    cmp = this->comparator(this->input_x, data);
  } else {
    this->input_y = NULL;
    goto exit;
  }

  if (cmp < 0) { // input_x is smaller than data
    _merger_unref_data(this, this->input_x);
    this->input_x = NULL;
    this->input_y = data;
  } else if (0 < cmp) { // input_x is larger than data
    _merger_unref_data(this, data);
  } else { // match
    gpointer result = this->merge(this->input_x, data);
    _transmit(&this->base, MERGER_OUTPUT, result);
    this->input_x = NULL;
    ++this->processed_num;
  }

exit:
  g_mutex_unlock(this->mutex);
}

static Merger* _make_merger(GCompareFunc comparator, RefTracker unref, MergeProcess merge) {
  Merger* this = g_malloc0(sizeof(Merger));
  this->mutex = g_mutex_new();
  this->cond = g_cond_new();
  this->comparator = comparator;
  this->unref = unref;
  this->merge = merge;
  return this;
}

static void _dispose_merger(Merger* this) {
  g_mutex_free(this->mutex);
  g_cond_free(this->cond);
  g_free(this);
}


/*----------------------- Dispatcher ---------------------------*/

typedef gpointer (*DispatcherCopier)(gpointer);
typedef struct{
  Component base;
  DispatcherCopier cpy;
}Dispatcher;

static void _dispatcher_process(Dispatcher* this, gpointer data) {
  gint i;
  gpointer forwarded = this->cpy ? this->cpy(data) : data;

  for(i = 0; i < COMPONENT_MAX_CONNECTION_NUM; ++i) {
    PusherConnection* connection = this->base.connections + i;
    if(!connection->target) continue;
    connection->target(connection->dst, forwarded);
  }
}

static Dispatcher* _make_dispatcher(DispatcherCopier cpy, PusherIO target, gpointer dst, ...) {
  Dispatcher* this = g_malloc0(sizeof(Dispatcher));
  gint io_num = 0;
  va_list arguments;
  this->cpy = cpy;
  _pushconnect_cmp(this, io_num, dst, target);
  va_start ( arguments, dst );
  for(target = va_arg( arguments, PusherIO); target; target = va_arg( arguments, PusherIO)){
    dst = va_arg( arguments, gpointer);
    _pushconnect_cmp(this, ++io_num, dst, target);
  }
  va_end ( arguments );
  return this;
}

static void _dispose_dispatcher(Dispatcher* this){
  g_free(this);
}

/*----------------------- Sampler ---------------------------*/
typedef GstClockTime (*SamplerTimestampExtractor)(gpointer data);
typedef struct{
  Component base;
  GstClockTime sampled;
  GstClockTime sampling;
  GstClockTime actual;
  RefTracker unref;
  SamplerTimestampExtractor extractor;
}Sampler;

typedef enum{
  SAMPLER_OUTPUT = 1,
}SamplerIO;

static void _sampler_process(Sampler* this, gpointer data) {
  if (!data) {
    g_print("Flush signal at Sampler\n");
    _transmit(&this->base, SAMPLER_OUTPUT, NULL);
    return;
  }
  if (!this->sampled) {
    this->sampled = this->actual = this->extractor(data);
    return;
  }
  this->actual = this->extractor(data);
  if(this->actual - this->sampling < this->sampled){
    if(this->unref) {
      this->unref(data);
    }
    return;
  }
  while(this->sampled < this->actual - this->sampling){
    _transmit(&this->base, SAMPLER_OUTPUT, data);
    this->sampled += this->sampling;
  }
  if(this->unref) {
    this->unref(data);
  }
}

static Sampler* _make_sampler(SamplerTimestampExtractor extractor, RefTracker unref, GstClockTime sampling) {
  Sampler* this = g_malloc0(sizeof(Sampler));
  this->sampling = sampling;
  this->extractor = extractor;
  this->unref = unref;
  return this;
}

static void _dispose_sampler(Sampler* this){
  g_free(this);
}


/*----------------------- Filter ---------------------------*/
typedef gboolean (*FilterFunc)(gpointer);
typedef struct{
  Component base;
  GSList* filters;
  RefTracker unref;
}Filter;

typedef enum{
  FILTER_OUTPUT = 1,
}FilterIO;

static void _filter_process(Filter* this, gpointer data) {
  GSList* it;
  gboolean allowed = TRUE;
  if (!data) {
    g_print("Flush signal at Filter\n");
    _transmit(&this->base, FILTER_OUTPUT, NULL);
    return;
  }
  for (it = this->filters; it; it = it->next) {
    FilterFunc filter = it->data;
    allowed &= filter(data);
  }
  if (!allowed) {
    if (this->unref) {
      this->unref(data);
    }
    return;
  }
  _transmit(&this->base, FILTER_OUTPUT, data);
}

static Filter* _make_filter(RefTracker unref, FilterFunc filter, ...) {
  Filter* this = g_malloc0(sizeof(Filter));
  va_list arguments;
  this->filters = g_slist_prepend(this->filters, filter);
  va_start ( arguments, filter );
  for(filter = va_arg( arguments, FilterFunc); filter; filter = va_arg( arguments, FilterFunc)){
    this->filters = g_slist_prepend(this->filters, filter);
  }
  va_end ( arguments );
  this->unref = unref;
  return this;
}

static void _dispose_filter(Filter* this){
  g_free(this);
}


/*----------------------- Monitor ---------------------------*/

typedef gboolean (*MonitorQueueIsFull)(GQueue*);
typedef struct _MonitorPlugin MonitorPlugin;
typedef void (*MonitorPluginActivator)(MonitorPlugin* , gpointer);
typedef void (*MonitorPluginNotifier)(gpointer user_data , gpointer value);

typedef gboolean (*MonitorPluginFilter)(gpointer);

typedef struct _MonitorPlugin{
  MonitorPluginActivator add;
  MonitorPluginActivator remove;
  MonitorPluginFilter    filter;
};

typedef struct{
  Component base;
  MonitorQueueIsFull is_full;
  GSList* plugins;
  volatile GQueue* items;
  RefTracker ref,unref;
  gint processedNum;
}Monitor;

typedef enum {
 MONITOR_FLUSH_OUTPUT = 2,
}MonitorIO;


static volatile void _monitor_receive_process(Monitor* this, gpointer value) {
  GSList* it;
  if(!value) {
    g_print("Flush signal at Monitor. processed values: %d\n", this->processedNum);
    _transmit(&this->base, MONITOR_FLUSH_OUTPUT, NULL);
    return;
  }

  if(this->ref){
    this->ref(value);
  }
  ++this->processedNum;
  g_queue_push_tail(this->items, value);
  for(it = this->plugins; it; it = it->next) {
    MonitorPlugin* plugin = it->data;
    gboolean allowed = plugin->filter ? plugin->filter(value) : TRUE;
    if(allowed) {
      plugin->add(plugin, value);
    }
  }

  while(this->is_full && this->is_full(this->items)) {
    gpointer obsolated = g_queue_pop_head(this->items);
    for(it = this->plugins; it; it = it->next) {
      MonitorPlugin* plugin = it->data;
      gboolean allowed = plugin->filter ? plugin->filter(obsolated) : TRUE;
      if(allowed) {
        plugin->remove(plugin, obsolated);
      }
    }
    if(this->unref){
      this->unref(obsolated);
    }
  }
}

static void _monitor_add_plugins(Monitor* this, MonitorPlugin* plugin, ...) {
  gint io_num = 0;
  va_list arguments;
  this->plugins = g_slist_prepend(this->plugins, plugin);
  va_start ( arguments, plugin );
  for(plugin = va_arg( arguments, MonitorPlugin*); plugin; plugin = va_arg( arguments, MonitorPlugin*)){
    this->plugins = g_slist_prepend(this->plugins, plugin);
  }
  va_end ( arguments );
}

static Monitor* _make_monitor(MonitorQueueIsFull is_full, RefTracker ref, RefTracker unref) {
  Monitor* this = g_malloc0(sizeof(Monitor));
  this->items = g_queue_new();
  this->is_full = is_full;
  this->ref = ref;
  this->unref = unref;
  return this;
}

static void _dispose_monitor(Monitor* this){
//  g_queue_free(this->items);
  g_slist_free(this->plugins);
  g_free(this);
}

/*======================= Plugin ==========================*/

typedef gint32 (*MonitorPluginInt32Extractor)(gpointer);

typedef struct{
  MonitorPlugin base;
  PusherConnection  connection;
  gint32 result;
  MonitorPluginInt32Extractor extractor;
}MonitorSumPlugin;

static void _monitor_sum_plugin_add(MonitorSumPlugin* this, gpointer data) {
//  g_print("%p %d + %d <-%hu\n", this, this->result, this->extractor(data), ((Packet*)data)->seq_num);
  this->result += this->extractor(data);
  this->connection.target(this->connection.dst, &this->result);
}

static void _monitor_sum_plugin_remove(MonitorSumPlugin* this, gpointer data) {
//  g_print("%p %d - %d <-%hu\n", this, this->result, this->extractor(data), ((Packet*)data)->seq_num);
  this->result -= this->extractor(data);
  this->connection.target(this->connection.dst, &this->result);
}

static MonitorSumPlugin* _make_monitor_sum_plugin(
    MonitorPluginFilter filter,
    MonitorPluginInt32Extractor extractor,
    Component* dst, PusherIO target)
{
  MonitorSumPlugin *this = g_malloc0(sizeof(MonitorSumPlugin));
  this->extractor = extractor;
  this->base.filter = filter;
  this->base.add = _monitor_sum_plugin_add;
  this->base.remove = _monitor_sum_plugin_remove;
  this->connection.dst = dst;
  this->connection.target = target;
  return this;
}



/*======================= Plugin ==========================*/
typedef gpointer (*MonitorPluginPointerExtractor)(gpointer);
#define PERCENTILE_MAX_VALUES_NUM 10000
typedef struct{
  MonitorPlugin base;
  PusherConnection  connection;
  GQueue* items;
  GCompareFunc cmp;
  MonitorPluginPointerExtractor extractor;
  gpointer values[PERCENTILE_MAX_VALUES_NUM];
  gint values_index;
  gint percentile;
}MonitorPercentilePlugin;

static void _monitor_percentile_plugin_helper(gpointer item, MonitorPercentilePlugin* this) {
  if(this->values_index < PERCENTILE_MAX_VALUES_NUM) {
    this->values[this->values_index++] = item;
//    g_print("Item: %p->%hu\n", item, ((Packet*) _tuple_get(item, 0))->seq_num);
  }
}

static void _monitor_percentile_plugin_calculate(MonitorPercentilePlugin* this) {
  this->values_index = 0;
  gpointer result;

  g_queue_foreach(this->items, _monitor_percentile_plugin_helper, this);
//  g_print("--------\n");
  if (PERCENTILE_MAX_VALUES_NUM <= g_queue_get_length(this->items)) {
    g_print("Number of items are too large for calculate the percentile in this version");
  }
//  g_print("Length: %d\n", this->values_index);
  qsort(this->values, this->values_index, sizeof(gpointer), this->cmp);
  {
    gint index = this->values_index * ((gdouble) this->percentile / 100.) - 1;
    result = this->values[index];
//    g_print("index: %d\n", index);
  }

  this->connection.target(this->connection.dst, this->extractor(result));
}

static void _monitor_percentile_plugin_add(MonitorPercentilePlugin* this, gpointer data) {
  g_queue_push_tail(this->items, data);
  _monitor_percentile_plugin_calculate(this);
}

static void _monitor_percentile_plugin_remove(MonitorPercentilePlugin* this, gpointer data) {
  g_queue_pop_head(this->items);
  _monitor_percentile_plugin_calculate(this);
}

static MonitorPercentilePlugin* _make_monitor_percentile_plugin(
    gint percentile,
    GCompareFunc cmp,
    MonitorPluginPointerExtractor extractor,
    MonitorPluginFilter filter,
    Component* dst,
    PusherIO target)
{
  MonitorPercentilePlugin *this = g_malloc0(sizeof(MonitorPercentilePlugin));

  this->base.filter = filter;
  this->base.add = _monitor_percentile_plugin_add;
  this->base.remove = _monitor_percentile_plugin_remove;
  this->connection.dst = dst;
  this->connection.target = target;
  this->items = g_queue_new();
  this->cmp = cmp;
  this->extractor = extractor;
  this->percentile = percentile;
  return this;
}

/*======================= Plugin ==========================*/

typedef struct{
  MonitorPlugin base;
  PusherConnection  add_connection;
  PusherConnection  rem_connection;
  MonitorPluginPointerExtractor extractor;
}MonitorExtractorPlugin;

static void _monitor_extract_plugin_add(MonitorExtractorPlugin* this, gpointer data) {
  if (!this->add_connection.target) {
    return;
  }
  this->add_connection.target(this->add_connection.dst, this->extractor(data));
}

static void _monitor_extract_plugin_remove(MonitorExtractorPlugin* this, gpointer data) {
  if (!this->rem_connection.target) {
    return;
  }
  this->rem_connection.target(this->rem_connection.dst, this->extractor(data));
}

static MonitorExtractorPlugin* _make_monitor_functor_plugin(
    MonitorPluginFilter filter,
    MonitorPluginPointerExtractor extractor,
    Component* add_dst, PusherIO add_target, Component* rem_dst, PusherIO rem_target)
{
  MonitorExtractorPlugin *this = g_malloc0(sizeof(MonitorExtractorPlugin));
  this->extractor = extractor;
  this->base.filter = filter;
  this->base.add = _monitor_extract_plugin_add;
  this->base.remove = _monitor_extract_plugin_remove;
  this->add_connection.dst = add_dst;
  this->add_connection.target = add_target;
  this->rem_connection.dst = rem_dst;
  this->rem_connection.target = rem_target;
  return this;
}


/*======================= Plugin ==========================*/

/*----------------------- Tupler ---------------------------*/
#define MAX_MUXER_VALUES_NUM 32

typedef gpointer (*MuxProcess)(gpointer *values);
typedef struct{
  Component base;
  gpointer values[MAX_MUXER_VALUES_NUM];
  gint invokes[MAX_MUXER_VALUES_NUM];
  gint32 barrier_num;
  gint32 received_num;
  MuxProcess process;
  gint processed_num;
  gint pivot_input;
}Muxer;

typedef enum {
 MUXER_OUTPUT = 1,
}MuxerIO;

static Muxer* _make_muxer(MuxProcess process, gint32 barrier_num, gint pivot_input) {
  Muxer* this = g_malloc0(sizeof(Muxer));
  this->barrier_num = barrier_num;
  this->process = process;
  this->pivot_input = pivot_input;
  return this;
}

static void _dispose_muxer(Muxer* this){
  g_free(this);
}

static void _muxer_flush(Muxer* this) {
  gchar line[1024];
  gint i;
  g_print("Muxer receive Flush signal, processed Num: %d\n", this->processed_num);
  sprintf(line, "Muxer Invokes ");
  for (i = 0; i < this->barrier_num; ++i) {
    gchar invoke_str[25];
    sprintf(invoke_str, "%d: %d ", i, this->invokes[i]);
    strcat(line, invoke_str);
  }
  g_print("%s\n", line);
  _transmit(&this->base, MUXER_OUTPUT, NULL);
}

static void _muxer_process(Muxer* this) {
  gpointer data;
  ++this->processed_num;
  data = this->process(this->values);
  memset(this->values, 0, sizeof(gpointer) * MAX_MUXER_VALUES_NUM);
  this->received_num = 0;
  _transmit(&this->base, MUXER_OUTPUT, data);
}

#define _define_muxer_input(num) \
  static void _muxer_input_##num(Muxer* this, gpointer value) { \
    if(!this->values[num] && value) { \
      ++this->received_num; \
    } \
    ++this->invokes[num]; \
    this->values[num] = value; \
    if(this->received_num == this->barrier_num || this->pivot_input == num) { \
      _muxer_process(this); \
    } \
  } \


_define_muxer_input(0)
_define_muxer_input(1)
_define_muxer_input(2)

#define _define_field_extractor(return_type, name, item_type, field_name) \
  static return_type name(gpointer item) { \
    return ((item_type*) item)->field_name; \
  } \

#define _define_gpointer_field_extractor(name, item_type, field_name) \
  static gpointer name(gpointer item) { \
    return &((item_type*) item)->field_name; \
  } \

//#define _setup_value(to, from, type) \
//  to = g_malloc0(sizeof(type)); \
//  memcpy(to, from, sizeof(type)); \


_define_field_extractor(gint32, _payload_extractor, Packet, payload_size);
_define_field_extractor(guint64, _tracked_ntp_extractor, Packet, tracked_ntp);

static gpointer _simple_passer(gpointer data) {
  return data;
}

static void _tuplpe_for_sr_unref(Tuple* tuple) {
  _object_unref(_tuple_get(tuple, 0)); // Packet
  _object_unref(_tuple_get(tuple, 1)); // Int32 - sending rate
  _object_unref(_tuple_get(tuple, 2)); // Int32 - fec rate
  _object_unref(tuple);
}

static void _sprintf_sr_tuple(gchar* result, Tuple* sr_tuple){
  Int32* sending_rate = _tuple_get(sr_tuple, 1);
  Int32* fec_rate = _tuple_get(sr_tuple, 2);
  sprintf(result, "%d,%d",
      sending_rate->value,
      fec_rate->value
  );
}

static gpointer _make_tuple_for_sr(gpointer* values) {
  gint32 default_value = 0;
  Packet* packet = values[0];
  Int32* sending_rate = _make_int32(values[1] ? values[1] : &default_value);
  Int32* fec_rate = _make_int32(values[2] ? values[2] : &default_value);
  Tuple* result = _make_tuple(packet, sending_rate, fec_rate, NULL);
  return result;
}

static gboolean _is_fec_packet(Packet* packet) {
  return packet->payload_type == FEC_PAYLOAD_TYPE;
}

static gboolean _is_not_fec_packet(Packet* packet) {
  return packet->payload_type != FEC_PAYLOAD_TYPE;
}

static volatile gboolean _packet_queue_is_full_1s_tracked_ntp(GQueue* queue) {
  Packet*head, *tail;
  GstClockTime elapsed;
  if(g_queue_is_empty(queue)){
    return FALSE;
  }
  head = g_queue_peek_head(queue);
  tail = g_queue_peek_tail(queue);
  elapsed = tail->tracked_ntp - head->tracked_ntp;
  return GST_SECOND < get_epoch_time_from_ntp_in_ns(elapsed);
}

static GstClockTime _epoch_timestamp_extractor(gpointer sr_tuple) {
  return get_epoch_time_from_ntp_in_ns(_tracked_ntp_extractor(_tuple_get(sr_tuple, 0)));
}

static void _write_sr(gchar* input_path, gchar* output_path) {

  //Construction
  FileReader* reader = _make_reader(input_path, _make_packet);
  Monitor* monitor = _make_monitor(_packet_queue_is_full_1s_tracked_ntp, _object_ref, _object_unref);
  Muxer* muxer = _make_muxer(_make_tuple_for_sr, 3, 0);
  Sampler* sampler = _make_sampler(_epoch_timestamp_extractor, _tuplpe_for_sr_unref, 100 * GST_MSECOND);
  FileWriter* writer = _make_writer(output_path, _sprintf_sr_tuple);

  //Initialization
  _monitor_add_plugins(monitor,
      _make_monitor_functor_plugin(NULL, _simple_passer, muxer, _muxer_input_0, NULL, NULL),
      _make_monitor_sum_plugin(_is_not_fec_packet, _payload_extractor, muxer, _muxer_input_1),
      _make_monitor_sum_plugin(_is_fec_packet, _payload_extractor, muxer, _muxer_input_2),
      NULL);

  //Connection
  _pushconnect_cmp(reader, FILE_READER_OUTPUT, monitor, _monitor_receive_process);
  _pushconnect_cmp(monitor, MONITOR_FLUSH_OUTPUT, muxer, _muxer_flush);
  _pushconnect_cmp(muxer, MUXER_OUTPUT, sampler, _sampler_process);
  _pushconnect_cmp(sampler, SAMPLER_OUTPUT, writer, _file_writer_process);

  //Start
  {
    GThread* process = g_thread_create(_logfile_reader_process, reader, TRUE, NULL);
    g_thread_join(process);
  }

  //Dispose
  _dispose_sampler(sampler);
  _dispose_reader(reader);
  _dispose_monitor(monitor);
  _dispose_muxer(muxer);
  _dispose_writer(writer);
}

static gint _cmp_packets(const Packet* packet_x, const Packet* packet_y) {
  return _cmp_seq(packet_x->seq_num, packet_y->seq_num);
}

static Tuple* _make_paired_packets_tuple(Packet* packet_x, Packet* packet_y){
//  g_print("Merged: %hu-%hu\n", packet_x->seq_num, packet_y->seq_num);
  return _make_tuple(packet_x, packet_y, NULL);
}

static void _paired_packets_unref(Tuple* paired_packets){
  Packet* packet_x = _tuple_get(paired_packets, 0);
  Packet* packet_y = _tuple_get(paired_packets, 1);
//  _object_unref(packet_x);
//  _object_unref(packet_y);
//  _object_unref(paired_packets);
}

static void _sprintf_paired_packets_for_qd(gchar* result, Tuple* paired_packets) {
  Packet* packet_x = _tuple_get(paired_packets, 0);
  Packet* packet_y = _tuple_get(paired_packets, 1);
  GstClockTime owd; // One Way Delay

  if(packet_x == packet_y)
    g_print("PacketX: %p | PacketY: %p\n", packet_x, packet_y);

  if(packet_x->tracked_ntp < packet_y->tracked_ntp) {
    owd = get_epoch_time_from_ntp_in_ns(packet_y->tracked_ntp - packet_x->tracked_ntp);
  } else {
    owd = get_epoch_time_from_ntp_in_ns(packet_x->tracked_ntp - packet_y->tracked_ntp);
  }
  sprintf(result, "%lu", owd / 1000000);

  _paired_packets_unref(paired_packets);
}

static gint _cmp_packets_with_udata(const Packet* packet_x, const Packet* packet_y, gpointer udata) {
  return _cmp_packets(packet_x, packet_y);
}

static void _write_qd(gchar* snd_packets_path, gchar* rcv_packets_path, gchar* output_path) {

  //Construction
  FileReader* snd_reader = _make_reader(snd_packets_path, _make_packet);
  FileReader* rcv_reader = _make_reader(rcv_packets_path, _make_packet);
  Filter* snd_filter = _make_filter(_object_unref, _is_not_fec_packet, NULL);
  Filter* rcv_filter = _make_filter(_object_unref, _is_not_fec_packet, NULL);
  Sorter* rcv_sorter = _make_sorter(_cmp_packets_with_udata, NULL);
  Merger* merger = _make_merger(_cmp_packets, _object_unref, _make_paired_packets_tuple);
  FileWriter* writer = _make_writer(output_path, _sprintf_paired_packets_for_qd);

  //Connection
  _pushconnect_cmp(snd_reader, FILE_READER_OUTPUT, snd_filter, _filter_process);
  _pushconnect_cmp(rcv_reader, FILE_READER_OUTPUT, rcv_filter, _filter_process);
  _pushconnect_cmp(snd_filter, FILTER_OUTPUT, merger, _merger_process_input_x);
  _pushconnect_cmp(rcv_filter, FILTER_OUTPUT, rcv_sorter, _sorter_process);
  _pushconnect_cmp(rcv_sorter, SORTER_OUTPUT, merger, _merger_process_input_y);
  _pushconnect_cmp(merger, MERGER_OUTPUT, writer, _file_writer_process);

  //Start
  {
    GThread* snd_process = g_thread_create(_logfile_reader_process, snd_reader, TRUE, NULL);
    GThread* rcv_process = g_thread_create(_logfile_reader_process, rcv_reader, TRUE, NULL);
    g_thread_join(snd_process);
    g_thread_join(rcv_process);
  }

  //Dispose
  _dispose_filter(snd_filter);
  _dispose_filter(rcv_filter);
  _dispose_reader(snd_reader);
  _dispose_reader(rcv_reader);
  _dispose_sorter(rcv_sorter);
  _dispose_merger(merger);
  _dispose_writer(writer);
}


static volatile gboolean _paired_packets_queue_is_full_1s_tracked_ntp(GQueue* queue) {
  Packet *head, *tail;
  GstClockTime elapsed;
  if(g_queue_is_empty(queue)){
    return FALSE;
  }
  head = _tuple_get(g_queue_peek_head(queue), 0);
  tail = _tuple_get(g_queue_peek_tail(queue), 0);
  elapsed = tail->tracked_ntp - head->tracked_ntp;
  return GST_SECOND < get_epoch_time_from_ntp_in_ns(elapsed);
}

static GstClockTime _paired_packets_epoch_timestamp_extractor(gpointer tuple) {
  return get_epoch_time_from_ntp_in_ns(_tracked_ntp_extractor(_tuple_get(tuple, 0)));
}

static void _paired_packets_ref(Tuple* paired_packets){
  Packet* packet_x = _tuple_get(paired_packets, 0);
  Packet* packet_y = _tuple_get(paired_packets, 1);
  _object_ref(packet_x);
  _object_ref(packet_y);
  _object_ref(paired_packets);
}

static gint _paired_packets_owd_qsort_cmp(Tuple** pair_x, Tuple** pair_y) {
  Packet *packet_x_snd, *packet_x_rcv;
  Packet *packet_y_snd, *packet_y_rcv;
  GstClockTime owd_x, owd_y;
//  g_print("X Item: %hu\n", ((Packet*) _tuple_get(*pair_x, 0))->seq_num);
//  g_print("Y Item: %hu\n", ((Packet*) _tuple_get(*pair_y, 0))->seq_num);
  packet_x_snd = _tuple_get(*pair_x, 0);
  packet_x_rcv = _tuple_get(*pair_x, 1);
  packet_y_snd = _tuple_get(*pair_y, 0);
  packet_y_rcv = _tuple_get(*pair_y, 1);
  owd_x = get_epoch_time_from_ntp_in_ns( packet_x_rcv->tracked_ntp - packet_x_snd->tracked_ntp);
  owd_y = get_epoch_time_from_ntp_in_ns( packet_y_rcv->tracked_ntp - packet_y_snd->tracked_ntp);
  return owd_x == owd_y ? 0 : owd_x < owd_y ? -1 : 1;
}

typedef struct{
  Component base;
  FileReader* snd_reader;
  FileReader* rcv_reader;
  Filter* snd_filter;
  Filter* rcv_filter;
  Sorter* rcv_sorter;
}RTPPacketsMerger;

static RTPPacketsMerger* _make_rtp_packets_merger(gchar* snd_packets_path, gchar* rcv_packets_path) {
  FileReader* snd_reader = _make_reader(snd_packets_path, _make_packet);
  FileReader* rcv_reader = _make_reader(rcv_packets_path, _make_packet);
  Filter* snd_filter = _make_filter(_object_unref, _is_not_fec_packet, NULL);
  Filter* rcv_filter = _make_filter(_object_unref, _is_not_fec_packet, NULL);
  Sorter* rcv_sorter = _make_sorter(_cmp_packets_with_udata, NULL);
  Merger* merger = _make_merger(_cmp_packets, _object_unref, _make_paired_packets_tuple);

}

static void _write_qmd(gchar* snd_packets_path, gchar* rcv_packets_path, gchar* output_path) {

  //Construction
  FileReader* snd_reader = _make_reader(snd_packets_path, _make_packet);
  FileReader* rcv_reader = _make_reader(rcv_packets_path, _make_packet);
  Filter* snd_filter = _make_filter(_object_unref, _is_not_fec_packet, NULL);
  Filter* rcv_filter = _make_filter(_object_unref, _is_not_fec_packet, NULL);
  Sorter* rcv_sorter = _make_sorter(_cmp_packets_with_udata, NULL);
  Merger* merger = _make_merger(_cmp_packets, _object_unref, _make_paired_packets_tuple);
  Monitor* monitor = _make_monitor(_paired_packets_queue_is_full_1s_tracked_ntp, _paired_packets_ref, _paired_packets_unref);
  Sampler* sampler = _make_sampler(_paired_packets_epoch_timestamp_extractor, _paired_packets_unref, 100 * GST_MSECOND);
  FileWriter* writer = _make_writer(output_path, _sprintf_paired_packets_for_qd);

  //Connection
  _pushconnect_cmp(snd_reader, FILE_READER_OUTPUT, snd_filter, _filter_process);
  _pushconnect_cmp(rcv_reader, FILE_READER_OUTPUT, rcv_filter, _filter_process);
  _pushconnect_cmp(snd_filter, FILTER_OUTPUT, merger, _merger_process_input_x);
  _pushconnect_cmp(rcv_filter, FILTER_OUTPUT, rcv_sorter, _sorter_process);
  _pushconnect_cmp(rcv_sorter, SORTER_OUTPUT, merger, _merger_process_input_y);
  _pushconnect_cmp(merger, MERGER_OUTPUT, monitor, _monitor_receive_process);
  _pushconnect_cmp(monitor, MONITOR_FLUSH_OUTPUT, sampler, _sampler_process);

  _monitor_add_plugins(monitor,
      _make_monitor_percentile_plugin(
          50,
          _paired_packets_owd_qsort_cmp,
          _simple_passer,
          NULL,
          sampler,
          _sampler_process
//          writer,
//          _file_writer_process
          ),
      NULL);

  _pushconnect_cmp(sampler, SAMPLER_OUTPUT, writer, _file_writer_process);

  //Start
  {
    GThread* snd_process = g_thread_create(_logfile_reader_process, snd_reader, TRUE, NULL);
    GThread* rcv_process = g_thread_create(_logfile_reader_process, rcv_reader, TRUE, NULL);
    g_thread_join(snd_process);
    g_thread_join(rcv_process);
  }

  //Dispose
  _dispose_filter(snd_filter);
  _dispose_filter(rcv_filter);
  _dispose_reader(snd_reader);
  _dispose_reader(rcv_reader);
  _dispose_sorter(rcv_sorter);
  _dispose_merger(merger);
  _dispose_writer(writer);
  _dispose_monitor(monitor);
  _dispose_sampler(sampler);
}




int main (int argc, char **argv)
{
  rational_recycle_ctor();
  tuple_recycle_ctor();
  packet_recycle_ctor();
  int32_recycle_ctor();
  line_recycle_ctor();

  if(argc < 3){
  usage:
    g_print("Usage: ./program result_path [sr|qd]\n");
    g_print("sr snd_packets - accumulates the sending rate\n");
    g_print("qd snd_packets rcv_packets - calculates the queueing delays for packets\n");
    g_print("qmd snd_packets rcv_packets - calculates the median queueing delays for packets\n");
    g_print("gp ply_packets - calculates the goodput for packets\n");
    g_print("gp_avg ply_packets - calculates the average goodput for packets\n");
    g_print("fec_avg fec_packets - calculates the average fec rate for packets\n");
    g_print("tcprate tcpdump - calculates the tcp sending rates for tcpdump\n");
    g_print("lr snd_packets rcv_packets - calculates the loss rate for packets\n");
    g_print("ffre fec_packets snd_packets rcv_packets ply_packets - calculates the ffre\n");
    g_print("tfs snd_packets tcpdump - calculates traffic fair share\n");
    g_print("nlf snd_packets ply_packets - calculates the number of lost frames\n");
    g_print("ratio snd_packets_1 snd_packets_2 - calculates the ratio between the flows\n");
    g_print("disc ply_packets - calculates the discarded packets ratio\n");
    g_print("tcpstat tcpdump - calculates the tcp rate based on pcap\n");
    return 0;
  }

  if(strcmp(argv[2], "sr") == 0) {
    _write_sr(argv[3], argv[1]);
  }else if(strcmp(argv[2], "qd") == 0) {
    _write_qd(argv[3], argv[4], argv[1]);
  }else if(strcmp(argv[2], "qmd") == 0) {
    _write_qmd(argv[3], argv[4], argv[1]);
  }else if(strcmp(argv[2], "lr") == 0) {

  }else if(strcmp(argv[2], "ffre") == 0) {

  }else if(strcmp(argv[2], "gp") == 0) {

  }else if(strcmp(argv[2], "gp_avg") == 0 || strcmp(argv[2], "fec_avg") == 0){

  }else if(strcmp(argv[2], "ratio") == 0) {

  }else if(strcmp(argv[2], "nlf") == 0) {

  }else if(strcmp(argv[2], "disc") == 0) {
    g_print("IMPLEMENT THIS!\n");
  }else if(strcmp(argv[2], "tcpstat") == 0) {

  }else{
    goto usage;
  }

//  packet_recycle_dtor();
//  int32_recycle_dtor();
//  tuple_recycle_dtor();
//  line_recycle_dtor();
//  rational_recycle_dtor();

  g_print("Results are made in %s\n", argv[1]);
  return 0;
}
