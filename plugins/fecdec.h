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
  GstClockTime               repair_window_min;
  GstClockTime               repair_window_max;
  GstClockTime               made;
  GRWLock                    rwmutex;

  guint8                     payload_type;
  guint32                    total_early_repaired_bytes;
  guint32                    total_repaired_bytes;
  guint32                    total_lost_bytes;
  GList*                     segments;
  GList*                     items;
//  GList*                     requests;

  guint32                    lost;
  guint32                    recovered;
};



struct _FECDecoderClass{
  GObjectClass parent_class;

};



GType fecdecoder_get_type (void);
FECDecoder *make_fecdecoder(void);
void fecdecoder_reset(FECDecoder *this);
void fecdecoder_get_stat(FECDecoder *this,
                         guint32 *early_repaired_bytes,
                         guint32 *total_repaired_bytes,
                         gdouble *FFRE);
gboolean fecdecoder_has_repaired_rtpbuffer(FECDecoder *this, guint16 hpsn, GstBuffer** repairedbuf);
void fecdecoder_set_payload_type(FECDecoder *this, guint8 fec_payload_type);
void fecdecoder_set_repair_window(FECDecoder *this, GstClockTime min, GstClockTime max);
void fecdecoder_add_rtp_packet(FECDecoder *this, GstMpRTPBuffer* buffer);
void fecdecoder_add_fec_packet(FECDecoder *this, GstMpRTPBuffer *mprtp);
void fecdecoder_clean(FECDecoder *this);
#endif /* FECDECODER_H_ */
