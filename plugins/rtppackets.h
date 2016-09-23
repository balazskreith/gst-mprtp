/*
 * rtppackets.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef RTPPACKETS_H_
#define RTPPACKETS_H_

#include <gst/gst.h>

typedef struct _RTPPackets RTPPackets;
typedef struct _RTPPacketsClass RTPPacketsClass;

#define RTPPACKETS_TYPE             (rtppackets_get_type())
#define RTPPACKETS(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),RTPPACKETS_TYPE,RTPPackets))
#define RTPPACKETS_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),RTPPACKETS_TYPE,RTPPacketsClass))
#define RTPPACKETS_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),RTPPACKETS_TYPE))
#define RTPPACKETS_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),RTPPACKETS_TYPE))
#define RTPPACKETS_CAST(src)        ((RTPPackets *)(src))

typedef struct _RTPPacket RTPPacket;

struct _RTPPacket
{
  GstClockTime         added;
  GstBuffer*           buffer;
  guint32              timestamp;
  gint32               size;
  guint16              abs_seq;
  guint                ref;
  guint16              subflow_seq;
  guint8               subflow_id;
  GstClockTime         sent;
  guint                payload_size;
  guint8*              payload;
  guint                header_size;
  guint32              ssrc;
  gboolean             acknowledged;
  gboolean             lost;
};

struct _RTPPackets
{
  GObject                    object;
  GstClock*                  sysclock;
  GstClockTime               made;
  GRWLock                    rwmutex;
  RTPPacket*                 packets;
  RTPPacket**                subflows[256];
  GSList*                    stalled_notifiers;
};



struct _RTPPacketsClass{
  GObjectClass parent_class;

};


GType rtppackets_get_type (void);
void rtppackets_add_stalled_notifier(RTPPackets* this, void (*callback)(gpointer udata, RTPPacket* packet), gpointer udata);
void rtppackets_add_subflow(RTPPackets* this, guint8 subflow_id);
void _stalled_notify_caller(RTPPackets* this, gpointer item, gpointer udata);
RTPPacket* rtppackets_get_packet(RTPPackets* this, GstBuffer *buffer);
void rtppackets_map_to_subflow(RTPPackets* this, RTPPacket *packet, guint8 subflow_id, guint16 subflow_seq);
RTPPacket* rtppackets_add_with_wrlock(RTPPackets* this, GstBuffer *buffer);
void rtppackets_packet_sent(RTPPackets* this, RTPPacket *packet);
void rtppackets_item_sent_with_wrlock(RTPPackets* this, RTPPacket *packet);
RTPPacket* rtppackets_get_by_abs_seq(RTPPackets* this, guint16 abs_seq);
RTPPacket* rtppackets_get_by_subflow_seq(RTPPackets* this, guint8 subflow_id, guint16 sub_seq);


#endif /* RTPPACKETS_H_ */
