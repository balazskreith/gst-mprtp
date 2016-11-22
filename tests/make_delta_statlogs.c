#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>

#define current_unix_time_in_us g_get_real_time ()
#define current_unix_time_in_ms (current_unix_time_in_us / 1000L)
#define current_unix_time_in_s  (current_unix_time_in_ms / 1000L)
#define epoch_now_in_ns ((guint64)((current_unix_time_in_us + 2208988800000000LL) * 1000))
#define get_ntp_from_epoch_ns(epoch_in_ns) gst_util_uint64_scale (epoch_in_ns, (1LL << 32), GST_SECOND)
#define get_epoch_time_from_ntp_in_ns(ntp_time) gst_util_uint64_scale (ntp_time, GST_SECOND, (1LL << 32))
#define NTP_NOW get_ntp_from_epoch_ns(epoch_now_in_ns)
#define _now(this) (gst_clock_get_time (this->sysclock))

#define PROFILING_BY_SYSCLOCK(sysclock, msg, func, code) \
{  \
  GstClockTime start, elapsed; \
  start = gst_clock_get_time (sysclock); \
  func; \
  elapsed = GST_TIME_AS_MSECONDS(gst_clock_get_time (sysclock) - start); \
  if(10 < elapsed) {g_print(msg" elapsed time in ms: %lu\n", elapsed); code;}\
}

//#define PROFILING(msg, func) \
//{  \
//  GstClockTime start, elapsed; \
//  start = _now(this); \
//  func; \
//  elapsed = GST_TIME_AS_MSECONDS(_now(this) - start); \
//  if(0 < elapsed) {g_print(msg" elapsed time in ms: %lu\n", elapsed); }\
//}

#define PROFILING(msg, func) PROFILING_BY_SYSCLOCK(this->sysclock, msg, func, ;)

#define PROFILING_AND_DOCODE(msg, func, code) PROFILING_BY_SYSCLOCK(this->sysclock, msg, func, code)

typedef struct{
  GstClockTime made;
  GstClockTime delay;
  gint32       BiF;

  GstClockTime playout_delay;
}dPacketItem;

typedef void (*dPacketsLogger)(dPacketItem*);

typedef struct datapuffer_struct_t
{
        gpointer                *items;                ///< A pointer array of data the puffer will uses for storing
        gint32                   length;               ///< The maximal amount of data the puffer can store
        gint32                   start;        ///< index for read operations. It points to the next element going to be read
        gint32                   end;      ///< index for write operations. It points to the last element, which was written by the puffer
        gint32                   count;
        gpointer                 read;
} datapuffer_t;


typedef struct{
  GstClock*         sysclock;
  GstClockTime      made;

//  GQueue*           items;
  GstClockTime      length;
  GstClockTime      sampling_time;
  GstClockTime      last_sampling;
  dPacketsLogger    owd_logger;
  dPacketsLogger    BiF_logger;
  dPacketsLogger    playout_rcv_logger;

  datapuffer_t*     items2;


}dPackets;

typedef struct{
  guint32 extended_seq;

  guint16 tracked_seq_snd;
  gint    state_snd;
  guint64 tracked_ntp_snd;
  gint    marker_snd;
  guint   header_size_snd;
  guint   payload_size_snd;
  guint32 timestamp_snd;
  gint32  payload_type_snd;
  guint64 played_out_snd;

  guint16 tracked_seq_rcv;
  gint    state_rcv;
  guint64 tracked_ntp_rcv;
  gint    marker_rcv;
  guint   header_size_rcv;
  guint   payload_size_rcv;
  guint32 timestamp_rcv;
  gint32  payload_type_rcv;
  guint64 played_out_rcv;
}MergedPacketLogsRecord;

typedef struct{
  gpointer items;
  gint32   length;
}Array;

typedef void(*CmpFunc)(const gpointer a, const gpointer b);

static Array* make_array(gpointer items, guint length)
{
  Array* result  = g_malloc0(sizeof(Array));
  result->items  = items;
  result->length = length;
  return result;
}

static gpointer array_nth(Array* this, gint32 nth){
  return this->items + nth;
}

static void array_dtor(Array* this){
  g_free(this);
}

