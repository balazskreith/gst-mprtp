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
#include "floatsbuffer.h"
#include "subanalyser.h"
#include "sndratedistor.h"


typedef struct _SubflowRateController SubflowRateController;
typedef struct _SubflowRateControllerClass SubflowRateControllerClass;

#define SUBRATECTRLER_TYPE             (subratectrler_get_type())
#define SUBRATECTRLER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SUBRATECTRLER_TYPE,SubflowRateController))
#define SUBRATECTRLER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SUBRATECTRLER_TYPE,SubflowRateControllerClass))
#define SUBRATECTRLER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SUBRATECTRLER_TYPE))
#define SUBRATECTRLER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SUBRATECTRLER_TYPE))
#define SUBRATECTRLER_CAST(src)        ((SubflowRateController *)(src))

typedef void (*SubRateCtrler)(SubflowRateController*);
typedef void (*SubRateAction)(SubflowRateController*);
typedef void (*SubTargetRateCtrler)(SubflowRateController*, gint32);

struct _SubflowRateController
{
  guint8                    id;
  GObject                   object;
  GRWLock                   rwmutex;
  GstClock*                 sysclock;
  MPRTPSPath*               path;
  SubAnalyser*              analyser;
  SendingRateDistributor*   rate_controller;

  gint32                    monitored_bitrate;

  SubflowUtilization        utilization;

  GstClockTime              disable_controlling;

  gint32                    max_target_point;
  gint32                    min_target_point;
  gint32                    desired_bitrate;
  gint32                    target_bitrate;

  gint32                    max_rate;
  gint32                    min_rate;

  gint32                    bottleneck_point;
  gint32                    keep;
  gboolean                  reduced;
  gint32                    in_congestion;

  GstClockTime              setup_time;

  //Need for monitoring
  guint                     monitoring_interval;
  GstClockTime              monitoring_started;

  guint                     pending_event;
  SubRateAction             stage_fnc;

  gpointer                  moments;
  gint                      moments_index;
  guint32                   moments_num;

  gdouble                   discard_aggressivity;
  gdouble                   ramp_up_aggressivity;

  gboolean                  cwnd_was_increased;
  gboolean                  bitrate_was_incrased;
  GstClockTime              last_skip_time;
  GstClockTime              packet_obsolation_treshold;

  GstClockTime              congestion_detected;

  gboolean                  log_enabled;
  guint                     logtick;
  gchar                     log_filename[255];

};


struct _SubflowRateControllerClass{
  GObjectClass parent_class;

};
GType subratectrler_get_type (void);
SubflowRateController *make_subratectrler(SendingRateDistributor* rate_controlller);
void subratectrler_enable_logging(SubflowRateController *this, const gchar *filename);
void subratectrler_disable_logging(SubflowRateController *this);
void subratectrler_set(SubflowRateController *this,
                         MPRTPSPath *path,
                         guint32 sending_target,
                         guint64 initial_disabling);
void subratectrler_unset(SubflowRateController *this);

void subratectrler_measurement_update(
                         SubflowRateController *this,
                         RRMeasurement * measurement);
void subratectrler_setup_controls(
                         SubflowRateController *this, struct _SubflowUtilizationControl* src);
gint32 subratectrler_get_target_bitrate(SubflowRateController *this);
gint32 subratectrler_get_monitoring_bitrate(SubflowRateController *this);
void subratectrler_add_extra_rate(SubflowRateController *this,
                                  gint32 extra_rate);

#endif /* SUBRATECTRLER_H_ */
