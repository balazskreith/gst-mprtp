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


typedef struct _Packet
{
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

static _setup_line(Packet* packet, gchar* line){
   sprintf(line, "%lu,%hu,%u,%u,%d,%u,%d,%d,%d,%hu,%hu,%d",
        packet->tracked_ntp,
        packet->seq_num,
        packet->timestamp,
        packet->ssrc,
        packet->payload_type,
        packet->payload_size,
        packet->subflow_id,
        packet->subflow_seq,
        packet->header_size,
        packet->protect_begin,
        packet->protect_end,
        packet->marker
  );
}

static Packet* _make_packet(gchar* line){
  Packet* packet = g_malloc0(sizeof(Packet));
  _setup_packet(packet, line);
  return packet;
}

typedef struct _Filter
{
  guint8               subflow_id;
  guint8               payload_type;
}Filter;

gboolean _filter(Filter* filter, Packet* packet){
  gboolean result = TRUE;
  if(0 < filter->payload_type){
      result &= packet->payload_type == filter->payload_type;
    }

  if(0 < filter->subflow_id){
      result &= packet->subflow_id == filter->subflow_id;
    }
  return result;
}

static GQueue* _get_filtered_packets(FILE* fp, Filter* filter){
  GQueue* result = g_queue_new();
  gchar line[1024];
  Packet* packet;

  while (fgets(line, 1024, fp)){
    packet = _make_packet(line);
    if(_filter(filter, packet)){
      g_queue_push_tail(result, packet);
    }
  }
  return result;
}

static void _fwrite(FILE* fp, GQueue* packets){
  Packet* packet;
  gchar line[1024];
  memset(line, 0, 1024);
  while(!g_queue_is_empty(packets)){
    packet = g_queue_pop_head(packets);
    _setup_line(packet, line);
    fprintf(fp, "%s\n", line);
    g_free(packet);
  }

}

int main (int argc, char **argv)
{
  Filter filter;
  FILE *inp,*outp;

  gint i;
  memset(&filter, 0, sizeof(Filter));

  if(argc < 3){
    g_print("Usage: ./program input_path output_path [option value]\n");
    g_print("option: payload_type [value]\n");
    g_print("option: subflow_id   [value]\n");
    return 0;
  }
  inp        = fopen (argv[1],"r");
  outp       = fopen (argv[2],"w");
  for(i = 3; i < argc; i+=2){
    if(!strcmp(argv[i], "subflow_id")){
      filter.subflow_id = atoi(argv[i+1]);
    }else if(!strcmp(argv[i], "payload_type")){
      filter.payload_type = atoi(argv[i+1]);
    }
  }
  _fwrite(outp, _get_filtered_packets(inp, &filter));
  fclose(inp);
  fclose(outp);
  g_print("Filtered packets are written into %s\n", argv[2]);
  return 0;
}