static void array_sort(Array* this, CmpFunc comparator){
  qsort(this->items, this->length, sizeof(gpointer), comparator);
}

typedef void(*array_sprint)(gpointer udata, gpointer item);
static void array_print(Array* this, array_sprint logger, gpointer logger_udata){
  gint32 i;
  for(i = 0; i<this->length; ++i){
    logger(logger_udata, this->items + i);
  }
}


static datapuffer_t* datapuffer_ctor(gint32 size)
{
        datapuffer_t* result;
        result = (datapuffer_t*) g_malloc0(sizeof(datapuffer_t));
        result->items = (gpointer*) g_malloc0(sizeof(gpointer) * size);
        result->length = size;
        result->start = 0;
        result->end = 0;
        result->count = 0;
        return result;
}//# datapuffer_ctor end


static void datapuffer_dtor(datapuffer_t* puffer)
{
        gint32 index;
        gpointer item;
        index = 0;
        if(puffer == NULL){
                return;
        }
        for(index = 0; index <  puffer->length; index++)
        {
                item = puffer->items[index];
                if(item == NULL)
                {
                  continue;
                }
                g_free(item);
        }
        g_free(puffer->items);
        g_free(puffer);
}//# datapuffer_dtor end

static void datapuffer_write(datapuffer_t* puffer, gpointer item)
{
        puffer->items[puffer->end++] = item;
        ++puffer->count;
        if(puffer->length <= puffer->end){
                puffer->end = 0;
        }
}//# datapuffer_write end

static gpointer datapuffer_read(datapuffer_t* puffer)
{
        puffer->read = puffer->items[puffer->start];
        puffer->items[puffer->start] = NULL;
        if(puffer->length <= ++puffer->start){
                puffer->start = 0;
        }
        --puffer->count;
        return puffer->read;
}//# datapuffer_read end

static gboolean datapuffer_isfull(datapuffer_t *datapuffer)
{
        return datapuffer->count == datapuffer->length;
}

static gboolean datapuffer_isempty(datapuffer_t *datapuffer)
{
        return datapuffer->count == 0;
}

static Array* datapuffer_get_items_in_array(datapuffer_t *this)
{
  gpointer items = g_malloc(sizeof(gpointer) * this->count);
  guint32 end;
  if(this->end == 0){
    end = this->length - 1;
  }else{
    end = this->end - 1;
  }
  if(this->start < end){
    memcpy(items, this->items + this->start, this->count);
  }else{
    guint32 count = this->length - this->start;
    memcpy(items, this->items + this->start, count);
    memcpy(items + count, this->items, this->count - count);
  }
  return make_array(items, this->count);
}

static gpointer datapuffer_peek_nth_item(datapuffer_t* this, gint32 nth)
{
  gpointer result;
  if(this->start + nth < this->length){
    return this->items + this->start + nth;
  }
  nth -= this->length - this->start;
  return this->items + nth;
}

gint32 datapuffer_readcapacity(datapuffer_t *datapuffer)
{
        return datapuffer->count;
}

static gpointer datapuffer_peek_first(datapuffer_t* puffer)
{
        return puffer->items[puffer->start];
}//# datapuffer_read end

static gpointer datapuffer_peek_last(datapuffer_t* puffer)
{
  gint32 pos;
  if(puffer->end == 0){
    pos = puffer->length - 1;
  }else{
    pos = puffer->end - 1;
  }
  return puffer->items[pos];
}//# datapuffer_read end




static dPackets* dpackets_ctor(void)
{
  dPackets* this;
  this = g_malloc0(sizeof(dPackets));
//  this->items    = g_queue_new();
  this->items2   = datapuffer_ctor(50000);
  this->sysclock = gst_system_clock_obtain ();
  return this;
}

static void dpackets_dtor(dPackets* this)
{
  datapuffer_dtor(this->items2);
//  g_queue_free_full(this->items, g_free);
  g_object_unref(this->sysclock);
  g_free(this);
}

