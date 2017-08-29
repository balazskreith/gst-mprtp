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
  gdouble                  rtpq_delay;
  gint32                   sender_bitrate;
  gint32                   receiver_bitrate;
  gint32                   fec_bitrate;

  GstClockTime             queue_delay_50th;
  GstClockTime             last_queue_delay;
  GstClockTime             queue_delay_std;

  guint16                  HSN;

  gdouble                  srtt;
  gdouble                  skew_avg;

  gdouble                  sr_avg;
  gdouble                  rr_avg;

  gdouble                  fl_avg;
  gdouble                  fl_std;

  gdouble                  fraction_lost;
  gdouble                  ewi_in_s;
  gint32                   psi_received_bytes;//pipe stability indicator
  gint32                   psi_sent_bytes;
  gint32                   psi_extra_bytes;
//  gint32                   queued_bytes_in_srtt;
  guint16                  sent_packets_in_1s;

}FRACTaLStat;

//
//typedef struct{
//  gint32        ref;
//  GstClockTime  drift;
//  gdouble      fraction_lost;
//  gint32       newly_rcved_fb;
//  gint32       newly_queued_bytes;
//  gint32       bytes_in_flight;
//}FRACTaLMeasurement;

struct _FRACTaLFBProcessor
{
  GObject                  object;
  GstClock*                sysclock;
  TimestampGenerator*      ts_generator;
  Recycle*                 reference_point_recycle;

  gint32                   psi_received_packets;
  gint32                   psi_received_bytes;
  gint32                   psi_sent_bytes;
  gint32                   psi_sent_packets;
  gint32                   psi_lost_packets;
  gint32                   extra_bytes;
  guint32                  ewi_in_ts;
  guint32                  min_ewi_in_ts;
  guint32                  max_ewi_in_ts;
  guint32                  dts_50th;
  guint32                  min_dts;
  guint32                  rtt_in_ts;


  GstClockTime             rtt;
  GQueue*                  sent_packets;
  SlidingWindow*           reference_sw;
  SlidingWindow*           psi_sw;

  SlidingWindow*           ewi_sw;

  FRACTaLStat*             stat;
  SndTracker*              sndtracker;
  SndSubflow*              subflow;

  GstClockTime             last_report_update;

  guint16                  HSN;


  guint32                  srtt_in_ts;

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
