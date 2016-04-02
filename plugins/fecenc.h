/*
 * fecencoder.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FECENCODER_H_
#define FECENCODER_H_

#include <gst/gst.h>
#include "gstmprtpbuffer.h"
#include "rtpfecbuffer.h"
#include "mprtpspath.h"

typedef struct _FECEncoder FECEncoder;
typedef struct _FECEncoderClass FECEncoderClass;

#define FECENCODER_TYPE             (fecencoder_get_type())
#define FECENCODER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FECENCODER_TYPE,FECEncoder))
#define FECENCODER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FECENCODER_TYPE,FECEncoderClass))
#define FECENCODER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FECENCODER_TYPE))
#define FECENCODER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FECENCODER_TYPE))
#define FECENCODER_CAST(src)        ((FECEncoder *)(src))


struct _FECEncoder
{
  GObject                    object;
  GstClock*                  sysclock;
  GstClockTime               made;
  GRWLock                    rwmutex;
  GHashTable*                subflows;

  gint32                     max_protection_num;
  guint16                    seq_num;
  guint8                     payload_type;

  GQueue*                    bitstrings;
};



struct _FECEncoderClass{
  GObjectClass parent_class;

};


GType fecencoder_get_type (void);
FECEncoder *make_fecencoder(void);
void fecencoder_reset(FECEncoder *this);
void fecencoder_set_payload_type(FECEncoder *this, guint8 fec_payload_type);
void fecencoder_get_stats(FECEncoder *this, guint8 subflow_id, guint32 *packets, guint32 *payloads);
void fecencoder_add_rtpbuffer(FECEncoder *this, GstBuffer *buf);
void fecencoder_add_path(FECEncoder* this, MPRTPSPath *path);
void fecencoder_rem_path(FECEncoder* this, guint8 subflow_id);
GstBuffer* fecencoder_get_fec_packet(FECEncoder *this);
void fecencoder_assign_to_subflow (
    FECEncoder * this, GstBuffer *buf, guint8 mprtp_ext_header_id, guint8 subflow_id);
#endif /* FECENCODER_H_ */
