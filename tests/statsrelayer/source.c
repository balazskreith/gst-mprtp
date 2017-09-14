#include "source.h"
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

static void _read_file(Source* this);
static void _read_mkfifo(Source* this);
static void _rcvfrom_unix_socket(Source* this);

static void _set_stop(Source* this);

Source* make_source(const gchar* string, guint item_size) {
  Source* this = g_malloc0(sizeof(Source));
  gchar **tokens = g_strsplit(string, ":", -1);
  this->type = common_assign_string_to_int(tokens[0], "file", "mkfifo", "unix_dgram_socket", NULL);
  switch (this->type) {
    case SOURCE_TYPE_FILE:
      strcpy(this->path, tokens[1]);
      this->start_process = make_process((ProcessCb)_read_file, this);
      this->stop_process = make_process((ProcessCb)_set_stop, this);
    break;
    case SOURCE_TYPE_MKFIFO:
      strcpy(this->path, tokens[1]);
      this->start_process = make_process((ProcessCb)_read_mkfifo, this);
      this->stop_process = make_process((ProcessCb)_set_stop, this);
      break;
    case SOURCE_TYPE_UNIX_DGRAM_SOCKET:
      strcpy(this->path, tokens[1]);
      this->start_process = make_process((ProcessCb)_rcvfrom_unix_socket, this);
      this->stop_process = make_process((ProcessCb)_set_stop, this);
      break;
    default:
      fprintf(stderr, "No Type for source\n");
  }
  this->item_size = item_size;
  this->databed = g_malloc0(this->item_size);
  g_strfreev(tokens);
  return this;
}

void source_dtor(Source* this) {
  process_dtor(this->stop_process);
  process_dtor(this->start_process);
  g_free(this);
}

void _read_file(Source* this) {
  FILE* fp = fopen(this->path, "r");
  size_t read_result;
  if (!fp) {
    fprintf(stderr, "Can not open file: %s\n", this->path);
    return;
  }
  while (!feof(fp) && !this->stop) {
    read_result = fread(this->databed, this->item_size, 1, fp);
    if (read_result < 0) {
      fprintf(stderr, "Problem occured during _read_file %s\n", this->path);
      continue;
    }
    pushport_send(this->output, this->databed);
  }
  fclose(fp);
}

void _read_mkfifo(Source* this) {
  this->socket = open(this->path, O_WRONLY);
  while(!this->stop) {
    if (read(this->socket, this->databed, this->item_size) < 0) {
      fprintf(stderr, "Problem occured during _read_mkfifo %s\n", this->path);
    }
    pushport_send(this->output, this->databed);
  }
  close(this->socket);
}

void _rcvfrom_unix_socket(Source* this) {
  struct sockaddr_un server_sockaddr;
  struct sockaddr_un peer_sock;
  socklen_t len;
  this->socket = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (this->socket == -1){
    fprintf(stderr,"SOCKET ERROR at server sock");
    exit(1);
  }

  server_sockaddr.sun_family = AF_UNIX;
  strcpy(server_sockaddr.sun_path, this->path);
  len = sizeof(server_sockaddr);
  unlink(this->path);
  if ((bind(this->socket, (struct sockaddr *) &server_sockaddr, len)) == -1){
      fprintf(stderr,"BIND ERROR at bind server sock (%s)", server_sockaddr.sun_path );
      perror("socket error");
      close(this->socket);
      return;
  }

  while(!this->stop) {
    recvfrom(this->socket, this->databed, (size_t)this->item_size, 0, (struct sockaddr *) &peer_sock, &len);
    pushport_send(this->output, this->databed);
  }

  close(this->socket);
}


void _set_stop(Source* this) {
  this->stop = TRUE;
}
