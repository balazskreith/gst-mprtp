/*
 * rcvpackets.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef RCVPACKETS_H_
#define RCVPACKETS_H_

#include <gst/gst.h>
#include <gst/net/gstnetaddressmeta.h>
#include "rcvsubflows.h"
#include "recycle.h"


typedef struct _RcvPackets RcvPackets;
typedef struct _RcvPacketsClass RcvPacketsClass;

#define RCVPACKETS_TYPE             (rcvpackets_get_type())
#define RCVPACKETS(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),RCVPACKETS_TYPE,RcvPackets))
#define RCVPACKETS_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),RCVPACKETS_TYPE,RcvPacketsClass))
#define RCVPACKETS_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),RCVPACKETS_TYPE))
#define RCVPACKETS_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),RCVPACKETS_TYPE))
#define RCVPACKETS_CAST(src)        ((RcvPackets *)(src))

typedef struct _RcvPacket RcvPacket;

struct _RcvPacket
{
//  RcvPackets*          base_db;
  GstBuffer*           buffer;
  gint                 ref;
  GstClockTime         received;
  GstClockTime         delay;

  guint64              abs_snd_ntp_chunk;
  guint64              abs_snd_ntp_time;
  guint64              abs_rcv_ntp_time;

  gboolean             marker;
  guint8               payload_type;
  guint16              abs_seq;
  guint32              timestamp;
  guint32              ssrc;

  guint                header_size;
  guint                payload_size;

  guint16              subflow_seq;
  guint8               subflow_id;

  Recycle*             destiny;
};


struct _RcvPackets
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;

  Recycle*             recycle;

  guint8               abs_time_ext_header_id;
  guint8               mprtp_ext_header_id;

};


struct _RcvPacketsClass{
  GObjectClass parent_class;

};


GType rcvpackets_get_type (void);
RcvPackets* make_rcvpackets(void);
void rcvpackets_reset(RcvPackets* this);

RcvPacket* rcvpackets_get_packet(RcvPackets* this, GstBuffer* buffer);

void rcvpackets_set_abs_time_ext_header_id(RcvPackets* this, guint8 abs_time_ext_header_id);
guint8 rcvpackets_get_abs_time_ext_header_id(RcvPackets* this);

void rcvpackets_set_mprtp_ext_header_id(RcvPackets* this, guint8 mprtp_ext_header_id);
guint8 rcvpackets_get_mprtp_ext_header_id(RcvPackets* this);

void rcvpacket_unref(RcvPacket *packet);
RcvPacket* rcvpacket_ref(RcvPacket *packet);

typedef void (*printfnc)(const gchar* format, ...);
void rcvpacket_print(RcvPacket *packet, printfnc print);

#endif /* RCVPACKETS_H_ */
