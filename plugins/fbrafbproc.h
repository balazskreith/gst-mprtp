/*
 * fbrafbprocessor.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FBRAFBPROCESSOR_H_
#define FBRAFBPROCESSOR_H_

#include <gst/gst.h>

#include "lib_swplugins.h"
#include "notifier.h"
#include "sndtracker.h"
#include "sndsubflows.h"
#include "reportproc.h"


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
  GstClockTime             owd_50th;
  GstClockTime             last_owd;

  gint32                   measurements_num;
  gint32                   BiF_80th;
  gint32                   BiF_max;
  gint32                   BiF_std;
  gint32                   stalled_bytes;
  gint32                   bytes_in_flight;
  gint32                   sender_bitrate;
  gint32                   receiver_bitrate;
  gint32                   fec_bitrate;
  gdouble                  owd_log_corr;
  GstClockTime             owd_std;
  gdouble                  srtt;

  gdouble                  FL_in_1s;
  gdouble                  FL_50th;

}FBRAPlusStat;

typedef struct{
  guint   counter;
  gdouble mean; //the mean
  gdouble var;
  gdouble emp;
}FBRAPlusStdHelper;

typedef struct{
  GstClockTime owd;
  gint32       bytes_in_flight;
  gdouble      fraction_lost;
}FBRAPlusMeasurement;

struct _FBRAFBProcessor
{
  GObject                  object;
  GstClock*                sysclock;

  SlidingWindow*           short_sw;
  SlidingWindow*           long_sw;
  Recycle*                 measurements_recycle;

  FBRAPlusMeasurement      actual_measurement;
  FBRAPlusStat*            stat;
  Notifier*                on_report_processed;
  SndTracker*              sndtracker;
  SndSubflow*              subflow;

  guint                    rcved_fb_since_changed;
  gint32                   last_bytes_in_flight;
  GstClockTime             RTT;
  GstClockTime             srtt_updated;

  gint32                   BiF_min;
  GstClockTime             owd_min;
  GstClockTime             owd_max;

  gdouble                  FL_min;
  gdouble                  FL_max;

  FBRAPlusStdHelper        owd_std_helper;
  FBRAPlusStdHelper        BiF_std_helper;
  GstClockTime             last_report_updated;
  GstClockTime             last_owd_log;

};

struct _FBRAFBProcessorClass{
  GObjectClass parent_class;

};

GType fbrafbprocessor_get_type (void);
FBRAFBProcessor *make_fbrafbprocessor(SndTracker* sndtracker, SndSubflow* subflow, FBRAPlusStat* stat);

void fbrafbprocessor_reset(FBRAFBProcessor *this);
void fbrafbprocessor_approve_measurement(FBRAFBProcessor *this);
void fbrafbprocessor_time_update(FBRAFBProcessor *this);
void fbrafbprocessor_report_update(FBRAFBProcessor *this, GstMPRTCPReportSummary *summary);

#endif /* FBRAFBPROCESSOR_H_ */
