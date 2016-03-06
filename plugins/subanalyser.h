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
  gdouble                  RR_avg;
  GstClockTime             window_time_limit;
  guint                    window_size;
  GstClockTime             append_log_abbr;
  GstClockTime             last_stable;
  GstClock*                sysclock;
  gboolean                 discarded;
  gpointer                 priv;
};

struct _SubAnalyserClass{
  GObjectClass parent_class;

};

struct _SubAnalyserResult{
  gdouble        qtrend;
  gdouble        off;
  gdouble        discards_rate;
  gboolean       tr_correlated;
  gboolean       pierced, distorted, congested;
  gint32         stable;
  gint32         sending_rate_median;

};

GType subanalyser_get_type (void);
void subanalyser_reset(SubAnalyser *this);
SubAnalyser *make_subanalyser(void);
void subanalyser_time_update(SubAnalyser *this, gint32 sending_bitrate);
void subanalyser_reset_stability(SubAnalyser *this);
void subanalyser_measurement_analyse(SubAnalyser *this,
                                     RRMeasurement *measurement,
                                     gint32         target_bitrate,
                                     gint32 sending_rate,
                                     SubAnalyserResult *result);

void subanalyser_append_logfile(SubAnalyser *this, FILE *file);
#endif /* SUBANALYSER_H_ */
