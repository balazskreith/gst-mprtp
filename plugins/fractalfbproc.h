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
#include "bucket.h"
#include "linreger.h"
#include "thresholdfinder.h"
#include "stdcalcer.h"
#include "qdelaystabilitycalcer.h"
#include "flstabcalcer.h"


typedef struct _FRACTaLFBProcessor FRACTaLFBProcessor;
typedef struct _FRACTaLFBProcessorClass FRACTaLFBProcessorClass;

#define FRACTALFBPROCESSOR_TYPE             (fractalfbprocessor_get_type())
#define FRACTALFBPROCESSOR(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FRACTALFBPROCESSOR_TYPE,FRACTaLFBProcessor))
#define FRACTALFBPROCESSOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FRACTALFBPROCESSOR_TYPE,FRACTaLFBProcessorClass))
#define FRACTALFBPROCESSOR_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FRACTALFBPROCESSOR_TYPE))
#define FRACTALFBPROCESSOR_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FRACTALFBPROCESSOR_TYPE))
#define FRACTALFBPROCESSOR_CAST(src)        ((FRACTALFBProcessor *)(src))

typedef struct {
  guint length;
  GQueue* pushed_items;
  guint *vector;
  GstClockTime window_size;
}BucketList;

typedef struct _FRACTaLStat
{
  gint32                   measurements_num;
  gdouble                  rtpq_delay;
  gint32                   sender_bitrate;
  gint32                   receiver_bitrate;
  gint32                   fec_bitrate;
  gint32                   rcved_bytes_in_ewi;

  guint16                  HSN;

  gdouble                  srtt;

  gdouble                  sr_avg;
  gdouble                  rr_avg;

  gdouble                  rr_hat;
  gdouble                  drr;

  gdouble                  fl_stability;

  gdouble                  FL_th;
  gdouble                  fraction_lost;
  gdouble                  fraction_lost_avg;
  gdouble                  ewi_in_s;
  guint16                  sent_packets_in_1s;

  gdouble                  qdelay_stability;
  gboolean                 qdelay_is_stable;
  gdouble                  drate_avg;
  gdouble                  drate_stability;

  gdouble avg_qd;
  gint qd_min, qd_max;
  gint lost_or_discarded;
  gint arrived_packets;


}FRACTaLStat;


struct _FRACTaLFBProcessor
{
  GObject                  object;
  GstClock*                sysclock;
  TimestampGenerator*      ts_generator;
  QDelayStabilityCalcer*   qdelay_stability_calcer;
  FLStabilityCalcer*       fl_stability_calcer;
  GstClockTime             made;

  guint32                  ewi_in_ts;
  guint32                  min_ewi_in_ts;
  guint32                  max_ewi_in_ts;
  guint32                  min_dts;
  guint32                  min_qts;
  guint32                  rtt_in_ts;

  GstClockTime             dts;
  GstClockTime             rtt;

  FRACTaLStat*             stat;
  SndTracker*              sndtracker;
  SndSubflow*              subflow;

  GstClockTime             last_report_update;
  GstClockTime             first_report_update;

  guint16                  HSN;

  guint32                  srtt_in_ts;
  gdouble                  qts_std, min_qts_std;
  gdouble                  last_qts;

  guint32                  last_dts;
  gdouble                  fb_interval_avg;

  guint16 cc_begin_seq, cc_end_seq;

};


struct _FRACTaLFBProcessorClass{
  GObjectClass parent_class;

};

GType fractalfbprocessor_get_type (void);
void fractalfbprocessor_set_evaluation_window_margins(FRACTaLFBProcessor *this, GstClockTime min, GstClockTime max);
FRACTaLFBProcessor *make_fractalfbprocessor(SndTracker* sndtracker, SndSubflow* subflow,
    FRACTaLStat* stat);

void fractalfbprocessor_reset(FRACTaLFBProcessor *this);
gint32 fractalfbprocessor_get_estimation(FRACTaLFBProcessor *this);
void fractalfbprocessor_start_estimation(FRACTaLFBProcessor *this);
void fractalfbprocessor_approve_feedback(FRACTaLFBProcessor *this);
void fractalfbprocessor_time_update(FRACTaLFBProcessor *this);
void fractalfbprocessor_report_update(FRACTaLFBProcessor *this, GstMPRTCPReportSummary *summary);

#endif /* FRACTALFBPROCESSOR_H_ */
