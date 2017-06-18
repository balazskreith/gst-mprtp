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
  Tuple* this = g_malloc0(sizeof(Tuple));
  va_list arguments;
  gpointer value = NULL;
  _init_object(&this->base, g_free);
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
//
//static void _tuple_set(Tuple* this, gint index, gpointer value) {
//  if(MIN(TUPLE_MAX_ITEMS_NUM, this->length) < index) {
//    return;
//  }
//  this->values[index] = value;
//}
//
//static void _tuple_add(Tuple* this, gpointer value) {
//  if(MIN(TUPLE_MAX_ITEMS_NUM, this->length) < index) {
//    return;
//  }
//  this->values[this->length++] = value;
//}


typedef struct _RTPPacket
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
}RTPPacket;


static _setup_packet(RTPPacket* packet, gchar* line){
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

static RTPPacket* _make_rtp_packet(gchar* line){
  RTPPacket* packet = g_malloc0(sizeof(RTPPacket));
  _init_object(packet, g_free);
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


typedef struct _TCPPacket
{
  Object       base;

  GstClockTime timestamp;
  guint16      dst_port;
  guint16      src_port;
  gint32       size;

}TCPPacket;

/* ethernet headers are always exactly 14 bytes [1] */
#define SIZE_ETHERNET 14

static TCPPacket* _make_tcp_packet(struct pcap_pkthdr* header, gchar* bytes){
  TCPPacket* packet = g_malloc0(sizeof(TCPPacket));
  guint16 ihl;//IP header length
  struct sniff_ip* ip;
  _init_object(&packet->base, g_free);
  packet->timestamp = (GstClockTime)header->ts.tv_sec * GST_SECOND + (GstClockTime)header->ts.tv_usec * GST_USECOND;
//  g_print("%lu-%lu = %lu\n",
//      (GstClockTime)header->ts.tv_sec * GST_SECOND, (GstClockTime)header->ts.tv_usec * GST_USECOND, packet->timestamp);
  ip = (struct sniff_ip*)(bytes + SIZE_ETHERNET);
  ihl = (*(guint8*)(bytes + SIZE_ETHERNET) & 0x0F)*4;
  packet->size = g_ntohs(*((guint16*) (bytes + SIZE_ETHERNET + 2)));
  packet->src_port = g_ntohs((guint16*)(bytes + SIZE_ETHERNET + ihl + 0));
  packet->dst_port = g_ntohs((guint16*)(bytes + SIZE_ETHERNET + ihl + 2));
  return packet;
}


typedef struct{
  Object       base;
  guint16      src_port;
  guint16      dst_port;
  GstClockTime last_timestamp;
}TCPFlow;

static gint _find_tcp_flow(TCPFlow* tcp_flow, TCPPacket* packet){
  return tcp_flow->dst_port == packet->dst_port && tcp_flow->src_port == packet->src_port ? 0 : -1;
}

static TCPFlow* _make_tcp_flow(TCPPacket* packet){
  TCPFlow* this = g_malloc0(sizeof(TCPFlow));
  _init_object(&this->base, g_free);
  this->src_port = packet->src_port;
  this->dst_port = packet->dst_port;
  return this;
}

#define COMPONENT_MAX_CONNECTION_NUM 16
typedef struct _Component Component;
typedef void (*PusherIO)(Component*, gpointer);

typedef struct{
  Component* dst;
  PusherIO target;
}PusherConnection;

typedef struct _Component{
  gchar name[255];
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
//    g_print("Not connected output\n");
    return; // If we want to cause segfault, comment the return
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

/*----------------------- FileStreamer ---------------------------*/
typedef gpointer (*PcapItemToStreamItem)(struct pcap_pkthdr* header, gchar* packet);
typedef struct{
  Component base;
  gchar path[256];
  gint written_num;
  PcapItemToStreamItem toItem;

}PcapFileStreamer;

typedef enum{
  FILE_STREAM_OUTPUT = 1,
}PcapFileStreamerIO;

static PcapFileStreamer* _make_pcap_flie_streamer(gchar* path, PcapItemToStreamItem toItem)
{
  PcapFileStreamer* this = g_malloc0(sizeof(PcapFileStreamer));
  strcpy(this->path, path);
  this->toItem = toItem;
  return this;
}

static void _pcap_flie_streamer_process(PcapFileStreamer* this) {
  pcap_t *pcap;
  const unsigned char *packet;
  char errbuf[1400];
  struct pcap_pkthdr header;
  gpointer stream_item;

  pcap = pcap_open_offline(this->path, errbuf);
  if (pcap == NULL) {
    g_print("error reading pcap file: %s\n", errbuf);
    exit(1);
  }

  while ((packet = pcap_next(pcap, &header)) != NULL){
    stream_item = this->toItem(&header, packet);
    if(!stream_item) {
      g_print("A generator process can not produce null while reading!\n");
      continue;
    }
    _transmit(&this->base, FILE_STREAM_OUTPUT, stream_item);
    ++this->written_num;
//    g_print("%d\n", this->written_num);
  }

  g_print("FILE READING DONE At %s Lines: %d\n", this->path, this->written_num);
  _transmit(&this->base, FILE_STREAM_OUTPUT, NULL);
}

static void _dispose_pcap_flie_streamer(PcapFileStreamer* this) {
  g_free(this);
}

/*----------------------- FileReader ---------------------------*/

typedef gpointer (*toStruct)(gchar*);

typedef struct{
  Component base;
  gchar path[256];
  toStruct toStruct;
  gint written_num;
  GQueue* recycle;
}FileReader;

typedef enum{
  FILE_READER_OUTPUT = 1,
}FileReaderIO;

static void _file_reader_process(FileReader* this) {
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
    ++this->written_num;
  }
  g_print("FILE READING DONE At %s Lines: %d\n", this->path, this->written_num);
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
  GQueue* lines;
  GQueue* recycle;
  gboolean started;
  gint written_num;
  gint received_num;
}FileWriter;

typedef enum{
  FILE_WRITER_TRASH_OUTPUT = 1,
}FileWriterIO;

static gchar* _file_writer_get_line(FileWriter* this, gpointer data) {
  gchar* result = g_queue_is_empty(this->recycle) ? g_malloc(1024) : g_queue_pop_head(this->recycle);
  memset(result, 0, 1024);
  this->toString(result, data);
  return result;
}

static void _file_writer_process(FileWriter* this, gpointer data) {

  if(!data){
    g_print("Flush signal at File Writer for %s written lines: %d, written data: %d\n",
        this->path, this->written_num + g_queue_get_length(this->lines), this->received_num);
    goto flush;
  }
//  g_print("FileWriter processing: %p\n", data);
  ++this->received_num;

  {
    gchar* line = _file_writer_get_line(this, data);
    g_queue_push_tail(this->lines, line);
  }

  _optional_transmit(&this->base, FILE_WRITER_TRASH_OUTPUT, data);
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
      ++this->written_num;
      g_queue_push_tail(this->recycle, line);
    }
    fclose(fp);
  }

}

static FileWriter* _make_writer(gchar* path, toString toString) {
  FileWriter* this = g_malloc0(sizeof(FileWriter));
  this->lines = g_queue_new();
  this->recycle = g_queue_new();
  strcpy(this->path, path);
  this->toString = toString;
  return this;
}

static void _dispose_writer(FileWriter* this) {
  g_queue_free(this->lines);
  g_free(this);
}

/*----------------------- Sorter ---------------------------*/

typedef struct{
  Component base;
  GQueue*   items;
  GCompareDataFunc cmp;
  gpointer         cmp_udata;
  gint32           length_threshold;
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
//    g_print("Transmitted seq (before flush) %hu\n", ((Packet*)g_queue_peek_head(this->items))->seq_num);
    _transmit(&this->base, SORTER_OUTPUT, g_queue_pop_head(this->items));
  }
  return;

flush:
  while(!g_queue_is_empty(this->items)) {
//    g_print("Transmitted seq (after flush) %hu\n", ((Packet*)g_queue_peek_head(this->items))->seq_num);
    _transmit(&this->base, SORTER_OUTPUT, g_queue_pop_head(this->items));
  }
  _transmit(&this->base, SORTER_OUTPUT, NULL);
}

static Sorter* _make_sorter(GCompareDataFunc cmp, gpointer cmp_udata, gint length_threshold) {
  Sorter* this = g_malloc0(sizeof(Sorter));
  this->cmp = cmp;
  this->cmp_udata = cmp_udata;
  this->items = g_queue_new();
  this->length_threshold = length_threshold;
  return this;
}