static dPackets* make_dpackets(GstClockTime accumulation_length,
                        GstClockTime sampling_time,
                        dPacketsLogger owd_logger,
                        dPacketsLogger BiF_logger,
                        dPacketsLogger playout_rcv_logger)
{
  dPackets* this = dpackets_ctor();
  this->length             = accumulation_length;
  this->sampling_time      = sampling_time;
  this->owd_logger         = owd_logger;
  this->BiF_logger         = BiF_logger;
  this->playout_rcv_logger = playout_rcv_logger;
  this->last_sampling      = 0;
  this->made               = _now(this);

  return this;
}

//static void _refresh_by_length(dPackets* this){
//  dPacketItem* first,*last;
//again:
//  if(g_queue_is_empty(this->items)){
//    return;
//  }
//  first = g_queue_peek_head(this->items);
//  last  = g_queue_peek_tail(this->items);
//  if(first->made < last->made - this->length){
//    g_free(g_queue_pop_head(this->items));
//    goto again;
//  }
//}


static void _refresh_by_length(dPackets* this){
  dPacketItem* first,*last;
again:
  if(datapuffer_isempty(this->items2)){
    return;
  }
  first = datapuffer_peek_first(this->items2);
  last  = datapuffer_peek_last(this->items2);
  if(first->made < last->made - this->length){
    g_free(datapuffer_read(this->items2));
    goto again;
  }
}

static gint _queue_sort_by_delay(dPacketItem* a, dPacketItem* b, gpointer udata){
  if(a->delay == b->delay){
    return 0;
  }
  return a->delay < b->delay ? -1 : 1;
}

static gint _queue_sort_by_BiF(dPacketItem* a, dPacketItem* b, gpointer udata){
  if(a->BiF == b->BiF){
    return 0;
  }
  return a->BiF < b->BiF ? -1 : 1;
}

static gint _queue_sort_by_playout_delay(dPacketItem* a, dPacketItem* b, gpointer udata){
  if(a->playout_delay == b->playout_delay){
    return 0;
  }
  return a->playout_delay < b->playout_delay ? -1 : 1;
}





static gint _dPacketItem_cmp_by_delay(const dPacketItem* a, const dPacketItem* b){
  if(a->delay == b->delay){
    return 0;
  }
  return a->delay < b->delay ? -1 : 1;
}

static gint _dPacketItem_cmp_by_BiF(const dPacketItem* a, const dPacketItem* b){
  if(a->BiF == b->BiF){
    return 0;
  }
  return a->BiF < b->BiF ? -1 : 1;
}

static gint _dPacketItem_cmp_by_playout_delay(const dPacketItem* a, const dPacketItem* b){
  if(a->playout_delay == b->playout_delay){
    return 0;
  }
  return a->playout_delay < b->playout_delay ? -1 : 1;
}


static void _merge_dpacket_items(dPacketItem *left, dPacketItem *right, dPacketItem *result){
  result->delay = (left->delay + right->delay)>>1;
  result->made = (left->made + right->made)>>1;
}


static void _get_queue_median(GQueue* queue, dPacketItem* result, GCompareDataFunc comparator){
  guint length;
  g_queue_sort(queue, comparator, NULL);
  length = g_queue_get_length(queue);

  if(length % 2 == 1){
    //If n is odd then Median (M) = value of ((n + 1)/2)th item from a sorted array of length n.
    memcpy(result, g_queue_peek_nth(queue, length / 2), sizeof(dPacketItem));
  }else{
    //If n is even then Median (M) = value of [((n)/2)th item term + ((n)/2 + 1)th item term ] /2
    dPacketItem *left,*right;
    left  = g_queue_peek_nth(queue, length / 2);
    right = g_queue_peek_nth(queue, length / 2 + 1);
    _merge_dpacket_items(left, right, result);
  }
}

static void _get_queue_max(GQueue* queue, dPacketItem* result, GCompareDataFunc comparator){
  guint length;
  g_queue_sort(queue, comparator, NULL);
  length = g_queue_get_length(queue);
  memcpy(result, g_queue_peek_nth(queue, length - 1), sizeof(dPacketItem));
}

static void _get_queue_percentile(GQueue* queue, dPacketItem* result, GCompareDataFunc comparator, guint percentile){
  guint item_num;
  g_queue_sort(queue, comparator, NULL);
  item_num = g_queue_get_length(queue) * (gdouble)percentile / 100.;
  memcpy(result, g_queue_peek_nth(queue, item_num - 1), sizeof(dPacketItem));
}

