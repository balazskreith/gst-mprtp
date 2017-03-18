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
  size_t group_num = 0,group_i,arg_i;
  guint32 bw_in_kbps;
  guint32 repeat_num,repeat_i;

  if(argc < 2){
    g_print("Usage: ./program result_path group_number [bw_in_kbps repeat_num]...\n");
    return 0;
  }
  fp        = fopen (argv[1],"w");
  group_num = atoi(argv[2]);

  for(group_i = 0; group_i < group_num; ++group_i){
    arg_i = 3 + group_i * 2;
    bw_in_kbps = atoi(argv[arg_i]);
    repeat_num = atoi(argv[arg_i + 1]);
    for(repeat_i = 0; repeat_i < repeat_num; ++repeat_i){
      fprintf(fp, "%u\n", bw_in_kbps);
    }
  }

  fclose(fp);
  g_print("Bandwidth csv is made for %s\n", argv[1]);
  return 0;
}
