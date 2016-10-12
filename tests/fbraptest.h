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

static int rtpbin_tx_rtcp_port  = 5013;
static int rtpbin_rx_rtcp_port  = 5015;

static int path_tx_rtp_port     = 5000;
static gchar *path_tx_ip        = NULL;
static gchar default_path_tx_ip[255];

static gchar *path_rx_ip        = NULL;
static gchar default_path_rx_ip[255];

static gchar *logsdir           = NULL;
static gchar default_logsdir[255];

static int logging              = 1;
static int rtcp_interval_type   = 2;
static int controlling_mode     = 2;
static int obsolation_th        = 0;
static int fec_interval         = 0;


static GOptionEntry entries[] =
{
    { "logsdir", 0, 0, G_OPTION_ARG_STRING, &logsdir, "Logsdir", NULL },
    { "video_width", 0, 0, G_OPTION_ARG_INT, &video_width, "video width", NULL },
    { "video_height", 0, 0, G_OPTION_ARG_INT, &video_height, "video width", NULL },
    { "path_rx_ip", 0, 0, G_OPTION_ARG_STRING, &path_rx_ip, "path_rx_ip", NULL },
    { "path_tx_ip", 0, 0, G_OPTION_ARG_STRING, &path_tx_ip, "path_tx_ip", NULL },
    { "fec_interval", 0, 0, G_OPTION_ARG_INT, &fec_interval, "fec_interval", NULL },
    { "obsolation_th", 0, 0, G_OPTION_ARG_INT, &obsolation_th, "obsolation_th", NULL },
    { "logging", 0, 0, G_OPTION_ARG_INT, &logging, "logging", NULL },
    { "rtcp_interval_type", 0, 0, G_OPTION_ARG_INT, &rtcp_interval_type, "rtcp_interval_type", NULL },
    { "controlling_mode", 0, 0, G_OPTION_ARG_INT, &controlling_mode, "controlling_mode", NULL },
    { "target_bitrate", 0, 0, G_OPTION_ARG_INT, &target_bitrate, "target_bitrate", NULL },
    { "path_tx_rtp_port", 0, 0, G_OPTION_ARG_INT, &path_tx_rtp_port, "path_tx_rtp_port", NULL },
    { "rtpbin_tx_rtcp_port", 0, 0, G_OPTION_ARG_INT, &rtpbin_tx_rtcp_port, "rtpbin_tx_rtcp_port", NULL },
    { "rtpbin_rx_rtcp_port", 0, 0, G_OPTION_ARG_INT, &rtpbin_rx_rtcp_port, "rtpbin_rx_rtcp_port", NULL },

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

  if(path_tx_rtp_port == 0){
    path_tx_rtp_port  = 5000;
  }


  if(rtpbin_tx_rtcp_port == 0){
      rtpbin_tx_rtcp_port  = 5013;
  }
  if(rtpbin_rx_rtcp_port == 0){
      rtpbin_rx_rtcp_port = 5015;
  }

  if(logsdir == NULL){
      sprintf(default_logsdir, "logs/");
      logsdir = default_logsdir;
    }

  if(path_tx_ip == NULL){
      sprintf(default_path_tx_ip, "10.0.0.6");
      path_tx_ip = default_path_tx_ip;
    }

//  _print_transfer_info();

}

#endif /* FBRAPTESTS_TEST_H_ */
