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

static void _write_file(Sink* this, gpointer item);
static void _write_mkfifo(Sink* this, gpointer item);
static void _sendto_unix_socket(Sink* this, gpointer item);

static void _set_stop(Sink* this);

Sink* make_sink(const gchar* string, guint item_size) {
  Sink* this = g_malloc0(sizeof(Sink));
  gchar **tokens = g_strsplit(string, ":", -1);
  this->type = common_assign_string_to_int(tokens[0], "file", "mkfifo", "unix_dgram_socket", NULL);
  switch (this->type) {
    case SINK_TYPE_FILE:
      strcpy(this->path, tokens[1]);
      this->input = make_pushport((PushCb)_write_file, this);
      this->stop_process = make_process((ProcessCb)_set_stop, this);
    break;
    case SINK_TYPE_MKFIFO:
      strcpy(this->path, tokens[1]);
      this->input = make_pushport((PushCb)_write_mkfifo, this);
      this->stop_process = make_process((ProcessCb)_set_stop, this);
      break;
    case SINK_TYPE_UNIX_DGRAM_SOCKET:
      strcpy(this->path, tokens[1]);
      this->input = make_pushport((PushCb)_sendto_unix_socket, this);
      this->stop_process = make_process((ProcessCb)_set_stop, this);
      break;
    default:
      fprintf(stderr, "No Type for source\n");
  }
  this->item_size = item_size;
  g_strfreev(tokens);
  return this;
}

void sink_dtor(Sink* this) {
  process_dtor(this->stop_process);
  pushport_dtor(this->input);
  g_free(this);
}

void _write_file(Sink* this, gpointer item) {
  if (!this->fp) {
    this->fp = fopen(this->path, "r");
    size_t read_result;
    if (!this->fp) {
      fprintf(stderr, "Can not open file: %s\n", this->path);
      return;
    }
  }

  fwrite(item, this->item_size, 1, this->fp);

  if (this->stop) {
    fclose(this->fp);
    this->fp = NULL;
    this->stop = FALSE;
  }
}

void _write_mkfifo(Sink* this, gpointer item) {
  if (!this->socket) {
    this->socket = open(this->path, O_RDONLY);
  }

  write(this->socket, item, this->item_size);

  if (this->stop) {
    close(this->socket);
    this->socket = 0;
    this->stop = FALSE;
  }

}

void _sendto_unix_socket(Sink* this, gpointer item) {
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

  sendto(this->socket, item, this->item_size, 0, (struct sockaddr *) &remote, sizeof(remote));

  if (this->stop) {
    close(this->socket);
    this->socket = 0;
    this->stop = FALSE;
  }
}


void _set_stop(Sink* this) {
  this->stop = TRUE;
}
