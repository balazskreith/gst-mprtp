/*
 * refctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FECENCODER_H_
#define FECENCODER_H_

#include <gst/gst.h>

#include "mprtpspath.h"
#include "rtpfecbuffer.h"

typedef struct _FECEncoder FECEncoder;
typedef struct _FECEncoderClass FECEncoderClass;

#define FECENCODER_TYPE             (fec_encoder_get_type())
#define FECENCODER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FECENCODER_TYPE,FECEncoder))
#define FECENCODER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FECENCODER_TYPE,FECEncoderClass))
#define FECENCODER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FECENCODER_TYPE))
#define FECENCODER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FECENCODER_TYPE))
#define FECENCODER_CAST(src)        ((FECEncoder *)(src))

#define FECPACKET_MAX_LENGTH 1400

struct _FECEncoder
{
  GObject                  object;
  GRWLock                  rwmutex;
  GstClock*                sysclock;
  guint8                   payload_type;
  guint16                  sequence;
  guint16                  cycle;

  guint16                  max_packets_num;
  guint16                  actual_packets_num;
  guint8                   parity_bytes[FECPACKET_MAX_LENGTH];
  guint16                  parity_bytes_length;
  gint32                   sn_base;
};

struct _FECEncoderClass{
  GObjectClass parent_class;
};

FECEncoder*
make_fec_encoder(void);

void
fec_encoder_set_payload_type(
    FECEncoder *this,
    guint8 payload_type);

void
fec_encoder_add_rtp_packet(
    FECEncoder *this,
    GstBuffer *buf);

GstBuffer*
fec_encoder_get_FEC_packet(
    FECEncoder *this,
    guint8 mprtp_ext_header_id,
    guint8 subflow_id);

GType fec_encoder_get_type (void);
#endif /* FECENCODER_H_ */
