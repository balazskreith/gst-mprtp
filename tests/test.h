/*
 * test.h
 *
 *  Created on: Jan 26, 2016
 *      Author: balazs
 */


/* ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * +     +              10.0.0.0/24                   +     +
 * +     +--------------------------------------------+     +
 * +     +          .1     sync         .2            +     +
 * +     +     .----o------------------>¤ :5000       +     +
 * +  P  +     | TX |     async                       +  P  +
 * +  E  +     '----o------------------>¤ :5001       +  E  +
 * +  E  +                                            +  E  +
 * +  R  +                 sync                       +  R  +
 * +     +    :5002 ¤<------------------o----.        +     +
 * +  1  +                async         | RX |        +  2  +
 * +     +    :5003 ¤<------------------o----'        +     +
 * +     +                                            +     +
 * +     +--------------------------------------------+     +
 * +     +              10.0.0.1/24                   +     +
 * +     +--------------------------------------------+     +
 * +     +          .1     sync         .2            +     +
 * +     +     .----o------------------>¤ :5004       +     +
 * +  P  +     | TX |     async                       +  P  +
 * +  E  +     '----o------------------>¤ :5005       +  E  +
 * +  E  +                                            +  E  +
 * +  R  +                 sync                       +  R  +
 * +     +    :5006 ¤<------------------o----.        +     +
 * +  1  +                async         | RX |        +  2  +
 * +     +    :5007 ¤<------------------o----'        +     +
 * +     +                                            +     +
 * +     +--------------------------------------------+     +
 * +     +              10.0.0.2/24                   +     +
 * +     +--------------------------------------------+     +
 * +     +          .1     sync         .2            +     +
 * +     +     .----o------------------>¤ :5008       +     +
 * +  P  +     | TX |     async                       +  P  +
 * +  E  +     '----o------------------>¤ :5009       +  E  +
 * +  E  +                                            +  E  +
 * +  R  +                 sync                       +  R  +
 * +     +    :5010 ¤<------------------o----.        +     +
 * +  1  +                async         | RX |        +  2  +
 * +     +    :5011 ¤<------------------o----'        +     +
 * +     +                                            +     +
 * +     +--------------------------------------------+     +
 * +                                                        +
 * +                       RTCP                             +
 * +        TX o----------------------------->¤ :5013       +
 * +                       RTCP                             +
 * +     :5015 ¤<-----------------------------o RX          +
 * +                                                        +
 * ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 */
#ifndef TESTS_TEST_H_
#define TESTS_TEST_H_

#include <string.h>

typedef enum{
  NO_CONTROLLING               = 0,
  MANUAL_RATE_CONTROLLING      = 1,
  AUTO_RATE_AND_CC_CONTROLLING = 2,
}TestSuite;

typedef enum{
  TEST_SOURCE    = 0,
  YUVFILE_SOURCE = 1,
  VL2SRC         = 2,
}VideoSession;

typedef enum{
  FOREMAN          = 0,
  KRISTEN_AND_SARA = 1,
  EMPTY_1          = 2,
  EMPTY_2          = 3,
}YUVSequence;

typedef struct _TestParams{
  TestSuite    test_directive;
  VideoSession video_session;
  gboolean     random_detach;
  gboolean     subflow1_active;
  gboolean     subflow2_active;
  gboolean     subflow3_active;
  guint        subflow_num;
  YUVSequence  yuvsequence;
  gchar        yuvfile_str[255];
//  gboolean     other_variable_used_for_debugging_because_i_am_tired_to_recompile_it_every_time;
}TestParams;

static TestParams test_parameters_;
static gint32 profile;
static gint32 info;

static int target_bitrate_start = 0;
static int target_bitrate_min   = 0;
static int target_bitrate_max   = 0;

static int path1_tx_rtp_port  = 5000;
static int path1_tx_rtcp_port = 5001;
static int path1_rx_rtp_port  = 5002;
static int path1_rx_rtcp_port = 5003;

static int path2_tx_rtp_port  = 5004;
static int path2_tx_rtcp_port = 5005;
static int path2_rx_rtp_port  = 5006;
static int path2_rx_rtcp_port = 5007;

static int path3_tx_rtp_port  = 5008;
static int path3_tx_rtcp_port = 5009;
static int path3_rx_rtp_port  = 5010;
static int path3_rx_rtcp_port = 5011;

static int rtpbin_tx_rtcp_port = 5013;
static int rtpbin_rx_rtcp_port = 5015;
static gchar *logsdir = NULL;

