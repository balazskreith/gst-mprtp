/*
 * smanctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FL_STABILITY_CALCER_H_
#define FL_STABILITY_CALCER_H_

#include <gst/gst.h>
#include "lib_datapuffer.h"
#include "notifier.h"
#include "slidingwindow.h"

typedef struct _FLStabilityCalcer FLStabilityCalcer;
typedef struct _FLStabilityCalcerClass FLStabilityCalcerClass;


#define FL_STABILITY_CALCER_TYPE             (fl_stability_calcer_get_type())
#define FL_STABILITY_CALCER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FL_STABILITY_CALCER_TYPE,FLStabilityCalcer))
#define FL_STABILITY_CALCER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FL_STABILITY_CALCER_TYPE,FLStabilityCalcerClass))
#define FL_STABILITY_CALCER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FL_STABILITY_CALCER_TYPE))
#define FL_STABILITY_CALCER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FL_STABILITY_CALCER_TYPE))
#define FL_STABILITY_CALCER_CAST(src)        ((FLStabilityCalcer *)(src))

#define FL_STABILITY_VECTOR_LENGTH 3

struct _FLStabilityCalcer
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;

  GstClockTime time_threshold;
  GQueue* items;
  GQueue* recycle;
  gint32 actual_count;
  gdouble actual_vector[FL_STABILITY_VECTOR_LENGTH];
  gdouble bad_ref_vector[FL_STABILITY_VECTOR_LENGTH];
  gdouble std;

  gdouble* std_vector;
  guint std_vector_index;
  gboolean std_vector_turned;
  GstClockTime last_std_vector_calced;
};

struct _FLStabilityCalcerClass{
  GObjectClass parent_class;
};

FLStabilityCalcer*
make_fl_stability_calcer(void);

void
fl_stability_calcer_set_time_threshold(FLStabilityCalcer* this, GstClockTime time_threshold);

gdouble
fl_stability_calcer_do(FLStabilityCalcer* this);

void
fl_stability_calcer_add_sample(FLStabilityCalcer* this, gdouble qts);

GType
fl_stability_calcer_get_type (void);

#endif /* FL_STABILITY_CALCER_H_ */
