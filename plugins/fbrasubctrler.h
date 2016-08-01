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
#include "sndratedistor.h"
#include "reportproc.h"
#include "signalreport.h"
#include "fbrafbproc.h"
#include "fbratargetctrler.h"


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

  GstClockTime              made;
//  gboolean                  disable_controlling;
  GstClockTime              last_executed;
  GstClockTime              disable_end;
  guint                     measurements_num;

  gint32                    monitored_bitrate;
  guint32                   monitored_packets;
  gint32                    bottleneck_point;

  gint32                    max_target_point;
  gint32                    min_target_point;
//  gint32                    target_bitrate;
//  gint32                    target_bitrate_t1;
  GstClockTime              last_tr_changed;
  GstClockTime              last_settled;

  gdouble                   rand_factor;

  GstClockTime              last_fb_arrived;

  gboolean                  enabled;

  FBRAFBProcessor*          fbprocessor;
  FBRAFBProcessorStat       fbstat;
  FBRATargetCtrler*         targetctrler;
  //Need for monitoring
  guint                     monitoring_interval;
  GstClockTime              monitoring_started;
  GstClockTime              increasement_started;

  guint                     pending_event;
  SubRateAction             stage_fnc;

  GstClockTime              congestion_detected;
  gboolean                  owd_approvement;

  guint                     consecutive_ok;
  guint                     consecutive_nok;
  GstClockTime              last_distorted;
  GstClockTime              last_reduced;

  GstClockTime              adjustment_time;
  GstClockTime              last_approved;

  gpointer                  priv;

  guint                     last_rtp_size;

};

struct _FBRASubControllerClass{
  GObjectClass parent_class;

};
GType fbrasubctrler_get_type (void);
FBRASubController *make_fbrasubctrler(MPRTPSPath *path);

gboolean fbrasubctrler_path_approver(gpointer data, GstRTPBuffer *buffer);

void fbrasubctrler_enable(FBRASubController *this);
void fbrasubctrler_disable(FBRASubController *this);

void fbrasubctrler_report_update(FBRASubController *this, GstMPRTCPReportSummary *summary);
void fbrasubctrler_time_update(FBRASubController *this);

void fbrasubctrler_signal_update(FBRASubController *this, MPRTPSubflowFECBasedRateAdaption *params);
void fbrasubctrler_signal_request(FBRASubController *this, MPRTPSubflowFECBasedRateAdaption *result);

#endif /* FBRASUBCTRLER_H_ */
