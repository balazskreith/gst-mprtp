/*
 * pipeline.h
 *
 *  Created on: 1 Nov 2016
 *      Author: balazskreith
 */

#ifndef TESTS_PIPELINE_H_
#define TESTS_PIPELINE_H_

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>
#include <gst/rtp/rtp.h>


typedef struct{
  gint32 numerator;
  gint32 divider;
}Framerate;

typedef void(*listener)(gpointer listener_obj, gpointer argument);

typedef struct{
  gpointer listener_obj;
  listener listener_func;
}Listener;

typedef struct{
  GSList* listeners;
  gchar   name[256];
}Notifier;


typedef enum{
  SOURCE_TYPE_TESTVIDEO = 1,
  SOURCE_TYPE_RAWPROXY  = 2,
  SOURCE_TYPE_LIVEFILE  = 3,
  SOURCE_TYPE_FILE      = 4,
}SourceTypes;

typedef struct{
  SourceTypes type;
  union{
   struct{
     guint16 port;
     guint32 clock_rate;
     gchar   width[16];
     gchar   height[16];

   }rawproxy;

   struct{
     gchar     width[16];
     gchar     height[16];

     gchar     location[256];
     gint      loop;
     Framerate framerate;
     gint32    format;
   }file;

   struct{

   }testvideo;

  };

  gchar to_string[256];
}SourceParams;



typedef enum{
  SINK_TYPE_AUTOVIDEO = 1,
  SINK_TYPE_RAWPROXY  = 2,
}SinkTypes;

typedef struct{
  SinkTypes type;

  gchar      to_string[256];
}SinkParams;


typedef enum{
  CODEC_TYPE_THEORA = 1,
  CODEC_TYPE_VP8    = 2,
}CodecTypes;

typedef struct{
  CodecTypes type;
  gchar      type_str[32];
  gchar      to_string[256];

}CodecParams;

typedef struct{
  gchar      width[16];
  gchar      height[16];
  gint32     clock_rate;
  Framerate  framerate;
  gchar      to_string[256];

}VideoParams;


typedef struct{
  gint       csv;
  gchar      touched_sync[256];

  gchar      to_string[1024];

}StatParams;

typedef struct{
  StatParams* stat_params;
  SinkParams* packetlogs_sink_params;
  SinkParams* statlogs_sink_params;

  gchar       to_string[2048];
}StatParamsTuple;


typedef enum{
  TRANSFER_TYPE_RTP    = 1,
  TRANSFER_TYPE_MPRTP  = 2,
}TransferTypes;

typedef struct{
  guint8  id;
  guint16 dest_port;
  gchar   dest_ip[256];
}SenderSubflow;

typedef struct{
  TransferTypes type;
  union{
    struct{
      gint32 dest_port;
      gchar  dest_ip[256];
    }rtp;

    struct{
      gint    subflows_num;
      GSList* subflows;
    }mprtp;
  };
  gchar to_string[1024];
}SndTransferParams;


typedef struct{
  guint8  id;
  guint16 bound_port;
}ReceiverSubflow;

typedef struct{
  TransferTypes type;
  union{
    struct{
      guint16 bound_port;
    }rtp;

    struct{
      gint    subflows_num;
      GSList* subflows;
    }mprtp;
  };
  gchar to_string[1024];
}RcvTransferParams;


typedef enum{
  CONGESTION_CONTROLLER_TYPE_SCREAM    = 1,
  CONGESTION_CONTROLLER_TYPE_FBRAPLUS  = 2,
}CongestionControllerTypes;

typedef struct{
  CongestionControllerTypes type;
  RcvTransferParams* rcv_transfer_params;
  union{
    struct{

    }scream;
    struct{

    }fbrap;
  };

  gchar to_string[256];
}CCSenderSideParams;

typedef struct{
  CongestionControllerTypes type;
  SndTransferParams* snd_transfer_params;
  union{
    struct{

    }scream;
    struct{

    }fbrap;
  };

  gchar to_string[256];
}CCReceiverSideParams;//TODO: write this

static gchar codec_params_rawstring_default[]           = "VP8";
static gchar* codec_params_rawstring                    = NULL;

static gchar video_params_rawstring_default[]           = "352:288:90000:25/1";
static gchar* video_params_rawstring                    = NULL;

//static gchar source_params_rawstring_default[]        = "TESTVIDEO";
static gchar source_params_rawstring_default[]          = "LIVEFILE:foreman_cif.yuv:1:352:288:2:25/1";
//static gchar source_params_rawstring_default[]        = "FILE:foreman_cif.yuv:1:352:288:2:25/1";
static gchar* source_params_rawstring                   = NULL;

static gchar  sink_params_rawstring_default[]           = "AUTOVIDEO";
static gchar* sink_params_rawstring                     = NULL;

static gchar  sndtransfer_params_rawstring_default[]    = "RTP:10.0.0.6:5000";
static gchar* sndtransfer_params_rawstring              = NULL;

static gchar  rcvtransfer_params_rawstring_default[]    = "RTP:5000";
static gchar* rcvtransfer_params_rawstring              = NULL;

