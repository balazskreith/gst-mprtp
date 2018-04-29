/*
 * smanctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef LINEAR_REGRESSOR_H_
#define LINEAR_REGRESSOR_H_

#include <gst/gst.h>
#include "lib_datapuffer.h"
#include "notifier.h"
#include "slidingwindow.h"

typedef struct _LinearRegressor LinearRegressor;
typedef struct _LinearRegressorClass LinearRegressorClass;


#define LINEAR_REGRESSOR_TYPE             (linear_regressor_get_type())
#define LINEAR_REGRESSOR(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),LINEAR_REGRESSOR_TYPE,LinearRegressor))
#define LINEAR_REGRESSOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),LINEAR_REGRESSOR_TYPE,LinearRegressorClass))
#define LINEAR_REGRESSOR_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),LINEAR_REGRESSOR_TYPE))
#define LINEAR_REGRESSOR_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),LINEAR_REGRESSOR_TYPE))
#define LINEAR_REGRESSOR_CAST(src)        ((LinearRegressor *)(src))


struct _LinearRegressor
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;

  gdouble*             x;
  gdouble*             y;
  guint                length;
  guint                index;
  guint                added;
  gboolean             calculate_r;

  gdouble              b,m,r;

  guint                refresh_period;

  gdouble   sumx;                      /* sum of x     */
  gdouble   sumx2;                     /* sum of x**2  */
  gdouble   sumxy;                     /* sum of x * y */
  gdouble   sumy;                      /* sum of y     */
  gdouble   sumy2;                     /* sum of y**2  */
};

typedef gdouble (*LinearRegressorSampleConverter) (gpointer udata, gpointer item);

struct _LinearRegressorClass{
  GObjectClass parent_class;
};

LinearRegressor*
make_linear_regressor(guint length, guint refresh_period);

void
linear_regressor_add_samples(LinearRegressor* this, gdouble x, gdouble y);

gdouble
linear_regressor_get_m(LinearRegressor* this);

gdouble
linear_regressor_get_r(LinearRegressor* this);

gdouble
linear_regressor_predict(LinearRegressor* this, gdouble x);

GType
linear_regressor_get_type (void);

#endif /* LINEAR_REGRESSOR_H_ */
