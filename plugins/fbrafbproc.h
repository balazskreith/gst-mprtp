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

typedef struct _FBRAPlusStat
{
  gint32                   measurement_num;
  gint32                   BiF_80th;
  gint32                   BiF_max;
  gint32                   BiF_min;
  gint32                   BiF_std;
  gint32                   stalled_bytes;
  gint32                   sender_bitrate;
  gint32                   received_bitrate;
  GstClockTime             owd_80th;
  GstClockTime             owd_min;
  GstClockTime             owd_max;
  gdouble                  owd_log_corr;
  gdouble                  owd_log_std;
  gdouble                  owd_srtt_ratio;
  gdouble                  srtt;
}FBRAPlusStat;

typedef struct{
  GstClockTime owd;
  gint32       bytes_in_flight;
}FBRAPlusMeasurement;

struct _FBRAFBProcessor
{
  GObject                  object;
  GstClock*                sysclock;

  SlidingWindow*           short_sw;
  SlidingWindow*           long_sw;

  FBRAPlusMeasurement      actual_measurement;
  FBRAPlusStat*            stat;
  Observer*                on_report_processed;
  SndTracker*              sndtracker;
  SndSubflow*              subflow;

  guint                    measurements_num;
  gint32                   last_bytes_in_flight;
  GstClockTime             last_owd;
  GstClockTime             RTT;
  GstClockTime             srtt_updated;

};

struct _FBRAFBProcessorClass{
  GObjectClass parent_class;

};

GType fbrafbprocessor_get_type (void);
FBRAFBProcessor *make_fbrafbprocessor(SndTracker* sndtracker, SndSubflow* subflow, FBRAPlusStat* stat);

void fbrafbprocessor_reset(FBRAFBProcessor *this);
void fbrafbprocessor_approve_measurement(FBRAFBProcessor *this);
void fbrafbprocessor_update(FBRAFBProcessor *this, GstMPRTCPReportSummary *summary);

#endif /* FBRAFBPROCESSOR_H_ */
