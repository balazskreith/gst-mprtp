/*
 * fractalfbprocessor.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FRACTALFBPROCESSOR_H_
#define FRACTALFBPROCESSOR_H_

#include <gst/gst.h>

#include "lib_swplugins.h"
#include "notifier.h"
#include "sndtracker.h"
#include "sndsubflows.h"
#include "reportproc.h"
#include "correlator.h"


typedef struct _FRACTaLFBProcessor FRACTaLFBProcessor;
typedef struct _FRACTaLFBProcessorClass FRACTaLFBProcessorClass;

#define FRACTALFBPROCESSOR_TYPE             (fractalfbprocessor_get_type())
#define FRACTALFBPROCESSOR(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FRACTALFBPROCESSOR_TYPE,FRACTaLFBProcessor))
#define FRACTALFBPROCESSOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FRACTALFBPROCESSOR_TYPE,FRACTaLFBProcessorClass))
#define FRACTALFBPROCESSOR_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FRACTALFBPROCESSOR_TYPE))
#define FRACTALFBPROCESSOR_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FRACTALFBPROCESSOR_TYPE))
#define FRACTALFBPROCESSOR_CAST(src)        ((FRACTALFBProcessor *)(src))

typedef struct _FRACTaLStat
{
  gint32                   measurements_num;
  gint32                   BiF_80th;
  gint32                   bytes_in_flight;
  gdouble                  rtpq_delay;
  gint32                   sender_bitrate;
  gint32                   receiver_bitrate;
  gint32                   fec_bitrate;


  gdouble                  BiF_avg;
  gdouble                  BiF_std;

  gdouble                  drift_avg;
  gdouble                  drift_std;
  gint64                   last_drift;
  gdouble                  srtt;

  gdouble                  sr_avg;
  gdouble                  rr_avg;

  gdouble                  lost_avg;
  gdouble                  lost_std;

  gdouble                  fraction_lost;
  gint32                   received_bytes_in_srtt;

  gdouble                  rr_sr_corr;
//  gdouble                  rr_sr_avg;
//  gdouble                  rr_sr_std;

//  gdouble                  est_queue_delay;
//  gdouble                  est_queue_std;
//  gdouble                  est_queue_avg;

}FRACTaLStat;

typedef struct{
  guint   counter;
  gdouble mean;
  gdouble var;
  gdouble emp;

  gdouble sum;
}FRACTaLStatHelper;


typedef struct{
  gint32       ref;
  gint32       drift;
  gdouble      fraction_lost;
//  gint32       newly_received_bytes;
//  gint32       newly_sent_bytes;
  gint32       sent_bytes_in_srtt;
  gint32       rcved_bytes_in_srtt;
  gint32       bytes_in_flight;

//  gint32       sent_kbps;
//  gint32       rcvd_kbps;

//  gdouble      sr_avg;
//  gdouble      rr_sr_corr;
//  gdouble      est_queue_delay;
}FRACTaLMeasurement;

struct _FRACTaLFBProcessor
{
  GObject                  object;
  GstClock*                sysclock;

  Correlator*              rr_sr_correlator;

  SlidingWindow*           srtt_sw;
  SlidingWindow*           short_sw;
  SlidingWindow*           long_sw;
  Recycle*                 measurements_recycle;

  FRACTaLStat*             stat;
  SndTracker*              sndtracker;
  SndSubflow*              subflow;
  FRACTaLMeasurement*      measurement;
  guint                    rcved_fb_since_changed;
  GstClockTime             RTT;
  GstClockTime             srtt_updated;

  gdouble                  FL_min;
  gdouble                  FL_max;
  gint32                   BiF_min;
  gint32                   BiF_max;

  gint32                   sent_bytes_in_srtt;
  gint32                   sent_bytes_in_srtt_t;

  FRACTaLStatHelper        BiF_stat_helper;
  FRACTaLStatHelper        drift_stat_helper;
  FRACTaLStatHelper        lost_stat_helper;
  GstClockTime             last_report_updated;
  GstClockTime             last_owd_log;

//  gint32                   newly_acked_packets;
//  gint32                   newly_received_packets;
  guint16                  HSN;

  gint64                  last_raise;
  gint64                  last_fall;
//  gint64                  queue_delay;

  gdouble                 est_received_bytes_in_srtt;

};

struct _FRACTaLFBProcessorClass{
  GObjectClass parent_class;

};

GType fractalfbprocessor_get_type (void);
FRACTaLFBProcessor *make_fractalfbprocessor(SndTracker* sndtracker, SndSubflow* subflow,
    FRACTaLStat* stat);

void fractalfbprocessor_reset(FRACTaLFBProcessor *this);
gint32 fractalfbprocessor_get_estimation(FRACTaLFBProcessor *this);
void fractalfbprocessor_start_estimation(FRACTaLFBProcessor *this);
void fractalfbprocessor_reset_short_sw(FRACTaLFBProcessor *this);
void fractalfbprocessor_approve_measurement(FRACTaLFBProcessor *this);
void fractalfbprocessor_time_update(FRACTaLFBProcessor *this);
void fractalfbprocessor_report_update(FRACTaLFBProcessor *this, GstMPRTCPReportSummary *summary);

#endif /* FRACTALFBPROCESSOR_H_ */
