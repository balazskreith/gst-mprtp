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
#include "reportproc.h"
#include "rtppackets.h"

G_BEGIN_DECLS

typedef struct _MPRTPSPath MPRTPSPath;
typedef struct _MPRTPSPathClass MPRTPSPathClass;

#define MPRTPS_PATH_TYPE             (mprtps_path_get_type())
#define MPRTPS_PATH(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),MPRTPS_PATH_TYPE,MPRTPSPath))
#define MPRTPS_PATH_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),MPRTPS_PATH_TYPE,MPRTPSPathClass))
#define MPRTPS_PATH_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),MPRTPS_PATH_TYPE))
#define MPRTPS_PATH_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),MPRTPS_PATH_TYPE))
#define MPRTPS_PATH_CAST(src)        ((MPRTPSPath *)(src))


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
  gint32                  monitored_packets;

  GstClockTime            sent_passive;
  GstClockTime            sent_active;
  GstClockTime            sent_non_congested;
  GstClockTime            sent_lossy;
  GstClockTime            sent_congested;

  GstClockTime            last_sent;

  GstClockTime            skip_until;

  guint32                 monitoring_interval;

  guint32                 total_sent_packets_num;
  guint32                 total_sent_payload_bytes;

  MPRTPSPathState         actual_state;

  gboolean                expected_lost;

  GstClockTime            keep_alive_period;

  void                  (*packetstracker)(gpointer, guint, guint16);
  gpointer                packetstracker_data;

  gpointer                approval_data;
  gboolean              (*approval)(gpointer, RTPPacket *);
};

struct _MPRTPSPathClass
{
  GObjectClass parent_class;

  guint32      max_sent_timestamp;
};


GType mprtps_path_get_type (void);
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

void mprtps_path_set_state(MPRTPSPath * this, MPRTPSPathState new_state);
MPRTPSPathState mprtps_path_get_state(MPRTPSPath * this);

void mprtps_path_set_monitored_bitrate(MPRTPSPath * this, gint32 monitored_bitrate, gint32 monitored_packets);
gint32 mprtps_path_get_monitored_bitrate(MPRTPSPath * this, guint32 *packets_num);

void mprtps_path_set_congested (MPRTPSPath * this);
void mprtps_path_set_non_congested (MPRTPSPath * this);

gboolean mprtps_path_is_monitoring (MPRTPSPath * this);
gboolean mprtps_path_has_expected_lost(MPRTPSPath * this);

void mprtps_path_process_rtp_packet(MPRTPSPath * this, GstBuffer * buffer, gboolean *monitoring_request);

void mprtps_path_set_keep_alive_period(MPRTPSPath *this, GstClockTime period);
void mprtps_path_set_approval_process(MPRTPSPath *this, gpointer data, gboolean(*approval)(gpointer, RTPPacket *));
gboolean mprtps_path_approve_request(MPRTPSPath *this, RTPPacket *packet);

void mprtps_path_set_packetstracker(MPRTPSPath *this, void(*packetstracker)(gpointer,  guint, guint16), gpointer data);

gboolean mprtps_path_request_keep_alive(MPRTPSPath *this);

guint32 mprtps_path_get_total_sent_packets_num (MPRTPSPath * this);
guint32 mprtps_path_get_total_sent_payload_bytes (MPRTPSPath * this);

guint8 mprtps_path_get_flags (MPRTPSPath * this);
guint16 mprtps_path_get_actual_seq(MPRTPSPath * this);
void mprtps_path_set_skip_duration(MPRTPSPath * this, GstClockTime duration);
void mprtps_path_set_mprtp_ext_header_id(MPRTPSPath *this, guint ext_header_id);
void mprtps_path_set_monitoring_interval(MPRTPSPath *this, guint monitoring_interval);
G_END_DECLS
#endif /* MPRTPSPATH_H_ */
