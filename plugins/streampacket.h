/*
 * streampackets.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef STREAMPACKETS_H_
#define STREAMPACKETS_H_

#include <gst/gst.h>
#include <gst/net/gstnetaddressmeta.h>
#include "rcvsubflows.h"
#include "recycle.h"


typedef struct _StreamPackets StreamPackets;
typedef struct _StreamPacketsClass StreamPacketsClass;

#define STREAMPACKETS_TYPE             (streampackets_get_type())
#define STREAMPACKETS(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),STREAMPACKETS_TYPE,StreamPackets))
#define STREAMPACKETS_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),STREAMPACKETS_TYPE,StreamPacketsClass))
#define STREAMPACKETS_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),STREAMPACKETS_TYPE))
#define STREAMPACKETS_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),STREAMPACKETS_TYPE))
#define STREAMPACKETS_CAST(src)        ((StreamPackets *)(src))

typedef struct _StreamPacket StreamPacket;

struct _StreamPacket
{
//  StreamPackets*          base_db;
//  GstBuffer*           buffer;
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


struct _StreamPackets
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;

  Recycle*             recycle;

  guint8               abs_time_ext_header_id;
  guint8               mprtp_ext_header_id;

  guint32              pivot_clock_rate;
  GSocketAddress*      pivot_address;
  guint8               pivot_address_subflow_id;

};


struct _StreamPacketsClass{
  GObjectClass parent_class;

};


GType streampackets_get_type (void);
StreamPackets* make_streampackets(void);
void streampackets_reset(StreamPackets* this);

StreamPacket* streampackets_get_packet(StreamPackets* this, GstBuffer* buffer);

void streampackets_set_abs_time_ext_header_id(StreamPackets* this, guint8 abs_time_ext_header_id);
guint8 streampackets_get_abs_time_ext_header_id(StreamPackets* this);

void streampackets_set_mprtp_ext_header_id(StreamPackets* this, guint8 mprtp_ext_header_id);
guint8 streampackets_get_mprtp_ext_header_id(StreamPackets* this);

void streampacket_unref(StreamPacket *packet);
StreamPacket* streampacket_ref(StreamPacket *packet);

typedef void (*printfnc)(const gchar* format, ...);
void streampacket_print(StreamPacket *packet, printfnc print);

#endif /* STREAMPACKETS_H_ */
