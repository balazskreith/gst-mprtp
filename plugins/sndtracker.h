/*
 * sndtracker.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef SNDTRACKER_H_
#define SNDTRACKER_H_


#include <gst/gst.h>
#include "sndsubflows.h"
#include "sndpackets.h"
#include "fecenc.h"
#include "slidingwindow.h"

typedef struct _SndTracker SndTracker;
typedef struct _SndTrackerClass SndTrackerClass;
#define SNDTRACKER_TYPE             (sndtracker_get_type())
#define SNDTRACKER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SNDTRACKER_TYPE,SndTracker))
#define SNDTRACKER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SNDTRACKER_TYPE,SndTrackerClass))
#define SNDTRACKER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SNDTRACKER_TYPE))
#define SNDTRACKER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SNDTRACKER_TYPE))
#define SNDTRACKER_CAST(src)        ((SndTracker *)(src))


typedef struct _SndTrackerSndStat{

  gint32                    sent_bytes_in_1s;
  gint16                    sent_packets_in_1s;

  guint32                   total_sent_bytes;
  guint32                   total_sent_packets;

  gint32                    sent_fec_bytes_in_1s;
  gint32                    sent_fec_packets_in_1s;

  gint32                    bytes_in_flight;
  gint32                    packets_in_flight;

  gint32                    received_bytes_in_1s;
  gint32                    received_packets_in_1s;

  gint32                    acked_bytes_in_1s;
  gint32                    acked_packets_in_1s;

  guint32                   total_acked_bytes;
  guint32                   total_acked_packets;

  guint32                   total_received_bytes;
  guint32                   total_received_packets;
}SndTrackerStat;

typedef struct _RTPQueueStat{
  gint32                    bytes_in_queue;
  gint16                    packets_in_queue;
  GstClockTime              last_arrived;
  GstClockTime              delay_length;
}RTPQueueStat;

struct _SndTracker
{
  GObject                   object;
  GstClock*                 sysclock;
  SndSubflows*              subflows_db;
  SlidingWindow*            sent_sw;
  SlidingWindow*            acked_sw;
  SlidingWindow*            fec_sw;

  gpointer                  priv;
  SndTrackerStat            stat;
  RTPQueueStat              rtpqstat;

  Notifier*                 on_packet_sent;
  Notifier*                 on_packet_obsolated;
};


struct _SndTrackerClass{
  GObjectClass parent_class;
};


GType sndtracker_get_type (void);
SndTracker *make_sndtracker(SndSubflows* subflows_db);
void sndtracker_refresh(SndTracker * this);

void sndtracker_packet_sent(SndTracker * this, SndPacket* packet);
SndPacket* sndtracker_add_packet_to_rtpqueue(SndTracker* this, SndPacket* packet);
SndPacket* sndtracker_retrieve_sent_packet(SndTracker * this, guint8 subflow_id, guint16 subflow_seq);
void sndtracker_packet_found(SndTracker * this, SndPacket* packet);
void sndtracker_packet_acked(SndTracker * this, SndPacket* packet);
void sndtracker_add_fec_response(SndTracker * this, FECEncoderResponse *fec_response);
void sndtracker_add_on_packet_sent(SndTracker * this, ListenerFunc callback, gpointer udata);
void sndtracker_add_on_packet_obsolated(SndTracker * this, ListenerFunc callback, gpointer udata);
void sndtracker_add_on_packet_sent_with_filter(SndTracker * this, ListenerFunc callback, ListenerFilterFunc filter, gpointer udata);
void sndtracker_rem_on_packet_sent(SndTracker * this, ListenerFunc callback);

SndTrackerStat* sndtracker_get_stat(SndTracker * this);
SndTrackerStat* sndtracker_get_subflow_stat(SndTracker * this, guint8 subflow_id);
RTPQueueStat*   sndtracker_get_rtpqstat(SndTracker * this);
#endif /* SNDTRACKER_H_ */