static void item_log(gpointer udata, dPacketItem* item){
  g_print("dPacketItem: %lu - %d - %lu\n",
      GST_TIME_AS_MSECONDS(item->delay),
      item->BiF,
      GST_TIME_AS_MSECONDS(item->playout_delay));
}

static void _get_array_median(Array* array, dPacketItem* result, CmpFunc comparator){
  guint length;
  array_sort(array, comparator);
//  array_print(array, (array_sprint) item_log, NULL);
  length = array->length;

  if(length % 2 == 1){
    //If n is odd then Median (M) = value of ((n + 1)/2)th item from a sorted array of length n.
    memcpy(result, array_nth(array, length / 2), sizeof(dPacketItem));
  }else{
    //If n is even then Median (M) = value of [((n)/2)th item term + ((n)/2 + 1)th item term ] /2
    dPacketItem *left,*right;
    left  = array_nth(array, length / 2);
    right = array_nth(array, length / 2 + 1);
    _merge_dpacket_items(left, right, result);
  }
}

static void _get_array_max(Array* array, dPacketItem* result, CmpFunc comparator){
  guint length;
  array_sort(array, comparator);
  length = array->length;
  memcpy(result, array_nth(array, length - 1), sizeof(dPacketItem));
}

static void _get_array_percentile(Array* array, dPacketItem* result, CmpFunc comparator, guint percentile){
  guint item_num;
  array_sort(array, comparator);
  item_num = array->length * (gdouble)percentile / 100.;
  memcpy(result, array_nth(array, item_num - 1), sizeof(dPacketItem));
}
//
//static void _refresh_by_time(dPackets* this){
//  guint length;
//  GQueue* copy = NULL;
//  dPacketItem result;
//  dPacketItem *last;
//
//
//  last = g_queue_peek_tail(this->items);
//
//  if(this->last_sampling == 0){
//    this->last_sampling = get_epoch_time_from_ntp_in_ns(last->made);
//    return;
//  }
//again:
//  if(get_epoch_time_from_ntp_in_ns(last->made) - this->sampling_time < this->last_sampling){
//    if(copy){
//      g_queue_free(copy);
//    }
//    return;
//  }
//  if(!copy){
//    copy = g_queue_copy(this->items);
//  }
//
////  g_print("Median of OWD: %lu\n", result.delay);
////  PROFILING("_get_queue_median:_queue_sort_by_delay",
//  _get_queue_median(copy, &result, (GCompareDataFunc) _queue_sort_by_delay);
//  this->owd_logger(&result);
////  );
//
////  PROFILING("_get_queue_median:_queue_sort_by_BiF",
//  _get_queue_median(copy, &result, (GCompareDataFunc) _queue_sort_by_BiF);
//  this->BiF_logger(&result);
////  );
//
////  PROFILING("_get_queue_percentile:_queue_sort_by_playout_delay",
////  _get_queue_max(copy, &result, (GCompareDataFunc) _queue_sort_by_playout_delay);
//  _get_queue_percentile(copy, &result, (GCompareDataFunc) _queue_sort_by_playout_delay, 80);
//  this->playout_rcv_logger(&result);
////  );
//
//
//  this->last_sampling += this->sampling_time;
//  goto again;
//}




static void _refresh_by_time(dPackets* this){
  guint length;
  Array* array = NULL;
  dPacketItem result;
  dPacketItem *last;


  last = datapuffer_peek_last(this->items2);

  if(this->last_sampling == 0){
    this->last_sampling = get_epoch_time_from_ntp_in_ns(last->made);
    return;
  }
again:
  if(get_epoch_time_from_ntp_in_ns(last->made) - this->sampling_time < this->last_sampling){
    if(array){
      array_dtor(array);
    }
    return;
  }
  if(!array){
    array = datapuffer_get_items_in_array(this->items2);
  }

  _get_array_median(array, &result, (CmpFunc) _dPacketItem_cmp_by_delay);
  this->owd_logger(&result);

  _get_array_median(array, &result, (CmpFunc) _dPacketItem_cmp_by_BiF);
  this->BiF_logger(&result);

  _get_array_percentile(array, &result, (CmpFunc) _dPacketItem_cmp_by_playout_delay, 80);
  this->playout_rcv_logger(&result);


  this->last_sampling += this->sampling_time;
  goto again;
}


