/*
 * monitor.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef MONITOR_H_
#define MONITOR_H_

#include <gst/gst.h>
#include <gst/net/gstnetaddressmeta.h>
#include "recycle.h"
#include "slidingwindow.h"

typedef struct _Monitor Monitor;
typedef struct _MonitorClass MonitorClass;

#define MONITOR_TYPE             (monitor_get_type())
#define MONITOR(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),MONITOR_TYPE,Monitor))
#define MONITOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),MONITOR_TYPE,MonitorClass))
#define MONITOR_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),MONITOR_TYPE))
#define MONITOR_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),MONITOR_TYPE))
#define MONITOR_CAST(src)        ((Monitor *)(src))

typedef struct _MonitorPacket MonitorPacket;

typedef enum{
  MONITOR_PACKET_STATE_UNKNOWN   = 0,
  MONITOR_PACKET_STATE_RECEIVED  = 1,
  MONITOR_PACKET_STATE_LOST      = 2,
  MONITOR_PACKET_STATE_DISCARDED = 3,
}MonitorPacketStates;

typedef struct{
  gint32 total_bytes;
  gint32 total_packets;
  gint32 accumulative_bytes;
  gint32 accumulative_packets;
}TrackingStat;

typedef struct{
  TrackingStat discarded;
  TrackingStat received;
  TrackingStat repaired;
  TrackingStat corrupted;
  TrackingStat lost;
  TrackingStat fec;
}MonitorStat;

typedef void(*MonitorPacketAction)(Monitor*,MonitorPacket*);

struct _MonitorPacket
{
  guint32              extended_seq;
  MonitorPacketStates  state;
  guint64              tracked_ntp;
  guint16              tracked_seq;

  gboolean             marker;
  guint8               payload_type;
  guint32              timestamp;

  guint                header_size;
  guint                payload_size;

  TrackingStat*        tracker;
};


struct _Monitor
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;
  SlidingWindow*       packets_sw;
  MonitorPacket**      tracked_packets;
  Recycle*             recycle;

  guint16              cycle_num;
  guint16              tracked_hsn;
  gboolean             initialized;
  guint8               mprtp_ext_header_id;
  guint8               fec_payload_type;

  MonitorStat          stat;
  GQueue*              prepared_packets;

};


struct _MonitorClass{
  GObjectClass parent_class;

};


GType monitor_get_type (void);
Monitor* make_monitor(void);
void monitor_reset(Monitor* this);

void monitor_set_mprtp_ext_header_id(Monitor* this, guint8 mprtp_ext_header_id);
void monitor_set_fec_payload_type(Monitor* this, guint8 fec_payload_type);
void monitor_track_rtpbuffer(Monitor* this, GstBuffer* buffer);
MonitorPacket* monitor_pop_prepared_packet(Monitor* this);
void monitor_track_packetbuffer(Monitor* this, GstBuffer* buffer);
void monitor_setup_packetbufffer(MonitorPacket* packet, GstBuffer *buffer);
void monitor_setup_monitorstatbufffer(Monitor *this, GstBuffer *buffer);
void monitor_set_accumulation_length(Monitor* this, GstClockTime length);

#endif /* MONITOR_H_ */
