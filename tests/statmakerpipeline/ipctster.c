#define _GNU_SOURCE
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

static const gchar* _mkfifo_path = "/tmp/mkfifo.test";
static const gchar* _socket_path = "/tmp/unixsocket.test";
static const int cyclenum = 1000000;
static const int SOCKET_STYLE = SOCK_DGRAM;
//static const int SOCKET_STYLE = SOCK_RAW;
#define BUF_SIZE 64


#define PROFILING(func) \
    { \
      clock_t start = clock(); \
      func \
      *elapsed = (clock() - start) / (double) CLOCKS_PER_SEC; \
    }

static void _mkfifo_writer(int* elapsed) {
  gint i;
  int fd;
  gchar buf[BUF_SIZE];
  fd = open(_mkfifo_path, O_WRONLY);
  sprintf(buf, "an item %d", 1);
  PROFILING(
    for (i=0; i < cyclenum; ++i) {
//      g_usleep(1000000);
//      g_print("item %d\n", i);
      write(fd, buf, BUF_SIZE);
    }
  );
  sprintf(buf, "DONE");
  write(fd, buf, BUF_SIZE);
  close(fd);
}

static void _mkfifo_reader(int* elapsed) {
  gint i;
  int fd;
  gchar buf[255];
  fd = open(_mkfifo_path, O_RDONLY);
  PROFILING(
    while(1) {
      read(fd, buf, BUF_SIZE);
      // if the reader is slower
//      g_usleep(10);
      g_print("%s\n", buf);
      if (strcmp(buf, "DONE") == 0)
        break;
    }
  );
  close(fd);
}

void test_mkfifo(void) {
  GThread *reader;
  GThread *writer;
  int writer_elapsed = 0, reader_elapsed = 0;

  mkfifo(_mkfifo_path, 0666);

  g_print("test_mkfifo: mkfifo created\n");

  reader = g_thread_new("_mkfifo_reader", _mkfifo_reader, &reader_elapsed);
  writer = g_thread_new("_mkfifo_writer", _mkfifo_writer, &writer_elapsed);
  g_thread_join(writer);
  g_thread_join(reader);

  g_print("test_mkfifo elapsed time: writer: %ds (avg: %fs) | reader: %ds (avg: %fs)\n",
      writer_elapsed, (gdouble)writer_elapsed / (gdouble)cyclenum,
      reader_elapsed, (gdouble)reader_elapsed / (gdouble)cyclenum);
}

static void _socket_reader(int* elapsed) {
  struct sockaddr_un server_sockaddr;
  struct sockaddr_un peer_sock;
  gchar buf[BUF_SIZE];
  int len;
  /****************************************/
  /* Create a UNIX domain datagram socket */
  /****************************************/
  int socketfd = socket(AF_UNIX, SOCKET_STYLE, 0);
  if (socketfd == -1){
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
  server_sockaddr.sun_family = AF_UNIX;
  strcpy(server_sockaddr.sun_path, _socket_path);
  len = sizeof(server_sockaddr);
  unlink(_socket_path);
  if ((bind(socketfd, (struct sockaddr *) &server_sockaddr, len)) == -1){
      g_print("BIND ERROR at bind server sock (%s)", server_sockaddr.sun_path );
      perror("socket error");
      close(socketfd);
      exit(1);
  }

  PROFILING(
    while(1) {
      // if the reader is slower
//      g_usleep(10);
      recvfrom(socketfd, buf, BUF_SIZE, 0, (struct sockaddr *) &peer_sock, &len);
      if (strcmp(buf, "DONE") == 0)
        break;
    }
  );

  close(socketfd);
}

static void _socket_writer(int* elapsed) {
  struct sockaddr_un remote;
  gchar buf[BUF_SIZE];
  gint i;
  int socketfd = socket(AF_UNIX, SOCKET_STYLE, 0);
  if (socketfd == -1) {
      g_print("SOCKET ERROR at opening client socket (%s)\n", _socket_path);
      return;
  }
  /***************************************/
  /* Set up the UNIX sockaddr structure  */
  /* by using AF_UNIX for the family and */
  /* giving it a filepath to send to.    */
  /***************************************/
  remote.sun_family = AF_UNIX;
  strcpy(remote.sun_path, _socket_path);

  /***************************************/
  /* Copy the data to be sent to the     */
  /* buffer and send it to the server.   */
  /***************************************/
  sprintf(buf, "an item");
  PROFILING(
    for (i=0; i < cyclenum; ++i) {
      sendto(socketfd, buf, BUF_SIZE, 0, (struct sockaddr *) &remote, sizeof(remote));
    }
  );
  sprintf(buf, "DONE");
  sendto(socketfd, buf, BUF_SIZE, 0, (struct sockaddr *) &remote, sizeof(remote));

  close(socketfd);
}

void test_socket(void) {
  GThread *reader;
  GThread *writer;
  int writer_elapsed = 0, reader_elapsed = 0;
  unlink(_socket_path);
  reader = g_thread_new("_socket_reader", _socket_reader, &reader_elapsed);
  writer = g_thread_new("_socket_writer", _socket_writer, &writer_elapsed);
  g_thread_join(writer);
  g_thread_join(reader);

  g_print("test_socket elapsed time: writer: %ds (avg: %fs) | reader: %ds (avg: %fs)\n",
      writer_elapsed, (gdouble)writer_elapsed / (gdouble)cyclenum,
      reader_elapsed, (gdouble)reader_elapsed / (gdouble)cyclenum);
}
