/*
 * smanctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef QDELAY_STABILITY_CALCER_H_
#define QDELAY_STABILITY_CALCER_H_

#include <gst/gst.h>
#include "lib_datapuffer.h"
#include "notifier.h"
#include "slidingwindow.h"

typedef struct _QDelayStabilityCalcer QDelayStabilityCalcer;
typedef struct _QDelayStabilityCalcerClass QDelayStabilityCalcerClass;


#define QDELAY_STABILITY_CALCER_TYPE             (qdelay_stability_calcer_get_type())
#define QDELAY_STABILITY_CALCER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),QDELAY_STABILITY_CALCER_TYPE,QDelayStabilityCalcer))
#define QDELAY_STABILITY_CALCER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),QDELAY_STABILITY_CALCER_TYPE,QDelayStabilityCalcerClass))
#define QDELAY_STABILITY_CALCER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),QDELAY_STABILITY_CALCER_TYPE))
#define QDELAY_STABILITY_CALCER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),QDELAY_STABILITY_CALCER_TYPE))
#define QDELAY_STABILITY_CALCER_CAST(src)        ((QDelayStabilityCalcer *)(src))

#define QDELAY_STABILITY_VECTOR_LENGTH 3

struct _QDelayStabilityCalcer
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;

  GstClockTime time_threshold;
  GQueue* items;
  GQueue* recycle;
  gdouble actual_vector[QDELAY_STABILITY_VECTOR_LENGTH];
  gint32 actual_count;
  gdouble bad_ref_vector[QDELAY_STABILITY_VECTOR_LENGTH];
  gdouble std;

  gdouble* std_vector;
  guint std_vector_index;
  gboolean std_vector_turned;
  GstClockTime last_std_vector_calced;


  gboolean qdelay_is_stable;
  gdouble stability_std;
  gdouble* stability_vector;
  gdouble prev_result;
  guint stability_vector_index;
  gboolean stability_vector_turned;
  GstClockTime last_stability_vector_calced;
};

struct _QDelayStabilityCalcerClass{
  GObjectClass parent_class;
};

QDelayStabilityCalcer*
make_qdelay_stability_calcer(void);

void
qdelay_stability_calcer_set_time_threshold(QDelayStabilityCalcer* this, GstClockTime time_threshold);

gdouble
qdelay_stability_calcer_do(QDelayStabilityCalcer* this, gboolean* qdelay_is_stable);

void
qdelay_stability_calcer_add_ts(QDelayStabilityCalcer* this, gdouble qts);

GType
qdelay_stability_calcer_get_type (void);

#endif /* QDELAY_STABILITY_CALCER_H_ */