static void _dispose_sorter(Sorter* this) {
  g_queue_free(this->items);
  g_free(this);
}

/*----------------------- Mapper ---------------------------*/
typedef void (*MapperProcess)(gpointer);
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
    _optional_transmit(&this->base, MAPPER_OUTPUT, NULL);
    return;
  }
  this->process(data);
  _optional_transmit(&this->base, MAPPER_OUTPUT, data);
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
  REDUCER_DATA_OUTPUT = 1,
  REDUCER_RESULT_OUTPUT = 2,
}ReducerIO;

static void _reducer_process(Reducer* this, gpointer data) {
  if (!data) {
    _optional_transmit(&this->base, REDUCER_RESULT_OUTPUT, this->result);
    _optional_transmit(&this->base, REDUCER_RESULT_OUTPUT, NULL);
    return;
  }
  this->process(this->result, data);
  _optional_transmit(&this->base, REDUCER_DATA_OUTPUT, data);
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
  MergeProcess merge;
  gint32 forwarded_num;
  gint32 invoked_x;
  gint32 invoked_y;
  PusherConnection on_input_x_flushed;
  PusherConnection on_input_y_flushed;
}Merger;

typedef enum{
  MERGER_OUTPUT = 1,
  MERGER_TRASH_OUTPUT = 2,
  MERGER_TRASH_OUTPUT_X = 3,
  MERGER_TRASH_OUTPUT_Y = 4,
}MergerIO;

static void _merger_add_on_input_x_flushed_handler(Merger* this, gpointer dst, PusherIO target) {
  this->on_input_x_flushed.dst = dst;
  this->on_input_x_flushed.target = target;
}

static void _merger_add_on_input_y_flushed_handler(Merger* this, gpointer dst, PusherIO target) {
  this->on_input_y_flushed.dst = dst;
  this->on_input_y_flushed.target = target;
}

static void _merger_on_input_x_flushed(Merger* this, gpointer data) {
  if(!this->on_input_x_flushed.target){
    return;
  }
  this->on_input_x_flushed.target(this->on_input_x_flushed.dst, data);
}

static void _merger_on_input_y_flushed(Merger* this, gpointer data) {
  if(!this->on_input_y_flushed.target){
      return;
    }
    this->on_input_y_flushed.target(this->on_input_y_flushed.dst, data);
}

static void _merger_unref_x_data(Merger* this, gpointer data) {
  if(this->base.connections[MERGER_TRASH_OUTPUT_X].target){
    _transmit(&this->base, MERGER_TRASH_OUTPUT_X, data);
  } else {
    _transmit(&this->base, MERGER_TRASH_OUTPUT, data);
  }
}

static void _merger_unref_y_data(Merger* this, gpointer data) {
  if(this->base.connections[MERGER_TRASH_OUTPUT_Y].target){
    _transmit(&this->base, MERGER_TRASH_OUTPUT_Y, data);
  } else {
    _transmit(&this->base, MERGER_TRASH_OUTPUT, data);
  }
}

