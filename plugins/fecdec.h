/*
 * fecdecoder.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FECDECODER_H_
#define FECDECODER_H_

#include <gst/gst.h>
#include "rcvpackets.h"
#include "rtpfecbuffer.h"
#include "mediator.h"
#include "messenger.h"
#include "gstmprtcpbuffer.h"

typedef struct _FECDecoder FECDecoder;
typedef struct _FECDecoderClass FECDecoderClass;

#define FECDECODER_TYPE             (fecdecoder_get_type())
#define FECDECODER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FECDECODER_TYPE,FECDecoder))
#define FECDECODER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FECDECODER_TYPE,FECDecoderClass))
#define FECDECODER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FECDECODER_TYPE))
#define FECDECODER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FECDECODER_TYPE))
#define FECDECODER_CAST(src)        ((FECDecoder *)(src))

struct _FECDecoder
{
  GObject       object;
  GMutex        mutex;
  GstClockTime  made;
  GstClock*     sysclock;
  GQueue*       rcv_packets;
  GQueue*       fec_recycle;
  GQueue*       fec_packets;
};


struct _FECDecoderClass{
  GObjectClass parent_class;

};

GType fecdecoder_get_type (void);
FECDecoder *make_fecdecoder(void);
void fecdecoder_reset(FECDecoder *this);

GstBuffer* fecdecoder_pop_rtp_packet(FECDecoder *this, guint16 seq_num);
void fecdecoder_push_rcv_packet(FECDecoder *this, RcvPacket *packet);
void fecdecoder_push_fec_buffer(FECDecoder *this, GstBuffer *buffer);

#endif /* FECDECODER_H_ */
