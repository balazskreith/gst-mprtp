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
#include "floatnumstracker.h"


typedef struct _SubflowRateController SubflowRateController;
typedef struct _SubflowRateControllerClass SubflowRateControllerClass;

#define SUBRATECTRLER_TYPE             (subratectrler_get_type())
#define SUBRATECTRLER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SUBRATECTRLER_TYPE,SubflowRateController))
#define SUBRATECTRLER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SUBRATECTRLER_TYPE,SubflowRateControllerClass))
#define SUBRATECTRLER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SUBRATECTRLER_TYPE))
#define SUBRATECTRLER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SUBRATECTRLER_TYPE))
#define SUBRATECTRLER_CAST(src)        ((SubflowRateController *)(src))

typedef void (*SubRateProc)(SubflowRateController*);

struct _SubflowRateController
{
  GObject                  object;
  GRWLock                  rwmutex;
  GstClock*                sysclock;
  guint8                   id;
  MPRTPSPath*              path;
  gboolean                 path_is_paced;

  gint32                   monitored_bitrate;
  gint32                   target_bitrate;

  guint                    overusing_indicator;
  GstClockTime             disable_controlling;

  //Video target bitrate inflection point i.e. the last known highest
  //target_bitrate during fast start. Used to limit bitrate increase
  //close to the last know congestion point. Initial value: 1
  gint32                   max_target_point;
  gint32                   min_target_point;
  NumsTracker*             target_points;

  NumsTracker*             bytes_in_flight_history;
  guint32                  bytes_in_queue_avg;

  gint32                   max_rate;
  gint32                   min_rate;

  gint32                   last_congestion_point;

  //Need for monitoring
  guint                    monitoring_interval;
  GstClockTime             monitoring_started;

  SubRateProc              controller;
  SubRateProc              stage;

  guint8*                  moments;
  gint                     moments_index;
  guint32                  moments_num;

  gboolean                 cwnd_was_increased;
  gboolean                 bitrate_was_incrased;
  GstClockTime             last_target_bitrate_i_adjust;
  GstClockTime             last_target_bitrate_adjust;
  GstClockTime             last_queue_clear;
  GstClockTime             last_skip_time;

  GstClockTime             packet_obsolation_treshold;

  PercentileTracker*       ltt_delays_th;
  PercentileTracker*       ltt_delays_target;

  //OWD target. Initial value: OWD_TARGET_LO
//  guint64                  owd_target;
  //EWMA filtered owd fraction.Initial value:  0.0
  gdouble                  owd_fraction_avg;
  gdouble                  BiF_ested_avg;
  gdouble                  BiF_acked_avg;
  gdouble                  BiF_off_avg;
  //Vector of the last 20 owd_fraction
  FloatNumsTracker*        owd_fraction_hist;
  //OWD trend indicates incipient congestion. Initial value: 0.0
  //True if in fast start state. Initial value: TRUE
//  gboolean                 in_fast_start;
//  gint                     n_fast_start;
//  gint32                   rise_up_max;
  //Maximum segment size
  gint                     mss;
  //Minimum congestion window [byte]. Initial value: 2*MSS
  gint                     cwnd_min;
  //COngestion window
  gint                     pacing_bitrate;
  //Congestion window inflection point. Initial value: 1
  gint                     cwnd_i;
  GstClockTime             last_congestion_detected;
  //Smoothed RTT [s], computed similar to method depicted in [RFC6298].
  //Initial value: 0.0
  gdouble                  s_rtt;
  gdouble                  s_SR;

  gdouble                  delay_t0;
  gdouble                  delay_t1;
  gdouble                  delay_t2;
  gdouble                  delay_t3;

};


struct _SubflowRateControllerClass{
  GObjectClass parent_class;

};
GType subratectrler_get_type (void);
SubflowRateController *make_subratectrler(void);
void subratectrler_set(SubflowRateController *this,
                              MPRTPSPath *path,
                              guint32 sending_target);
void subratectrler_unset(SubflowRateController *this);
void subratectrler_extract_stats(SubflowRateController *this,
                                  guint64 *median_delay,
                                  gint32  *sender_rate,
                                  gdouble *target_rate,
                                  gdouble *goodput,
                                  gdouble *next_target);
void subratectrler_time_update(
                         SubflowRateController *this,
                         gint32 *target_bitrate,
                         gint32 *extra_bitrate,
                         UtilizationSubflowReport *rep);

void subratectrler_measurement_update(
                         SubflowRateController *this,
                         RRMeasurement * measurement);
gint32 subratectrler_get_target_bitrate(SubflowRateController *this);
void subratectrler_add_extra_rate(SubflowRateController *this,
                                  gint32 extra_rate);

#endif /* SUBRATECTRLER_H_ */
