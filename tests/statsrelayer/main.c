/************************************************************/
/* This is a datagram socket server sample program for UNIX */
/* domain sockets. This program creates a socket and        */
/* receives data from a client.                             */
/************************************************************/
#include <stdlib.h>
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
#include <signal.h>

#include "statsrelayer.h"
#include "source.h"

static void _print_help(void) {
  g_print("\n");
  g_print("Usage: ./statsrelayer SOURCE \n");
  g_print("Commands: \n");
  g_print("add GROUP Add a group to the service for listening\n");
  g_print("rem NAME  Remove a group from the service (* - all)\n");
  g_print("fls NAME  Flush a collected items from all participents inside of a group (* - all)\n");
  g_print("ext       Remove and close all groups and exit\n");
  g_print("\n\n");
  g_print("Syntax:\n");
  g_print("NAME   aA-zZ0-9*\n");
  g_print("GROUP  NAME RELAYS [SIZE]\n");
  g_print("NAME   aA-zZ0-9\n");
  g_print("RELAYS RELAY|...\n");
  g_print("RELAY  SOURCE!MAPPER!SINK\n");
  g_print("MAPPER binary!packet2csv\n");
  g_print("SOURCE file|mkfifo|unix_dgram_socket:PATH\n");
  g_print("SINK   file|mkfifo|unix_dgram_socket:PATH\n");
  g_print("PATH   aA-zZ0-9/\n");
  g_print("SIZE   0-9...\n");
}

static void _execute(StatsRelayer* this, gchar* string);
static void _execute_default(StatsRelayer* this);
static Source* source;
static StatsRelayer* this;

static void term(int signum)
{
  gchar command[255];
  g_print("term signal received\n");
  sprintf(command, "fls *");
  _execute(this, command);
  memset(command, 0, 255);
  sprintf(command, "ext");
  _execute(this, command);
  memset(command, 0, 255);
  exit(0);
}

int main(int argc, char *argv[]) {
  struct sigaction action;
  gchar* src_params;

  if (argc < 2) {
    _print_help();
    goto done;
  }

  this = make_statsrelayer();
  memset(&action, 0, sizeof(action));
  action.sa_handler = term;
  sigaction(SIGTERM, &action, NULL);
  if (strcmp(argv[1], "-d") == 0) {
    _execute_default(this);
//    unlink("/tmp/statsrelayer.cmd.in");
//    mkfifo("/tmp/statsrelayer.cmd.in", 0666);
//    src_params = g_malloc0(255);
//    strcpy(src_params, "mkfifo:/tmp/statsrelayer.cmd.in");
    while(1) {
      g_usleep(100000);
    }

  } else {
    src_params = argv[1];
  }

  source = make_source(src_params, 1024);
  source->output = make_pushport((PushCb) _execute, this);
  process_call(source->start_process);
  process_call(source->stop_process);
done:
  statsrelayer_dtor(this);
  fprintf(stdout, "StatRelayer is done\n");
  return 0;
}

/*
for FLOWNUM in 1 2 3
do
  for ACTOR in "snd_packets" "rcv_packets" "ply_packets"
  do
    TARGET=$ACTOR"_"$FLOWNUM".csv"
    SRC="$FROMDIR/$TARGET"
    SNK="$TODIR/$TARGET"
    unlink $SRC
    unlink $SNK
    echo "add $TARGET source:mkfifo:$SRC!buffer!mapper:packet2csv!sink:file:$SNK" >> $CMDIN
    sleep 2
  done
done
 * */

void _execute_default(StatsRelayer* this)
{
  const gchar* from_dir = "/tmp";
  const gchar* to_dir = "temp/..";
  const gchar* sources[] = {"snd", "rcv", "ply"};
  gchar command[255];
  gint i,j;
  g_print("_execute_default\n");
  sprintf(command, "lst *");
  _execute(this, command);
  for (i = 0; i < 10; ++i) {
    for (j = 0; j < 3; ++j) {
      gchar pipe_name[255], src[255], snk[255];
      sprintf(pipe_name, "%s_packets_%d.csv", sources[j], i + 1);
      sprintf(src, "%s/%s", from_dir, pipe_name);
      sprintf(snk, "%s/%s", to_dir, pipe_name);
      unlink(src);
      unlink(snk);
      sprintf(command, "add %s source:mkfifo:%s!buffer!mapper:packet2csv!sink:file:%s", pipe_name, src, snk);
      _execute(this, command);
      memset(command, 0, 255);
    }

  }
}

typedef enum {
  CMD_NOT_RECOGNIZED = -1,
  CMD_ADD_PIPELINE = 1,
  CMD_REM_PIPELINE = 2,
  CMD_LIST = 3,
  CMD_FLUSH_PIPELINE = 4,
  CMD_RESET_PIPELINE = 5,
  CMD_SETUP_DEFAULTLY = 5,
  CMD_EXIT = 6
}Command;

void _execute(StatsRelayer* this, gchar* strings) {
  gchar **tokens = g_strsplit(strings, ";", -1);
  gchar* string;
  gint i;
  gchar cmd[4];
  Command command;
  cmd[3] = '\0';
  for (i = 0; i < g_strv_length(tokens); ++i){
    string = tokens[i];
    if (*(string + strlen(string) - 1) == '\n') {
      *(string + strlen(string) - 1) = '\0';
    }
    if (*string == '\n') {
      ++string;
    }
    if (*string == '\0'){
      continue;
    }
    fprintf(stdout, "-------------------------------\n");
    memset(cmd, 0, 3);
    memcpy(cmd, string, 3);
    fprintf(stdout, "Command received: |%s|->|%s|\n", string, cmd);
    command = common_assign_string_to_int(cmd, "add", "rem", "lst", "fls", "rst", "ext", NULL);
    switch (command) {
      case CMD_ADD_PIPELINE:
        statsrelayer_add_pipeline(this, string + 4);
        break;
      case CMD_REM_PIPELINE:
        statsrelayer_rem_pipeline(this, string + 4);
        break;
      case CMD_LIST:
        statsrelayer_list_pipeline(this, string + 4);
        break;
      case CMD_FLUSH_PIPELINE:
        statsrelayer_flush_pipeline(this, string + 4);
        break;
      case CMD_RESET_PIPELINE:
        statsrelayer_reset_pipeline(this, string + 4);
        break;
      case CMD_EXIT:
        statsrelayer_rem_pipeline(this, "*");
        process_call(source->stop_process);
        break;
      case CMD_NOT_RECOGNIZED:
      default:
        fprintf(stderr, "Command |%s| not recognized\n", cmd);
      break;
    }
  }
}
