#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>

#define current_unix_time_in_us g_get_real_time ()
#define current_unix_time_in_ms (current_unix_time_in_us / 1000L)
#define current_unix_time_in_s  (current_unix_time_in_ms / 1000L)
#define epoch_now_in_ns ((guint64)((current_unix_time_in_us + 2208988800000000LL) * 1000))
#define get_ntp_from_epoch_ns(epoch_in_ns) gst_util_uint64_scale (epoch_in_ns, (1LL << 32), GST_SECOND)
#define get_epoch_time_from_ntp_in_ns(ntp_time) gst_util_uint64_scale (ntp_time, GST_SECOND, (1LL << 32))
#define NTP_NOW get_ntp_from_epoch_ns(epoch_now_in_ns)
#define _now(this) gst_clock_get_time (this->sysclock)

int main (int argc, char **argv)
{
  FILE *fp;
  char * line = NULL;
  size_t len = 0;
  ssize_t read;
  guint32 i=0, read_num, wait_in_seconds;

  if(argc < 2){
    g_print("Usage: ./program commands_path\n");
    return 0;
  }
  fp = fopen (argv[1],"r");

  g_print("Bash Command Executor (bcex) Executing commands in %s\n",
      argv[1]
  );
  do
  {
    read = getline(&line, &len, fp);
    if(read == -1){
      break;
    }
    if(line[0] == '#'){
      //Comment omitted
      continue;
    }

    sscanf(line, "%d %d", &read_num, &wait_in_seconds);
    for(i=0; i<read_num; ++i){
      read = getline(&line, &len, fp);
      if(read == -1){
        break;
      }
      if(line[0] == '#'){
        //Comment omitted
        --i; //comments inside blocks are not counted
        continue;
      }
      g_print("Executing command: %s\n", line);
      system(line);
    }

    if(read == -1){
      break;
    }
    g_print("Halt for %d seconds\n", wait_in_seconds);
    g_usleep(wait_in_seconds * 1000 * 1000);
  }while(1);

  fclose(fp);
  return 0;
}
