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
static void _read_stdin(Source* this);

static void _set_stop(Source* this);

Source* make_source(const gchar* string, guint item_size) {
  Source* this = g_malloc0(sizeof(Source));
  gchar **tokens = g_strsplit(string, ":", -1);
  this->type = common_assign_string_to_int(tokens[0], "file", "mkfifo", "unix_dgram_socket", "stdin", NULL);
  this->type_in_string = g_ascii_strup(tokens[0], strlen(tokens[0]));
  fprintf(stdout, "Create Source. Type: %s\n", tokens[0]);
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
    case SOURCE_TYPE_STDIN:
      this->start_process = make_process((ProcessCb)_read_stdin, this);
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

const gchar* source_get_type_in_string(Source* this) {
  return this->type_in_string;
}

const gchar* source_get_path(Source* this) {
  return this->path;
}

void source_dtor(Source* this) {
  process_dtor(this->stop_process);
  process_dtor(this->start_process);
  g_free(this->type_in_string);
  g_free(this);
}

void _read_file(Source* this) {
  FILE* fp = fopen(this->path, "rb");
  size_t read_result;
  if (!fp) {
    perror("Source error");
    fprintf(stderr, "Can not open file at source: %s\n", this->path);
    return;
  }
  while (!feof(fp) && !this->stop) {
    memset(this->databed, 0, this->item_size);
    read_result = fread(this->databed, this->item_size, 1, fp);
    if (read_result < 0) {
      perror("file reading error");
      fprintf(stderr, "Problem occured during _read_file %s\n", this->path);
      continue;
    }
    pushport_send(this->output, this->databed);
  }
  fclose(fp);
}

void _read_mkfifo(Source* this) {
  ssize_t read_bytes;
  if (!g_file_test(this->path, G_FILE_ERROR_EXIST)) {
    mkfifo(this->path, 0666);
  }
  this->socket = open(this->path, O_RDONLY | O_NONBLOCK);
  while(!this->stop) {
    memset(this->databed, 0, this->item_size);
    read_bytes = read(this->socket, this->databed, this->item_size);
    if (read_bytes == 0) { // The mkfifo is not open for writing
      this->socket = open(this->path, O_RDONLY | O_NONBLOCK);
      g_usleep(100000);
      continue;
    } else if (read_bytes < 0) {
      perror("mkfifo");
      fprintf(stderr, "Problem occured during _read_mkfifo %s\n", this->path);
      process_call(this->stop_process);
      continue;
    }
    pushport_send(this->output, this->databed);
  }
  close(this->socket);
}

void _rcvfrom_unix_socket(Source* this) {
  struct sockaddr_un server_sockaddr;
  struct sockaddr_un peer_sock;
  socklen_t len;
  struct timeval timeout;
  int read_bytes;
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
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;
  if (setsockopt(this->socket, SOL_SOCKET, SO_RCVTIMEO,(char*)&timeout, sizeof(struct timeval)) < 0) {
    perror("setsockopt");
  }

  while(!this->stop) {
    memset(this->databed, 0, this->item_size);
    read_bytes = recvfrom(this->socket, this->databed, (size_t)this->item_size, 0, (struct sockaddr *) &peer_sock, &len);
    if (read_bytes < 0) {
      continue;
    }
    pushport_send(this->output, this->databed);

  }

  close(this->socket);
}

void _read_stdin(Source* this) {
  // timeout structure passed into select
 struct timeval tv;
 gint read;
 // fd_set passed into select
 fd_set fds;
 // Set up the timeout.  here we can wait for 1 second
 tv.tv_sec = 0;
 tv.tv_usec = 100000; //100ms

 while(!this->stop) {
   memset(this->databed, 0, this->item_size);
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds); //STDIN_FILENO is 0
    select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
    read = FD_ISSET(STDIN_FILENO, &fds);
    if(read < 0) {
      perror("stdin error");
    } else if (0 == read) {
      continue;
    }
    if(!fgets (this->databed, this->item_size, stdin)) {
      continue;
    }
    pushport_send(this->output, this->databed);
 }
}


void _set_stop(Source* this) {
  fprintf(stdout, "Stop Source %s\n", this->path);
  this->stop = TRUE;
}
