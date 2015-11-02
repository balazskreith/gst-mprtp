/*
 * ricalcer.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef RICALCER_H_
#define RICALCER_H_

#include <gst/gst.h>

#include "mprtpspath.h"

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
  gdouble          media_rate;
  gboolean         sender_side;
  gdouble          avg_rtcp_size;
  gboolean         initialized;
  gboolean         allow_early;
  gboolean         urgent;
  GstClock*        sysclock;
  GstClockTime     next_time;
  GstClockTime     actual_interval;
  GstClockTime     last_interval;
  GstClockTime     urgent_time;
};

struct _ReportIntervalCalculatorClass{
  GObjectClass parent_class;
};


GType ricalcer_get_type (ReportIntervalCalculator *this);
ReportIntervalCalculator *make_ricalcer(gboolean sender_side);
gboolean ricalcer_do_report_now (ReportIntervalCalculator * this);
void ricalcer_do_next_report_time (ReportIntervalCalculator * this);
void ricalcer_refresh_parameters(ReportIntervalCalculator * this);
void ricalcer_urgent_report_request(ReportIntervalCalculator * this);
GstClockTime ricalcer_get_sum_last_two_interval(ReportIntervalCalculator * this);

#endif /* RICALCER_H_ */
