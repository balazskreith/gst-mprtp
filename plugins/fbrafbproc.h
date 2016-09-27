/*
 * fbrafbprocessor.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FBRAFBPROCESSOR_H_
#define FBRAFBPROCESSOR_H_

#include <gst/gst.h>
#include "reportproc.h"
#include "lib_swplugins.h"

typedef struct _FBRAFBProcessor FBRAFBProcessor;
typedef struct _FBRAFBProcessorClass FBRAFBProcessorClass;

#define FBRAFBPROCESSOR_TYPE             (fbrafbprocessor_get_type())
#define FBRAFBPROCESSOR(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FBRAFBPROCESSOR_TYPE,FBRAFBProcessor))
#define FBRAFBPROCESSOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FBRAFBPROCESSOR_TYPE,FBRAFBProcessorClass))
#define FBRAFBPROCESSOR_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FBRAFBPROCESSOR_TYPE))
#define FBRAFBPROCESSOR_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FBRAFBPROCESSOR_TYPE))
#define FBRAFBPROCESSOR_CAST(src)        ((FBRAFBProcessor *)(src))

typedef struct{
  guint        ref;
  GstClockTime owd;
  gint32       bytes_in_flight;
  gint32       fec_bitrate;
}FBRAPlusMeasurement;

struct _FBRAFBProcessor
{
  GObject                  object;
  GstClock*                sysclock;
  gpointer                 last_statitem;
  SlidingWindow           *feedbacks_long_sw;
  FBRAPlusMeasurement      actual_measurement;
  FBRAPlusStat            *stat;
  Observer*                on_report_processed;
};

struct _FBRAFBProcessorClass{
  GObjectClass parent_class;

};

GType fbrafbprocessor_get_type (void);
FBRAFBProcessor *make_fbrafbprocessor(void);

void fbrafbprocessor_reset(FBRAFBProcessor *this);
void fbrafbprocessor_approve_measurement(FBRAFBProcessor *this);
void fbrafbprocessor_update(FBRAFBProcessor *this, GstMPRTCPReportSummary *summary);

#endif /* FBRAFBPROCESSOR_H_ */
