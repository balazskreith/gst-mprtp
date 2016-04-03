/*
 * mprtpssubflow.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef MPRTPSPATH_H_
#define MPRTPSPATH_H_

#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include "mprtpdefs.h"
#include "mprtplogger.h"
#include "gstmprtcpbuffer.h"
#include "packetssndqueue.h"
#include "percentiletracker.h"
#include "numstracker.h"
#include "reportproc.h"
#include "packetstracker.h"

G_BEGIN_DECLS

typedef struct _MPRTPSPath MPRTPSPath;
typedef struct _MPRTPSPathClass MPRTPSPathClass;
typedef struct _MPRTPSPathPackets MPRTPSPathPackets;
typedef struct _MPRTPSPathPacketsItem MPRTPSPathPacketsItem;
typedef struct _MPRTPSPathPacketsSummary MPRTPSPathPacketsSummary;

#define MPRTPS_PATH_TYPE             (mprtps_path_get_type())
#define MPRTPS_PATH(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),MPRTPS_PATH_TYPE,MPRTPSPath))
#define MPRTPS_PATH_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),MPRTPS_PATH_TYPE,MPRTPSPathClass))
#define MPRTPS_PATH_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),MPRTPS_PATH_TYPE))
#define MPRTPS_PATH_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),MPRTPS_PATH_TYPE))
#define MPRTPS_PATH_CAST(src)        ((MPRTPSPath *)(src))



typedef enum
{
  MPRTPS_PATH_FLAG_NON_LOSSY     = 1,
  MPRTPS_PATH_FLAG_NON_CONGESTED = 2,
  MPRTPS_PATH_FLAG_ACTIVE        = 4,
} MPRTPSPathFlags;


struct _MPRTPSPathPacketsItem{
  guint16      seq_num;
  guint16      payload_bytes;
  GstClockTime sent;
};

struct _MPRTPSPathPacketsSummary{
  gint32                   bytes_in_flight;
  gint32                   receiver_bitrate;
  gint32                   goodput_bitrate;
  guint32                  total_sent_packets_num;
  guint32                  total_sent_bytes;
};

struct _MPRTPSPathPackets
{
  gboolean                 activated;

  MPRTPSPathPacketsItem*   items;
  gint32                   length;
  gint32                   counter;
  gint32                   read_index;
  gint32                   write_index;
  gint32                   sent_obsolation_index;
  guint16                  last_hssn;
  gboolean                 unkown_last_hssn;

  gint32                   received_bytes_in_1s;
  gint32                   goodput_bytes_in_1s;
  gint32                   sent_bytes_in_1s;
  gint32                   bytes_in_flight;

  guint32                  last_sent_frame_timestamp;
  guint32                  total_sent_packets_num;
  guint32                  total_sent_payload_bytes;
  NumsTracker*             sent_bytes;
};

struct _MPRTPSPath
{
  GObject   object;

  GRWLock                 rwmutex;
  GstClock*               sysclock;
  guint8                  id;
  guint16                 seq;
  guint16                 cycle_num;
  guint8                  flags;
  guint                   abs_time_ext_header_id;
  guint                   mprtp_ext_header_id;

  gint32                  target_bitrate;
  gint32                  monitored_bitrate;

  GstClockTime            sent_passive;
  GstClockTime            sent_active;
  GstClockTime            sent_non_congested;
  GstClockTime            sent_lossy;
  GstClockTime            sent_congested;

  GstClockTime            last_sent;

  GstClockTime            skip_until;

  guint32                 monitoring_interval;

//  MPRTPSPathPackets       packets;
  PacketsTracker*         packetstracker;

  guint32                 total_sent_packets_num;
  guint32                 total_sent_payload_bytes;

  gboolean                expected_lost;

  GstClockTime            keep_alive_period;

  gpointer                approval_data;
  gboolean              (*approval)(gpointer, GstBuffer *);
};

struct _MPRTPSPathClass
{
  GObjectClass parent_class;

  guint32      max_sent_timestamp;
};


typedef struct _SubflowUtilization{
  gboolean   controlled;
  struct _SubflowUtilizationReport{
    gint32   lost_bytes;
    gint32   discarded_bytes;
    gint32   target_rate;
    gint32   sending_rate;
    guint64  owd;
    gdouble  rtt;
    gint     state;
  }report;
  struct _SubflowUtilizationControl{
    //values for congestion controlling
    gint32   max_rate;
    gint32   min_rate;


    //Todo: add this
    //gdouble  aggressivity;
  }control;
}SubflowUtilization;

typedef struct _MPRTPPluginUtilization{
  struct{
    gint32 target_rate;
  }report;
  struct{
    gint32  max_rate,min_rate;
    gdouble max_mtakover,max_stakover;
  }control;
  SubflowUtilization subflows[32];
}MPRTPPluginUtilization;


GType mprtps_path_get_type (void);
//MPRTPSPath *make_mprtps_path (guint8 id, void (*send_func)(gpointer, GstBuffer*), gpointer func_this);
MPRTPSPath *make_mprtps_path (guint8 id);

guint8 mprtps_path_get_id (MPRTPSPath * this);

gboolean mprtps_path_is_new (MPRTPSPath * this);
void mprtps_path_set_not_new(MPRTPSPath * this);

gboolean mprtps_path_is_active (MPRTPSPath * this);
void mprtps_path_set_active (MPRTPSPath * this);
void mprtps_path_set_passive (MPRTPSPath * this);

gboolean mprtps_path_is_non_lossy (MPRTPSPath * this);
void mprtps_path_set_lossy (MPRTPSPath * this);
void mprtps_path_set_non_lossy (MPRTPSPath * this);

gboolean mprtps_path_is_non_congested (MPRTPSPath * this);
void mprtps_path_set_target_bitrate(MPRTPSPath * this, gint32 target_bitrate);
gint32 mprtps_path_get_target_bitrate(MPRTPSPath * this);

void mprtps_path_set_monitored_bitrate(MPRTPSPath * this, gint32 monitored_bitrate);
gint32 mprtps_path_get_monitored_bitrate(MPRTPSPath * this);

void mprtps_path_set_congested (MPRTPSPath * this);
void mprtps_path_set_non_congested (MPRTPSPath * this);

gboolean mprtps_path_is_monitoring (MPRTPSPath * this);
gboolean mprtps_path_has_expected_lost(MPRTPSPath * this);

void mprtps_path_process_rtp_packet(MPRTPSPath * this, GstBuffer * buffer, gboolean *monitoring_request);

void mprtps_path_set_keep_alive_period(MPRTPSPath *this, GstClockTime period);
void mprtps_path_set_approval_process(MPRTPSPath *this, gpointer data, gboolean(*approval)(gpointer, GstBuffer *));
gboolean mprtps_path_approve_request(MPRTPSPath *this, GstBuffer *buf);
void mprtps_path_set_packets_tracker(MPRTPSPath *this, PacketsTracker *tracker);


gboolean mprtps_path_request_keep_alive(MPRTPSPath *this);

guint32 mprtps_path_get_total_sent_packets_num (MPRTPSPath * this);
guint32 mprtps_path_get_total_sent_payload_bytes (MPRTPSPath * this);

guint8 mprtps_path_get_flags (MPRTPSPath * this);
guint16 mprtps_path_get_actual_seq(MPRTPSPath * this);
void mprtps_path_set_skip_duration(MPRTPSPath * this, GstClockTime duration);
void mprtps_path_set_mprtp_ext_header_id(MPRTPSPath *this, guint ext_header_id);
void mprtps_path_set_monitor_packet_interval(MPRTPSPath *this, guint monitoring_interval);
G_END_DECLS
#endif /* MPRTPSPATH_H_ */
