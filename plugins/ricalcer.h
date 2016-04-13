/*
 * ricalcer.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef RICALCER_H_
#define RICALCER_H_

#include <gst/gst.h>
//#include "mprtpspath.h"

typedef struct _ReportIntervalCalculator ReportIntervalCalculator;
typedef struct _ReportIntervalCalculatorClass ReportIntervalCalculatorClass;

#define RICALCER_TYPE             (ricalcer_get_type())
#define RICALCER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),RICALCER_TYPE,ReportIntervalCalculator))
#define RICALCER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),RICALCER_TYPE,ReportIntervalCalculatorClass))
#define RICALCER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),RICALCER_TYPE))
#define RICALCER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),RICALCER_TYPE))
#define RICALCER_CAST(src)        ((ReportIntervalCalculator *)(src))

typedef enum{
  RTCP_INTERVAL_REGULAR_INTERVAL_MODE      = 0,
  RTCP_INTERVAL_EARLY_RTCP_MODE            = 1,
  RTCP_INTERVAL_IMMEDIATE_FEEDBACK_MODE    = 2,
}RTCPIntervalMode;

struct _ReportIntervalCalculator
{
  GObject          object;
  GRWLock          rwmutex;
  gdouble          media_rate;
  gboolean         sender_side;
  gdouble          avg_rtcp_size;
  gboolean         initialized;
  gboolean         allow_early;
  gboolean         urgent;
  gdouble          max_interval;
  gdouble          min_interval;
  gdouble          base_interval;
  GstClock*        sysclock;
  GstClockTime     last_time;
  GstClockTime     next_time;
  GstClockTime     actual_interval;
  gdouble          interval_spread;
  GstClockTime     urgent_time;

  GstClockTime     t_rr_last;

  RTCPIntervalMode  mode;
  void            (*interval_calcer)(ReportIntervalCalculator * this);
};

struct _ReportIntervalCalculatorClass{
  GObjectClass parent_class;
};

GType ricalcer_get_type (void);
ReportIntervalCalculator *make_ricalcer(gboolean sender_side);
void ricalcer_set_mode(ReportIntervalCalculator *this, RTCPIntervalMode mode);
gboolean ricalcer_rtcp_regular_allowed(ReportIntervalCalculator * this);
gboolean ricalcer_rtcp_fb_allowed(ReportIntervalCalculator * this);

void ricalcer_refresh_parameters(ReportIntervalCalculator * this, gdouble media_rate, gdouble avg_rtcp_size);
void ricalcer_urgent_report_request(ReportIntervalCalculator * this);

#endif /* RICALCER_H_ */
