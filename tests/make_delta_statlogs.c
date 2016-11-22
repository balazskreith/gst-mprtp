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

#define _now(this) gst_clock_get_time (this->sysclock)

typedef struct{
  GstClockTime made;
  GstClockTime delay;
  gint32       BiF;

  GstClockTime playout_delay;
}dPacketItem;

typedef void (*dPacketsLogger)(dPacketItem*);

typedef struct{
  GstClock*         sysclock;
  GQueue*           items;
  GQueue*           recycle;
  GstClockTime      length;
  GstClockTime      sampling_time;
  GstClockTime      last_sampling;
  dPacketsLogger    owd_logger;
  dPacketsLogger    BiF_logger;
  dPacketsLogger    playout_rcv_logger;

  GstClockTime      made;
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

dPackets* dpackets_ctor(void)
{
  dPackets* this;
  this = g_malloc0(sizeof(dPackets));
  this->items   = g_queue_new();
  this->recycle = g_queue_new();
  this->sysclock = gst_system_clock_obtain();
  return this;
}

void dpackets_dtor(dPackets* this)
{
  g_queue_free_full(this->items,   g_free);
  g_queue_free_full(this->recycle, g_free);
  g_object_unref(this->sysclock);
  g_free(this);
}

dPackets* make_dpackets(GstClockTime accumulation_length,
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

static void _refresh_by_length(dPackets* this){
  dPacketItem* first,*last;
again:
  if(g_queue_is_empty(this->items)){
    return;
  }
  first = g_queue_peek_head(this->items);
  last  = g_queue_peek_tail(this->items);
  if(first->made < last->made - this->length){
    g_queue_push_tail(this->recycle, g_queue_pop_head(this->items));
    goto again;
  }
}

static GstClockTime dpackets_get_lifetime(dPackets* this)
{
  return _now(this) - this->made;
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


static void _refresh_by_time(dPackets* this){
  guint length;
  GQueue* copy = NULL;
  dPacketItem result;
  dPacketItem *last;
  last = g_queue_peek_tail(this->items);
  if(this->last_sampling == 0){
    this->last_sampling = last->made;
    return;
  }

again:
  if(last->made - this->sampling_time < this->last_sampling){
    if(copy){
      g_queue_free(copy);
    }
    return;
  }
  if(!copy){
    copy = g_queue_copy(this->items);
  }

//  g_print("Median of OWD: %lu\n", result.delay);
  _get_queue_median(copy, &result, (GCompareDataFunc) _queue_sort_by_delay);
  this->owd_logger(&result);
  _get_queue_median(copy, &result, (GCompareDataFunc) _queue_sort_by_BiF);
  this->BiF_logger(&result);
  _get_queue_percentile(copy, &result, (GCompareDataFunc) _queue_sort_by_playout_delay, 80);
  this->playout_rcv_logger(&result);
  this->last_sampling += this->sampling_time;
  goto again;

}

static void dpackets_add(dPackets* this, dPacketItem *item)
{
  _refresh_by_length(this);
  g_queue_push_tail(this->items, item);
  _refresh_by_time(this);

}

//static gint
//_cmp_ts (guint32 x, guint32 y)
//{
//  if(x == y) return 0;
//  if(x < y && y - x < 2147483648) return -1;
//  if(x > y && x - y > 2147483648) return -1;
//  if(x < y && y - x > 2147483648) return 1;
//  if(x > y && x - y < 2147483648) return 1;
//  return 0;
//}
static void dpackets_add_by_record(dPackets* this, MergedPacketLogsRecord* record, gint32 bytes_in_flight)
{
  dPacketItem* item;
  GstClockTime sent, received, played;
  if(g_queue_is_empty(this->recycle)){
    item = g_malloc0(sizeof(dPacketItem));
  }else{
    item = g_queue_pop_head(this->recycle);
    memset(item, 0, sizeof(dPacketItem));
  }

  sent     = get_epoch_time_from_ntp_in_ns(record->tracked_ntp_snd);
  received = get_epoch_time_from_ntp_in_ns(record->tracked_ntp_rcv);
  played   = get_epoch_time_from_ntp_in_ns(record->played_out_rcv);

  item->made           = sent;
  item->delay          = received - item->made;
  item->playout_delay  = played   - received;
  item->BiF            = bytes_in_flight;

//  g_print("Item added. %lu-%d-%lu\n", item->delay, item->BiF, item->playout_delay);
  dpackets_add(this, item);
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
  gint32  processed_rows = 0;
  GQueue* records = g_queue_new();
  GQueue* recycle = g_queue_new();

  dPackets *dpackets = make_dpackets(GST_SECOND, 100 * GST_MSECOND,
      _set_owd,
      _set_BiF,
      _set_playout_delay);

  char line[1024];

  if(argc < 3){
    g_print("Usage: ./program source_path result_path\n");
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
        "Elapsed time:            %fs\n"
        "Nr. of processed rows:   %d\n",

        (gdouble)GST_TIME_AS_MSECONDS(dpackets_get_lifetime(dpackets)) / 1000.,
        processed_rows
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

