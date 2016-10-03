/*
 * ricalcer.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef RICALCER_H_
#define RICALCER_H_

#include <gst/gst.h>
#include "sndsubflows.h"
#include "rcvsubflows.h"

typedef struct _ReportIntervalCalculator ReportIntervalCalculator;
typedef struct _ReportIntervalCalculatorClass ReportIntervalCalculatorClass;

#define RICALCER_TYPE             (ricalcer_get_type())
#define RICALCER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),RICALCER_TYPE,ReportIntervalCalculator))
#define RICALCER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),RICALCER_TYPE,ReportIntervalCalculatorClass))
#define RICALCER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),RICALCER_TYPE))
#define RICALCER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),RICALCER_TYPE))
#define RICALCER_CAST(src)        ((ReportIntervalCalculator *)(src))

struct _ReportIntervalCalculator
{
  GObject          object;
  GstClock*        sysclock;
  gboolean         sender_side;
};

struct _ReportIntervalCalculatorClass{
  GObjectClass parent_class;
};

GType ricalcer_get_type (void);
ReportIntervalCalculator *make_ricalcer(gboolean sender_side);
gboolean ricalcer_rtcp_fb_allowed(ReportIntervalCalculator * this, SndSubflow *subflow);
gboolean ricalcer_rtcp_regular_allowed_sndsubflow(ReportIntervalCalculator * this, SndSubflow *subflow);
gboolean ricalcer_rtcp_regular_allowed_rcvsubflow(ReportIntervalCalculator * this, RcvSubflow *subflow);

#endif /* RICALCER_H_ */
