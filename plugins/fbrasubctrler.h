/*
 * fbractrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FBRASUBCTRLER_H_
#define FBRASUBCTRLER_H_

#include <gst/gst.h>
#include "fbrafbproc.h"
#include "sndtracker.h"
#include "sndsubflows.h"
#include "sndpackets.h"

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
  GstClock*                 sysclock;
  GstClockTime              made;
  gboolean                  enabled;
  SndSubflow*               subflow;

  GstClockTime              last_log;

  gint32                    target_bitrate;
  gint32                    stable_bitrate;

  guint                     rcved_fb_since_changed;

  GstClockTime              last_executed;

  gint32                    bottleneck_point;
  gint32                    keeping_point;

  FBRAFBProcessor*          fbprocessor;
  FBRAPlusStat*             stat;
  guint                     sent_packets;

  guint                     monitoring_interval;
  GstClockTime              monitoring_started;
  GstClockTime              monitoring_approvement_started;
  gboolean                  monitoring_approved;

  gboolean                  increasing_approved;
  GstClockTime              increasing_rr_reached;
  GstClockTime              increasing_sr_reached;
  gint32                    increasement;

  guint                     pending_event;
  SubRateAction             stage_fnc;

  GstClockTime              congestion_detected;

  GstClockTime              last_approved;
  GstClockTime              last_settled;
  GstClockTime              last_distorted;

  gint32                    distorted_BiF;

  gdouble                   cwnd;//congestion window
  gdouble                   awnd;//allowed bitrate window

  gpointer                  priv;

  SndTracker*               sndtracker;

};

struct _FBRASubControllerClass{
  GObjectClass parent_class;

};
GType fbrasubctrler_get_type (void);
FBRASubController *make_fbrasubctrler(SndTracker *sndtracker, SndSubflow *subflow);

gboolean fbrasubctrler_path_approver(gpointer data, SndPacket *packet);

void fbrasubctrler_enable(FBRASubController *this);
void fbrasubctrler_disable(FBRASubController *this);

void fbrasubctrler_report_update(FBRASubController *this, GstMPRTCPReportSummary *summary);
void fbrasubctrler_time_update(FBRASubController *this);


#endif /* FBRASUBCTRLER_H_ */