//static void dpackets_add(dPackets* this, dPacketItem *item)
//{
//  PROFILING("_refresh_by_length",
//  _refresh_by_length(this);
//  );
//  g_queue_push_tail(this->items, item);
//
//  PROFILING_AND_DOCODE("_refresh_by_time",
//  _refresh_by_time(this);,
//  g_print("queue length: %u", g_queue_get_length(this->items));
//  );
//}

static void dpackets_add(dPackets* this, dPacketItem *item)
{
  PROFILING("_refresh_by_length",
  _refresh_by_length(this);
  );
  if(datapuffer_isfull(this->items2)){
    g_print("WARNING! datapuffer is too small!");
    return;
  }
  datapuffer_write(this->items2, item);

  PROFILING_AND_DOCODE("_refresh_by_time",
  _refresh_by_time(this);,
  g_print("queue length: %u", datapuffer_readcapacity(this->items2));
  );
}

static GstClockTime dpackets_get_lifetime(dPackets* this){
  return _now(this) - this->made;
}

static gint
_cmp_ts (guint32 x, guint32 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 2147483648) return -1;
  if(x > y && x - y > 2147483648) return -1;
  if(x < y && y - x > 2147483648) return 1;
  if(x > y && x - y < 2147483648) return 1;
  return 0;
}
static GstClockTime last_playout_delay = 0;
static GstClockTime last_played_out = 0;
static guint32 last_timestamp_snd = 0;
static void dpackets_add_by_record(dPackets* this, MergedPacketLogsRecord* record, gint32 bytes_in_flight)
{
  dPacketItem* item = g_malloc0(sizeof(dPacketItem));
  item->made   = get_epoch_time_from_ntp_in_ns(record->tracked_ntp_snd);
  item->delay  = get_epoch_time_from_ntp_in_ns(record->tracked_ntp_rcv) - item->made;
  item->BiF    = bytes_in_flight;

  if(!last_played_out){
    last_played_out        = get_epoch_time_from_ntp_in_ns(record->played_out_rcv);
    last_timestamp_snd     = record->timestamp_snd;
    last_playout_delay     = 0;
    item->playout_delay    = 0;
  }else if(_cmp_ts(last_timestamp_snd, record->timestamp_snd) < 0){
    GstClockTime played_out = get_epoch_time_from_ntp_in_ns(record->played_out_rcv);
    item->playout_delay    = last_playout_delay = played_out - last_played_out;
    last_played_out        = played_out;
    last_timestamp_snd     = record->timestamp_snd;
  }else{
    item->playout_delay    = last_playout_delay;
  }

//  g_print("Item added. %lu-%d-%lu\n", item->delay, item->BiF, item->playout_delay);
  PROFILING("dpackets_add_by_record",
  dpackets_add(this, item);
  );
}

typedef struct{
 GstClockTime owd;
 GstClockTime playout_delay;
 gint32       BiF;
 gboolean     refreshed;
}ResultRecord;

static ResultRecord result_record = {0,0,0};

static void _set_owd(dPacketItem* item){
  result_record.owd = item->delay;
  result_record.refreshed = TRUE;
}

static void _set_BiF(dPacketItem* item){
  result_record.BiF = item->BiF;
  result_record.refreshed = TRUE;
}

static void _set_playout_delay(dPacketItem* item){
  result_record.playout_delay = item->playout_delay;
  result_record.refreshed         = TRUE;
}


