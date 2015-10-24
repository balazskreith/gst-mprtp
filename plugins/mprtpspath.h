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
#include "gstmprtcpbuffer.h"

#define MPRTP_DEFAULT_EXTENSION_HEADER_ID 3
#define ABS_TIME_DEFAULT_EXTENSION_HEADER_ID 8

G_BEGIN_DECLS typedef struct _MPRTPSPath MPRTPSPath;
typedef struct _MPRTPSPathClass MPRTPSPathClass;
typedef struct _MPRTPSubflowHeaderExtension MPRTPSubflowHeaderExtension;
typedef struct _RTPAbsTimeExtension RTPAbsTimeExtension;

#define MPRTPS_PATH_TYPE             (mprtps_path_get_type())
#define MPRTPS_PATH(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),MPRTPS_PATH_TYPE,MPRTPSPath))
#define MPRTPS_PATH_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),MPRTPS_PATH_TYPE,MPRTPSPathClass))
#define MPRTPS_PATH_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),MPRTPS_PATH_TYPE))
#define MPRTPS_PATH_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),MPRTPS_PATH_TYPE))
#define MPRTPS_PATH_CAST(src)        ((MPRTPSPath *)(src))



struct _MPRTPSubflowHeaderExtension
{
  guint8 id;
  guint16 seq;
};

struct _RTPAbsTimeExtension
{
  guint8 time[3];
};


typedef enum
{
  MPRTPS_PATH_MARKER_OVERUSED   = -1,
  MPRTPS_PATH_MARKER_STABLE     =  0,
  MPRTPS_PATH_MARKER_UNDERUSED  =  1,
} MPRTPPathMarker;


typedef enum
{
  MPRTPS_PATH_FLAG_TRIAL = 1,
  MPRTPS_PATH_FLAG_NON_LOSSY = 2,
  MPRTPS_PATH_FLAG_NON_CONGESTED = 4,
  MPRTPS_PATH_FLAG_ACTIVE = 8,
} MPRTPPathFlags;

typedef enum{
  MPRTPS_PATH_STATE_NON_CONGESTED    = 1,
  MPRTPS_PATH_STATE_MIDDLY_CONGESTED = 2,
  MPRTPS_PATH_STATE_CONGESTED        = 4,
  MPRTPS_PATH_STATE_PASSIVE          = 8,
}MPRTPSPathState;

#define MAX_INT32_POSPART 32767

struct _MPRTPSPath
{
  GObject   object;

  GRWLock   rwmutex;
  gboolean  is_new;
  GstClock* sysclock;
  guint8    id;
  guint16   seq;
  guint16   cycle_num;
  guint8    state;
  guint32   total_sent_packet_num;
  guint32   total_sent_payload_bytes_sum;

  GstClockTime  sent_passive;
  GstClockTime  sent_active;
  GstClockTime  sent_non_congested;
  GstClockTime  sent_middly_congested;
  GstClockTime  sent_congested;

  guint8        sent_octets[MAX_INT32_POSPART];
  guint16       sent_octets_read;
  guint16       sent_octets_write;
  guint32       max_bytes_per_ms;
  GstClockTime  last_packet_sent_time;
  guint32       last_sent_payload_bytes;
};

struct _MPRTPSPathClass
{
  GObjectClass parent_class;
};



GType mprtps_path_get_type (void);
MPRTPSPath *make_mprtps_path (guint8 id);

gboolean mprtps_path_is_new (MPRTPSPath * this);
void mprtps_path_set_not_new(MPRTPSPath * this);
void mprtps_path_set_trial_end (MPRTPSPath * this);
void mprtps_path_set_trial_begin (MPRTPSPath * this);
gboolean mprtps_path_is_in_trial (MPRTPSPath * this);
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
guint32 mprtps_path_get_total_sent_packets_num (MPRTPSPath * this);
void mprtps_path_process_rtp_packet (MPRTPSPath * this, guint ext_header_id, GstRTPBuffer * rtp);
guint32 mprtps_path_get_total_sent_payload_bytes (MPRTPSPath * this);
guint32 mprtps_path_get_sent_octet_sum_for(MPRTPSPath *this, guint32 amount);

void mprtps_path_set_underused(MPRTPSPath * this);

MPRTPSPathState mprtps_path_get_state (MPRTPSPath * this);
void mprtps_path_set_state (MPRTPSPath * this, MPRTPSPathState state);
GstClockTime mprtps_path_get_time_sent_to_passive(MPRTPSPath *this);
GstClockTime mprtps_path_get_time_sent_to_middly_congested (MPRTPSPath * this);
GstClockTime mprtps_path_get_time_sent_to_non_congested (MPRTPSPath * this);
GstClockTime mprtps_path_get_time_sent_to_congested (MPRTPSPath * this);
void mprtps_path_set_max_bytes_per_ms(MPRTPSPath *this, guint32 bytes);
gboolean mprtps_path_is_overused (MPRTPSPath * this);
G_END_DECLS
#endif /* MPRTPSPATH_H_ */
