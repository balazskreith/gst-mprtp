#ifndef FBRAPTESTS_TEST_H_
#define FBRAPTESTS_TEST_H_

#define MPRTP_PLUGIN_MAX_SUBFLOW_NUM 32
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct _MPRTPSubflowUtilizationSignalData{
  guint                      controlling_mode;
  gint8                      path_state;
  gint8                      path_flags_value;
  gint32                     target_bitrate;

  gdouble                    RTT;
  guint32                    jitter;
  gdouble                    lost_rate;
  guint16                    HSSN;
  guint16                    cycle_num;
  guint32                    cum_packet_lost;
  GstClockTime               owd_median;
  GstClockTime               owd_min;
  GstClockTime               owd_max;

}MPRTPSubflowUtilizationSignalData;

typedef struct _MPRTPPluginSignalData{
  MPRTPSubflowUtilizationSignalData subflow[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
  gint32                            target_media_rate;
}MPRTPPluginSignalData;

#include <string.h>

static int info                 = 0;
static gint32 framerate         = 25;
static gint32 video_height      = 288;
static gint32 video_width       = 352;
static gint32 target_bitrate    = 500000;

static int snd_rtcp_port   = 5013;
static int rcv_rtcp_port   = 5015;

static int rcv_rtp_port    = 5000;
static int rcv_mprtcp_port = 5001;
static int snd_mprtcp_port = 5003;
static gchar *snd_ip       = NULL;
static gchar *rcv_ip       = NULL;
static gchar default_snd_ip[255];
static gchar default_rcv_ip[255];

static gchar *src_filepath = NULL;
static gchar default_src_filepath[255];

static gchar *logs_path         = NULL;
static gchar default_logsdir[255];

static int logging              = 1;
static int rtcp_interval_type   = 2;
static int controlling_mode     = 2;
static int obsolation_th        = 0;
static int fec_interval         = 0;


static GOptionEntry entries[] =
{
    { "logs_path", 0, 0, G_OPTION_ARG_STRING, &logs_path, "logs_path", NULL },
    { "video_width", 0, 0, G_OPTION_ARG_INT, &video_width, "video width", NULL },
    { "video_height", 0, 0, G_OPTION_ARG_INT, &video_height, "video width", NULL },
    { "snd_ip", 0, 0, G_OPTION_ARG_STRING, &snd_ip, "Sender IP Address", NULL },
    { "rcv_ip", 0, 0, G_OPTION_ARG_STRING, &rcv_ip, "Receiver IP Address", NULL },
    { "src_filepath", 0, 0, G_OPTION_ARG_STRING, &src_filepath, "Source filepath", NULL },
    { "snd_rtcp_port", 0, 0, G_OPTION_ARG_INT, &snd_rtcp_port, "snd_rtcp_port", NULL },
    { "rcv_rtcp_port", 0, 0, G_OPTION_ARG_INT, &rcv_rtcp_port, "rcv_rtcp_port", NULL },
    { "rcv_mprtcp_port", 0, 0, G_OPTION_ARG_INT, &rcv_mprtcp_port, "rcv_mprtcp_port", NULL },
    { "snd_mprtcp_port", 0, 0, G_OPTION_ARG_INT, &snd_mprtcp_port, "snd_mprtcp_port", NULL },
    { "fec_interval", 0, 0, G_OPTION_ARG_INT, &fec_interval, "fec_interval", NULL },
    { "obsolation_th", 0, 0, G_OPTION_ARG_INT, &obsolation_th, "obsolation_th", NULL },
    { "logging", 0, 0, G_OPTION_ARG_INT, &logging, "logging", NULL },
    { "rtcp_interval_type", 0, 0, G_OPTION_ARG_INT, &rtcp_interval_type, "rtcp_interval_type", NULL },
    { "controlling_mode", 0, 0, G_OPTION_ARG_INT, &controlling_mode, "controlling_mode", NULL },
    { "target_bitrate", 0, 0, G_OPTION_ARG_INT, &target_bitrate, "target_bitrate", NULL },
    { "path_tx_rtp_port", 0, 0, G_OPTION_ARG_INT, &rcv_rtp_port, "path_tx_rtp_port", NULL },

    { "info", 0, 0, G_OPTION_ARG_NONE, &info, "Info", NULL },
  { NULL }
};


#define println(str) g_print(str"\n")

static void _print_info(void)
{
  println("[sender|receiver] [--option=value]");
}

static void _setup_test_params(void)
{

  if(rcv_rtp_port == 0){
    rcv_rtp_port  = 5000;
  }


  if(snd_rtcp_port == 0){
      snd_rtcp_port  = 5013;
  }
  if(rcv_rtcp_port == 0){
      rcv_rtcp_port = 5015;
  }
  if(snd_mprtcp_port == 0){
    snd_mprtcp_port = 5003;
  }
  if(rcv_mprtcp_port == 0){
    rcv_mprtcp_port = 5001;
  }

  if(logs_path == NULL){
      sprintf(default_logsdir, "logs/");
      logs_path = default_logsdir;
    }

  if(rcv_ip == NULL){
      sprintf(default_rcv_ip, "10.0.0.6");
      rcv_ip = default_rcv_ip;
    }

  if(snd_ip == NULL){
      sprintf(default_snd_ip, "10.0.0.1");
      snd_ip = default_snd_ip;
    }

  if(src_filepath == NULL){
      sprintf(default_src_filepath, "foreman_cif.yuv");
      src_filepath = default_src_filepath;
    }

}






#endif /* FBRAPTESTS_TEST_H_ */
