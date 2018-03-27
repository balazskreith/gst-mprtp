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

  gboolean                  approve_measurement;
  GstClockTime              obligated_approvement;

  gint32                    target_bitrate;
  gint32                    tracked_target;
  gint32                    desired_target;

  gdouble                   tcp_flow_presented;

  guint                     rcved_fb_since_changed;

  GstClockTime              last_executed;

  gint32                    bottleneck_point;
  gint32                    congested_bitrate;

  GstClockTime              approvement_interval;

  FRACTaLFBProcessor*       fbprocessor;
  FRACTaLStat*              stat;
  guint                     sent_packets;
  guint16                   border_packet_seq;
  gboolean                  set_border_packet;

  guint                     monitoring_interval;
  guint                     prev_monitoring_interval;
  gint32                    monitoring_target_bitrate;
  GstClockTime              monitoring_started;
  GstClockTime              monitoring_approvement_started;
  gboolean                  monitoring_approved;

  gboolean                  increasing_approved;
  GstClockTime              increasing_started;
  GstClockTime              increasing_sr_reached;
  gint32                    increasement;

  gint32 last_approved_sr;
  GstClockTime sr_reached;

  gdouble                   FL_th;
  gboolean                  reducing_approved;
  GstClockTime              reducing_sr_reached;

  guint                     pending_event;
  SubRateAction             stage_fnc;

  gint32                    distortion_num;

  GstClockTime              congestion_detected;

  GstClockTime              last_approved;
  GstClockTime              last_settled;
  GstClockTime              last_distorted;
  GstClockTime              last_increased;
  GstClockTime              last_inflicted;
  GstClockTime              last_reset;

  gdouble                   bottleneck_cwnd;
  GstClockTime            (*refresh_target)(FRACTaLSubController* this);

  gpointer                  priv;

  SndTracker*               sndtracker;
};

struct _FRACTaLSubControllerClass{
  GObjectClass parent_class;
  gint32 approved_increasement;
  gint32 approved_correction;
  gint subflows_num;
};
GType fractalsubctrler_get_type (void);
FRACTaLSubController *make_fractalsubctrler(SndTracker *sndtracker, SndSubflow *subflow);

void fractalsubctrler_enable(FRACTaLSubController *this);
void fractalsubctrler_disable(FRACTaLSubController *this);

void fractalsubctrler_report_update(FRACTaLSubController *this, GstMPRTCPReportSummary *summary);
void fractalsubctrler_time_update(FRACTaLSubController *this);


#endif /* FRACTALSUBCTRLER_H_ */
