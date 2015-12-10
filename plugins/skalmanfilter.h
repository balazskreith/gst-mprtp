/*
 * skalmanfilter.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef SKALMANFILTER_H_
#define SKALMANFILTER_H_

#include <gst/gst.h>
#include "variancetracker.h"

typedef struct _SKalmanFilter SKalmanFilter;
typedef struct _SKalmanFilterClass SKalmanFilterClass;

#include "bintree.h"

#define SKALMANFILTER_TYPE             (skalmanfilter_get_type())
#define SKALMANFILTER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SKALMANFILTER_TYPE,SKalmanFilter))
#define SKALMANFILTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SKALMANFILTER_TYPE,SKalmanFilterClass))
#define SKALMANFILTER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SKALMANFILTER_TYPE))
#define SKALMANFILTER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SKALMANFILTER_TYPE))
#define SKALMANFILTER_CAST(src)        ((SKalmanFilter *)(src))

struct _SKalmanFilter
{
  GObject                  object;
  GRWLock                  rwmutex;
  VarianceTracker*         measurement_variances;
  VarianceTracker*         ewma_variances;
  gdouble                  estimated_value;
  gdouble                  control_value;
  gdouble                  estimate_error;
  gdouble                  measurement_error;
  gdouble                  measurement_diff;
  gdouble                  kalman_gain;
  gint32                   counter;
//  gint64                   diff_squere;
  gdouble                  alpha;
//  gdouble                  ewma_1;
//  gdouble                  ewma_2;
};


struct _SKalmanFilterClass{
  GObjectClass parent_class;

};

GType skalmanfilter_get_type (void);
SKalmanFilter *make_skalmanfilter(guint32 length, GstClockTime max_time);
SKalmanFilter *make_skalmanfilter_full(guint32 length,
                                       GstClockTime max_time,
                                       gdouble alpha_1);
void skalmanfilter_setup_notifiers(SKalmanFilter *this);
gdouble skalmanfilter_measurement_update(SKalmanFilter *this, gint64 measured_value);
gdouble skalmanfilter_get_measurement_std(SKalmanFilter *this);
void skalmanfilter_reset(SKalmanFilter *this);

#endif /* SKALMANFILTER_H_ */
