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
#include <gst/rtp/gstrtcpbuffer.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtpdefs.h>
#include "owr_arrival_time_meta.h"


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

typedef struct _SourceParams SourceParams;
typedef struct _SinkParams SinkParams;
typedef struct _CodecParams CodecParams;
typedef struct _VideoParams VideoParams;
typedef struct _StatParams StatParams;
typedef struct _StatParamsTuple StatParamsTuple;
typedef struct _TransferParams TransferParams;
typedef struct _PlayouterParams PlayouterParams;
typedef struct _SchedulerParams SchedulerParams;
typedef struct _ExtraDelayParams ExtraDelayParams;


#include <gst/rtp/gstmprtpdefs.h>
//typedef struct _MPRTPSubflowUtilizationSignalData{
//  guint                      controlling_mode;
//  gint8                      path_state;
//  gint8                      path_flags_value;
//  gint32                     target_bitrate;
//
//  gdouble                    RTT;
//  guint32                    jitter;
//  gdouble                    lost_rate;
//  guint16                    HSSN;
//  guint16                    cycle_num;
//  guint32                    cum_packet_lost;
//  GstClockTime               owd_median;
//  GstClockTime               owd_min;
//  GstClockTime               owd_max;
//
//}MPRTPSubflowUtilizationSignalData;
//
//
//
//typedef struct _MPRTPPluginSignalData{
//  MPRTPSubflowUtilizationSignalData subflow[32];
//  gint32                            target_media_rate;
//}MPRTPPluginSignalData;

typedef struct{
  gint32 numerator;
  gint32 divider;
}Framerate;

typedef void(*subscriber)(gpointer subscriber_obj, gpointer argument);

typedef struct{
  gpointer subscriber_obj;
  subscriber subscriber_func;
}Subscriber;

typedef struct{
  GSList* subscribers;
  gchar   name[256];
  guint   ref;
}Eventer;


typedef enum{
  SOURCE_TYPE_TESTVIDEO = 1,
  SOURCE_TYPE_RAWPROXY  = 2,
  SOURCE_TYPE_FILE      = 3,
  SOURCE_TYPE_V4L2      = 4,
}SourceTypes;

struct _SourceParams{
  SourceTypes type;
  union{
   struct{

   }testvideo;

   struct{
     guint16            port;
     guint32            clock_rate;
     gchar              width[16];
     gchar              height[16];
     Framerate          framerate;
     TransferParams*    rcv_transfer_params;
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
     gchar     width[16];
     gchar     height[16];

     gchar     location[256];
     gint      loop;
     Framerate framerate;
     gint32    format;
   }v4l2;

  };

  gchar to_string[256];
};


typedef enum{
  SINK_TYPE_AUTOVIDEO = 1,
  SINK_TYPE_RAWPROXY  = 2,
  SINK_TYPE_FILE      = 3,
  SINK_TYPE_FAKESINK  = 4,
  SINK_TYPE_MULTIFILE = 5,
}SinkTypes;

struct _SinkParams{
  SinkTypes type;
  union{
    struct{

    }autovideo;

    struct{

    }rawproxy;

    struct{
      gchar location[256];
    }file;

    struct{
      GSList* items;
    }multifile;
  };
  gchar      to_string[256];
};


typedef enum{
  CODEC_TYPE_THEORA = 1,
  CODEC_TYPE_VP8    = 2,
}CodecTypes;

struct _CodecParams{
  CodecTypes type;
  gint       keyframe_mode;
  gint       keyframe_max_dist;
  gchar      type_str[32];
  gchar      to_string[256];

};

struct _VideoParams{
  gchar      width[16];
  gchar      height[16];
  gint32     clock_rate;
  Framerate  framerate;
  gchar      to_string[256];

};


struct _StatParams{
  gint       sampling_time;
  gint       accumulation_length;
  gint       csv_logging;
  gchar      touched_sync[256];

  gchar      to_string[1024];

};

struct _ExtraDelayParams{
  gint32     extra_delay_in_ms;
  gchar      to_string[1024];

};

struct _SubflowsParams{
  guint   length;
  GSList* subflows;
  gchar   to_string[1024];
  guint   ref;
};


