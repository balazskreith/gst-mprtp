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
  FloatsBuffer*            DeT_window;
  NumsTracker*             RR_window;
  NumsTracker*             BiF_window;
  NumsTracker*             De_window;
  NumsTracker*             SR_window;
  NumsTracker*             TR_window;
//  PercentileTracker*       delaysH;
//  PercentileTracker*       delaysL;
  GstClockTime             window_time_limit;
  guint                    window_size;
  GstClockTime             append_log_abbr;
  guint                    target_aim;
  GstClock*                sysclock;
  gpointer                 priv;
};

struct _SubAnalyserClass{
  GObjectClass parent_class;

};

struct _SubAnalyserResult{
//  gdouble DeCorrH;
  gdouble DeCorrT;
//  gdouble DeCorrL;
  gdouble RateCorr;
  gdouble TRateCorr;
  gdouble BiFCorr;
  gdouble DeCorrT_dev;

  gboolean add2reference;
};

GType subanalyser_get_type (void);
void subanalyser_reset(SubAnalyser *this);
SubAnalyser *make_subanalyser(guint size,
                              GstClockTime obsolation_treshold);
void subanalyser_measurement_analyse(SubAnalyser *this,
                                     RRMeasurement *measurement,
                                     gint32         target_bitrate,
                                     SubAnalyserResult *result);

void subanalyser_measurement_add_to_reference(SubAnalyser *this, RRMeasurement *measurement);
void subanalyser_append_logfile(SubAnalyser *this, FILE *file);
#endif /* SUBANALYSER_H_ */
