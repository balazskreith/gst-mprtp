/*
 * subratectrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef SUBRATECTRLER_H_
#define SUBRATECTRLER_H_

#include <gst/gst.h>
#include "mprtpspath.h"
#include "bintree.h"
#include "sndratedistor.h"
#include "marcfbproc.h"
#include "reportproc.h"


typedef struct _SubflowRateController SubflowRateController;
typedef struct _SubflowRateControllerClass SubflowRateControllerClass;
//typedef struct _SubflowMeasurement SubflowMeasurement;

#define SUBRATECTRLER_TYPE             (subratectrler_get_type())
#define SUBRATECTRLER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SUBRATECTRLER_TYPE,SubflowRateController))
#define SUBRATECTRLER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SUBRATECTRLER_TYPE,SubflowRateControllerClass))
#define SUBRATECTRLER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SUBRATECTRLER_TYPE))
#define SUBRATECTRLER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SUBRATECTRLER_TYPE))
#define SUBRATECTRLER_CAST(src)        ((SubflowRateController *)(src))

typedef void (*SubRateCtrler)(SubflowRateController*);
typedef void (*SubRateAction)(SubflowRateController*);
typedef void (*SubTargetRateCtrler)(SubflowRateController*, gint32);

typedef enum{
  SUBFLOW_STATE_OVERUSED       = -1,
  SUBFLOW_STATE_STABLE         =  0,
  SUBFLOW_STATE_UNDERUSED      =  1,
}SubflowState;

//struct _SubflowMeasurement{
//  GstMPRTCPReportSummary *reports;
//  NetQueueAnalyserResult  netq_analysation;
//  guint32                 sending_bitrate;
//  guint32                 receiver_bitrate;
//  guint32                 goodput_bitrate;
//  gint32                  bytes_in_flight;
//  gboolean                lost;
//};


struct _SubflowRateController
{
  GObject                   object;
  guint8                    id;
  GRWLock                   rwmutex;
  GstClock*                 sysclock;
  MPRTPSPath*               path;
  MARCFBProcessor*          fb_processor;
  MARCFBProcessorResult     marc_result;

  GstClockTime              made;
  GstClockTime              disable_controlling;
  guint                     measurements_num;

  gint32                    monitored_bitrate;
  gint32                    bottleneck_point;
  gint32                    max_target_point;
  gint32                    min_target_point;
  gint32                    target_bitrate;
  gint32                    target_bitrate_t1;
  GstClockTime              last_decrease;
  GstClockTime              last_settled;
  GstClockTime              last_increase;

  SubflowState              state;
  SubflowState              state_t1;
  gboolean                  enabled;

  //Need for monitoring
  guint                     monitoring_interval;
  GstClockTime              monitoring_started;

  guint                     pending_event;
  SubRateAction             stage_fnc;

  GstClockTime              congestion_detected;

  gpointer                  priv;

};

struct _SubflowRateControllerClass{
  GObjectClass parent_class;

};
GType subratectrler_get_type (void);
SubflowRateController *make_subratectrler(MPRTPSPath *path);

void subratectrler_enable(SubflowRateController *this);
void subratectrler_disable(SubflowRateController *this);

void subratectrler_report_update(SubflowRateController *this, GstMPRTCPReportSummary *summary);
void subratectrler_time_update(SubflowRateController *this);

#endif /* SUBRATECTRLER_H_ */
