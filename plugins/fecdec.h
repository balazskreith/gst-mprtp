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

typedef struct _FECDecoder FECDecoder;
typedef struct _FECDecoderClass FECDecoderClass;

#define FECDECODER_TYPE             (fecdecoder_get_type())
#define FECDECODER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FECDECODER_TYPE,FECDecoder))
#define FECDECODER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FECDECODER_TYPE,FECDecoderClass))
#define FECDECODER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FECDECODER_TYPE))
#define FECDECODER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FECDECODER_TYPE))
#define FECDECODER_CAST(src)        ((FECDecoder *)(src))

typedef struct _FECDecoderSegment
{
  GstClockTime         added;
  guint16              base_sn;
  guint16              high_sn;
  guint16              protected;
  gint32               missing;
  guint32              ssrc;
  gboolean             complete;
  gboolean             repaired;
  guint8               fecbitstring[GST_RTPFEC_PARITY_BYTES_MAX_LENGTH];
  gint16               fecbitstring_length;
  GList*               items;

}FECDecoderSegment;

typedef struct _FECDecoderItem
{
  GstClockTime         added;
  guint8               bitstring[GST_RTPFEC_PARITY_BYTES_MAX_LENGTH];
  gint16               bitstring_length;
  guint16              seq_num;
  guint32              ssrc;
}FECDecoderItem;


struct _FECDecoder
{
  GObject                    object;
  GstClock*                  sysclock;
  GstTask*                   thread;
  GRecMutex                  thread_mutex;
  GstClockTime               made;
  GList*                     segments;
  GList*                     items;

  Messenger*                 messenger;
  Notifier*                  on_response;

};



struct _FECDecoderClass{
  GObjectClass parent_class;

};

GType fecdecoder_get_type (void);
FECDecoder *make_fecdecoder(void);
void fecdecoder_reset(FECDecoder *this);

void fecdecoder_request_repair(FECDecoder *this, guint16 missing_seq);
void fecdecoder_add_response_listener(FECDecoder *this, ListenerFunc listener, gpointer udata);
void fecdecoder_add_rtp_buffer(FECDecoder *this, GstBuffer *packet);
void fecdecoder_add_fec_buffer(FECDecoder *this, GstBuffer *buffer);

#endif /* FECDECODER_H_ */
