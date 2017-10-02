/************************************************************/
/* This is a datagram socket server sample program for UNIX */
/* domain sockets. This program creates a socket and        */
/* receives data from a client.                             */
/************************************************************/

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

//#define MAX_THREADS_READERS_NUM 32
//static volatile gint listenersnum = 0;
//static volatile gint listeners_started = 0;
//GThread* listeners[MAX_THREADS_READERS_NUM];

static gchar output_path[256];

typedef struct {
  GSList* processes;
  gint32 started;
  gint32 listeners_num;
}ReaderGroup;

typedef struct{
  GSList* reader_groups;
  gchar input_commands[1024];
}StatListener;


typedef struct _RTPStatPacket
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
}RTPStatPacket;

typedef struct {
  gboolean init;
  int server_socket;
  struct sockaddr_un server_sockaddr;
  gchar sock_path[256];
  gchar output_path[256];
  GQueue* packets;
  volatile gboolean end;
  gboolean started;
  ReaderGroup* group;
}SocketReader;


static SocketReader* _make_socket_reader(const gchar* pipe_name) {
  gint32 len;
  gint er;
  SocketReader* this = g_malloc0(sizeof(SocketReader));
  gchar sock_path[1024];
  memset(&this->server_sockaddr, 0, sizeof(struct sockaddr_un));

//  g_print("|%c|", sock_path + strlen(sock_path) - 1 );
  if (*(pipe_name + strlen(pipe_name) - 1) == '\n') {
    g_print("last character is newline.");
    memcpy(sock_path, pipe_name, strlen(pipe_name) - 1);
  } else {
    strcpy(sock_path, pipe_name);
  }
  sprintf(this->sock_path, "/tmp/%s\n", sock_path);
  sprintf(this->output_path, "%s/%s", output_path, sock_path);
  this->packets = g_queue_new();
  /****************************************/
  /* Create a UNIX domain datagram socket */
  /****************************************/
  this->server_socket = socket(AF_UNIX, SOCK_DGRAM, 0);
  if (this->server_socket == -1){
      g_printf("SOCKET ERROR at server sock");
      exit(1);
  }

  /***************************************/
  /* Set up the UNIX sockaddr structure  */
  /* by using AF_UNIX for the family and */
  /* giving it a filepath to bind to.    */
  /*                                     */
  /* Unlink the file so the bind will    */
  /* succeed, then bind to that file.    */
  /***************************************/
  this->server_sockaddr.sun_family = AF_UNIX;
  strcpy(this->server_sockaddr.sun_path, this->sock_path);
  len = sizeof(this->server_sockaddr);
  unlink(this->sock_path);
  if ((er = bind(this->server_socket, (struct sockaddr *) &this->server_sockaddr, len)) == -1){
      g_print("BIND ERROR at bind server sock (%s)", this->server_sockaddr.sun_path );
      perror("socket error");
      close(this->server_socket);
      exit(1);
  }

  int timeout = 100;
  setsockopt(this->server_socket, SOL_SOCKET, SO_RCVTIMEO,(char*)&timeout,sizeof(timeout));
  g_print("Unix socket server listening for %s\n", this->sock_path);
  return this;
}

static void _socket_reader_listen(SocketReader* this) {
    struct sockaddr_un peer_sock;
    int len;
    int bytes_rec;
    /****************************************/
    /* Read data on the server from clients */
    /* and print the data that was read.    */
    /****************************************/
//    printf("waiting to recvfrom...\n");
    while(1) {
      RTPStatPacket* packet = g_malloc0(sizeof(RTPStatPacket));
      if (this->end) {
        g_print("end\n");
        break;
      }
      bytes_rec = recvfrom(this->server_socket, packet, sizeof(RTPStatPacket), 0, (struct sockaddr *) &peer_sock, &len);
      if (bytes_rec < 0) {
        continue;
      }else if (!this->started) {
        ++this->group->started;
        this->started = TRUE;
      }

      if (this->group->started < this->group->listeners_num) {
        continue;
      }
      g_queue_push_tail(this->packets, packet);
    }
    g_print("exit from thread %s\n", this->sock_path);
}

