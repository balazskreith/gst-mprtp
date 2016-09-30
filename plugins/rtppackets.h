/*
 * rtppackets.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef RTPPACKETS_H_
#define RTPPACKETS_H_

#include <gst/gst.h>
#include "sndsubflows.h"
#include "rcvsubflows.h"

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
  RTPPackets*          base_db;
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
  RTPPacket*                 packets;

  Observer*                  on_stalled_packets;

  guint8                     abs_time_ext_header_id;
  guint8                     mprtp_ext_header_id;

};


struct _RTPPacketsClass{
  GObjectClass parent_class;

};


GType rtppackets_get_type (void);
RTPPackets* make_rtppackets(void);
void rtppackets_reset(RTPPackets* this);

void rtppackets_add_stalled_packet_cb(RTPPackets* this, void (*callback)(gpointer udata, RTPPacket* packet), gpointer udata);

void _stalled_notify_caller(RTPPackets* this, gpointer item, gpointer udata);
RTPPacket* rtppackets_retrieve_packet_for_sending(RTPPackets* this, GstBuffer *buffer);
RTPPacket* rtppackets_retrieve_packet_at_receiving(RTPPackets* this, RcvSubflow* subflow, GstBuffer *buffer);

void rtppacket_setup_mprtp(RTPPacket *packet, SndSubflow* subflow);
void rtppacket_setup_abs_time_extension(RTPPacket* packet);

void rtppackets_packet_forwarded(RTPPackets* this, RTPPacket *packet);
RTPPacket* rtppackets_get_by_abs_seq(RTPPackets* this, guint16 abs_seq);


void rtppackets_set_abs_time_ext_header_id(RTPPackets* this, guint8 abs_time_ext_header_id);
guint8 rtppackets_get_abs_time_ext_header_id(RTPPackets* this);

void rtppackets_set_mprtp_ext_header_id(RTPPackets* this, guint8 mprtp_ext_header_id);
guint8 rtppackets_get_mprtp_ext_header_id(RTPPackets* this);

void rtppackets_packet_unref(RTPPacket *packet);
void rtppackets_packet_ref(RTPPacket *packet);

#endif /* RTPPACKETS_H_ */