struct _StatParamsTuple{
  StatParams* stat_params;
  SinkParams* packetlogs_sink_params;
  SinkParams* statlogs_sink_params;

  gchar       to_string[2048];
};

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
  guint8  id;
  guint16 bound_port;
}ReceiverSubflow;

struct _TransferParams{
  TransferTypes type;
  guint         length;
  GSList*       subflows;
  gchar         to_string[1024];
  guint         ref;
};

typedef enum{
  TRANSFER_CONTROLLER_TYPE_SCREAM        = 1,
  TRANSFER_CONTROLLER_TYPE_MPRTP          = 2,
  TRANSFER_CONTROLLER_TYPE_MPRTPRRACTAL  = 3,
}CongestionControllerTypes;


struct _SchedulerParams{
  CongestionControllerTypes type;
  TransferParams* rcv_transfer_params;
  union{
    struct{

    }scream;
    struct{
      struct{
        gint32 target_bitrate;
      }subflows[256];
    }mprtp;

    struct{
    }mprtp_fractal;
  };

  gchar to_string[256];
};

struct _PlayouterParams{
  CongestionControllerTypes type;
  TransferParams* snd_transfer_params;
  union{
    struct{

    }scream;
    struct{

    }mprtp;
    struct{

    }mprtp_fractal;
  };

  gchar to_string[256];
};//TODO: write this

typedef struct{
  GstElement* element;
  gchar       padname[256];
}Interface;

typedef struct _SinkFileItem{
  gchar  path[1024];
  gint   id;
}SinkFileItem;



static gchar codec_params_rawstring_default[]           = "VP8";
static gchar* codec_params_rawstring                    = NULL;

static gchar video_params_rawstring_default[]           = "352:288:90000:25/1";
static gchar* video_params_rawstring                    = NULL;

//static gchar source_params_rawstring_default[]        = "TESTVIDEO";
static gchar source_params_rawstring_default[]          = "FILE:foreman_cif.yuv:1:352:288:2:25/1";
//static gchar source_params_rawstring_default[]          = "V4L2";
//static gchar source_params_rawstring_default[]          = "RAWPROXY:352:288:90000:25/1:RTP:5111";
static gchar* source_params_rawstring                   = NULL;

static gchar  sink_params_rawstring_default[]           = "AUTOVIDEO";
static gchar* sink_params_rawstring                     = NULL;

static gchar  sndtransfer_params_rawstring_default[]    = "RTP:10.0.0.6:5000";
static gchar* sndtransfer_params_rawstring              = NULL;

static gchar  snd_subflows_params_rawstring_default[]   = "1:1:10.0.0.6:5000";
static gchar* snd_subflows_params_rawstring             = NULL;

static gchar  rcv_subflows_params_rawstring_default[]   = "1:1:5000";
static gchar* rcv_subflows_params_rawstring             = NULL;

static gchar  rcvtransfer_params_rawstring_default[]    = "RTP:5000";
static gchar* rcvtransfer_params_rawstring              = NULL;

static gchar* scheduler_params_rawstring                = NULL;
static gchar* playouter_params_rawstring                = NULL;

static gchar* stat_params_rawstring                     = NULL;
static gchar* sourcesink_params_rawstring               = NULL;
static gchar* encodersink_params_rawstring              = NULL;
static gchar* statlogs_sink_params_rawstring            = NULL;
static gchar* packetlogs_sink_params_rawstring          = NULL;
static gchar* extradelay_params_rawstring               = NULL;

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

    { "scheduler",  0, 0, G_OPTION_ARG_STRING, &scheduler_params_rawstring,    "scheduler",      NULL },
    { "playouter",  0, 0, G_OPTION_ARG_STRING, &playouter_params_rawstring,    "playouter",      NULL },

    { "stat",           0, 0, G_OPTION_ARG_STRING, &stat_params_rawstring,             "stat",            NULL },
    { "encodersink",    0, 0, G_OPTION_ARG_STRING, &encodersink_params_rawstring,      "encodersink",     NULL },
    { "sourcesink",     0, 0, G_OPTION_ARG_STRING,  &sourcesink_params_rawstring,      "sourcesink",      NULL },
    { "statlogsink",    0, 0, G_OPTION_ARG_STRING, &statlogs_sink_params_rawstring,    "statlogsink",     NULL },
    { "packetlogsink",  0, 0, G_OPTION_ARG_STRING, &packetlogs_sink_params_rawstring,  "packetlogsink",   NULL },
    { "extradelay",     0, 0, G_OPTION_ARG_STRING, &extradelay_params_rawstring,       "extradelay",      NULL },

    { "target_bitrate", 0, 0, G_OPTION_ARG_INT, &target_bitrate, "target_bitrate", NULL },

    { "info", 0, 0, G_OPTION_ARG_NONE, &info, "Info", NULL },
  { NULL }
};