static void _socket_reader_flush(SocketReader* this) {
  gchar path[255];
  FILE* fp;

  fp = fopen(this->output_path, "w");
  while(!g_queue_is_empty(this->packets)){
    RTPStatPacket* packet = g_queue_pop_head(this->packets);
    fprintf(fp, "%lu,%hu,%u,%u,%d,%u,%d,%d,%d,%hu,%hu,%d\n",
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
  }
  close(fp);
}
static void _command_done(StatListener* this) {
  GSList *it,*ij;
  for (it = this->reader_groups; it; it = it->next) {
    ReaderGroup* group = it->data;
    GSList* ij;
    for (ij = group->processes; ij; ij = ij->next) {
      GThread* listener = ij->data;
      SocketReader *socket_reader = listener->data;
      g_print("Giving order to end %s\n", socket_reader->sock_path);
      socket_reader->end = TRUE;
      _socket_reader_flush(socket_reader);
      close(socket_reader->server_socket);
    }
  }
}

static void _validate(StatListener* this, const gchar* fifo) {
  mkfifo(fifo, 0666);
  int fd = open(fifo, O_WRONLY);
  // TODO: do the validation process
//  g_print("sending validation in %s\n", fifo);
  write(fd, "1", 1);
  close(fd);
}

static void _receive_commands(StatListener* this)
{
  int fd;
  char buf[1024];
  gchar **tokens;

//  unlink(this->input_commands);
again:
  mkfifo(this->input_commands, 0666);
  fd = open(this->input_commands, O_RDONLY);
  memset(buf, 0, 1024);
  read(fd, buf, 1024);
  if (*(buf + strlen(buf) - 1) == '\n') {
    g_print("last character is newline in command.");
    *(buf + strlen(buf) - 1) = '\0';
  }
  printf("Received Command: |%s|\n", buf);
  tokens = g_strsplit(buf, " ", -1);
  if (g_strv_length(tokens) < 1) {
    // do nothing
    g_print("do nothing\n");
  } else if (strcmp(tokens[0], "DONE") == 0) {
    _command_done(this);
    g_strfreev(tokens);
    close(fd);
    goto done;
  } else if (strcmp(tokens[0], "VALIDATE") == 0) {
    _validate(this, tokens[1]);
  } else if (strcmp(tokens[0], "ADD") == 0) {
    ReaderGroup* reader_group = g_malloc0(sizeof(ReaderGroup));
    gint i;
    reader_group->listeners_num = g_strv_length(tokens) - 1;
    for (i = 0; i < reader_group->listeners_num; ++i) {
      gchar *sock_path = tokens[i+1];
      SocketReader* socket_reader = _make_socket_reader(sock_path);
      GThread* process;
      socket_reader->group = reader_group;
      process = g_thread_new(sock_path, _socket_reader_listen, socket_reader);
      reader_group->processes = g_slist_prepend(reader_group->processes, process);
    }
    this->reader_groups = g_slist_prepend(this->reader_groups, reader_group);
  }
  g_strfreev(tokens);
  close(fd);
  unlink(this->input_commands);
  goto again;
done:
  return;
}

#include "ipctster.c"

int main(int argc, char *argv[]){
  gint i;
  StatListener* this = g_malloc0(sizeof(StatListener));

  test_mkfifo();
  test_socket();

  if (argc < 2) {
    g_print("program output_path input_command_mkfifo\n");
    return 0;
  }

  strcpy(output_path, argv[1]);
  strcpy(this->input_commands, argv[2]);
  g_print("Statlistener receiving commands in %s\n",
      this->input_commands);
  _receive_commands(this);
  g_free(this);

  return 0;
}
