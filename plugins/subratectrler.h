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

struct _SubflowRateController
{
  GObject                   object;
  GRWLock                   rwmutex;
  GstClock*                 sysclock;
  guint8                    id;
  MPRTPSPath*               path;
  gboolean                  path_is_paced;

  guint                     bitrate_flags;

  gint32                    monitored_bitrate;

  SubAnalyser*              analyser;

//  gboolean                 stabilize;
//  gboolean                 steady;
//  gboolean                 distorted;
//  gboolean                 settled;

  GstClockTime             disable_controlling;

  //Video target bitrate inflection point i.e. the last known highest
  //target_bitrate during fast start. Used to limit bitrate increase
  //close to the last know congestion point. Initial value: 1
  gint32                   max_target_point;
  gint32                   min_target_point;
  gint32                   desired_bitrate;
  gint32                   target_bitrate;

  gint32                   max_rate;
  gint32                   min_rate;

  gint32                   last_congestion_point;

  GstClockTime             setup_time;

  NumsTracker*             IR_window;
  NumsTracker*             TR_window;
  gdouble                  ir_sum;
  gdouble                  tr_sum;
  gdouble                  target_fraction;
  //Need for monitoring
  guint                    monitoring_interval;
  GstClockTime             monitoring_started;

  SubRateCtrler            state;
  SubRateAction            stage_fnc;

  guint8*                  moments;
  gint                     moments_index;
  guint32                  moments_num;

  gdouble                  discard_aggressivity;
  gdouble                  ramp_up_aggressivity;

  gboolean                 cwnd_was_increased;
  gboolean                 bitrate_was_incrased;
  GstClockTime             last_target_bitrate_i_adjust;
  GstClockTime             last_target_bitrate_adjust;
  GstClockTime             last_queue_clear;
  GstClockTime             last_skip_time;
  GstClockTime             packet_obsolation_treshold;

  //OWD target. Initial value: OWD_TARGET_LO
//  guint64                  owd_target;
  //EWMA filtered owd fraction.Initial value:  0.0
  //COngestion window
  gint32                   pacing_bitrate;
  GstClockTime             last_congestion_detected;
  //Smoothed RTT [s], computed similar to method depicted in [RFC6298].
  //Initial value: 0.0

  gboolean                 log_enabled;
  guint                    logtick;
  gchar                    log_filename[255];

};


struct _SubflowRateControllerClass{
  GObjectClass parent_class;

};
GType subratectrler_get_type (void);
SubflowRateController *make_subratectrler(void);
void subratectrler_enable_logging(SubflowRateController *this, const gchar *filename);
void subratectrler_disable_logging(SubflowRateController *this);
void subratectrler_set(SubflowRateController *this,
                         MPRTPSPath *path,
                         guint32 sending_target,
                         guint64 initial_disabling);
void subratectrler_unset(SubflowRateController *this);

void subratectrler_time_update(
                         SubflowRateController *this,
                         gint32 *target_bitrate,
                         gint32 *extra_bitrate,
                         UtilizationSubflowReport *rep,
                         gboolean *overused);

void subratectrler_change_targets(
                         SubflowRateController *this,
                         gint32 min_rate,
                         gint32 max_rate,
                         gdouble ramp_up_aggressivity,
                         gdouble discard_aggressivity);

void subratectrler_measurement_update(
                         SubflowRateController *this,
                         RRMeasurement * measurement);
gint32 subratectrler_get_target_bitrate(SubflowRateController *this);
gint32 subratectrler_get_monitoring_bitrate(SubflowRateController *this);
void subratectrler_add_extra_rate(SubflowRateController *this,
                                  gint32 extra_rate);

#endif /* SUBRATECTRLER_H_ */
