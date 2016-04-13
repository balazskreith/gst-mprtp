/*
 * fbractrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FBRASUBCTRLER_H_
#define FBRASUBCTRLER_H_

#include <gst/gst.h>
#include "mprtpspath.h"
#include "bintree.h"
#include "sndratedistor.h"
#include "reportproc.h"
#include "rmdiproc.h"
#include "signalreport.h"


typedef struct _FBRASubController FBRASubController;
typedef struct _FBRASubControllerClass FBRASubControllerClass;
//typedef struct _SubflowMeasurement SubflowMeasurement;

#define FBRASUBCTRLER_TYPE             (fbrasubctrler_get_type())
#define FBRASUBCTRLER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FBRASUBCTRLER_TYPE,FBRASubController))
#define FBRASUBCTRLER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FBRASUBCTRLER_TYPE,FBRASubControllerClass))
#define FBRASUBCTRLER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FBRASUBCTRLER_TYPE))
#define FBRASUBCTRLER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FBRASUBCTRLER_TYPE))
#define FBRASUBCTRLER_CAST(src)        ((FBRASubController *)(src))

typedef void (*SubRateCtrlerFnc)(FBRASubController*);
typedef void (*SubRateAction)(FBRASubController*);
typedef void (*SubTargetRateCtrler)(FBRASubController*, gint32);


struct _FBRASubController
{
  GObject                   object;
  guint8                    id;
  GRWLock                   rwmutex;
  GstClock*                 sysclock;
  MPRTPSPath*               path;
  RMDIProcessor*            fb_processor;
  RMDIProcessorResult       rmdi_result;

  GstClockTime              made;
  GstClockTime              disable_controlling;
  guint                     measurements_num;

  gint32                    monitored_bitrate;
  guint32                   monitored_packets;
  gint32                    bottleneck_point;
  gint32                    max_target_point;
  gint32                    min_target_point;
  gint32                    target_bitrate;
  gint32                    target_bitrate_t1;
  GstClockTime              last_decrease;
  GstClockTime              last_settled;
  GstClockTime              last_increase;

  GstClockTime              last_report_arrived_t1;
  GstClockTime              last_report_arrived;
  GstClockTime              report_interval;

  gboolean                  enabled;

  //Need for monitoring
  guint                     monitoring_interval;
  GstClockTime              monitoring_started;

  guint                     pending_event;
  SubRateAction             stage_fnc;

  GstClockTime              congestion_detected;

  gpointer                  priv;

};

struct _FBRASubControllerClass{
  GObjectClass parent_class;

};
GType fbrasubctrler_get_type (void);
FBRASubController *make_fbrasubctrler(MPRTPSPath *path);

gboolean fbrasubctrler_path_approver(gpointer data,    GstBuffer *buffer);

void fbrasubctrler_enable(FBRASubController *this);
void fbrasubctrler_disable(FBRASubController *this);

void fbrasubctrler_report_update(FBRASubController *this, GstMPRTCPReportSummary *summary);
void fbrasubctrler_time_update(FBRASubController *this);

void fbrasubctrler_signal_update(FBRASubController *this, MPRTPSubflowFECBasedRateAdaption *params);
void fbrasubctrler_signal_request(FBRASubController *this, MPRTPSubflowFECBasedRateAdaption *result);

void fbrasubctrler_logging2csv(FBRASubController *this);
void fbrasubctrler_logging(FBRASubController *this);
#endif /* FBRASUBCTRLER_H_ */
