/*
 * fecdecoder.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FECDECODER_H_
#define FECDECODER_H_

#include <gst/gst.h>
#include "gstmprtpbuffer.h"
#include "rtpfecbuffer.h"

typedef struct _FECDecoder FECDecoder;
typedef struct _FECDecoderClass FECDecoderClass;

#define FECDECODER_TYPE             (fecdecoder_get_type())
#define FECDECODER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FECDECODER_TYPE,FECDecoder))
#define FECDECODER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FECDECODER_TYPE,FECDecoderClass))
#define FECDECODER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FECDECODER_TYPE))
#define FECDECODER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FECDECODER_TYPE))
#define FECDECODER_CAST(src)        ((FECDecoder *)(src))

#define FEC_PARITY_BYTES_MAX_LENGTH 1400

typedef struct _FECDecoderItem FECDecoderItem;

struct _FECDecoderItem
{
  GstClockTime         added;
  GstRTPFECSegment     segment;
  GstBuffer*           fec;
  guint16              processed;
  guint16              base_sn;
  guint16              expected_hsn;
  guint16              protected;
  gint32               sequences[16];
};

#define FECDECODER_MAX_ITEMS_NUM 200

struct _FECDecoder
{
  GObject                    object;
  GstClock*                  sysclock;
  GstClockTime               made;
  GRWLock                    rwmutex;
  FECDecoderItem*            items;
  gint32                     items_read_index;
  gint32                     items_write_index;
  gint32                     items_length;
  gint32                     counter;
  guint8                     payload_type;

};



struct _FECDecoderClass{
  GObjectClass parent_class;

};



GType fecdecoder_get_type (void);
FECDecoder *make_fecdecoder(void);
void fecdecoder_reset(FECDecoder *this);
GstBuffer* fecdecoder_repair_rtp(FECDecoder *this, guint16 sn);
void fecdecoder_set_payload_type(FECDecoder *this, guint8 fec_payload_type);
void fecdecoder_add_rtp_packet(FECDecoder *this, GstMpRTPBuffer* buffer);
void fecdecoder_add_fec_packet(FECDecoder *this, GstMpRTPBuffer *mprtp);
void fecdecoder_obsolate(FECDecoder *this);
#endif /* FECDECODER_H_ */
