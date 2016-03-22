/*
 * refctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FECDECODER_H_
#define FECDECODER_H_

#include <gst/gst.h>

#include "mprtpspath.h"
#include "rtpfecbuffer.h"
#include "numstracker.h"

typedef struct _FECDecoder FECDecoder;
typedef struct _FECDecoderClass FECDecoderClass;

#define FECDECODER_TYPE             (fec_decoder_get_type())
#define FECDECODER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FECDECODER_TYPE,FECDecoder))
#define FECDECODER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FECDECODER_TYPE,FECDecoderClass))
#define FECDECODER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FECDECODER_TYPE))
#define FECDECODER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FECDECODER_TYPE))
#define FECDECODER_CAST(src)        ((FECDecoder *)(src))

#define FECPACKET_MAX_LENGTH 1400
#define FECPACKETITEMS_LENGTH 200

typedef struct _FECPacketItem{
  GstBuffer   *buffer;
  guint16      sn_base;
}FECPacketItem;

struct _FECDecoder
{
  GObject                  object;
  GRWLock                  rwmutex;
  GstClock*                sysclock;
  guint8                   payload_type;
  guint16                  sequence;
  guint16                  cycle;

  guint16                  HPSN;
  gboolean                 HPSN_init;
  guint16                  max_packets_num;
  guint16                  actual_packets_num;
  guint8                   parity_bytes[FECPACKET_MAX_LENGTH];
  guint16                  parity_bytes_length;
  gint32                   sn_base;
  GstClockTime             repair_window;

  FECPacketItem            items[FECPACKETITEMS_LENGTH];
  guint16                  items_read_index;
  guint16                  items_write_index;
  guint16                  items_counter;
};

struct _FECDecoderClass{
  GObjectClass parent_class;
};

FECDecoder*
make_fec_decoder(void);

void
fec_decoder_set_payload_type(
    FECDecoder *this,
    guint8 payload_type);


void
fec_decoder_set_repair_window(
    FECDecoder *this,
    GstClockTime repair_window);

void
fec_decoder_add_FEC_packet(
    FECDecoder *this,
    GstBuffer *buf);

GstBuffer*
fec_decoder_get_RTP_packet(
    FECDecoder *this,
    guint8 mprtp_ext_header_id,
    guint8 subflow_id);

GType fec_decoder_get_type (void);
#endif /* FECDECODER_H_ */
