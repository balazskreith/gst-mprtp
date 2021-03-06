/*
 * fractalfbproducer.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FRACTALFBPRODUCER_H_
#define FRACTALFBPRODUCER_H_

#include <gst/gst.h>
#include "gstmprtcpbuffer.h"
#include "reportprod.h"
#include "lib_swplugins.h"

typedef struct _FRACTaLFBProducer      FRACTaLFBProducer;
typedef struct _FRACTaLFBProducerClass FRACTaLFBProducerClass;

#define FRACTALFBPRODUCER_TYPE             (fractalfbproducer_get_type())
#define FRACTALFBPRODUCER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FRACTALFBPRODUCER_TYPE,FRACTaLFBProducer))
#define FRACTALFBPRODUCER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FRACTALFBPRODUCER_TYPE,FRACTaLFBProducerClass))
#define FRACTALFBPRODUCER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FRACTALFBPRODUCER_TYPE))
#define FRACTALFBPRODUCER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FRACTALFBPRODUCER_TYPE))
#define FRACTALFBPRODUCER_CAST(src)        ((FRACTaLFBProducer *)(src))

typedef struct _CorrBlock CorrBlock;

#define FRACTALPRODUCER_CHUNKS_MAX_LENGTH 64

struct _FRACTaLFBProducer
{
  GObject                  object;
  GstClock*                sysclock;

  guint16                  cycle_num;
  gboolean                 initialized;

  RcvSubflow*              subflow;
  RcvTracker*              tracker;

  guint16                  begin_seq;
  guint16                  end_seq;

  SlidingWindow*           rle_sw;
  TimestampGenerator*      ts_generator;

  GstClockTime             last_fb;
  gint                     rcved_packets;

  GstRTCPXRChunk           chunks[FRACTALPRODUCER_CHUNKS_MAX_LENGTH];

//  gint32                   discarded_bytes;
  gint32                   received_bytes;



};

struct _FRACTaLFBProducerClass{
  GObjectClass parent_class;
};

GType fractalfbproducer_get_type (void);
FRACTaLFBProducer *make_fractalfbproducer(RcvSubflow* subflow, RcvTracker *tracker);
void fractalfbproducer_reset(FRACTaLFBProducer *this);


#endif /* FRACTALFBPRODUCER_H_ */
