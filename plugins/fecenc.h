/*
 * fecencoder.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FECENCODER_H_
#define FECENCODER_H_

#include <gst/gst.h>
#include "rtpfecbuffer.h"

typedef struct _FECEncoder FECEncoder;
typedef struct _FECEncoderClass FECEncoderClass;

#define FECENCODER_TYPE             (fecencoder_get_type())
#define FECENCODER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FECENCODER_TYPE,FECEncoder))
#define FECENCODER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FECENCODER_TYPE,FECEncoderClass))
#define FECENCODER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FECENCODER_TYPE))
#define FECENCODER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FECENCODER_TYPE))
#define FECENCODER_CAST(src)        ((FECEncoder *)(src))

typedef struct{
  guint              ref;
  GstBuffer*         fecbuffer;
  gint32             payload_size;
  guint8             subflow_id;
}FECEncoderResponse;

struct _FECEncoder
{
  GObject                    object;
  GstClock*                  sysclock;
  GstClockTime               made;

  gint32                     max_protection_num;
  guint16                    seq_num;
  guint8                     payload_type;
  guint8                     mprtp_ext_header_id;

  GQueue*                    bitstrings;

  GstTask*                   thread;
  GRecMutex                  thread_mutex;
  GAsyncQueue*               requests;
  GAsyncQueue*               messages_out;

  SubflowSeqTrack*           seqtracks;
};



struct _FECEncoderClass{
  GObjectClass parent_class;

};


GType fecencoder_get_type (void);
FECEncoder *make_fecencoder(GAsyncQueue *responses);
void fecencoder_reset(FECEncoder *this);
void fecencoder_add_rtpbuffer(FECEncoder *this, GstBuffer* buffer);
void fecencoder_set_payload_type(FECEncoder* this, guint8 fec_payload_type);
void fecencoder_request_fec(FECEncoder* this, guint8 subflow_id);

void fecencoder_ref_response(FECEncoderResponse* response);
void fecencoder_unref_response(FECEncoderResponse* response);
#endif /* FECENCODER_H_ */
