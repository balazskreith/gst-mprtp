/*
 * sndpackets.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef SNDPACKETS_H_
#define SNDPACKETS_H_

#include <gst/gst.h>
#include <gst/net/gstnetaddressmeta.h>
#include "sndsubflows.h"
#include "rcvsubflows.h"
#include "recycle.h"

typedef struct _SndPackets SndPackets;
typedef struct _SndPacketsClass SndPacketsClass;

typedef gboolean(*SndPacketFilterCb)(GstBuffer* rtp);

#define SNDPACKETS_TYPE             (sndpackets_get_type())
#define SNDPACKETS(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SNDPACKETS_TYPE,SndPackets))
#define SNDPACKETS_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SNDPACKETS_TYPE,SndPacketsClass))
#define SNDPACKETS_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SNDPACKETS_TYPE))
#define SNDPACKETS_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SNDPACKETS_TYPE))
#define SNDPACKETS_CAST(src)        ((SndPackets *)(src))

typedef enum{
  SNDPACKET_IFRAME_FILTER_MODE_NONE = 0,
  SNDPACKET_IFRAME_FILTER_MODE_VP8 = 1,
}SndPacketIFrameFilters;

typedef struct _SndPacket
{
//  SndPackets*          base_db;
  GstBuffer*           buffer;
  gint                 ref;
  GstClockTime         made;
  GstClockTime         sent;
  GstClockTime         queued;

  gboolean             marker;
  guint8               payload_type;
  guint16              abs_seq;
  guint32              timestamp;
  guint32              ssrc;

  guint                header_size;
  guint                payload_size;

  guint16              subflow_seq;
  guint8               subflow_id;
  gpointer             schnode;
  GstClockTime         scheduled;

  gboolean             lost;
  gboolean             acknowledged;

  guint8               abs_time_ext_header_id;
  guint8               mprtp_ext_header_id;
  Recycle*             destiny;

  gboolean             keyframe;
  guint32              sent_ts;
  guint32              rcvd_ts;
  gint64               skew;
}SndPacket;


struct _SndPackets
{
  GObject                    object;
  GstClock*                  sysclock;
  GstClockTime               made;

  Recycle*                   recycle;
  SndPacketFilterCb          keyframe_filtercb;

  guint8                     abs_time_ext_header_id;
  guint8                     mprtp_ext_header_id;

};


struct _SndPacketsClass{
  GObjectClass parent_class;

};


GType sndpackets_get_type (void);
SndPackets* make_sndpackets(void);
void sndpackets_reset(SndPackets* this);

SndPacket* sndpackets_make_packet(SndPackets* this, GstBuffer* buffer);
SndPacket* sndpackets_get_by_abs_seq(SndPackets* this, guint16 abs_seq);

void sndpacket_setup_mprtp(SndPacket *packet, guint8 subflow_id, guint16 subflow_seq);
void sndpackets_set_keyframe_filter_mode(SndPackets* this, guint filter_mode);
void sndpackets_set_abs_time_ext_header_id(SndPackets* this, guint8 abs_time_ext_header_id);
guint8 sndpackets_get_abs_time_ext_header_id(SndPackets* this);

void sndpackets_set_mprtp_ext_header_id(SndPackets* this, guint8 mprtp_ext_header_id);
guint8 sndpackets_get_mprtp_ext_header_id(SndPackets* this);

GstBuffer* sndpacket_retrieve(SndPacket* packet);
void sndpacket_unref(SndPacket *packet);
SndPacket* sndpacket_ref(SndPacket *packet);

#endif /* SNDPACKETS_H_ */
