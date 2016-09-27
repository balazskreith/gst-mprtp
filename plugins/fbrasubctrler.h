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
#include "rtppackets.h"


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


typedef struct _FBRAPlusStat
{
  gint32                   bytes_in_flight;
  gint32                   sender_bitrate;
  gdouble                  owd_corr;
  gdouble                  owd_std;
  GstClockTime             RTT;
  gdouble                  srtt;
}FBRAPlusStat;

struct _FBRASubController
{
  GObject                   object;
  GstClock*                 sysclock;
  GstClockTime              made;
  gboolean                  enabled;
  SndSubflow*               subflow;

  GstClockTime              last_executed;

  gint32                    bottleneck_point;

  FBRAFBProcessor*          fbprocessor;
  FBRAPlusStat              fbstat;

  guint                     monitoring_interval;
  GstClockTime              monitoring_started;
  GstClockTime              increasement_started;

  guint                     pending_event;
  SubRateAction             stage_fnc;

  GstClockTime              congestion_detected;

  GstClockTime              last_distorted;
  GstClockTime              last_reduced;

  gdouble                   cwnd;

  GstClockTime              last_approved;

  gpointer                  priv;

  SndTracker*               sndtracker;

};

struct _FBRASubControllerClass{
  GObjectClass parent_class;

};
GType fbrasubctrler_get_type (void);
FBRASubController *make_fbrasubctrler(MPRTPSPath *path);

gboolean fbrasubctrler_path_approver(gpointer data, RTPPacket *packet);

void fbrasubctrler_enable(FBRASubController *this);
void fbrasubctrler_disable(FBRASubController *this);

void fbrasubctrler_report_update(FBRASubController *this, GstMPRTCPReportSummary *summary);
gboolean fbrasubctrler_time_update(FBRASubController *this);


#endif /* FBRASUBCTRLER_H_ */
