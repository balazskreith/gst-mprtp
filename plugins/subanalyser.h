/*
 * cosubanalyser.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef SUBANALYSER_H_
#define SUBANALYSER_H_

#include <gst/gst.h>
#include <stdio.h>

typedef struct _SubAnalyser SubAnalyser;
typedef struct _SubAnalyserClass SubAnalyserClass;


#include "bintree2.h"
#include "numstracker.h"
#include "percentiletracker.h"
#include "mprtpspath.h"
#include "floatsbuffer.h"

#define SUBANALYSER_TYPE             (subanalyser_get_type())
#define SUBANALYSER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SUBANALYSER_TYPE,SubAnalyser))
#define SUBANALYSER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SUBANALYSER_TYPE,SubAnalyserClass))
#define SUBANALYSER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SUBANALYSER_TYPE))
#define SUBANALYSER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SUBANALYSER_TYPE))
#define SUBANALYSER_CAST(src)        ((SubAnalyser *)(src))

typedef struct _SubAnalyserResult SubAnalyserResult;

struct _SubAnalyser
{
  GObject                  object;
  FloatsBuffer*            DeOff_window;
  NumsTracker*             De_window;
  PercentileTracker*       SR_window;
  gdouble                  RR_avg;
  GstClockTime             window_time_limit;
  guint                    window_size;
  GstClockTime             append_log_abbr;
  GstClock*                sysclock;
  gpointer                 priv;
};

struct _SubAnalyserClass{
  GObjectClass parent_class;

};

struct _SubAnalyserResult{
  gdouble        qtrend;
  gdouble        discards_rate;
  gint32         sending_rate_median;
  gdouble        delay_off;

  struct{
    gboolean     congestion, bottleneck, blockage;
  }delay_indicators;

  struct{
    gboolean     rr_correlated, tr_correlated, distorted, congested;
  }rate_indicators;
};

GType subanalyser_get_type (void);
void subanalyser_reset(SubAnalyser *this);
SubAnalyser *make_subanalyser(guint size,
                              GstClockTime obsolation_treshold);
void subanalyser_time_update(SubAnalyser *this, MPRTPSPath *path);
void subanalyser_measurement_analyse(SubAnalyser *this,
                                     RRMeasurement *measurement,
                                     gint32         target_bitrate,
                                     SubAnalyserResult *result);

void subanalyser_append_logfile(SubAnalyser *this, FILE *file);
#endif /* SUBANALYSER_H_ */
