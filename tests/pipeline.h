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

typedef enum{
  SOURCE_TYPE_TESTVIDEO = 1,
  SOURCE_TYPE_RAWPROXY  = 2,
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
     gint32 horizontal_speed;
   }testvideo;

  };

  gchar to_string[256];
}SourceParams;



typedef enum{
  SINK_TYPE_RAWPROXY  = 1,
  SINK_TYPE_AUTOVIDEO = 2,
}SinkTypes;

typedef struct{
  SinkTypes type;

  gchar      to_string[256];
}SinkParams;


typedef enum{
  CODEC_TYPE_VP8    = 1,
  CODEC_TYPE_THEORA = 2,
}CodecTypes;

typedef struct{
  CodecTypes type;
  gchar      width[16];
  gchar      height[16];
  gint32     clock_rate;

  gchar      to_string[256];
}CodecParams;


typedef enum{
  TRANSFER_TYPE_RTPSIMPLE    = 1,
  TRANSFER_TYPE_RTPSCREAM    = 2,
  TRANSFER_TYPE_RTPFBRAP     = 3,
}TransferTypes;

typedef struct{
  TransferTypes type;

  gchar      to_string[256];
}SenderParams;


typedef struct{
  TransferTypes type;

  gchar      to_string[256];
}ReceiverParams;

typedef void(*listener)(gpointer listener_obj, gpointer argument);

typedef struct{
  gpointer listener_obj;
  listener listener_func;
}Listener;

typedef struct{
  GSList* listeners;
  gchar   name[256];
}Notifier;

static gchar codec_params_rawstring_default[]     = "";
static gchar* codec_params_rawstring              = NULL;

static gchar source_params_rawstring_default[]    = "";
static gchar* source_params_rawstring             = NULL;

static gchar sink_params_rawstring_default[]      = "";
static gchar* sink_params_rawstring               = NULL;

static gchar sender_params_rawstring_default[]    = "";
static gchar* sender_params_rawstring             = NULL;

static gchar receiver_params_rawstring_default[]  = "";
static gchar* receiver_params_rawstring           = NULL;

static gchar videosave_params_rawstring_default[] = "";
static gchar* videosave_params_rawstring          = NULL;

static gchar logging_params_rawstring_default[]   = "";
static gchar* logging_params_rawstring            = NULL;

static int target_bitrate = 500000;

static GOptionEntry entries[] =
{
    { "codec_params",     0, 0, G_OPTION_ARG_STRING, &codec_params_rawstring,      "codec_params",     NULL },
    { "source_params",    0, 0, G_OPTION_ARG_STRING, &source_params_rawstring,     "source_params",    NULL },
    { "sink_params",      0, 0, G_OPTION_ARG_STRING, &sink_params_rawstring,       "sink_params",      NULL },
    { "sender_params",    0, 0, G_OPTION_ARG_STRING, &sender_params_rawstring,     "sender_params",    NULL },
    { "receiver_params",  0, 0, G_OPTION_ARG_STRING, &receiver_params_rawstring,   "receiver_params",  NULL },

    { "videosave_params", 0, 0, G_OPTION_ARG_STRING, &videosave_params_rawstring,  "videosave_params", NULL },
    { "logging_params",   0, 0, G_OPTION_ARG_STRING, &logging_params_rawstring,    "videosave_params", NULL },

    { "target_bitrate", 0, 0, G_OPTION_ARG_INT, &target_bitrate, "target_bitrate", NULL },

    { "info", 0, 0, G_OPTION_ARG_NONE, &info, "Info", NULL },
  { NULL }
};

gchar* _string_test(gchar *subject_str, gchar* failed_str);

SourceParams*   make_source_params(gchar* params_rawstring);
CodecParams*    make_codec_params (gchar* params_rawstring);
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

#endif /* TESTS_PIPELINE_H_ */
