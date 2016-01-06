/*
 * kalmanfilter.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef KALMANFILTER_H_
#define KALMANFILTER_H_

#include <gst/gst.h>
#include "variancetracker.h"

typedef struct _KalmanFilter KalmanFilter;
typedef struct _KalmanFilterClass KalmanFilterClass;

#include "bintree.h"

#define KALMANFILTER_TYPE             (kalmanfilter_get_type())
#define KALMANFILTER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),KALMANFILTER_TYPE,KalmanFilter))
#define KALMANFILTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),KALMANFILTER_TYPE,KalmanFilterClass))
#define KALMANFILTER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),KALMANFILTER_TYPE))
#define KALMANFILTER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),KALMANFILTER_TYPE))
#define KALMANFILTER_CAST(src)        ((KalmanFilter *)(src))

struct _KalmanFilter
{
  GObject                  object;
  GRWLock                  rwmutex;
  VarianceTracker*         measurement_variances;
  gdouble                  estimated_value;
  gdouble                  prior_value;
  gdouble                  estimated_error;
  gdouble                  prior_error;
  gdouble                  measurement_error;
  gdouble                  kalman_gain;
  gint32                   counter;
};


struct _KalmanFilterClass{
  GObjectClass parent_class;

};

GType kalmanfilter_get_type (void);
KalmanFilter *make_kalmanfilter(guint32 length, GstClockTime max_time);
gdouble kalmanfilter_time_update(KalmanFilter *this,
                              gdouble control_factor,
                              gint64 control_value,
                              gdouble distrust);
void kalmanfilter_measurement_update(KalmanFilter *this, gint64 measured_value);
void kalmanfilter_reset(KalmanFilter *this);

#endif /* KALMANFILTER_H_ */