static GOptionEntry entries[] =
{
    { "profile", 0, 0, G_OPTION_ARG_INT, &profile, "Profile", NULL },
    { "logsdir", 0, 0, G_OPTION_ARG_STRING, &logsdir, "Logsdir", NULL },
    { "target_bitrate_start", 0, 0, G_OPTION_ARG_INT, &target_bitrate_start, "target_bitrate_start", NULL },
    { "target_bitrate_min", 0, 0, G_OPTION_ARG_INT, &target_bitrate_min, "target_bitrate_min", NULL },
    { "target_bitrate_max", 0, 0, G_OPTION_ARG_INT, &target_bitrate_max, "target_bitrate_max", NULL },
    { "path1_tx_rtp_port", 0, 0, G_OPTION_ARG_INT, &path1_tx_rtp_port, "path1_tx_rtp_port", NULL },
    { "path1_tx_rtcp_port", 0, 0, G_OPTION_ARG_INT, &path1_tx_rtcp_port, "path1_tx_rtcp_port", NULL },
    { "path1_rx_rtp_port", 0, 0, G_OPTION_ARG_INT, &path1_rx_rtp_port, "path1_rx_rtp_port", NULL },
    { "path1_rx_rtcp_port", 0, 0, G_OPTION_ARG_INT, &path1_rx_rtcp_port, "path1_rx_rtcp_port", NULL },

    { "path2_tx_rtp_port", 0, 0, G_OPTION_ARG_INT, &path2_tx_rtp_port, "path2_tx_rtp_port", NULL },
    { "path2_tx_rtcp_port", 0, 0, G_OPTION_ARG_INT, &path2_tx_rtcp_port, "path2_tx_rtcp_port", NULL },
    { "path2_rx_rtp_port", 0, 0, G_OPTION_ARG_INT, &path2_rx_rtp_port, "path2_rx_rtp_port", NULL },
    { "path2_rx_rtcp_port", 0, 0, G_OPTION_ARG_INT, &path2_rx_rtcp_port, "path2_rx_rtcp_port", NULL },

    { "path3_tx_rtp_port", 0, 0, G_OPTION_ARG_INT, &path3_tx_rtp_port, "path3_tx_rtp_port", NULL },
    { "path3_tx_rtcp_port", 0, 0, G_OPTION_ARG_INT, &path3_tx_rtcp_port, "path3_tx_rtcp_port", NULL },
    { "path3_rx_rtp_port", 0, 0, G_OPTION_ARG_INT, &path3_rx_rtp_port, "path3_rx_rtp_port", NULL },
    { "path3_rx_rtcp_port", 0, 0, G_OPTION_ARG_INT, &path3_rx_rtcp_port, "path3_rx_rtcp_port", NULL },

    { "rtpbin_tx_rtcp_port", 0, 0, G_OPTION_ARG_INT, &rtpbin_tx_rtcp_port, "rtpbin_tx_rtcp_port", NULL },
    { "rtpbin_rx_rtcp_port", 0, 0, G_OPTION_ARG_INT, &rtpbin_rx_rtcp_port, "rtpbin_rx_rtcp_port", NULL },

    { "info", 0, 0, G_OPTION_ARG_NONE, &info, "Info", NULL },
  { NULL }
};

