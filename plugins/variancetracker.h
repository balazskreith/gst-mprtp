/*
 * covariancetracker.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef VARIANCETRACKER_H_
#define VARIANCETRACKER_H_

#include <gst/gst.h>

typedef struct _VarianceTracker VarianceTracker;
typedef struct _VarianceTrackerClass VarianceTrackerClass;


#include "bintree.h"

#define VARIANCETRACKER_TYPE             (variancetracker_get_type())
#define VARIANCETRACKER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),VARIANCETRACKER_TYPE,VarianceTracker))
#define VARIANCETRACKER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),VARIANCETRACKER_TYPE,VarianceTrackerClass))
#define VARIANCETRACKER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),VARIANCETRACKER_TYPE))
#define VARIANCETRACKER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),VARIANCETRACKER_TYPE))
#define VARIANCETRACKER_CAST(src)        ((VarianceTracker *)(src))


typedef struct _VarianceTrackerItem VarianceTrackerItem;
struct _VarianceTracker
{
  GObject                  object;
  GRWLock                  rwmutex;
  VarianceTrackerItem*     items;
  gint64                   value_sum;
  gint64                   squere_sum;
  guint32                  length;
  GstClock*                sysclock;
  GstClockTime             treshold;
  gint32                   write_index;
  gint32                   read_index;
  gint32                   counter;
};

struct _VarianceTrackerItem
{
  gint64        value;
  gint64        squere;
  GstClockTime  added;
};

struct _VarianceTrackerClass{
  GObjectClass parent_class;

};

GType variancetracker_get_type (void);
VarianceTracker *make_variancetracker(guint32 length, GstClockTime obsolation_treshold);
guint32 variancetracker_get_num(VarianceTracker *this);
guint64 variancetracker_get_last(VarianceTracker *this);

gdouble
variancetracker_get_stats (VarianceTracker * this,
                         gint64 *sum,
                         gint64 *squere_sum);

void variancetracker_obsolate (VarianceTracker * this);
void variancetracker_reset(VarianceTracker *this);
void variancetracker_add(VarianceTracker *this, gint64 value);

#endif /* VARIANCETRACKER_H_ */
