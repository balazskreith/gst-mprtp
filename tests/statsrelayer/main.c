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

#include "statsrelayer.h"
#include "source.h"

static void _print_help(void) {
  g_print("\n");
  g_print("Usage: ./statsrelayer SOURCE \n");
  g_print("Commands: \n");
  g_print("add GROUP      Add a group to the service for listening\n");
  g_print("rem GROUP_NAME Remove a group from the service\n");
  g_print("fls GROUP_NAME Flush a collected items from all participents inside of a group\n");
  g_print("ext            Remove and close all groups and exit\n");
  g_print("\n\n");
  g_print("Syntax:\n");
  g_print("GROUP_NAME aA-zZ0-9* - * means all\n");
  g_print("GROUP      NAME RELAYS\n");
  g_print("NAME       aA-zZ0-9\n");
  g_print("RELAYS     RELAY!...\n");
  g_print("RELAY      SOURCE|SINK\n");
  g_print("SOURCE     file|mkfifo|unix_dgram_socket:PATH\n");
  g_print("SINK       file|mkfifo|unix_dgram_socket:PATH\n");
  g_print("PATH       aA-zZ0-9/\n");
}

static void _execute(StatsRelayer* this, gchar* cmd);
static Source* source;

int main(int argc, char *argv[]){
  StatsRelayer* this;
  if (argc < 2) {
    _print_help();
    goto done;
  }

  this = make_statsrelayer();
  source = make_source(argv[1], 255);
  source->output = make_pushport((PushCb) _execute, this);

  process_call(source->start_process);
  statsrelayer_dtor(this);
done:
  return 0;
}

typedef enum {
  CMD_NOT_RECOGNIZED = -1,
  CMD_ADD_GROUP = 1,
  CMD_REM_GROUP = 2,
  CMD_FLUSH_GROUP = 3,
  CMD_EXIT = 4
}Command;

void _execute(StatsRelayer* this, gchar* cmd) {
  Command command = common_assign_string_to_int(cmd, "add", "rem", "fls", "ext", NULL);
  switch (command) {
    case CMD_ADD_GROUP:
      statsrelayer_add_group(this, cmd + 3);
      break;
    case CMD_REM_GROUP:
      statsrelayer_rem_group(this, cmd + 3);
      break;
    case CMD_FLUSH_GROUP:
      statsrelayer_flush_group(this, cmd + 3);
      break;
    case CMD_EXIT:
      statsrelayer_rem_group(this, "*");
      process_call(source->stop_process);
      break;
    case CMD_NOT_RECOGNIZED:
    default:
      fprintf(stderr, "Command %s not recognized\n", cmd);
    break;
  }
}
