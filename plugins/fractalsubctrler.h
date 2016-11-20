/*
 * fractalctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FRACTALSUBCTRLER_H_
#define FRACTALSUBCTRLER_H_

#include <gst/gst.h>
#include "fractalfbproc.h"
#include "sndtracker.h"
#include "sndsubflows.h"
#include "sndpackets.h"

typedef struct _FRACTaLSubController FRACTaLSubController;
typedef struct _FRACTaLSubControllerClass FRACTaLSubControllerClass;
//typedef struct _SubflowMeasurement SubflowMeasurement;

#define FRACTALSUBCTRLER_TYPE             (fractalsubctrler_get_type())
#define FRACTALSUBCTRLER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FRACTALSUBCTRLER_TYPE,FRACTaLSubController))
#define FRACTALSUBCTRLER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FRACTALSUBCTRLER_TYPE,FRACTaLSubControllerClass))
#define FRACTALSUBCTRLER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FRACTALSUBCTRLER_TYPE))
#define FRACTALSUBCTRLER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FRACTALSUBCTRLER_TYPE))
#define FRACTALSUBCTRLER_CAST(src)        ((FRACTaLSubController *)(src))

typedef void (*SubRateCtrlerFnc)(FRACTaLSubController*);
typedef void (*SubRateAction)(FRACTaLSubController*);
typedef void (*SubTargetRateCtrler)(FRACTaLSubController*, gint32);


struct _FRACTaLSubController
{
  GObject                   object;
  GstClock*                 sysclock;
  GstClockTime              made;
  gboolean                  enabled;
  SndSubflow*               subflow;

  gboolean                  backward_congestion;
  GstClockTime              last_report;

  GstClockTime              last_log;
  gboolean                  approve_measurement;

  gint32                    target_bitrate;
  gint32                    stable_bitrate;

  guint                     rcved_fb_since_changed;

  GstClockTime              last_executed;

  gint32                    bottleneck_point;
  gint32                    keeping_point;

  FRACTaLFBProcessor*       fbprocessor;
  FRACTaLStat*              stat;
  FRACTaLApprovement*      approvement;
  guint                     sent_packets;

  guint                     monitoring_interval;
  GstClockTime              monitoring_started;
  GstClockTime              monitoring_approvement_started;
  gboolean                  monitoring_approved;

  gboolean                  increasing_approved;
  GstClockTime              increasing_rr_reached;
  GstClockTime              increasing_sr_reached;
  gint32                    increasement;


  gboolean                  reducing_approved;
  GstClockTime              reducing_sr_reached;

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

struct _FRACTaLSubControllerClass{
  GObjectClass parent_class;

};
GType fractalsubctrler_get_type (void);
FRACTaLSubController *make_fractalsubctrler(SndTracker *sndtracker, SndSubflow *subflow);

void fractalsubctrler_enable(FRACTaLSubController *this);
void fractalsubctrler_disable(FRACTaLSubController *this);

void fractalsubctrler_report_update(FRACTaLSubController *this, GstMPRTCPReportSummary *summary);
void fractalsubctrler_time_update(FRACTaLSubController *this);


#endif /* FRACTALSUBCTRLER_H_ */
