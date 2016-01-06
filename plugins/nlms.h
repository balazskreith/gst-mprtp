/*
 * nlms.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef NLMS_H_
#define NLMS_H_

#include <gst/gst.h>
#include "variancetracker.h"

typedef struct _NormalizedLeastMeanSquere NormalizedLeastMeanSquere;
typedef struct _NormalizedLeastMeanSquereClass NormalizedLeastMeanSquereClass;

#include "bintree.h"

#define NLMS_TYPE             (nlms_get_type())
#define NLMS(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),NLMS_TYPE,NormalizedLeastMeanSquere))
#define NLMS_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),NLMS_TYPE,NormalizedLeastMeanSquereClass))
#define NLMS_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),NLMS_TYPE))
#define NLMS_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),NLMS_TYPE))
#define NLMS_CAST(src)        ((NormalizedLeastMeanSquere *)(src))

struct _NormalizedLeastMeanSquere
{
  GObject                  object;
  GRWLock                  rwmutex;
  gint64                  *inputs;
  gdouble                 *weights;
  guint                    length;
  guint                    counter;
  gdouble                  step_size;
  gdouble                  divider_constant;
  gdouble                  estimation_error;
};


struct _NormalizedLeastMeanSquereClass{
  GObjectClass parent_class;

};

GType nlms_get_type (void);
void nlms_test (void);
NormalizedLeastMeanSquere *make_nlms(guint32 inputs_length,
                                     gdouble step_size,
                                     gdouble constant_a
                                     );
gdouble nlms_measurement_update(NormalizedLeastMeanSquere *this,
                                gint64 measured_value,
                                gdouble *error);

void nlms_reset(NormalizedLeastMeanSquere *this);

#endif /* NLMS_H_ */
