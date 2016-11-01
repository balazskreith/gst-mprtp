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

typedef struct{
  gint32 numerator;
  gint32 divider;
}Fraction;

typedef enum{
  CODEC_TYPE_THEORA = 1,
  CODEC_TYPE_VP8    = 2
}CodecTypes;

typedef struct{
  gchar       codec_string[256];
  CodecTypes  codec;
  gint32      clock_rate;
  gint32      width;
  gint32      height;
  Fraction    framerate;
}VideoParams;

static VideoParams *video_params;
static gchar* video_params_rawstring = NULL;

typedef enum{
  SOURCE_TYPE_TESTVIDEO = 1,
  SOURCE_TYPE_RAWPROXY  = 2
}SourceTypes;

typedef struct{
  SourceTypes type;
}SourceParams;

typedef struct{
  SourceTypes  type;
  gint32       port;
  gint32       width;
  gint32       height;
  gint32       clock_rate;
  Fraction     framerate;
}RawProxySourceParams;

static SourceParams *source_params;
static gchar* source_params_rawstring = NULL;

typedef enum{
  TRANSFER_TYPE_RTPSIMPLE = 1,
  TRANSFER_TYPE_RTPFBRAP  = 2,
  TRANSFER_TYPE_RTPSCREAM = 3,
}TransferTypes;

typedef struct{
 TransferTypes   type;
}TransferParams;

typedef struct{
  TransferTypes  type;
  gint32         peer_port;
  gchar          peer_ip[256];
}RTPSimpleSenderParams;

typedef struct{
  TransferTypes  type;
  gint32          bound_port;
}RTPSimpleReceiverParams;

static TransferParams *sender_params;
static gchar* sender_params_rawstring = NULL;

static TransferParams *receiver_params;
static gchar* receiver_params_rawstring = NULL;

