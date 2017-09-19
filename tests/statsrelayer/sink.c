#include "sink.h"
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

static void _write_file(Sink* this, gpointer item, gint item_size);
static void _write_mkfifo(Sink* this, gpointer item, gint item_size);
static void _sendto_unix_socket(Sink* this, gpointer item, gint item_size);

static void _close_file(Sink* this);
static void _close_socket(Sink* this);

static void _packet_to_csv_proxy(Sink* this, gpointer item);
static void _binary_proxy(Sink* this, gpointer item);

Sink* make_sink(const gchar* string, guint item_size) {
  Sink* this = g_malloc0(sizeof(Sink));
  gchar **tokens = g_strsplit(string, ":", -1);
  this->type = common_assign_string_to_int(tokens[0], "file", "mkfifo", "unix_dgram_socket", NULL);
  this->type_in_string = g_ascii_strup(tokens[0], strlen(tokens[0]));
  this->format = common_assign_string_to_int(tokens[1], "binary", "packet2csv", NULL);
  this->format_in_string = g_ascii_strup(tokens[1], strlen(tokens[1]));
  fprintf(stdout, "Create Sink. Type: %s Format: %s\n", this->type_in_string, this->format_in_string);
  switch(this->format) {
    case WRITE_FORMAT_PACKET_CSV:
      this->input = make_pushport((PushCb)_packet_to_csv_proxy, this);
      break;
    default:
    case WRITE_FORMAT_BINARY:
      this->input = make_pushport((PushCb)_binary_proxy, this);
      break;
  }

  switch (this->type) {
    case SINK_TYPE_FILE:
      strcpy(this->path, tokens[2]);
      this->writer_process = _write_file;
      this->stop_process = make_process((ProcessCb)_close_file, this);
      if (3 < g_strv_length(tokens)) {
        strcpy(this->file_open_mode, tokens[3]);
      } else {
        strcpy(this->file_open_mode, "wb");
      }
      fprintf(stdout, "File open mode: %s\n", this->file_open_mode);
    break;
    case SINK_TYPE_MKFIFO:
      strcpy(this->path, tokens[2]);
      this->writer_process = _write_mkfifo;
      this->stop_process = make_process((ProcessCb)_close_socket, this);
      break;
    case SINK_TYPE_UNIX_DGRAM_SOCKET:
      strcpy(this->path, tokens[2]);
      this->writer_process = _sendto_unix_socket;
      this->stop_process = make_process((ProcessCb)_close_socket, this);
      break;
    default:
      fprintf(stderr, "No Type for source\n");
  }
  this->item_size = item_size;
  g_strfreev(tokens);
  return this;
}

const gchar* sink_get_type_in_string(Sink* this) {
  return this->type_in_string;
}

const gchar* sink_get_format_in_string(Sink* this) {
  return this->format_in_string;
}

const gchar* sink_get_path(Sink* this) {
  return this->path;
}

void sink_dtor(Sink* this) {
  process_dtor(this->stop_process);
  pushport_dtor(this->input);
  g_free(this->type_in_string);
  g_free(this);
}

void _write_file(Sink* this, gpointer item, gint item_size) {
  size_t write_result;
  if (!this->fp) {
    this->fp = fopen(this->path, this->file_open_mode);
    if (!this->fp) {
      fprintf(stderr, "Can not open file at sink: %s\n", this->path);
      return;
    }
  }

  write_result = fwrite(item, item_size, 1, this->fp);
  if (write_result < 0) {
    fprintf(stderr, "Error during write a file : %s\n", this->path);
  }
}

void _write_mkfifo(Sink* this, gpointer item, gint item_size) {
  size_t write_result;
  if (!g_file_test(this->path, G_FILE_ERROR_EXIST)) {
    mkfifo(this->path, 0666);
  }
  if (!this->socket) {
    this->socket = open(this->path, O_WRONLY | O_NONBLOCK);
  }
  write_result = write(this->socket, item, item_size);
  if (write_result <= 0) {
    fprintf(stderr, "Error during write an mkfifo : %s\n", this->path);
  }
}

void _sendto_unix_socket(Sink* this, gpointer item, gint item_size) {
  int sent_bytes;
  struct sockaddr_un remote;
  if (!this->socket) {
    this->socket = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (this->socket == -1) {
      g_print("SOCKET ERROR at opening client socket (%s)\n", this->path);
      return;
    }
  }

  remote.sun_family = AF_UNIX;
  strcpy(remote.sun_path, this->path);

  sent_bytes = sendto(this->socket, item, item_size, 0, (struct sockaddr *) &remote, sizeof(remote));
  if (sent_bytes < 0) {
    fprintf(stderr, "Errors occured during sending");
  }
}

void _packet_to_csv_proxy(Sink* this, gpointer item) {
  Packet* packet = item;
  gchar buffer[255];
  gint buffer_size = sprintf(buffer,"%lu,%hu,%u,%u,%d,%u,%d,%d,%d,%hu,%hu,%d\n",
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
  this->writer_process(this, buffer, buffer_size);
}

void _binary_proxy(Sink* this, gpointer item) {
  this->writer_process(this, item, this->item_size);
}

void _close_file(Sink* this) {
  fclose(this->fp);
  this->fp = NULL;
}

void _close_socket(Sink* this) {
  close(this->socket);
  this->socket = 0;
}
