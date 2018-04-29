/*
 * smanctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef THRESHOLD_FINDER_H_
#define THRESHOLD_FINDER_H_

#include <gst/gst.h>
#include "lib_datapuffer.h"
#include "notifier.h"
#include "slidingwindow.h"

typedef struct _ThresholdFinder ThresholdFinder;
typedef struct _ThresholdFinderClass ThresholdFinderClass;


#define THRESHOLD_FINDER_TYPE             (threshold_finder_get_type())
#define THRESHOLD_FINDER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),THRESHOLD_FINDER_TYPE,ThresholdFinder))
#define THRESHOLD_FINDER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),THRESHOLD_FINDER_TYPE,ThresholdFinderClass))
#define THRESHOLD_FINDER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),THRESHOLD_FINDER_TYPE))
#define THRESHOLD_FINDER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),THRESHOLD_FINDER_TYPE))
#define THRESHOLD_FINDER_CAST(src)        ((ThresholdFinder *)(src))


struct _ThresholdFinder
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;
};

struct _ThresholdFinderClass{
  GObjectClass parent_class;
};

ThresholdFinder*
make_threshold_finder(void);

gint
threshold_finder_do(ThresholdFinder* this, gdouble* x, guint length);

GType
threshold_finder_get_type (void);

#endif /* THRESHOLD_FINDER_H_ */