static GOptionEntry entries[] =
{
    { "video_params",    0, 0, G_OPTION_ARG_STRING, &video_params_rawstring,    "video_params",    NULL },
    { "source_params",   0, 0, G_OPTION_ARG_STRING, &source_params_rawstring,   "source_params",   NULL },
    { "sender_params",   0, 0, G_OPTION_ARG_STRING, &sender_params_rawstring,   "sender_params",   NULL },
    { "receiver_params", 0, 0, G_OPTION_ARG_STRING, &receiver_params_rawstring, "receiver_params", NULL },

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

static void setup_fraction(const gchar* fraction_string, Fraction* result)
{
  gchar **fraction_vector = g_strsplit(fraction_string, "/", -1);
  result->numerator = atoi(fraction_vector[0]);
  result->divider   = atoi(fraction_vector[1]);
  g_strfreev(fraction_vector);
}

static VideoParams* build_video_params(const gchar* video_params)
{
  gchar **tokens;
  gint length;
  VideoParams* result = NULL;
  gchar *codec;
  tokens = g_strsplit(video_params, ":", -1);

  for (length = 0; tokens && tokens[length]; length++);
  if(length < 4){
   g_print("Error in video parameters setup\n");
   goto done;
  }

  result = g_malloc(sizeof(VideoParams));
  memset(result->codec_string, 0, 256);

  codec = tokens[0];

  if(!strcmp(codec, "VP8")){
    result->codec = CODEC_TYPE_VP8;
    strcpy(result->codec_string, "VP8");
  }else if(!strcmp(codec, "THEORA")){
    result->codec = CODEC_TYPE_THEORA;
    strcpy(result->codec_string, "THEORA");
  }

  result->width      = atoi(tokens[1]);
  result->height     = atoi(tokens[2]);
  result->clock_rate = atoi(tokens[3]);

  setup_fraction(tokens[4], &result->framerate);

  g_print("Video Codec is %s, width: %d, height: %d, clock-rate: %d, framerate: %d/%d\n",
      codec,
      result->width,
      result->height,
      result->clock_rate,
      result->framerate.numerator,
      result->framerate.divider
      );

done:
  g_strfreev(tokens);
  return result;
}

static SourceParams* build_source_params(const gchar* source_params)
{
  gchar  **tokens;
  gint     length;
  gpointer result = NULL;
  gchar   *type;
  tokens = g_strsplit(source_params, ":", -1);

  for (length = 0; tokens && tokens[length]; length++);
  if(length < 1){
   g_print("Error in selected source type: no parameter found\n");
   goto done;
  }

  type = tokens[0];
  if(!strcmp(type, "TESTVIDEO")){
    result = g_malloc0(sizeof(SourceParams));
    ((SourceParams*)result)->type = SOURCE_TYPE_TESTVIDEO;
    goto done;
  }

  if(!strcmp(type, "RAWPROXY")){
    RawProxySourceParams *raw_proxy_params;
    if(length < 5){
      goto done;
    }
    raw_proxy_params = g_malloc0(sizeof(RawProxySourceParams));
    raw_proxy_params->type       = SOURCE_TYPE_RAWPROXY;
    raw_proxy_params->port       = atoi(tokens[1]);

    raw_proxy_params->width      = atoi(tokens[2]);
    raw_proxy_params->height     = atoi(tokens[3]);
    raw_proxy_params->clock_rate = atoi(tokens[4]);

    setup_fraction(tokens[5], &raw_proxy_params->framerate);

    result = (SourceParams*) raw_proxy_params;

  }

done:
  return result;
}

static void _print_video_params()
{
  if(source_params->type == SOURCE_TYPE_TESTVIDEO){
    g_print("Source is TestVideo\n");
    return;
  }
  if(source_params->type == SOURCE_TYPE_RAWPROXY){
    RawProxySourceParams *raw_proxy_params = (RawProxySourceParams*) source_params;
    g_print("Source is RawVideoProxy, port: %d, width: %d, height: %d, clock-rate: %d framerate: %d/%d\n",
      raw_proxy_params->port,
      raw_proxy_params->width,
      raw_proxy_params->height,
      raw_proxy_params->clock_rate,
      raw_proxy_params->framerate.numerator,
      raw_proxy_params->framerate.divider
      );
  }
}



static TransferParams* build_sender_params(const gchar* sender_params)
{
  gchar  **tokens;
  gint     length;
  gpointer result = NULL;
  gchar   *type;
  tokens = g_strsplit(sender_params, ":", -1);

  for (length = 0; tokens && tokens[length]; length++);
  if(length < 1){
   g_print("Error in selected source type: no parameter found\n");
   goto done;
  }

  type = tokens[0];
  if(!strcmp(type, "RTPSIMPLE")){
    RTPSimpleSenderParams* simple;
    simple = g_malloc0(sizeof(RTPSimpleSenderParams));
    simple->type = TRANSFER_TYPE_RTPSIMPLE;
    strcpy(simple->peer_ip, tokens[1]);
    simple->peer_port = atoi(tokens[2]);
    result = simple;
    goto done;
  }

done:
  return result;
}

static void _print_sender_params(){
  if(sender_params->type == TRANSFER_TYPE_RTPSIMPLE){
    RTPSimpleSenderParams* simple = (RTPSimpleSenderParams*) sender_params;
    g_print("Simple RTP Sender, peer ip: %s, peer port: %d\n",
        simple->peer_ip,
        simple->peer_port);
  }
}


static TransferParams* build_receiver_params(const gchar* receiver_params)
{
  gchar  **tokens;
  gint     length;
  gpointer result = NULL;
  gchar   *type;
  tokens = g_strsplit(receiver_params, ":", -1);

  for (length = 0; tokens && tokens[length]; length++);
  if(length < 1){
   g_print("Error in selected source type: no parameter found\n");
   goto done;
  }

  type = tokens[0];
  if(!strcmp(type, "RTPSIMPLE")){
    RTPSimpleReceiverParams* simple;
    simple = g_malloc0(sizeof(RTPSimpleReceiverParams));
    simple->type = TRANSFER_TYPE_RTPSIMPLE;
    simple->bound_port = atoi(tokens[1]);
    result = simple;
    goto done;
  }

done:
  return result;
}

static void _print_receiver_params(){
  if(receiver_params->type == TRANSFER_TYPE_RTPSIMPLE){
    RTPSimpleReceiverParams* simple = (RTPSimpleReceiverParams*) receiver_params;
    g_print("Simple RTP Receiver, bound port: %d\n",
        simple->bound_port
    );
  }
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

  video_params = build_video_params(video_params_rawstring == NULL ?
      "VP8:352:288:90000:25/1" : video_params_rawstring);

  source_params = build_source_params(source_params_rawstring == NULL ?
      "RAWPROXY:5111:352:288:90000:25/1" : source_params_rawstring);

  sender_params = build_sender_params(sender_params_rawstring == NULL ?
      "RTPSIMPLE:10.0.0.6:5000" : sender_params_rawstring);

  receiver_params = build_receiver_params(receiver_params_rawstring == NULL ?
      "RTPSIMPLE:5000" : receiver_params_rawstring);

}



static void
cb_eos (GstBus * bus, GstMessage * message, gpointer data)
{
  GMainLoop *loop = data;
  g_print ("Got EOS\n");
  g_main_loop_quit (loop);
}

static void
cb_state (GstBus * bus, GstMessage * message, gpointer data)
{
  GstObject *pipe = GST_OBJECT (data);
  GstState old, new, pending;
  gst_message_parse_state_changed (message, &old, &new, &pending);
  if (message->src == pipe) {
    g_print ("Pipeline %s changed state from %s to %s\n",
        GST_OBJECT_NAME (message->src),
        gst_element_state_get_name (old), gst_element_state_get_name (new));
  }
}

static void
cb_warning (GstBus * bus, GstMessage * message, gpointer data)
{
  GError *error = NULL;
  gst_message_parse_warning (message, &error, NULL);
  g_printerr ("Got warning from %s: %s\n", GST_OBJECT_NAME (message->src),
      error->message);
  g_error_free (error);
}

static void
cb_error (GstBus * bus, GstMessage * message, gpointer data)
{
  GMainLoop *loop = data;
  GError *error = NULL;
  gst_message_parse_error (message, &error, NULL);
  g_printerr ("Got error from %s: %s\n", GST_OBJECT_NAME (message->src),
      error->message);
  g_error_free (error);
  g_main_loop_quit (loop);
}

static void
setup_ghost_sink_by_padnames (GstElement * sink, const gchar* sinkpadName, GstBin * bin, const gchar *binPadName)
{
  GstPad *sinkPad = gst_element_get_static_pad (sink, sinkpadName);
  GstPad *binPad = gst_ghost_pad_new (binPadName, sinkPad);
  gst_element_add_pad (GST_ELEMENT (bin), binPad);
}

static void
setup_ghost_sink (GstElement * sink, GstBin * bin)
{
  setup_ghost_sink_by_padnames(sink, "sink", bin, "sink");
}

static void
setup_ghost_src_by_padnames (GstElement * src, const gchar* srcpadName, GstBin * bin, const gchar *binPadName)
{
  GstPad *srcPad = gst_element_get_static_pad (src, srcpadName);
  GstPad *binPad = gst_ghost_pad_new (binPadName, srcPad);
  gst_element_add_pad (GST_ELEMENT (bin), binPad);
}


static void
setup_ghost_src (GstElement * src, GstBin * bin)
{
  setup_ghost_src_by_padnames(src, "src", bin, "src");
}



#endif /* FBRAPTESTS_TEST_H_ */