static void _merger_process_input_x(Merger* this, gpointer data) {
  gint cmp;
//  g_print("_merger_process_input_x\n");
  g_mutex_lock(this->mutex);
  if (!data) {
    this->flushed_x = TRUE;
    _merger_on_input_x_flushed(this, NULL);
    _merger_unref_x_data(this, this->input_x);
    this->input_x = NULL;
    if(this->flushed_y) {
      g_print("Flush siangl at Merger, number of merged object: %d | Invokes: x: %d y:%d\n",
                this->forwarded_num, this->invoked_x, this->invoked_y);
      _transmit(&this->base, MERGER_OUTPUT, NULL);
    }
    g_cond_signal(this->cond);
    goto exit;
  } else if (this->flushed_y) {
    _merger_unref_x_data(this, data);
    goto exit;
  }

  ++this->invoked_x;

  while (this->input_x) {
    if(this->flushed_y) {
      _merger_unref_x_data(this, data);
      _merger_unref_x_data(this, this->input_x);
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
    _merger_unref_x_data(this, data);
  } else if (0 < cmp) { // data is larger than data in input y
    _merger_unref_y_data(this, this->input_y);
    this->input_x = data;
    this->input_y = NULL;
  } else { // match
    gpointer result = this->merge(data, this->input_y);
    _transmit(&this->base, MERGER_OUTPUT, result);
    this->input_y = NULL;
    ++this->forwarded_num;
  }


exit:
  g_mutex_unlock(this->mutex);
}

static void _merger_process_input_y(Merger* this, gpointer data) {
  gint cmp;
  g_mutex_lock(this->mutex);
  if (!data) {
    this->flushed_y = TRUE;
    _merger_on_input_y_flushed(this, NULL);
    _merger_unref_y_data(this, this->input_y);
    this->input_y = NULL;
    if(this->flushed_x) {
      g_print("Flush siangl at Merger, number of merged object: %d | Invokes: x: %d y:%d\n",
                this->forwarded_num, this->invoked_x, this->invoked_y);
      _transmit(&this->base, MERGER_OUTPUT, NULL);
    }
    g_cond_signal(this->cond);
    goto exit;
  } else if (this->flushed_x) {
    _merger_unref_y_data(this, data);
    goto exit;
  }

  ++this->invoked_y;

  while (this->input_y) {
    if(this->flushed_x) {
      _merger_unref_y_data(this, data);
      _merger_unref_y_data(this, this->input_y);
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
    _merger_unref_x_data(this, this->input_x);
    this->input_x = NULL;
    this->input_y = data;
  } else if (0 < cmp) { // input_x is larger than data
    _merger_unref_y_data(this, data);
  } else { // match
    gpointer result = this->merge(this->input_x, data);
    _transmit(&this->base, MERGER_OUTPUT, result);
    this->input_x = NULL;
    ++this->forwarded_num;
  }

exit:
  g_mutex_unlock(this->mutex);
}

static Merger* _make_merger(GCompareFunc comparator, MergeProcess merge) {
  Merger* this = g_malloc0(sizeof(Merger));
  this->mutex = g_mutex_new();
  this->cond = g_cond_new();
  this->comparator = comparator;
  this->merge = merge;
  return this;
}

static void _dispose_merger(Merger* this) {
  g_mutex_free(this->mutex);
  g_cond_free(this->cond);
  g_free(this);
}


/*----------------------- Sampler ---------------------------*/
typedef GstClockTime (*SamplerTimestampExtractor)(gpointer data);
typedef struct{
  Component base;
  GstClockTime sampled;
  GstClockTime sampling;
  GstClockTime actual;
  SamplerTimestampExtractor extractor;
  gint processed_num;
  gint duplicated_num;
}Sampler;

typedef enum{
  SAMPLER_OUTPUT = 1,
  SAMPLER_TRASH_OUTPUT = 2,
}SamplerIO;

static void _sampler_process(Sampler* this, gpointer data) {
  gpointer prev_forwarded = NULL;
  if (!data) {
    g_print("Flush signal at Sampler, forwarded sample: %d | duplicated forwards: %d\n",
        this->processed_num, this->duplicated_num);
    _transmit(&this->base, SAMPLER_OUTPUT, NULL);
    return;
  }
  if (!this->sampled) {
    this->sampled = this->actual = this->extractor(data);
    return;
  }

  this->actual = this->extractor(data);
  if(this->actual < this->sampling) {
    return;
  }
  if(this->actual - this->sampling < this->sampled){
    _optional_transmit(&this->base, SAMPLER_TRASH_OUTPUT, data);
    return;
  }
  while(this->sampled < this->actual - this->sampling){
    ++this->processed_num;
    this->duplicated_num += prev_forwarded == data ? 1 : 0;
    _transmit(&this->base, SAMPLER_OUTPUT,  data);
    this->sampled += this->sampling;
    prev_forwarded = data;
  }
  _optional_transmit(&this->base, SAMPLER_TRASH_OUTPUT, data);

}

static Sampler* _make_sampler(SamplerTimestampExtractor extractor, GstClockTime sampling) {
  Sampler* this = g_malloc0(sizeof(Sampler));
  this->sampling = sampling;
  this->extractor = extractor;
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
  gint32 passed_num;
  gint32 failed_num;
}Filter;

typedef enum{
  FILTER_PASSES_OUTPUT = 1,
  FILTER_FAILS_OUTPUT = 2,
}FilterIO;

static void _filter_process(Filter* this, gpointer data) {
  GSList* it;
  gboolean allowed = TRUE;
  if (!data) {
    g_print("Flush signal at Filter. Passed: %d Failed: %d\n", this->passed_num, this->failed_num);
    _transmit(&this->base, FILTER_PASSES_OUTPUT, NULL);
    return;
  }
  for (it = this->filters; it; it = it->next) {
    FilterFunc filter = it->data;
    allowed &= filter(data);
  }
  if (!allowed) {
    _optional_transmit(&this->base, FILTER_FAILS_OUTPUT, data);
    ++this->failed_num;
    return;
  }
  _transmit(&this->base, FILTER_PASSES_OUTPUT, data);
  ++this->passed_num;
}

static Filter* _make_filter(FilterFunc filter, ...) {
  Filter* this = g_malloc0(sizeof(Filter));
  va_list arguments;
  this->filters = g_slist_prepend(this->filters, filter);
  va_start ( arguments, filter );
  for(filter = va_arg( arguments, FilterFunc); filter; filter = va_arg( arguments, FilterFunc)){
    this->filters = g_slist_prepend(this->filters, filter);
  }
  va_end ( arguments );
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
  gint received_num;
}Monitor;

typedef enum {
  MONITOR_FLUSH_OUTPUT = 1,
  MONITOR_TRASH_OUTPUT = 2,
}MonitorIO;


static volatile void _monitor_refresh(Monitor* this, gpointer null_value) {
  GSList* it;
  while(this->is_full && this->is_full(this->items)) {
    gpointer obsolated = g_queue_pop_head(this->items);
    for(it = this->plugins; it; it = it->next) {
      MonitorPlugin* plugin = it->data;
      gboolean allowed = plugin->filter ? plugin->filter(obsolated) : TRUE;
      if(allowed) {
        plugin->remove(plugin, obsolated);
      }
    }
//    g_print("Monitor trashing: %p\n", obsolated);
    _optional_transmit(&this->base, MONITOR_TRASH_OUTPUT, obsolated);
  }
}

static volatile void _monitor_receive_process(Monitor* this, gpointer value) {
  GSList* it;
  if(!value) {
    g_print("Flush signal at Monitor. processed values: %d\n", this->received_num);
    _transmit(&this->base, MONITOR_FLUSH_OUTPUT, NULL);
    return;
  }

  ++this->received_num;
  g_queue_push_tail(this->items, value);
  for(it = this->plugins; it; it = it->next) {
    MonitorPlugin* plugin = it->data;
    gboolean allowed = plugin->filter ? plugin->filter(value) : TRUE;
    if(allowed) {
      plugin->add(plugin, value);
    }
  }
  _monitor_refresh(this, NULL);
}

//TODO: hopp! nincs obsolation :) ez lehet osszefuggesben miert neznek ki ugy a grafikonok ahogy
// A samplernel kell keresni valamit.

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

static Monitor* _make_monitor(MonitorQueueIsFull is_full) {
  Monitor* this = g_malloc0(sizeof(Monitor));
  this->items = g_queue_new();
  this->is_full = is_full;
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

typedef void (*MonitorReducerPluginProcessor)(gpointer result, gpointer data);

typedef struct{
  MonitorPlugin base;
  PusherConnection  connection;
  gpointer result;
  MonitorReducerPluginProcessor add_reducer;
  MonitorReducerPluginProcessor rem_reducer;
}MonitorReducerPlugin;

static void _monitor_reducer_plugin_add(MonitorReducerPlugin* this, gpointer data) {
  if(this->add_reducer) {
    this->add_reducer(this->result, data);
  }
  this->connection.target(this->connection.dst, this->result);
}

static void _monitor_reducer_plugin_remove(MonitorReducerPlugin* this, gpointer data) {
  if(this->rem_reducer) {
    this->rem_reducer(this->result, data);
  }
  this->connection.target(this->connection.dst, &this->result);
}

static MonitorReducerPlugin* _make_monitor_reducer_plugin(
    MonitorPluginFilter filter,
    gpointer result,
    MonitorReducerPluginProcessor add_reducer,
    MonitorReducerPluginProcessor rem_reducer,
    Component* dst, PusherIO target)
{
  MonitorReducerPlugin *this = g_malloc0(sizeof(MonitorReducerPlugin));
  this->add_reducer = add_reducer;
  this->rem_reducer = rem_reducer;
  this->base.filter = filter;
  this->base.add = _monitor_reducer_plugin_add;
  this->base.remove = _monitor_reducer_plugin_remove;
  this->connection.dst = dst;
  this->connection.target = target;
  this->result = result;
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

  gint32 read_index, write_index, values_count;
}MonitorPercentilePlugin;

static void _monitor_percentile_plugin_helper(gpointer item, MonitorPercentilePlugin* this) {
  if(this->values_index < PERCENTILE_MAX_VALUES_NUM) {
    this->values[this->values_index++] = item;
//    g_print("Item: %p->%hu\n", item, ((RTPPacket*) _tuple_get(item, 0))->seq_num);
  }
}

static void _monitor_percentile_plugin_calculate(MonitorPercentilePlugin* this) {
  this->values_index = 0;
  gpointer result;
  gint index;

  g_queue_foreach(this->items, _monitor_percentile_plugin_helper, this);
  if (PERCENTILE_MAX_VALUES_NUM <= g_queue_get_length(this->items)) {
    g_print("Number of items are too large for calculate the percentile in this version");
  }
  qsort(this->values, this->values_index, sizeof(gpointer), this->cmp);
  index = this->values_index * ((gdouble) this->percentile / 100.) - 1;
  result = this->values[index];
  this->connection.target(this->connection.dst, this->extractor(result));
}

static void _monitor_percentile_plugin_add(MonitorPercentilePlugin* this, gpointer data) {
  g_queue_push_tail(this->items, data);
//  g_print("Add: %p->%hu\n", data, ((RTPPacket*) _tuple_get(data, 0))->seq_num);
  _monitor_percentile_plugin_calculate(this);
}

static void _monitor_percentile_plugin_remove(MonitorPercentilePlugin* this, gpointer data) {
  gpointer popped = g_queue_pop_head(this->items);
//  g_print("Rem: %p-%p->%hu\n", popped, data, ((RTPPacket*) _tuple_get(data, 0))->seq_num);
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
}MonitorFunctorPlugin;

static void _monitor_extract_plugin_add(MonitorFunctorPlugin* this, gpointer data) {
  if (!this->add_connection.target) {
    return;
  }
  this->add_connection.target(this->add_connection.dst, this->extractor(data));
}

static void _monitor_extract_plugin_remove(MonitorFunctorPlugin* this, gpointer data) {
  if (!this->rem_connection.target) {
    return;
  }
  this->rem_connection.target(this->rem_connection.dst, this->extractor(data));
}

static MonitorFunctorPlugin* _make_monitor_functor_plugin(
    MonitorPluginFilter filter,
    MonitorPluginPointerExtractor extractor,
    Component* add_dst, PusherIO add_target, Component* rem_dst, PusherIO rem_target)
{
  MonitorFunctorPlugin *this = g_malloc0(sizeof(MonitorFunctorPlugin));
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

/*----------------------- Muxer ---------------------------*/

#define MAX_MUXER_VALUES_NUM 32

typedef gpointer (*MuxProcess)(gpointer *values);
typedef struct{
  Component base;
  gpointer values[MAX_MUXER_VALUES_NUM];
  gint invokes[MAX_MUXER_VALUES_NUM];
  gint32 barrier_num;
  gint32 received_num;
  MuxProcess process;
  gint forwarded_num;
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
  g_print("Muxer received Flush signal, forwarded data: %d\n", this->forwarded_num);
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
  ++this->forwarded_num;
  data = this->process(this->values);
//  memset(this->values, 0, sizeof(gpointer) * MAX_MUXER_VALUES_NUM);
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
_define_muxer_input(3)
_define_muxer_input(4)




/*----------------------- Dedispatcher ---------------------------*/

typedef gpointer (*MuxProcess)(gpointer *values);
typedef struct{
  Component base;
  guint32   output_length;
}Dispatcher;

static Dispatcher* _make_dispatcher(void) {
  Dispatcher* this = g_malloc0(sizeof(Dispatcher));
  this->output_length = 0;
  return this;
}

static void _dispose_dispatcher(Dispatcher* this){
  g_free(this);
}

static void _dispatcher_add_output(Dispatcher* this, Component* dst, PusherIO target) {
  _pushconnect_cmp(&this->base, this->output_length++, dst, target);
}

static void _dispatcher_add_outputs(Dispatcher* this, Component* dst, PusherIO target, ...) {
  gint io_num = 0;
  va_list arguments;
  _dispatcher_add_output(this, dst, target);
  va_start ( arguments, target );
  for(dst = va_arg( arguments, Component*); dst; dst = va_arg( arguments, Component*)) {
    _dispatcher_add_output(this, dst, va_arg( arguments, PusherIO));
  }
  va_end ( arguments );
}

static void _dispatcher_process(Dispatcher* this, gpointer value) {
  gint output_id;
  for (output_id = 0; output_id < this->output_length; ++output_id) {
    _transmit(&this->base, output_id, value);
  }
}


//################################ P I P E L I N E S ##########################################################

#define _define_field_extractor(return_type, name, item_type, field_name) \
  static return_type name(gpointer item) { \
    return ((item_type*) item)->field_name; \
  } \

#define _define_gpointer_field_extractor(name, item_type, field_name) \
  static gpointer name(gpointer item) { \
    return &((item_type*) item)->field_name; \
  } \

_define_field_extractor(gint32, _payload_extractor, RTPPacket, payload_size);
_define_field_extractor(guint64, _tracked_ntp_extractor, RTPPacket, tracked_ntp);

static gint32 _counter_extractor(gpointer data) {
  return 1;
}

static gpointer _simple_passer(gpointer data) {
  return data;
}

static gboolean _is_fec_packet(RTPPacket* packet) {
  return packet->payload_type == FEC_PAYLOAD_TYPE;
}

static gboolean _is_not_fec_packet(RTPPacket* packet) {
  return packet->payload_type != FEC_PAYLOAD_TYPE;
}

static GstClockTime _get_rtp_packets_elapsed_time(RTPPacket* packet1, RTPPacket* packet2) {
  GstClockTime ntp_dtime;
  if (packet1->tracked_ntp < packet2->tracked_ntp) {
    ntp_dtime = packet2->tracked_ntp - packet1->tracked_ntp;
  } else {
    ntp_dtime = packet1->tracked_ntp - packet2->tracked_ntp;
  }
  return get_epoch_time_from_ntp_in_ns(ntp_dtime);
}

static GstClockTime _epoch_timestamp_extractor(gpointer sr_tuple) {
  return get_epoch_time_from_ntp_in_ns(_tracked_ntp_extractor(_tuple_get(sr_tuple, 0)));
}

static volatile gboolean _rtp_packet_queue_is_full_1s_tracked_ntp(GQueue* queue) {
  RTPPacket*head, *tail;
  GstClockTime elapsed;
  if(g_queue_is_empty(queue)){
    return FALSE;
  }
  head = g_queue_peek_head(queue);
  tail = g_queue_peek_tail(queue);
  return GST_SECOND < _get_rtp_packets_elapsed_time(head, tail);
}

static gpointer _make_sr_fr_tuple(gpointer* values) {
  gint32 default_value = 0;
  RTPPacket* packet = values[0];
  Int32* rtp_payload_rate = _make_int32(values[1] ? values[1] : &default_value);
  Int32* fec_payload_rate = _make_int32(values[2] ? values[2] : &default_value);
  Int32* rtp_packets_rate = _make_int32(values[3] ? values[3] : &default_value);
  Int32* fec_packets_rate = _make_int32(values[4] ? values[4] : &default_value);
  Tuple* result = _make_tuple(packet, rtp_payload_rate, fec_payload_rate,
      rtp_packets_rate, fec_packets_rate, NULL);
//  g_print("Tuple produced: %p\n", result);
  return result;
}

static void _sr_fr_full_unref(Tuple* packets_sr_fr) {
//  g_print("Tuple _sr_fr_full_unref: %p\n", packets_sr_fr);
//  g_print("Packets ref %d\n", ((Packet*)_tuple_get(packets_sr_fr, 0))->base.ref);
  _object_unref((Object*)_tuple_get(packets_sr_fr, 0));
  _object_unref((Object*)_tuple_get(packets_sr_fr, 1));
  _object_unref((Object*)_tuple_get(packets_sr_fr, 2));
  _object_unref((Object*)_tuple_get(packets_sr_fr, 3));
  _object_unref((Object*)_tuple_get(packets_sr_fr, 4));
  _object_unref((Object*)packets_sr_fr);
}

typedef struct {
  Component   base;
  Monitor*    monitor;
  Muxer*      muxer;
  Sampler*    sampler;
  Mapper*     packets_refer;
  Mapper*     packets_unrefer;
  Mapper*     tuple_packets_unrefer;
  Dispatcher* dispatcher;
}RateSampler;

typedef enum{
  RATE_SAMPLER_TUPLE_OUTPUT = 1
};

static void _rate_sampler_packet_input_transmitter(RateSampler* this, gpointer data) {
  _mapper_process(this->packets_refer, data);
}
static void _rate_sampler_tuple_output_transmitter(RateSampler* this, gpointer data) {
  _transmit(&this->base, RATE_SAMPLER_TUPLE_OUTPUT, data);
}

static RateSampler* _make_rate_sampler() {
  RateSampler* this = g_malloc0(sizeof(RateSampler));
  this->monitor = _make_monitor(_rtp_packet_queue_is_full_1s_tracked_ntp);
  this->muxer = _make_muxer(_make_sr_fr_tuple, 5, 0);
  this->sampler = _make_sampler(_epoch_timestamp_extractor, 100 * GST_MSECOND);
  this->packets_refer   = _make_mapper((MapperProcess) _object_ref);
  this->packets_unrefer = _make_mapper((MapperProcess) _object_unref);
  this->tuple_packets_unrefer = _make_mapper((MapperProcess) _sr_fr_full_unref);
  this->dispatcher = _make_dispatcher();

  //Initialization
  _monitor_add_plugins(this->monitor,
      _make_monitor_functor_plugin(NULL, _simple_passer, this->muxer, _muxer_input_0, NULL, NULL),
      _make_monitor_sum_plugin(_is_not_fec_packet, _payload_extractor, this->muxer, _muxer_input_1),
      _make_monitor_sum_plugin(_is_fec_packet, _payload_extractor, this->muxer, _muxer_input_2),
      _make_monitor_sum_plugin(_is_not_fec_packet, _counter_extractor, this->muxer, _muxer_input_3),
      _make_monitor_sum_plugin(_is_fec_packet, _counter_extractor, this->muxer, _muxer_input_4),
      NULL);

  _dispatcher_add_outputs(this->dispatcher,
      this->tuple_packets_unrefer, _mapper_process,
      this->monitor, _monitor_refresh,
      NULL);

  //Connection
  _pushconnect_cmp(this->packets_refer, MAPPER_OUTPUT, this->monitor, _monitor_receive_process);


  _pushconnect_cmp(this->muxer, MUXER_OUTPUT, this->sampler, _sampler_process);
  _pushconnect_cmp(this->sampler, SAMPLER_OUTPUT, this, _rate_sampler_tuple_output_transmitter);
//  _pushconnect_cmp(muxer, MUXER_OUTPUT, writer, _file_writer_process);

  _pushconnect_cmp(this->monitor, MONITOR_FLUSH_OUTPUT, this->muxer, _muxer_flush);
  _pushconnect_cmp(this->monitor, MONITOR_TRASH_OUTPUT, this->packets_unrefer, _mapper_process);
  _pushconnect_cmp(this->sampler, SAMPLER_TRASH_OUTPUT, this->dispatcher, _dispatcher_process);
//  _pushconnect_cmp(this->sampler, SAMPLER_TRASH_OUTPUT, this->tuple_packets_unrefer, _mapper_process);
  return this;
}

static void _dispose_rate_sampler(RateSampler* this) {
  _dispose_mapper(this->packets_refer);
  _dispose_mapper(this->packets_unrefer);
  _dispose_mapper(this->tuple_packets_unrefer);

  _dispose_sampler(this->sampler);
  _dispose_monitor(this->monitor);
  _dispose_muxer(this->muxer);
  _dispose_dispatcher(this->dispatcher);
}

static void _sprintf_sr_fr_tuple_for_sr_fec(gchar* result, Tuple* sr_tuple){
  Int32* rtp_payload_rate = _tuple_get(sr_tuple, 1);
  Int32* fec_payload_rate = _tuple_get(sr_tuple, 2);
  Int32* rtp_packets_rate = _tuple_get(sr_tuple, 3);
  Int32* fec_packets_rate = _tuple_get(sr_tuple, 4);
  sprintf(result, "%d,%d",
      rtp_payload_rate->value + rtp_packets_rate->value * 28,
      fec_payload_rate->value + fec_packets_rate->value * 28
  );
}

static void _write_sr_fec_rate(gchar* input_path, gchar* output_path) {

  //Construction
  FileReader* reader = _make_reader(input_path, _make_rtp_packet);
  RateSampler* rate_sampler = _make_rate_sampler();
  FileWriter* writer = _make_writer(output_path, _sprintf_sr_fr_tuple_for_sr_fec);

  //Connection
  _pushconnect_cmp(reader, FILE_READER_OUTPUT, rate_sampler, _rate_sampler_packet_input_transmitter);
  _pushconnect_cmp(rate_sampler, RATE_SAMPLER_TUPLE_OUTPUT, writer, _file_writer_process);

  //Start
  {
    GThread* process = g_thread_create(_file_reader_process, reader, TRUE, NULL);
    g_thread_join(process);
  }

  //Dispose
  _dispose_reader(reader);
  _dispose_rate_sampler(rate_sampler);
  _dispose_writer(writer);
}

static void _sprintf_sr_fr_tuple_for_gp(gchar* result, Tuple* sr_tuple){
  Int32* rtp_payload_rate = _tuple_get(sr_tuple, 1);
  Int32* rtp_packets_rate = _tuple_get(sr_tuple, 3);
  sprintf(result, "%d",
      rtp_payload_rate->value + rtp_packets_rate->value * 28
  );
}

static void _write_gp_rate(gchar* input_path, gchar* output_path) {

  //Construction
  FileReader* reader = _make_reader(input_path, _make_rtp_packet);
  RateSampler* rate_sampler = _make_rate_sampler();
  FileWriter* writer = _make_writer(output_path, _sprintf_sr_fr_tuple_for_gp);

  //Connection
  _pushconnect_cmp(reader, FILE_READER_OUTPUT, rate_sampler, _rate_sampler_packet_input_transmitter);
  _pushconnect_cmp(rate_sampler, RATE_SAMPLER_TUPLE_OUTPUT, writer, _file_writer_process);

  //Start
  {
    GThread* process = g_thread_create(_file_reader_process, reader, TRUE, NULL);
    g_thread_join(process);
  }

  //Dispose
  _dispose_reader(reader);
  _dispose_rate_sampler(rate_sampler);
  _dispose_writer(writer);
}

static gint _cmp_packets(const RTPPacket* packet_x, const RTPPacket* packet_y) {
  return _cmp_seq(packet_x->seq_num, packet_y->seq_num);
}

static Tuple* _make_paired_packets_tuple(RTPPacket* packet_x, RTPPacket* packet_y){
//  g_print("Merged: %hu-%hu\n", packet_x->seq_num, packet_y->seq_num);
  return _make_tuple(packet_x, packet_y, NULL);
}

static void _paired_packets_unref(Tuple* paired_packets){
  RTPPacket* packet_x = _tuple_get(paired_packets, 0);
  RTPPacket* packet_y = _tuple_get(paired_packets, 1);
  _object_unref(packet_x);
  _object_unref(packet_y);
  _object_unref(paired_packets);
}


typedef struct{
  Component base;
  FileReader* snd_reader;
  FileReader* rcv_reader;
  Filter* snd_filter;
  Filter* rcv_filter;
  Sorter* rcv_sorter;
  Merger* merger;
  Mapper* packets_unrefer;
}RTPPacketsMerger;

typedef enum {
  RTP_PACKETS_MERGER_OUTPUT = 1,
  RTP_PACKETS_MERGER_TRASH_OUTPUT = 2,
}RTPPacketsMergerIO;

static void _rtp_packets_merger_transmitter(RTPPacketsMerger* this, gpointer data) {
  _transmit(&this->base, RTP_PACKETS_MERGER_OUTPUT, data);
}

static void _rtp_packets_merger_trash_transmitter(RTPPacketsMerger* this, gpointer data) {
  _transmit(&this->base, RTP_PACKETS_MERGER_TRASH_OUTPUT, data);
}

static gint _cmp_packets_with_udata(const RTPPacket* packet_x, const RTPPacket* packet_y, gpointer udata) {
  return _cmp_packets(packet_x, packet_y);
}

static RTPPacketsMerger* _make_rtp_packets_merger(gchar* snd_packets_path, gchar* rcv_packets_path) {
  RTPPacketsMerger* this = g_malloc0(sizeof(RTPPacketsMerger));
  this->snd_reader = _make_reader(snd_packets_path, _make_rtp_packet);
  this->rcv_reader = _make_reader(rcv_packets_path, _make_rtp_packet);
  this->snd_filter = _make_filter(_is_not_fec_packet, NULL);
  this->rcv_filter = _make_filter(_is_not_fec_packet, NULL);
  this->rcv_sorter = _make_sorter(_cmp_packets_with_udata, NULL, 32000);
  this->merger = _make_merger(_cmp_packets, _make_paired_packets_tuple);
  this->packets_unrefer = _make_mapper(_object_unref);

  _pushconnect_cmp(this->snd_reader, FILE_READER_OUTPUT, this->snd_filter, _filter_process);
  _pushconnect_cmp(this->rcv_reader, FILE_READER_OUTPUT, this->rcv_filter, _filter_process);
  _pushconnect_cmp(this->snd_filter, FILTER_PASSES_OUTPUT, this->merger, _merger_process_input_x);
  _pushconnect_cmp(this->rcv_filter, FILTER_PASSES_OUTPUT, this->rcv_sorter, _sorter_process);
  _pushconnect_cmp(this->rcv_sorter, SORTER_OUTPUT, this->merger, _merger_process_input_y);
  _pushconnect_cmp(this->merger, MERGER_OUTPUT, this, _rtp_packets_merger_transmitter);

  _pushconnect_cmp(this->snd_filter, FILTER_FAILS_OUTPUT, this->packets_unrefer, _mapper_process);
  _pushconnect_cmp(this->rcv_filter, FILTER_FAILS_OUTPUT, this->packets_unrefer, _mapper_process);
  _pushconnect_cmp(this->merger, MERGER_TRASH_OUTPUT, this, _rtp_packets_merger_trash_transmitter);
  _pushconnect_cmp(this, RTP_PACKETS_MERGER_TRASH_OUTPUT, this->packets_unrefer, _mapper_process);

  return this;
}

static void _rtp_packets_merger_start_and_join(RTPPacketsMerger* this) {
  GThread* snd_process = g_thread_create(_file_reader_process, this->snd_reader, TRUE, NULL);
  GThread* rcv_process = g_thread_create(_file_reader_process, this->rcv_reader, TRUE, NULL);
  g_thread_join(snd_process);
  g_thread_join(rcv_process);
}

static void _dispose_rtp_packets_merger(RTPPacketsMerger* this) {
  _dispose_filter(this->snd_filter);
  _dispose_filter(this->rcv_filter);
  _dispose_reader(this->snd_reader);
  _dispose_reader(this->rcv_reader);
  _dispose_sorter(this->rcv_sorter);
  _dispose_merger(this->merger);
}

static void _sprintf_paired_packets_for_qd(gchar* result, Tuple* paired_packets) {
  RTPPacket* packet_x = _tuple_get(paired_packets, 0);
  RTPPacket* packet_y = _tuple_get(paired_packets, 1);
  GstClockTime owd; // One Way Delay

  if(packet_x == packet_y)
    g_print("PacketX: %p | PacketY: %p\n", packet_x, packet_y);

  if(packet_x->tracked_ntp < packet_y->tracked_ntp) {
    owd = get_epoch_time_from_ntp_in_ns(packet_y->tracked_ntp - packet_x->tracked_ntp);
  } else {
    owd = get_epoch_time_from_ntp_in_ns(packet_x->tracked_ntp - packet_y->tracked_ntp);
  }
  sprintf(result, "%lu", owd / 1000);
}

static void _write_qd(gchar* snd_packets_path, gchar* rcv_packets_path, gchar* output_path) {

  //Construction
  RTPPacketsMerger* rtp_packets_merger = _make_rtp_packets_merger(snd_packets_path, rcv_packets_path);
  FileWriter* writer = _make_writer(output_path, _sprintf_paired_packets_for_qd);
  Mapper* packets_unrefer = _make_mapper(_object_unref);

  //Connection
  _pushconnect_cmp(rtp_packets_merger, RTP_PACKETS_MERGER_OUTPUT, writer, _file_writer_process);
  _pushconnect_cmp(writer, FILE_WRITER_TRASH_OUTPUT, packets_unrefer, _mapper_process);

  //Start
  _rtp_packets_merger_start_and_join(rtp_packets_merger);

  //Dispose
  _dispose_rtp_packets_merger(rtp_packets_merger);
  _dispose_writer(writer);
  _dispose_mapper(packets_unrefer);
}


static volatile gboolean _paired_packets_queue_is_full_1s_tracked_ntp(GQueue* queue) {
  RTPPacket *head, *tail;
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
  RTPPacket* packet_x = _tuple_get(paired_packets, 0);
  RTPPacket* packet_y = _tuple_get(paired_packets, 1);
//  g_print("%p-%p <- %hu\n", packet_x, packet_y, packet_x->seq_num);
  _object_ref(packet_x);
  _object_ref(packet_y);
  _object_ref(paired_packets);
}

static gint _paired_packets_owd_qsort_cmp(Tuple** pair_x, Tuple** pair_y) {
  RTPPacket *packet_x_snd, *packet_x_rcv;
  RTPPacket *packet_y_snd, *packet_y_rcv;
  GstClockTime owd_x, owd_y;
  packet_x_snd = _tuple_get(*pair_x, 0);
  packet_x_rcv = _tuple_get(*pair_x, 1);
  packet_y_snd = _tuple_get(*pair_y, 0);
  packet_y_rcv = _tuple_get(*pair_y, 1);
  owd_x = get_epoch_time_from_ntp_in_ns( packet_x_rcv->tracked_ntp - packet_x_snd->tracked_ntp);
  owd_y = get_epoch_time_from_ntp_in_ns( packet_y_rcv->tracked_ntp - packet_y_snd->tracked_ntp);
  return owd_x == owd_y ? 0 : owd_x < owd_y ? -1 : 1;
}

static gpointer _simple_paired_packets_refer(Tuple* tuple) {
  _paired_packets_ref(tuple);
  return tuple;
}

static void _write_qmd(gchar* snd_packets_path, gchar* rcv_packets_path, gchar* output_path) {

  //Construction
  RTPPacketsMerger* rtp_packets_merger = _make_rtp_packets_merger(snd_packets_path, rcv_packets_path);
  Mapper* paired_packets_refer = _make_mapper(_paired_packets_ref);
  Mapper* paired_packets_unrefer = _make_mapper(_paired_packets_unref);
  Monitor* monitor = _make_monitor(_paired_packets_queue_is_full_1s_tracked_ntp);
  Sampler* sampler = _make_sampler(_paired_packets_epoch_timestamp_extractor, 100 * GST_MSECOND);
  FileWriter* writer = _make_writer(output_path, _sprintf_paired_packets_for_qd);

  //Connection
  _pushconnect_cmp(rtp_packets_merger, RTP_PACKETS_MERGER_OUTPUT, paired_packets_refer, _mapper_process);
  _pushconnect_cmp(paired_packets_refer, MAPPER_OUTPUT, monitor, _monitor_receive_process);
  _pushconnect_cmp(monitor, MONITOR_FLUSH_OUTPUT, sampler, _sampler_process);

  _monitor_add_plugins(monitor,
      _make_monitor_percentile_plugin(
          50,
          _paired_packets_owd_qsort_cmp,
          _simple_paired_packets_refer, // Extractor
          NULL, // Filter
          sampler,
          _sampler_process
          ),
      NULL);

  _pushconnect_cmp(sampler, SAMPLER_OUTPUT, writer, _file_writer_process);
  _pushconnect_cmp(sampler, SAMPLER_TRASH_OUTPUT, paired_packets_unrefer, _mapper_process);

  //Start
  _rtp_packets_merger_start_and_join(rtp_packets_merger);

  //Dispose
  _dispose_rtp_packets_merger(rtp_packets_merger);
  _dispose_writer(writer);
  _dispose_monitor(monitor);
  _dispose_sampler(sampler);

  _dispose_mapper(paired_packets_unrefer);
  _dispose_mapper(paired_packets_refer);
}

typedef struct{
  gboolean first_rcved;
  gboolean last_rcved;
  gint32 lost_bytes;
  gint32 lost_num;
  gint32 snd_bytes;
  gint32 snd_num;
}TrackedPackets;

static void _tracking_lost_packets(TrackedPackets* tracked_packets, RTPPacket* lost_packet){
  if(tracked_packets->last_rcved || _is_fec_packet(lost_packet)) {
    return;
  }
//  g_print("Lost seq: %hu\n", lost_packet->seq_num);
  tracked_packets->lost_bytes += lost_packet->payload_size;
  ++tracked_packets->lost_num;
  tracked_packets->snd_bytes += lost_packet->payload_size;
  ++tracked_packets->snd_num;
  _object_unref((RTPPacket*)lost_packet);
}

static void _tracking_rcved_packets(TrackedPackets* tracked_packets, Tuple* paired_packets) {
  RTPPacket* snd_packet = _tuple_get(paired_packets, 0);
  tracked_packets->first_rcved = TRUE;
  tracked_packets->snd_bytes += snd_packet->payload_size;
  ++tracked_packets->snd_num;
}

static void _on_inputs_flushed_for_lr(TrackedPackets* tracked_packets, gpointer data) {
  tracked_packets->last_rcved = TRUE;
}

static void _sprintf_tracked_packets_for_lots(gchar* result, TrackedPackets* tracked_packets) {
  gdouble lost_rate = (gdouble)tracked_packets->lost_num / (gdouble)tracked_packets->snd_num;
  sprintf(result, "%1.3f", lost_rate);
}


static void _write_lr(gchar* snd_packets_path, gchar* rcv_packets_path, gchar* output_path) {

  //Construction
  TrackedPackets tracked_packets = {0,0,0,0};
  RTPPacketsMerger* rtp_packets_merger = _make_rtp_packets_merger(snd_packets_path, rcv_packets_path);
  Reducer* rcv_packets_reducer = _make_reducer(_tracking_rcved_packets, &tracked_packets);
  Reducer* lost_packets_reducer = _make_reducer(_tracking_lost_packets, &tracked_packets);
  Mapper* paired_packets_unrefer = _make_mapper(_paired_packets_unref);
  FileWriter* writer = _make_writer(output_path, _sprintf_tracked_packets_for_lots);

  //Connection
  _pushconnect_cmp(rtp_packets_merger, RTP_PACKETS_MERGER_OUTPUT, rcv_packets_reducer, _reducer_process);
  _pushconnect_cmp(rtp_packets_merger, RTP_PACKETS_MERGER_TRASH_OUTPUT, lost_packets_reducer, _reducer_process);
  _pushconnect_cmp(rcv_packets_reducer, REDUCER_DATA_OUTPUT, paired_packets_unrefer, _mapper_process);
  _pushconnect_cmp(rcv_packets_reducer, REDUCER_RESULT_OUTPUT, writer, _file_writer_process);

  //Event handlers
  _merger_add_on_input_x_flushed_handler(rtp_packets_merger->merger, &tracked_packets, _on_inputs_flushed_for_lr);
  _merger_add_on_input_y_flushed_handler(rtp_packets_merger->merger, &tracked_packets, _on_inputs_flushed_for_lr);

  //Start
  _rtp_packets_merger_start_and_join(rtp_packets_merger);

  //Dispose
  _dispose_rtp_packets_merger(rtp_packets_merger);
  _dispose_writer(writer);
  _dispose_reducer(lost_packets_reducer);
  _dispose_reducer(rcv_packets_reducer);

  _dispose_mapper(paired_packets_unrefer);
}

static gint _cmp_packets_with_fec(const RTPPacket* rtp_packet, const RTPPacket* fec_packet) {
  if( _cmp_seq(rtp_packet->seq_num, fec_packet->protect_begin) < 0) {
    return -1;
  }
  if( _cmp_seq(fec_packet->protect_end, rtp_packet->seq_num) < 0) {
    return 1;
  }
//  g_print("fec range: %d-%d, lost packet seq: %d\n",
//      fec_packet->protect_begin, fec_packet->protect_end, rtp_packet->seq_num);
  return 0;
}

typedef struct {
  gboolean last_rcved;
  gint32 protected_but_lost;
  gint32 recovered;
  FileWriter* writer;
}FFRETuple;

static void _ffre_increase_recovered(FFRETuple* ffre_tuple, gpointer data) {
  if(ffre_tuple->last_rcved) return;
  ++ffre_tuple->recovered;
}

static void _ffre_increase_protected_but_lost(FFRETuple* ffre_tuple, gpointer data) {
  if(ffre_tuple->last_rcved) return;
  ++ffre_tuple->protected_but_lost;
}

static void _ffre_sprintf(gchar* result, FFRETuple* ffre_tuple) {
  gdouble ffre = (gdouble)ffre_tuple->recovered / (gdouble)(ffre_tuple->recovered + ffre_tuple->protected_but_lost);
  g_print("recovered packets: %d, protected but lost: %d\n", ffre_tuple->recovered, ffre_tuple->protected_but_lost);
  sprintf(result, "%1.3f", ffre);
}

static void _on_inputs_flushed_for_ffre(FFRETuple* ffre_tuple, gpointer data) {
  ffre_tuple->last_rcved = TRUE;
  _file_writer_process(ffre_tuple->writer, ffre_tuple);
  _file_writer_process(ffre_tuple->writer, NULL);
}

static void _write_ffre(gchar* snd_packets_path, gchar* rcv_packets_path,
    gchar* ply_packets_path, gchar* fec_packets_path, gchar* output_path) {

  //Construction
  FileWriter* writer = _make_writer(output_path, _ffre_sprintf);
  FFRETuple ffre_tuple = {FALSE, 0,0, writer};
  RTPPacketsMerger* rtp_packets_merger = _make_rtp_packets_merger(snd_packets_path, rcv_packets_path);
  FileReader* fec_packets_reader = _make_reader(fec_packets_path, _make_rtp_packet);
  FileReader* ply_packets_reader = _make_reader(ply_packets_path, _make_rtp_packet);
  Sorter* ply_packets_sorter = _make_sorter(_cmp_packets_with_udata, NULL, 32000);
  Merger* not_received_packets_merger = _make_merger(_cmp_packets, _make_paired_packets_tuple);
  Merger* not_played_packets_merger = _make_merger(_cmp_packets_with_fec, _make_paired_packets_tuple);
  Reducer* ffre_recovered_reducer = _make_reducer(_ffre_increase_recovered, &ffre_tuple);
  Reducer* ffre_protected_but_lost_reducer = _make_reducer(_ffre_increase_protected_but_lost, &ffre_tuple);
  Mapper* paired_packets_unrefer = _make_mapper(_paired_packets_unref);
  Mapper* packet_unrefer = _make_mapper(_object_unref);

  //Connection
  _pushconnect_cmp(rtp_packets_merger, RTP_PACKETS_MERGER_OUTPUT, paired_packets_unrefer, _mapper_process);
  _pushconnect_cmp(rtp_packets_merger, RTP_PACKETS_MERGER_TRASH_OUTPUT, not_received_packets_merger, _merger_process_input_x);
  _pushconnect_cmp(ply_packets_reader, FILE_READER_OUTPUT, ply_packets_sorter, _sorter_process);
  _pushconnect_cmp(ply_packets_sorter, SORTER_OUTPUT, not_received_packets_merger, _merger_process_input_y);

  _pushconnect_cmp(not_received_packets_merger, MERGER_OUTPUT, ffre_recovered_reducer, _reducer_process);
  _pushconnect_cmp(not_received_packets_merger, MERGER_TRASH_OUTPUT_Y, packet_unrefer, _mapper_process);
  _pushconnect_cmp(not_received_packets_merger, MERGER_TRASH_OUTPUT_X, not_played_packets_merger, _merger_process_input_x);
  _pushconnect_cmp(ffre_recovered_reducer, REDUCER_DATA_OUTPUT, paired_packets_unrefer, _mapper_process);
  _pushconnect_cmp(fec_packets_reader, FILE_READER_OUTPUT, not_played_packets_merger, _merger_process_input_y);

  _pushconnect_cmp(not_played_packets_merger, MERGER_OUTPUT, ffre_protected_but_lost_reducer, _reducer_process);
  _pushconnect_cmp(not_played_packets_merger, MERGER_TRASH_OUTPUT, packet_unrefer, _mapper_process);
  _pushconnect_cmp(ffre_protected_but_lost_reducer, REDUCER_DATA_OUTPUT, paired_packets_unrefer, _mapper_process);

  //Event handlers
  _merger_add_on_input_y_flushed_handler(rtp_packets_merger->merger, &ffre_tuple, _on_inputs_flushed_for_ffre);

  //Start
  {
    GThread* fec_process = g_thread_create(_file_reader_process, fec_packets_reader, TRUE, NULL);
    GThread* ply_process = g_thread_create(_file_reader_process, ply_packets_reader, TRUE, NULL);
    _rtp_packets_merger_start_and_join(rtp_packets_merger);
    g_thread_join(fec_process);
    g_thread_join(ply_process);
  }


  //Dispose
  _dispose_rtp_packets_merger(rtp_packets_merger);
  _dispose_merger(not_received_packets_merger);
  _dispose_merger(not_played_packets_merger);
  _dispose_writer(writer);
  _dispose_reducer(ffre_recovered_reducer);
  _dispose_reducer(ffre_protected_but_lost_reducer);
  _dispose_mapper(packet_unrefer);
  _dispose_mapper(paired_packets_unrefer);
}

typedef struct {
  gint32 counter;
  gint32 sum;
  gdouble avg;
}AvgTuple;

static void _sprintf_avg_tup1e(gchar* result, AvgTuple* avg_tuple){
  sprintf(result, "%1.3f",
      avg_tuple->avg
  );
}

static void _sr_fr_tuple_to_rtp_avg_producer(AvgTuple* result, Tuple* sr_tuple){
  Int32* rtp_payload_rate = _tuple_get(sr_tuple, 1);
  Int32* rtp_packets_rate = _tuple_get(sr_tuple, 3);
  ++result->counter;
  result->sum += rtp_payload_rate->value + rtp_packets_rate->value * 28;
  result->avg = (gdouble)result->sum / (gdouble)result->counter;
}

static void _sr_fr_tuple_to_fec_avg_producer(AvgTuple* result, Tuple* sr_tuple){
  Int32* fec_payload_rate = _tuple_get(sr_tuple, 2);
  Int32* fec_packets_rate = _tuple_get(sr_tuple, 4);
  ++result->counter;
  result->sum += fec_payload_rate->value + fec_packets_rate->value * 28;
  result->avg = (gdouble)result->sum / (gdouble)result->counter;
}

static void _write_avg(gchar* input_path, gchar* output_path, ReducerProcess avg_producer) {

  //Construction
  AvgTuple avg_tuple = {0,0,0.};
  FileReader* reader = _make_reader(input_path, _make_rtp_packet);
  RateSampler* rate_sampler = _make_rate_sampler();
  Reducer* gp_reducer = _make_reducer(avg_producer, &avg_tuple);
  FileWriter* writer = _make_writer(output_path, _sprintf_avg_tup1e);

  //Connection
  _pushconnect_cmp(reader, FILE_READER_OUTPUT, rate_sampler, _rate_sampler_packet_input_transmitter);
  _pushconnect_cmp(rate_sampler, RATE_SAMPLER_TUPLE_OUTPUT, gp_reducer, _reducer_process);
  _pushconnect_cmp(gp_reducer, REDUCER_RESULT_OUTPUT, writer, _file_writer_process);

  //Start
  {
    GThread* process = g_thread_create(_file_reader_process, reader, TRUE, NULL);
    g_thread_join(process);
  }

  //Dispose
  _dispose_reader(reader);
  _dispose_rate_sampler(rate_sampler);
  _dispose_writer(writer);
}

typedef struct{
  gboolean last_packet_rcved;
  guint32 last_rcved_timestamp;
  guint32 last_lost_timestamp;
  gint32 received;
  gint32 lost;
}NLFTuple;

static void _nlf_increase_rcved_frames(NLFTuple* nlf_tuple, Tuple* paired_packets) {
  RTPPacket* snd_packet = _tuple_get(paired_packets, 0);
  if(nlf_tuple->last_packet_rcved) return;
  if(snd_packet->timestamp == nlf_tuple->last_rcved_timestamp)
  ++nlf_tuple->received;
  nlf_tuple->last_rcved_timestamp = snd_packet->timestamp;
}

static void _nlf_increase_lost_rcved_frames(NLFTuple* nlf_tuple, RTPPacket* lost_packet) {
  if(nlf_tuple->last_packet_rcved) return;
  if(lost_packet->timestamp == nlf_tuple->last_lost_timestamp)
  ++nlf_tuple->lost;
  nlf_tuple->last_lost_timestamp = lost_packet->timestamp;
}

static void _on_inputs_flushed_for_nlf(NLFTuple* nlf_tuple, gpointer data) {
  nlf_tuple->last_packet_rcved = TRUE;
}

static void _nlf_sprintf(gchar* result, NLFTuple* nlf_tuple) {
  g_print("received frames: %d, lost frames: %d\n", nlf_tuple->received, nlf_tuple->lost);
  sprintf(result, "%1.3f",
      nlf_tuple->lost
  );
}

static void _print_packet_seq(RTPPacket* packet) {
  g_print("Packet seq: %hu\n", packet->seq_num);
}

static void _write_nlf(gchar* snd_packets_path, gchar* ply_packets_path, gchar* output_path) {

  //Construction
  NLFTuple nlf_tuple = {FALSE, 0, 0, 0, 0};
  FileWriter* writer = _make_writer(output_path, _nlf_sprintf);
  RTPPacketsMerger* rtp_packets_merger = _make_rtp_packets_merger(snd_packets_path, ply_packets_path);
  Reducer* nlf_rcved_frames_reducer = _make_reducer(_nlf_increase_rcved_frames, &nlf_tuple);
  Reducer* nlf_not_rcved_frames_reducer = _make_reducer(_nlf_increase_lost_rcved_frames, &nlf_tuple);
  Mapper* paired_packets_unrefer = _make_mapper(_paired_packets_unref);
  Mapper* packet_unrefer = _make_mapper(_object_unref);
//  Mapper* packet_seq_printer = _make_mapper(_print_packet_seq);

  //Connection
  _pushconnect_cmp(rtp_packets_merger, RTP_PACKETS_MERGER_OUTPUT, nlf_rcved_frames_reducer, _reducer_process);
  _pushconnect_cmp(rtp_packets_merger, RTP_PACKETS_MERGER_TRASH_OUTPUT, nlf_not_rcved_frames_reducer, _reducer_process);
//  _pushconnect_cmp(rtp_packets_merger, RTP_PACKETS_MERGER_TRASH_OUTPUT, packet_seq_printer, _mapper_process);
//  _pushconnect_cmp(packet_seq_printer, MAPPER_OUTPUT, nlf_not_rcved_frames_reducer, _reducer_process);
  _pushconnect_cmp(nlf_rcved_frames_reducer, REDUCER_DATA_OUTPUT, paired_packets_unrefer, _mapper_process);
  _pushconnect_cmp(nlf_rcved_frames_reducer, REDUCER_RESULT_OUTPUT, writer, _file_writer_process);
  _pushconnect_cmp(nlf_not_rcved_frames_reducer, REDUCER_DATA_OUTPUT, packet_unrefer, _mapper_process);

  //Event handlers
  _merger_add_on_input_x_flushed_handler(rtp_packets_merger->merger, &nlf_tuple, _on_inputs_flushed_for_nlf);
  _merger_add_on_input_y_flushed_handler(rtp_packets_merger->merger, &nlf_tuple, _on_inputs_flushed_for_nlf);

  //Start
  {
    _rtp_packets_merger_start_and_join(rtp_packets_merger);
  }

  //Dispose
  _dispose_rtp_packets_merger(rtp_packets_merger);
  _dispose_writer(writer);
  _dispose_reducer(nlf_rcved_frames_reducer);
  _dispose_reducer(nlf_not_rcved_frames_reducer);
  _dispose_mapper(packet_unrefer);
  _dispose_mapper(paired_packets_unrefer);
}

static GstClockTime _get_tcp_packets_elapsed_time(TCPPacket* packet1, TCPPacket* packet2) {
  GstClockTime dtime;
  if (packet1->timestamp < packet2->timestamp) {
    dtime = packet2->timestamp - packet1->timestamp;
  } else {
    dtime = packet1->timestamp - packet2->timestamp;
  }
  return dtime;
}

static volatile gboolean _tcp_packet_queue_is_full_1s_tracked_ntp(GQueue* queue) {
  TCPPacket*head, *tail;
  GstClockTime elapsed;
  if(g_queue_is_empty(queue)){
    return FALSE;
  }
  head = g_queue_peek_head(queue);
  tail = g_queue_peek_tail(queue);
  return GST_SECOND < _get_tcp_packets_elapsed_time(head, tail);
}

typedef struct{
  gint32 packets_num;
  gint32 sending_rate;
  gint32 flownum;
  GSList* flows;
  GstClockTime last_timestamp;
}TCPStat;

static void _add_tcp_packet(TCPStat* tcpstat, TCPPacket* tcp_packet) {
  TCPFlow* tcp_flow;
  ++tcpstat->packets_num;
  tcpstat->sending_rate += tcp_packet->size;
  tcp_flow = g_list_find_custom(tcpstat->flows, tcp_packet, _find_tcp_flow);
//  g_print("src port: %hu dst port: %hu\n", tcp_packet->src_port, tcp_packet->dst_port);
  if(!tcp_flow){
    tcp_flow = _make_tcp_flow(tcp_packet);
    tcpstat->flows = g_list_prepend(tcpstat->flows, tcp_flow);
    ++tcpstat->flownum;
  }
  tcp_flow->last_timestamp = tcp_packet->timestamp;
  tcpstat->last_timestamp = tcp_packet->timestamp;
}

static void _rem_tcp_packet(TCPStat* tcpstat, TCPPacket* tcp_packet) {
  TCPFlow* tcp_flow;
  --tcpstat->packets_num;
  tcpstat->sending_rate -= tcp_packet->size;

  tcp_flow = g_list_find_custom(tcpstat->flows, tcp_packet, _find_tcp_flow);
  if(tcp_flow && tcp_flow->last_timestamp == tcp_packet->timestamp){
    tcpstat->flows = g_list_remove(tcpstat->flows, tcp_flow);
    --tcpstat->flownum;
    g_free(tcp_flow);
  }
}

static GstClockTime _extract_tcp_stat_timestamp(TCPStat* tcp_stat) {
  return tcp_stat->last_timestamp;
}

static void _sprintf_tcpstat(gchar* result, TCPStat* tcp_stat) {
  sprintf(result, "%d,%d", tcp_stat->sending_rate, tcp_stat->flownum);
}

static void _write_tcpstat(gchar* input_path, gchar* output_path) {

  //Construction
  TCPStat tcpstat = {0, 0, 0, NULL};
  PcapFileStreamer* reader = _make_pcap_flie_streamer(input_path, _make_tcp_packet);
  Monitor* monitor = _make_monitor(_tcp_packet_queue_is_full_1s_tracked_ntp);
  Sampler* sampler = _make_sampler(_extract_tcp_stat_timestamp, 100 * GST_MSECOND);
  FileWriter* writer = _make_writer(output_path, _sprintf_tcpstat);

  //Initialization
    _monitor_add_plugins(monitor,
        _make_monitor_reducer_plugin(NULL, &tcpstat, _add_tcp_packet, _rem_tcp_packet, sampler, _sampler_process),
        NULL);

  //Connection
  _pushconnect_cmp(reader, FILE_STREAM_OUTPUT, monitor, _monitor_receive_process);
  _pushconnect_cmp(monitor, MONITOR_FLUSH_OUTPUT, writer, _file_writer_process);
  _pushconnect_cmp(sampler, SAMPLER_OUTPUT, writer, _file_writer_process);

  //Start
  {
    GThread* process = g_thread_create(_pcap_flie_streamer_process, reader, TRUE, NULL);
    g_thread_join(process);
  }

  //Dispose
  _dispose_reader(reader);
  _dispose_monitor(monitor);
  _dispose_sampler(sampler);
  _dispose_writer(writer);
}

int main (int argc, char **argv)
{
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
    _write_sr_fec_rate(argv[3], argv[1]);
  }else if(strcmp(argv[2], "qd") == 0) {
    _write_qd(argv[3], argv[4], argv[1]);
  }else if(strcmp(argv[2], "qmd") == 0) {
    _write_qmd(argv[3], argv[4], argv[1]);
  }else if(strcmp(argv[2], "lr") == 0) {
    _write_lr(argv[3], argv[4], argv[1]);
  }else if(strcmp(argv[2], "ffre") == 0) {
    _write_ffre(argv[3], argv[4], argv[5], argv[6], argv[1]);
  }else if(strcmp(argv[2], "gp") == 0) {
    _write_gp_rate(argv[3], argv[1]);
  }else if(strcmp(argv[2], "gp_avg") == 0){
    _write_avg(argv[3], argv[1], _sr_fr_tuple_to_rtp_avg_producer);
  }else if(strcmp(argv[2], "fec_avg") == 0) {
    _write_avg(argv[3], argv[1], _sr_fr_tuple_to_fec_avg_producer);
  }else if(strcmp(argv[2], "ratio") == 0) {

  }else if(strcmp(argv[2], "nlf") == 0) {
    _write_nlf(argv[3], argv[4], argv[1]);
  }else if(strcmp(argv[2], "disc") == 0) {
    g_print("IMPLEMENT THIS!\n");
  }else if(strcmp(argv[2], "tcpstat") == 0) {
    _write_tcpstat(argv[3], argv[1]);
  }else{
    goto usage;
  }
  g_print("----------------------------------------\n");
  g_print("Results are made in %s\n", argv[1]);
  g_print("----------------------------------------\n");
  return 0;
}
