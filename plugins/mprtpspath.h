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

#define MPRTP_DEFAULT_EXTENSION_HEADER_ID 3
#define ABS_TIME_DEFAULT_EXTENSION_HEADER_ID 8
#define MONITOR_PAYLOAD_DEFAULT_ID 126
#define DELAY_SKEW_MAX (100 * GST_MSECOND)
//#define SUBFLOW_DEFAULT_SENDING_RATE 128000
#define SUBFLOW_DEFAULT_SENDING_RATE 150000

G_BEGIN_DECLS typedef struct _MPRTPSPath MPRTPSPath;
typedef struct _MPRTPSPathClass MPRTPSPathClass;

#define DISABLE_LINE if(0)

#include "gstmprtcpbuffer.h"
#include "packetssndqueue.h"
#include "percentiletracker.h"
#include "numstracker.h"
#include "monitorpackets.h"

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

#define MAX_INT32_POSPART 32767

struct _MPRTPSPath
{
  GObject   object;

  GRWLock                 rwmutex;
  gboolean                is_new;
  GstClock*               sysclock;
  guint8                  id;
  guint16                 seq;
  guint16                 cycle_num;
  guint8                  flags;
  guint32                 total_sent_packet_num;
  guint32                 total_sent_normal_packet_num;
  guint32                 total_sent_payload_bytes_sum;
  guint32                 total_sent_frames_num;
  guint32                 last_sent_frame_timestamp;
  guint                   abs_time_ext_header_id;
  guint                   mprtp_ext_header_id;

  GstClockTime            sent_passive;
  GstClockTime            sent_active;
  GstClockTime            sent_non_congested;
  GstClockTime            sent_middly_congested;
  GstClockTime            sent_congested;
  GstClockTime            skip_until;

  guint32                 ssrc_allowed;
  guint8                  sent_octets[MAX_INT32_POSPART];
  guint16                 sent_octets_read;
  guint16                 sent_octets_write;

  GstClockTime            last_packet_sent_time;
  guint32                 last_sent_payload_bytes;
  guint32                 monitoring_interval;
  guint16                 monitor_seq;
  guint8                  monitor_payload_type;
  MonitorPackets*         monitorpackets;
  guint8                  pivot_payload_type;

  void                    (*send_mprtp_packet_func)(gpointer, GstBuffer*);
  gpointer                  send_mprtp_func_data;

  GstClockTime            path_delay;

  guint32                 octets_in_flight_acked;
  gboolean                expected_lost;
  NumsTracker*            sent_bytes;
};

struct _MPRTPSPathClass
{
  GObjectClass parent_class;

  guint32      max_sent_timestamp;
};


typedef struct _RunningLengthEncodingRR RLERR;
struct _RunningLengthEncodingRR{
  guint64      values[MPRTP_PLUGIN_MAX_RLE_LENGTH];
  guint        length;
};

typedef struct _RRMeasurement RRMeasurement;
void g_print_rrmeasurement(RRMeasurement *measurement);
struct _RRMeasurement{
  GstClockTime        time;
  GstClockTime        RTT;
  guint32             jitter;
  guint32             cum_packet_lost;
  guint32             lost;
  gboolean            expected_lost;
  guint64             min_delay;
  guint64             max_delay;
  guint32             early_discarded_bytes;
  guint32             late_discarded_bytes;
  guint32             early_discarded_bytes_sum;
  guint32             late_discarded_bytes_sum;
  guint16             HSSN;
  guint16             cycle_num;
  guint16             expected_packets;
  guint16             PiT;
  guint32             expected_payload_bytes;
  guint32             sent_payload_bytes_sum;
  guint32             sent_payload_bytes;
  guint32             received_payload_bytes;
  gdouble             lost_rate;
  gdouble             goodput;
  gdouble             sender_rate;
  gint64              incoming_rate;
  gdouble             receiver_rate;
  gboolean            checked;

  gboolean            rfc7097_arrived;
  gboolean            rfc3611_arrived;
  gboolean            rfc7243_arrived;
  gboolean            owd_rle_arrived;

  RLERR               rle_discards;
  RLERR               rle_losts;
  RLERR               rle_delays;
  guint32             recent_discarded_bytes;
  GstClockTime        recent_delay;
  guint16             recent_lost;
  guint16             rfc3611_cum_lost;

  guint32             bytes_in_flight_acked;
  guint32             bytes_in_queue;
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
MPRTPSPath *make_mprtps_path (guint8 id, void (*send_func)(gpointer, GstBuffer*), gpointer func_this);

gboolean mprtps_path_is_new (MPRTPSPath * this);
void mprtps_path_set_not_new(MPRTPSPath * this);
gboolean mprtps_path_is_active (MPRTPSPath * this);
void mprtps_path_set_active (MPRTPSPath * this);
void mprtps_path_set_passive (MPRTPSPath * this);
gboolean mprtps_path_is_non_lossy (MPRTPSPath * this);
void mprtps_path_set_lossy (MPRTPSPath * this);
void mprtps_path_set_non_lossy (MPRTPSPath * this);
gboolean mprtps_path_is_non_congested (MPRTPSPath * this);
void mprtps_path_set_congested (MPRTPSPath * this);
void mprtps_path_set_non_congested (MPRTPSPath * this);
guint8 mprtps_path_get_id (MPRTPSPath * this);
gboolean mprtps_path_is_monitoring (MPRTPSPath * this);
guint32 mprtps_path_get_total_sent_packets_num (MPRTPSPath * this);
void mprtps_path_process_rtp_packet(MPRTPSPath * this, GstBuffer * buffer);
gboolean mprtps_path_has_expected_lost(MPRTPSPath * this);
guint32 mprtps_path_get_total_sent_payload_bytes (MPRTPSPath * this);
guint32 mprtps_path_get_total_sent_frames_num (MPRTPSPath * this);
guint32 mprtps_path_get_sent_octet_sum_for(MPRTPSPath *this, guint32 amount);
void mprtps_path_get_bytes_in_flight(MPRTPSPath *this, guint32 *acked);
guint32 mprtps_path_get_sent_bytes_in1s(MPRTPSPath *this);
guint32 mprtps_path_get_bytes_in_queue(MPRTPSPath *this);
guint8 mprtps_path_get_flags (MPRTPSPath * this);
guint16 mprtps_path_get_HSN(MPRTPSPath * this);
void mprtps_path_set_delay(MPRTPSPath * this, GstClockTime delay);
GstClockTime mprtps_path_get_delay(MPRTPSPath * this);
void mprtps_path_set_skip_duration(MPRTPSPath * this, GstClockTime duration);
void mprtps_path_set_monitor_payload_id(MPRTPSPath *this, guint8 payload_type);
void mprtps_path_set_monitor_packet_provider(MPRTPSPath *this, MonitorPackets *monitorpackets);
void mprtps_path_set_mprtp_ext_header_id(MPRTPSPath *this, guint ext_header_id);
G_END_DECLS
#endif /* MPRTPSPATH_H_ */