Interface* make_interface(GstElement* element, gchar* padname);
void interface_dtor(Interface* this);
void connect_interfaces(Interface* source, Interface* sink);

SinkFileItem* sink_file_item_ctor();
void sink_file_item_dtor(SinkFileItem* this);
SinkFileItem* make_sink_file_item(gint id, gchar* path);


void _print_info(void);
gchar* _null_test(gchar *subject_str, gchar* failed_str);

void setup_framerate(gchar *string, Framerate* framerate);

SourceParams*      make_source_params(gchar* params_rawstring);
CodecParams*       make_codec_params (gchar* params_rawstring);
VideoParams*       make_video_params (gchar* params_rawstring);
StatParams*        make_stat_params (gchar* params_rawstring);
ExtraDelayParams*  make_extra_delay_params (gchar* params_rawstring);

TransferParams*  make_snd_transfer_params(gchar* params_rawstring);
TransferParams*  make_rcv_transfer_params(gchar* params_rawstring);
TransferParams* transfer_params_ref(TransferParams* transfer_params);
void transfer_params_unref(TransferParams *transfer_params);

SchedulerParams* make_scheduler_params(gchar* params_rawstring);
void free_scheduler_params(SchedulerParams *scheduler_params);

PlayouterParams*   make_playouter_params(gchar* params_rawstring);
void free_playouter_params(PlayouterParams *congestion_controller_params);

SinkParams*     make_sink_params(gchar* params_rawstring);
void free_sink_params(SinkParams* sink_params);

StatParamsTuple* make_statparams_tuple_by_raw_strings(gchar* statparams_rawstring,
                                       gchar* statlogs_sink_params_rawstring,
                                       gchar* packetlogs_sink_params_rawstring
                                      );

StatParamsTuple* make_statparams_tuple(StatParams* stat_params,
                                       SinkParams* statlogs_sink_params,
                                       SinkParams* packetlogs_sink_params
                                      );

void free_statparams_tuple(StatParamsTuple* statparams_tuple);

void on_fi_called(gpointer object, gpointer user_data);

Eventer* eventer_ctor(void);
void eventer_unref(Eventer* this);
Eventer* make_eventer(const gchar* event_name);
void eventer_add_subscriber_full(Eventer* this, subscriber subscriber_func, gpointer user_data);
void eventer_add_subscriber(Eventer* this, Subscriber* subscriber);
void eventer_do(Eventer* this, gpointer user_data);
Eventer* eventer_ref(Eventer* this);

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
  gpointer       object;
  GDestroyNotify dtor;
}ObjectsHolderItem;

typedef struct{
  GSList* holders;
}ObjectsHolder;

ObjectsHolder* objects_holder_ctor(void);
void object_holder_dtor(ObjectsHolder* holder);
void objects_holder_add(ObjectsHolder* this, gpointer object, GDestroyNotify dtor);

typedef struct{
  Eventer* on_target_bitrate_change;
  Eventer* on_assembled;
  Eventer* on_playing;
  Eventer* on_destroy;

  GSList*        objects;
  ObjectsHolder* objects_holder;
  GHashTable*    events_to_eventers;

}Pipeline;

Pipeline* get_pipeline(void);
void pipeline_dtor(void);

void pipeline_add_eventer(const gchar* event_name, Eventer* eventer);
void pipeline_add_subscriber_full(const gchar* event_name, subscriber subscriber_func, gpointer user_data);
void pipeline_add_subscriber(const gchar* event_name, Subscriber* subscriber);
void pipeline_firing_event(const gchar* event_name, gpointer data);

void pipeline_add_object(gpointer object, GDestroyNotify dtor);


#endif /* TESTS_PIPELINE_H_ */