static void _print_transfer_info(void)
{
  g_print("################### Network transfer port parameters ##############################\n");
  g_print("#                                                                                 #\n");
  g_print(" ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
  g_print(" +     +                          10.0.0.0/24                               +     +\n");
  g_print(" +     +--------------------------------------------------------------------+     +\n");
  g_print(" +     +                                                                    +     +\n");
  g_print(" +     +                               RTCP                                 +     +\n");
  g_print(" +     +                 TX o----------------------------->   %-13d +     +\n",rtpbin_tx_rtcp_port);
  g_print(" +     +                               RTCP                                 +     +\n");
  g_print(" +     +%19d  <-----------------------------o RX             +     +\n",rtpbin_rx_rtcp_port);
  g_print(" +     +                                                                    +     +\n");
  g_print(" +     +--------------------------------------------------------------------+     +\n");
  g_print(" +     +                                                                    +     +\n");
  g_print(" +     +                     .1      sync         .2                        +     +\n");
  g_print(" +     +                .----o------------------>  :%-18d      +     +\n",path1_tx_rtp_port);
  g_print(" +  P  +                | TX |       async                                  +  P  +\n");
  g_print(" +  E  +                '----o------------------>  :%-18d      +  E  +\n",path1_tx_rtcp_port);
  g_print(" +  E  +                                                                    +  E  +\n");
  g_print(" +  R  +                              sync                                  +  R  +\n");
  g_print(" +     +     %18d  <------------------o----.                  +     +\n",path1_rx_rtp_port);
  g_print(" +  1  +                              async         | RX |                  +  2  +\n");
  g_print(" +     +     %18d  <------------------o----'                  +     +\n",path1_rx_rtcp_port);
  g_print(" +     +                                                                    +     +\n");
  g_print(" +     ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++     +\n");
  g_print(" +     +                          10.0.1.0/24                               +     +\n");
  g_print(" +     +--------------------------------------------------------------------+     +\n");
  g_print(" +     +                     .1      sync         .2                        +     +\n");
  g_print(" +     +                .----o------------------>  :%-18d      +     +\n",path2_tx_rtp_port);
  g_print(" +  P  +                | TX |       async                                  +  P  +\n");
  g_print(" +  E  +                '----o------------------>  :%-18d      +  E  +\n",path2_tx_rtcp_port);
  g_print(" +  E  +                                                                    +  E  +\n");
  g_print(" +  R  +                              sync                                  +  R  +\n");
  g_print(" +     +     %18d  <------------------o----.                  +     +\n",path2_rx_rtp_port);
  g_print(" +  1  +                              async         | RX |                  +  2  +\n");
  g_print(" +     +     %18d  <------------------o----'                  +     +\n",path2_rx_rtcp_port);
  g_print(" +     +                                                                    +     +\n");
  g_print(" +     ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++     +\n");
  g_print(" +     +                          10.0.2.0/24                               +     +\n");
  g_print(" +     +--------------------------------------------------------------------+     +\n");
  g_print(" +     +                     .1      sync         .2                        +     +\n");
  g_print(" +     +                .----o------------------>   %-18d      +     +\n",path2_tx_rtp_port);
  g_print(" +  P  +                | TX |       async                                  +  P  +\n");
  g_print(" +  E  +                '----o------------------>   %-18d      +  E  +\n",path2_tx_rtcp_port);
  g_print(" +  E  +                                                                    +  E  +\n");
  g_print(" +  R  +                              sync                                  +  R  +\n");
  g_print(" +     +     %18d  <------------------o----.                  +     +\n",path2_rx_rtp_port);
  g_print(" +  1  +                              async         | RX |                  +  2  +\n");
  g_print(" +     +     %18d  <------------------o----'                  +     +\n",path2_rx_rtcp_port);
  g_print(" +     +                                                                    +     +\n");
  g_print(" ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
  g_print("#                                                                                 #\n");
  g_print("# Example to setup a port: --path3_rx_rtcp_port=1234                              #\n");
  g_print("###################################################################################\n");
}


#define println(str) g_print(str"\n")
static void _print_info(void)
{
  println("####################### Test profiles ################################");
  println("#                                                                    #");
  println("# profile = 0b00|0|00|00|0|0|0                                       #");
  println("#              ^ ^  ^  ^ ^ ^ ^                                       #");
  println("#              | |  |  | | | |0/1 - Deactivate/Activate subflow 1    #");
  println("#              | |  |  | | |0/1 - Deactivate/Activate subflow 2      #");
  println("#              | |  |  | |0/1 - Deactivate/Activate subflow 3        #");
  println("#              | |  |  |0 - Test source,1 - yuv sequence, 2 - v4l2src#");
  println("#              | |  |0 - No rate control, 1 - random rate ctrl,      #");
  println("#              | |  |2 - auto rate and cc control                    #");
  println("#              | |0/1 - join detach subflow continously              #");
  println("#              |0 - foreman,                                         #");
  println("# Examples:                                                          #");
  println("# --profile=1 <- subflow 1, test source, no rate controller          #");
  println("# --profile=3 <- subflow 1 and 2, test source, no rate controller    #");
  println("# --profile=67 <- sub 1 and 2, test source, rate and cc              #");
  println("######################################################################");

  println("\n\n");
//  _print_transfer_info();
}



static void _setup_test_params(guint profile)
{
  memset(&test_parameters_, 0, sizeof(TestParams));
  if(profile == 0){
      profile = 1;
//      _print_info();
  }
  g_print("Selected test profile: %d, it setups the following:\n", profile);

  test_parameters_.subflow1_active = (profile & 1) ? TRUE : FALSE;
  g_print("%s subflow 1\n", test_parameters_.subflow1_active?"Active":"Deactive");
  test_parameters_.subflow2_active = (profile & 2) ? TRUE : FALSE;
  g_print("%s subflow 2\n", test_parameters_.subflow2_active?"Active":"Deactive");
  test_parameters_.subflow3_active = (profile & 4) ? TRUE : FALSE;
  g_print("%s subflow 3\n", test_parameters_.subflow3_active?"Active":"Deactive");
  g_print("logsdir: %s\n", logsdir);
  test_parameters_.subflow_num = 0;
  test_parameters_.subflow_num+=test_parameters_.subflow1_active ? 1 : 0;
  test_parameters_.subflow_num+=test_parameters_.subflow2_active ? 1 : 0;
  test_parameters_.subflow_num+=test_parameters_.subflow3_active ? 1 : 0;

  test_parameters_.video_session =(VideoSession)((profile & 24)>>3);
  switch (test_parameters_.video_session) {
    case VL2SRC:
      g_print("Vl2 source is selected\n");
      break;
    case YUVFILE_SOURCE:
      g_print("Yuvfile sequence is selected\n");
      break;
    case TEST_SOURCE:
    default:
      g_print("Test video source is selected\n");
      break;
  }

  test_parameters_.test_directive = (TestSuite)((profile & 96)>>5);
  switch (test_parameters_.test_directive) {
      case MANUAL_RATE_CONTROLLING:
        g_print("Manual Rate controller is selected.\n");
        break;
      case AUTO_RATE_AND_CC_CONTROLLING:
        g_print("Automatic rate and congestion controller mode is selected.\n");
        break;
      default:
      case NO_CONTROLLING:
        g_print("No rate or flow controlling is enabled.\n");
        break;
    }

  test_parameters_.random_detach = (profile & 128) > 0 ? TRUE : FALSE;
  if(test_parameters_.random_detach){
    g_print("Random join/detach is activated\n");
  }

  test_parameters_.yuvsequence =(VideoSession)((profile & 768)>>8);
  switch(test_parameters_.yuvsequence){
    case KRISTEN_AND_SARA:
      strcpy(test_parameters_.yuvfile_str, "KristenAndSara_1280x720_60.yuv");
      break;
    case EMPTY_1:
      strcpy(test_parameters_.yuvfile_str, "foreman_cif.yuv");
      break;
    case EMPTY_2:
      strcpy(test_parameters_.yuvfile_str, "foreman_cif.yuv");
      break;
    default:
    case FOREMAN:
      strcpy(test_parameters_.yuvfile_str, "foreman_cif.yuv");
      break;
  }

  if(test_parameters_.video_session == YUVFILE_SOURCE) g_print("Yuv sequence: %s\n", test_parameters_.yuvfile_str);



  if(path1_tx_rtp_port == 0){
    path1_tx_rtp_port  = 5000;
  }
  if(path1_tx_rtcp_port == 0){
      path1_tx_rtcp_port = 5001;
  }
  if(path1_rx_rtp_port == 0){
      path1_rx_rtp_port  = 5002;
  }
  if(path1_rx_rtcp_port == 0){
      path1_rx_rtcp_port = 5003;
  }


  if(path2_tx_rtp_port == 0){
    path2_tx_rtp_port  = 5004;
  }
  if(path2_tx_rtcp_port == 0){
    path2_tx_rtcp_port = 5005;
  }
  if(path2_rx_rtp_port == 0){
    path2_rx_rtp_port  = 5006;
  }
  if(path2_rx_rtcp_port == 0){
    path2_rx_rtcp_port = 5007;
  }

  if(path3_tx_rtp_port == 0){
    path3_tx_rtp_port  = 5008;
  }
  if(path3_tx_rtcp_port == 0){
    path3_tx_rtcp_port = 5009;
  }
  if(path3_rx_rtp_port == 0){
    path3_rx_rtp_port  = 5010;
  }
  if(path3_rx_rtcp_port == 0){
    path3_rx_rtcp_port = 5011;
  }

  if(rtpbin_tx_rtcp_port == 0){
      rtpbin_tx_rtcp_port  = 5013;
  }
  if(rtpbin_rx_rtcp_port == 0){
      rtpbin_rx_rtcp_port = 5015;
  }

  if(logsdir == NULL){
    logsdir = g_malloc(255);
    sprintf(logsdir, "logs/");
  }

  if(target_bitrate_start == 0){
      target_bitrate_start = 128000 * test_parameters_.subflow_num;
  }

  if(target_bitrate_min == 0){
      target_bitrate_min = 500000;
  }
//  _print_transfer_info();

}



#endif /* TESTS_TEST_H_ */
