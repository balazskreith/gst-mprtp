/*
 * smanctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef STABILITY_CALCER_H_
#define STABILITY_CALCER_H_

#include <gst/gst.h>
#include "lib_datapuffer.h"
#include "notifier.h"
#include "slidingwindow.h"

typedef struct _StabilityCalcer StabilityCalcer;
typedef struct _StabilityCalcerClass StabilityCalcerClass;


#define STABILITY_CALCER_TYPE             (stability_calcer_get_type())
#define STABILITY_CALCER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),STABILITY_CALCER_TYPE,StabilityCalcer))
#define STABILITY_CALCER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),STABILITY_CALCER_TYPE,StabilityCalcerClass))
#define STABILITY_CALCER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),STABILITY_CALCER_TYPE))
#define STABILITY_CALCER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),STABILITY_CALCER_TYPE))
#define STABILITY_CALCER_CAST(src)        ((StabilityCalcer *)(src))

#define QDELAY_STABILITY_VECTOR_LENGTH 3

struct _StabilityCalcer
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;

  GstClockTime time_threshold;
  GQueue* items;
  GQueue* recycle;

  gint sum;
};

struct _StabilityCalcerClass{
  GObjectClass parent_class;
};

StabilityCalcer*
make_stability_calcer(void);

void
stability_calcer_set_time_threshold(StabilityCalcer* this, GstClockTime time_threshold);

gdouble
stability_calcer_get_score(StabilityCalcer* this);

void
stability_calcer_add_sample(StabilityCalcer* this, gint value);

GType
stability_calcer_get_type (void);

#endif /* STABILITY_CALCER_H_ */
