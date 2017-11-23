#include "mapper.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <gst/gst.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#define MAX_PACKET_BUF_SIZE 255

static void _packet_to_csv_proxy(Mapper* this, gpointer item);
static void _binary_proxy(Mapper* this, gpointer item);

Mapper* make_mapper(const gchar* string, guint item_size) {
  Mapper* this = g_malloc0(sizeof(Mapper));
  gchar **tokens = g_strsplit(string, ":", -1);
  this->type = common_assign_string_to_int(tokens[0], "binary", "packet2csv", NULL);
  this->type_in_string = g_ascii_strup(tokens[0], strlen(tokens[0]));

  this->reset_process = make_process((ProcessCb)mapper_reset_metrics, this);

  fprintf(stdout, "Create Mapper. Type: %s\n", this->type_in_string);
  switch(this->type) {
    case MAPPER_FORMAT_PACKET_CSV:
      this->input = make_pushport((PushCb)_packet_to_csv_proxy, this);
      this->write_item.subject = g_malloc(MAX_PACKET_BUF_SIZE); // Size of the char buffer
      break;
    default:
    case MAPPER_FORMAT_BINARY:
      this->input = make_pushport((PushCb)_binary_proxy, this);
      break;
  }

  this->item_size = item_size;
  g_strfreev(tokens);
  return this;
}

const gchar* mapper_get_type_in_string(Mapper* this) {
  return this->type_in_string;
}

void mapper_dtor(Mapper* this) {
  pushport_dtor(this->input);
  pushport_dtor(this->output);
  g_free(this->type_in_string);
  g_free(this);
}

void mapper_sprintf(Mapper* this, gchar* string) {
  sprintf(string, "Mapper type: %s, number of transcieved items: %d, amount of bytes: %d->%d\n",
      this->type_in_string, this->transcieved_packets_num, this->rcved_bytes, this->sent_bytes);
}

void mapper_reset_metrics(Mapper* this) {
  fprintf(stdout, "Reset mapper");
  this->transcieved_packets_num = 0;
  this->rcved_bytes = 0;
  this->sent_bytes = 0;
}

static void _refresh_metrics(Mapper* this, guint rcved_item_length, guint sent_item_length) {
  ++this->transcieved_packets_num;
  this->rcved_bytes += rcved_item_length;
  this->sent_bytes += sent_item_length;
}

void _packet_to_csv_proxy(Mapper* this, gpointer item) {
  RTPStatPacket* packet = item;
  gchar* buffer = this->write_item.subject;
  gint buffer_size;
  memset(buffer, 0, MAX_PACKET_BUF_SIZE);
  buffer_size = sprintf(buffer,"%lu,%hu,%u,%d,%d,%u,%d,%d,%d,%hu,%hu,%d\n",
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
      packet->marker);

  this->write_item.length = buffer_size;
//  g_print("%s\n", buffer);
  pushport_send(this->output, &this->write_item);
  _refresh_metrics(this, this->item_size, buffer_size);
}

void _binary_proxy(Mapper* this, gpointer item) {
  this->write_item.subject = item;
  this->write_item.length = this->item_size;
  pushport_send(this->output, &this->write_item);
  _refresh_metrics(this, this->item_size, this->item_size);
}


