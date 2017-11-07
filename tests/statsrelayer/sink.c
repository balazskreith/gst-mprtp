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

static void _write_file(Sink* this, WriteItem* write_item);
static void _write_mkfifo(Sink* this, WriteItem* write_item);
static void _sendto_unix_socket(Sink* this, WriteItem* write_item);

static void _reset_file(Sink* this);
static void _reset_socket(Sink* this);

static void _close_file(Sink* this);
static void _close_socket(Sink* this);

Sink* make_sink(const gchar* string) {
  Sink* this = g_malloc0(sizeof(Sink));
  gchar **tokens = g_strsplit(string, ":", -1);
  this->type = common_assign_string_to_int(tokens[0], "file", "mkfifo", "unix_dgram_socket", NULL);
  this->type_in_string = g_ascii_strup(tokens[0], strlen(tokens[0]));
  fprintf(stdout, "Create Sink. Type: %s \n", this->type_in_string);

  switch (this->type) {
    case SINK_TYPE_FILE:
      strcpy(this->path, tokens[1]);
      this->stop_process = make_process((ProcessCb)_close_file, this);
      this->input = make_pushport((PushCb)_write_file, this);
      if (2 < g_strv_length(tokens)) {
        strcpy(this->file_open_mode, tokens[3]);
      } else {
        strcpy(this->file_open_mode, "wb");
      }
      fprintf(stdout, "File open mode: %s\n", this->file_open_mode);
      this->reset_process = make_process((ProcessCb)_reset_file, this);
    break;
    case SINK_TYPE_MKFIFO:
      strcpy(this->path, tokens[1]);
      this->stop_process = make_process((ProcessCb)_write_mkfifo, this);
      this->input = make_pushport((PushCb)_write_mkfifo, this);
      this->reset_process = make_process((ProcessCb)_reset_socket, this);
      break;
    case SINK_TYPE_UNIX_DGRAM_SOCKET:
      strcpy(this->path, tokens[1]);
      this->stop_process = make_process((ProcessCb)_close_socket, this);
      this->input = make_pushport((PushCb)_sendto_unix_socket, this);
      this->reset_process = make_process((ProcessCb)_reset_socket, this);
      break;
    default:
      fprintf(stderr, "No Type for source\n");
  }
  g_strfreev(tokens);
  return this;
}

void sink_sprintf(Sink* this, gchar* string) {
  sprintf(string, "Sink type: %s, path: %s number of sent items: %d, amount of bytes: %d\n",
      this->type_in_string, this->path, this->packets_num, this->bytes_num);
}

void sink_reset_metrics(Sink* this) {
  this->packets_num = 0;
  this->bytes_num = 0;
}

const gchar* sink_get_type_in_string(Sink* this) {
  return this->type_in_string;
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

static void _refresh_metrics(Sink* this, WriteItem* item) {
  ++this->packets_num;
  this->bytes_num += item->length;
}

void _write_file(Sink* this, WriteItem* write_item) {
  size_t write_result;
  if (!this->fp) {
    this->fp = fopen(this->path, this->file_open_mode);
    if (!this->fp) {
      fprintf(stderr, "Can not open file at sink: %s\n", this->path);
      return;
    }
  }

  write_result = fwrite(write_item->subject, write_item->length, 1, this->fp);
  if (write_result < 0) {
    fprintf(stderr, "Error during write a file : %s\n", this->path);
  }
  _refresh_metrics(this, write_item);
}

void _write_mkfifo(Sink* this, WriteItem* write_item) {
  size_t write_result;
  if (!g_file_test(this->path, G_FILE_ERROR_EXIST)) {
    mkfifo(this->path, 0666);
  }
  if (!this->socket) {
    this->socket = open(this->path, O_WRONLY | O_NONBLOCK);
  }
  write_result = write(this->socket, write_item->subject, write_item->length);
  if (write_result <= 0) {
    fprintf(stderr, "Error during write an mkfifo : %s\n", this->path);
  }
  _refresh_metrics(this, write_item);
}

void _sendto_unix_socket(Sink* this, WriteItem* write_item) {
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

  sent_bytes = sendto(this->socket, write_item->subject, write_item->length, 0, (struct sockaddr *) &remote, sizeof(remote));
  if (sent_bytes < 0) {
    fprintf(stderr, "Errors occured during sending");
  }
  _refresh_metrics(this, write_item);
}

static void _reset_metrics(Sink* this) {
  this->bytes_num = 0;
  this->packets_num = 0;
}

void _reset_file(Sink* this) {
  fprintf(stdout, "Reset sink file\n");
  _reset_metrics(this);
  _close_file(this);
}

void _reset_socket(Sink* this) {
  fprintf(stdout, "Reset sink socket\n");
  _reset_metrics(this);
  _close_socket(this);
}

void _close_file(Sink* this) {
  if (!this->fp) {
    return;
  }
  fclose(this->fp);
  this->fp = NULL;
}

void _close_socket(Sink* this) {
  if (!this->socket) {
    return;
  }
  close(this->socket);
  this->socket = 0;
}