int main (int argc, char **argv)
{
  FILE *source, *destination;
  guint32 bytes_in_flight = 0;
  GQueue* recycle = g_queue_new();
  GQueue* records = g_queue_new();
  guint32 processed_rows = 0;

  dPackets *dpackets = make_dpackets(GST_SECOND, 100 * GST_MSECOND,
      _set_owd,
      _set_BiF,
      _set_playout_delay);

  char line[1024];

  if(argc < 3){
    g_print("Usage: ./program merged_packetslog_path delta_packetlogs_stat\n");
    return 0;
  }
  source = fopen (argv[1],"r");
  destination = fopen (argv[2],"w");

  g_print("Make Delta Packet Statlogs.\n"
      "Source file:             %s\n"
      "Destination file:        %s\n"
      "Accumulation length:     %lums\n"
      "Sampling time:           %lums\n"
      "Unit of delay:           us\n"
      "Unit of bytes in flight: byte\n",

      argv[1],
      argv[2],
      GST_TIME_AS_MSECONDS(dpackets->length),
      GST_TIME_AS_MSECONDS(dpackets->sampling_time)
  );

  fgets(line, 1024, source);//ignore first line;

  while (fgets(line, 1024, source))
  {
    gint32 rownum;
    MergedPacketLogsRecord *first;
    MergedPacketLogsRecord *last;

    if(g_queue_is_empty(recycle)){
      last = g_malloc0(sizeof(MergedPacketLogsRecord));
    }else{
      last = g_queue_pop_head(recycle);
      memset(last, 0, sizeof(MergedPacketLogsRecord));
    }


    ++processed_rows;

    sscanf(line, "%d,%u,"
                 "%hu,%d,%lu,%d,%d,%d,%u,%d,%lu,%hu,%d,%lu,%d,%d,%d,%u,%d,%lu",
                &rownum,
                &last->extended_seq,

                &last->tracked_seq_snd,
                &last->state_snd,
                &last->tracked_ntp_snd,
                &last->marker_snd,
                &last->header_size_snd,
                &last->payload_size_snd,
                &last->timestamp_snd,
                &last->payload_type_snd,
                &last->played_out_snd,

                &last->tracked_seq_rcv,
                &last->state_rcv,
                &last->tracked_ntp_rcv,
                &last->marker_rcv,
                &last->header_size_rcv,
                &last->payload_size_rcv,
                &last->timestamp_rcv,
                &last->payload_type_rcv,
                &last->played_out_rcv
         );


    bytes_in_flight += last->payload_size_snd;
    g_queue_push_tail(records, last);
  again:
    first = g_queue_peek_head(records);
    if(first->tracked_ntp_rcv < last->tracked_ntp_snd){
      bytes_in_flight -= first->payload_size_snd;
      g_queue_push_tail(recycle, g_queue_pop_head(records));
      goto again;
    }

    dpackets_add_by_record(dpackets, last, bytes_in_flight);

    if(result_record.refreshed){
      fprintf(destination, "%lu,%d,%lu\n",
          result_record.owd / 1000,
          result_record.BiF,
          result_record.playout_delay / 1000);
      result_record.refreshed = FALSE;
    }

  }
  g_print(
      "Nr. of rows processed:   %u\n"
      "Processing time:         %fs\n",
      processed_rows,
      (gdouble)GST_TIME_AS_MSECONDS(dpackets_get_lifetime(dpackets)) / 1000.
  );


  dpackets_dtor(dpackets);
  g_queue_free_full(records, g_free);
  g_queue_free_full(recycle, g_free);
  fclose(source);
  fclose(destination);

  g_print("Delta PacketStatlog is made\n");
  return 0;
}


//g_print("%s", line);
//        g_print("%d,%u,"
//                "%hu,%d,%lu,%d,%d,%d,%u,%d,%lu,"
//                "%hu,%d,%lu,%d,%d,%d,%u,%d,%lu\n\n",
//                    rownum,
//                    last->extended_seq,
//
//                    last->tracked_seq_snd,
//                    last->state_snd,
//                    last->tracked_ntp_snd,
//                    last->marker_snd,
//                    last->header_size_snd,
//                    last->payload_size_snd,
//                    last->timestamp_snd,
//                    last->payload_type_snd,
//                    last->played_out_snd,
//
//                    last->tracked_seq_rcv,
//                    last->state_rcv,
//                    last->tracked_ntp_rcv,
//                    last->marker_rcv,
//                    last->header_size_rcv,
//                    last->payload_size_rcv,
//                    last->timestamp_rcv,
//                    last->payload_type_rcv,
//                    last->played_out_rcv
//    );


