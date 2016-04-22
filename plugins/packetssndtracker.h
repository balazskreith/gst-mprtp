/*
 * packetssndtracker.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef PACKETSSNDTRACKER_H_
#define PACKETSSNDTRACKER_H_

#include <gst/gst.h>
#include "reportproc.h"

typedef struct _PacketsSndTracker PacketsSndTracker;
typedef struct _PacketsSndTrackerClass PacketsSndTrackerClass;

#define PACKETSSNDTRACKER_TYPE             (packetssndtracker_get_type())
#define PACKETSSNDTRACKER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),PACKETSSNDTRACKER_TYPE,PacketsSndTracker))
#define PACKETSSNDTRACKER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),PACKETSSNDTRACKER_TYPE,PacketsSndTrackerClass))
#define PACKETSSNDTRACKER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),PACKETSSNDTRACKER_TYPE))
#define PACKETSSNDTRACKER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),PACKETSSNDTRACKER_TYPE))
#define PACKETSSNDTRACKER_CAST(src)        ((PacketsSndTracker *)(src))


typedef struct _PacketsSndTrackerStat
{
  gint32                   bytes_in_flight;
  gint32                   packets_in_flight;
  gint32                   acked_bytes_in_1s;
  gint32                   acked_packets_in_1s;
  gint32                   sent_bytes_in_1s;
  gint32                   sent_packets_in_1s;
}PacketsSndTrackerStat;

struct _PacketsSndTracker
{
  GObject                  object;
  GRWLock                  rwmutex;
  GstClock*                sysclock;
  GQueue*                  sent;
  GQueue*                  sent_in_1s;
  GQueue*                  acked;

  gint32                   bytes_in_flight;
  gint32                   packets_in_flight;
  gint32                   acked_bytes_in_1s;
  gint32                   acked_packets_in_1s;
  gint32                   sent_bytes_in_1s;
  gint32                   sent_packets_in_1s;

  gint32                   actual_discarded_bytes;
  gint32                   actual_discarded_packets;

  guint16                  highest_discarded_seq;
};

struct _PacketsSndTrackerClass{
  GObjectClass parent_class;

};

GType packetssndtracker_get_type (void);
PacketsSndTracker *make_packetssndtracker(void);

void packetssndtracker_reset(PacketsSndTracker *this);
void packetssndtracker_add(PacketsSndTracker *this, guint payload_len, guint16 sn);
void packetssndtracker_get_stats (PacketsSndTracker * this, PacketsSndTrackerStat* result);
void packetssndtracker_update_hssn(PacketsSndTracker *this, guint16 hssn);
void packetssndtracker_add_discarded_bitvector(PacketsSndTracker *this,
                                               guint16 begin_seq,
                                               guint16 end_seq,
                                               GstRTCPXRBitvectorChunk *chunks);
guint32 packetssndtracker_get_goodput_bytes_from_acked(PacketsSndTracker *this, gdouble *fraction_utilized);
gint32 packetssndtracker_get_sent_bytes_in_1s(PacketsSndTracker *this);

#endif /* PACKETSSNDTRACKER_H_ */
