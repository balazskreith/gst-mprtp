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


typedef enum{
  TRANSFER_TYPE_RTPSIMPLE    = 1,
  TRANSFER_TYPE_RTPSCREAM    = 2,
  TRANSFER_TYPE_RTPFBRAP     = 3,
}TransferTypes;//TOOD: change it into containing rtp and mprtp

typedef struct{
  guint8 id;
  gint32 dest_port;
  gchar  dest_ip[256];
}SndSubflows;

typedef struct{
  TransferTypes type;
  union{
    struct{
      gint32 dest_port;
      gchar  dest_ip[256];
    }RTP;

    struct{
      GSList* subflows;
    }MPRTP;
  };
}SndTransferParams;//TODO: write the script handles the string

typedef enum{
  CONGESTION_CONTROLLING_TYPE_NONE = 1,
  CONGESTION_CONTROLLING_TYPE_SCREAM = 1,
  CONGESTION_CONTROLLING_TYPE_FBRAP = 1,
}CongestionControllingTypes;

typedef struct{
  CongestionControllingTypes type;

}CongestionControllerParams;//TODO: write the scripts handles the strings

typedef enum{
  STAT_COLLECTION_TYPE_NONE          = 1,
  STAT_COLLECTION_TYPE_ONLY_SENDER   = 2,
  STAT_COLLECTION_TYPE_ONLY_RECEIVER = 3,
  STAT_COLLECTION_TYPE_FULL          = 4
}StatCollectionTypes;

typedef struct{
  StatCollectionTypes type;
}StatCollectionParams;//TODO: Write the scripts handles these strings


typedef struct{
  TransferTypes type;

  gint32     dest_port;
  gchar      dest_ip[256];

  gchar      to_string[256];
}SenderParams;


typedef struct{
  TransferTypes type;
  gint32     bound_port;

  gchar      to_string[256];
}ReceiverParams;


static gchar codec_params_rawstring_default[]     = "VP8";
static gchar* codec_params_rawstring              = NULL;

static gchar video_params_rawstring_default[]     = "352:288:90000:25/1";
static gchar* video_params_rawstring              = NULL;

static gchar source_params_rawstring_default[]    = "LIVEFILE:foreman_cif.yuv:1:352:288:2:25/1";
//static gchar source_params_rawstring_default[]    = "TESTVIDEO:1";
//static gchar source_params_rawstring_default[]    = "TESTVIDEO:1";
static gchar* source_params_rawstring             = NULL;

static gchar sink_params_rawstring_default[]      = "AUTOVIDEO";
static gchar* sink_params_rawstring               = NULL;

static gchar sender_params_rawstring_default[]    = "RTPSIMPLE:5000:10.0.0.6";
static gchar* sender_params_rawstring             = NULL;

static gchar receiver_params_rawstring_default[]  = "RTPSIMPLE:5000";
static gchar* receiver_params_rawstring           = NULL;

static gchar videosave_params_rawstring_default[] = "";
static gchar* videosave_params_rawstring          = NULL;

static gchar logging_params_rawstring_default[]   = "";
static gchar* logging_params_rawstring            = NULL;

static int target_bitrate = 500000;
static int info           = 0;


static GOptionEntry entries[] =
{
    { "video",     0, 0, G_OPTION_ARG_STRING, &video_params_rawstring,      "video",     NULL },
    { "codec",     0, 0, G_OPTION_ARG_STRING, &codec_params_rawstring,      "codec",     NULL },
    { "source",    0, 0, G_OPTION_ARG_STRING, &source_params_rawstring,     "source",    NULL },
    { "sink",      0, 0, G_OPTION_ARG_STRING, &sink_params_rawstring,       "sink",      NULL },
    { "sender",    0, 0, G_OPTION_ARG_STRING, &sender_params_rawstring,     "sender",    NULL },
    { "receiver",  0, 0, G_OPTION_ARG_STRING, &receiver_params_rawstring,   "receiver",  NULL },

    { "videosave", 0, 0, G_OPTION_ARG_STRING, &videosave_params_rawstring,  "videosave", NULL },
    { "logging",   0, 0, G_OPTION_ARG_STRING, &logging_params_rawstring,    "videosave", NULL },

    { "target_bitrate", 0, 0, G_OPTION_ARG_INT, &target_bitrate, "target_bitrate", NULL },

    { "info", 0, 0, G_OPTION_ARG_NONE, &info, "Info", NULL },
  { NULL }
};

gchar* _string_test(gchar *subject_str, gchar* failed_str);

void setup_framerate(gchar *string, Framerate* framerate);

SourceParams*   make_source_params(gchar* params_rawstring);
CodecParams*    make_codec_params (gchar* params_rawstring);
VideoParams*    make_video_params (gchar* params_rawstring);
SenderParams*   make_sender_params(gchar* params_rawstring);
ReceiverParams* make_receiver_params(gchar* params_rawstring);
SinkParams*     make_sink_params(gchar* params_rawstring);

Notifier* notifier_ctor(void);
void notifier_dtor(Notifier* this);
Notifier* make_notifier(const gchar* event_name);
void notifier_add_listener(Notifier* this, listener listener_func, gpointer user_data);
void notifier_do(Notifier* this, gpointer user_data);

void cb_eos (GstBus * bus, GstMessage * message, gpointer data);
void cb_state (GstBus * bus, GstMessage * message, gpointer data);
void cb_warning (GstBus * bus, GstMessage * message, gpointer data);
void cb_error (GstBus * bus, GstMessage * message, gpointer data);

void setup_ghost_sink_by_padnames (GstElement * sink, const gchar* sinkpadName, GstBin * bin, const gchar *binPadName);
void setup_ghost_sink (GstElement * sink, GstBin * bin);
void setup_ghost_src_by_padnames (GstElement * src, const gchar* srcpadName, GstBin * bin, const gchar *binPadName);
void setup_ghost_src (GstElement * src, GstBin * bin);


typedef struct{
  Notifier* on_target_bitrate_change;
  Notifier* on_playing;
  Notifier* on_destroy;
}SenderNotifiers;

SenderNotifiers* get_sender_eventers(void);
void sender_eventers_dtor(void);



#endif /* TESTS_PIPELINE_H_ */
