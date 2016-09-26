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

typedef enum{
  RTP_PACKET_POSITION_ONSENDING = 1,
  RTP_PACKET_POSITION_RECEIVED  = 2,
}RTPPacketPosition;

struct _RTPPacket
{
  GstBuffer*           buffer;
  gint                 ref;
  GstClockTime         created;
  RTPPacketPosition    position;


  gboolean             marker;
  guint8               payload_type;
  guint16              abs_seq;
  guint32              timestamp;
  guint32              ssrc;

  guint                header_size;
  guint                payload_size;

  guint16              subflow_seq;
  guint8               subflow_id;
  GstClockTime         forwarded;

  union{
    struct{
      gboolean             acknowledged;
      gboolean             lost;
    }onsending_info;
    struct{
      guint64              abs_snd_ntp_time;
      guint64              abs_rcv_ntp_time;
      GstClockTime         delay;
    }received_info;
  };
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

  guint8                     mprtp_ext_header_id;
  guint8                     abs_time_ext_header_id;
  guint8                     fec_payload_type;
};



struct _RTPPacketsClass{
  GObjectClass parent_class;

};


GType rtppackets_get_type (void);

void rtppackets_add_stalled_notifier(RTPPackets* this, void (*callback)(gpointer udata, RTPPacket* packet), gpointer udata);
void rtppackets_add_subflow(RTPPackets* this, guint8 subflow_id);
void _stalled_notify_caller(RTPPackets* this, gpointer item, gpointer udata);
RTPPacket* rtppackets_retrieve_packet_for_sending(RTPPackets* this, GstBuffer *buffer);
gboolean rtppackets_buffer_is_mprtp(RTPPackets* this, GstBuffer *buffer);
gboolean rtppackets_buffer_is_fec(RTPPackets* this, GstBuffer *buffer);
RTPPacket* rtppackets_retrieve_packet_at_receiving(RTPPackets* this, GstBuffer *buffer);
void rtppackets_map_to_subflow(RTPPackets* this, RTPPacket *packet, guint8 subflow_id, guint16 subflow_seq);
void rtppackets_packet_forwarded(RTPPackets* this, RTPPacket *packet);
RTPPacket* rtppackets_get_by_abs_seq(RTPPackets* this, guint16 abs_seq);
RTPPacket* rtppackets_get_by_subflow_seq(RTPPackets* this, guint8 subflow_id, guint16 sub_seq);

void rtppackets_setup_packet_timestamp(RTPPackets *this, RTPPacket *packet);

void rtppackets_set_mprtp_ext_header_id(RTPPackets* this, guint8 mprtp_ext_header_id);
void rtppackets_set_abs_time_ext_header_id(RTPPackets* this, guint8 abs_time_ext_header_id);
guint8 rtppackets_get_mprtp_ext_header_id(RTPPackets* this);
guint8 rtppackets_get_abs_time_ext_header_id(RTPPackets* this);

void rtppackets_packet_unref(RTPPacket *packet);
void rtppackets_packet_ref(RTPPacket *packet);

#endif /* RTPPACKETS_H_ */