static gchar* stat_params_rawstring                     = NULL;
static gchar* encodersink_params_rawstring              = NULL;
static gchar* statlogs_sink_params_rawstring            = NULL;
static gchar* packetlogs_sink_params_rawstring          = NULL;

static int target_bitrate = 500000;
static int info           = 0;


static GOptionEntry entries[] =
{
    { "video",     0, 0, G_OPTION_ARG_STRING, &video_params_rawstring,           "video",         NULL },
    { "codec",     0, 0, G_OPTION_ARG_STRING, &codec_params_rawstring,           "codec",         NULL },
    { "source",    0, 0, G_OPTION_ARG_STRING, &source_params_rawstring,          "source",        NULL },
    { "sink",      0, 0, G_OPTION_ARG_STRING, &sink_params_rawstring,            "sink",          NULL },
    { "sender",    0, 0, G_OPTION_ARG_STRING, &sndtransfer_params_rawstring,     "sender",        NULL },
    { "receiver",  0, 0, G_OPTION_ARG_STRING, &rcvtransfer_params_rawstring,     "receiver",      NULL },

    //TODO: MUHAHA
//    { "cc",  0, 0, G_OPTION_ARG_STRING, &congestion_controller_rawstring,    "cc",      NULL },
//    { "fb",  0, 0, G_OPTION_ARG_STRING, &feedbacker_rawstring,    "fb",      NULL },

    { "stat",           0, 0, G_OPTION_ARG_STRING, &stat_params_rawstring,            "stat",            NULL },
    { "encodersink",    0, 0, G_OPTION_ARG_STRING, &encodersink_params_rawstring,     "encodersink",     NULL },
    { "statlogsink",    0, 0, G_OPTION_ARG_STRING, &statlogs_sink_params_rawstring,   "statlogsink",     NULL },
    { "packetlogsink",  0, 0, G_OPTION_ARG_STRING, &packetlogs_sink_params_rawstring, "packetlogsink",   NULL },

    { "target_bitrate", 0, 0, G_OPTION_ARG_INT, &target_bitrate, "target_bitrate", NULL },

    { "info", 0, 0, G_OPTION_ARG_NONE, &info, "Info", NULL },
  { NULL }
};

gchar* _null_test(gchar *subject_str, gchar* failed_str);

void setup_framerate(gchar *string, Framerate* framerate);

SourceParams*   make_source_params(gchar* params_rawstring);
CodecParams*    make_codec_params (gchar* params_rawstring);
VideoParams*    make_video_params (gchar* params_rawstring);
StatParams*     make_stat_params (gchar* params_rawstring);

SndTransferParams*  make_snd_transfer_params(gchar* params_rawstring);
void free_snd_transfer_params(SndTransferParams *snd_transfer_params);

CCSenderSideParams* make_cc_sender_side_params(gchar* params_rawstring);
void free_cc_sender_side_params(CCSenderSideParams *cc_sender_side_params);

RcvTransferParams*  make_rcv_transfer_params(gchar* params_rawstring);
void free_rcv_transfer_params(RcvTransferParams *rcv_transfer_params);

CCReceiverSideParams*   make_cc_receiver_side_params(gchar* params_rawstring);
void free_cc_receiver_side_params(CCReceiverSideParams *congestion_controller_params);

SinkParams*     make_sink_params(gchar* params_rawstring);

StatParamsTuple* make_statparams_tuple_by_raw_strings(gchar* statparams_rawstring,
                                       gchar* statlogs_sink_params_rawstring,
                                       gchar* packetlogs_sink_params_rawstring
                                      );

StatParamsTuple* make_statparams_tuple(StatParams* stat_params,
                                       SinkParams* statlogs_sink_params,
                                       SinkParams* packetlogs_sink_params
                                      );

void free_statparams_tuple(StatParamsTuple* statparams_tuple);

Notifier* notifier_ctor(void);
void notifier_dtor(Notifier* this);
Notifier* make_notifier(const gchar* event_name);
void notifier_add_listener(Notifier* this, listener listener_func, gpointer user_data);
void notifier_do(Notifier* this, gpointer user_data);

void cb_eos (GstBus * bus, GstMessage * message, gpointer data);
void cb_state (GstBus * bus, GstMessage * message, gpointer data);
void cb_warning (GstBus * bus, GstMessage * message, gpointer data);
void cb_error (GstBus * bus, GstMessage * message, gpointer data);

GstElement* debug_by_src(GstElement*element);
GstElement* debug_by_sink(GstElement*element);

void setup_ghost_sink_by_padnames (GstElement * sink, const gchar* sinkpadName, GstBin * bin, const gchar *binPadName);
void setup_ghost_sink (GstElement * sink, GstBin * bin);
void setup_ghost_src_by_padnames (GstElement * src, const gchar* srcpadName, GstBin * bin, const gchar *binPadName);
void setup_ghost_src (GstElement * src, GstBin * bin);


typedef struct{
  Notifier* on_target_bitrate_change;
  Notifier* on_assembled;
  Notifier* on_playing;
  Notifier* on_destroy;
}SenderEventers;

SenderEventers* get_sender_eventers(void);
void sender_eventers_dtor(void);



#endif /* TESTS_PIPELINE_H_ */