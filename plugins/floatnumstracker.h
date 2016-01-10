/*
 * cofloatnumstracker.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FLOATNUMSTRACKER_H_
#define FLOATNUMSTRACKER_H_

#include <gst/gst.h>

typedef struct _FloatNumsTracker FloatNumsTracker;
typedef struct _FloatNumsTrackerClass FloatNumsTrackerClass;


#include "bintree.h"

#define FLOATNUMSTRACKER_TYPE             (floatnumstracker_get_type())
#define FLOATNUMSTRACKER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FLOATNUMSTRACKER_TYPE,FloatNumsTracker))
#define FLOATNUMSTRACKER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FLOATNUMSTRACKER_TYPE,FloatNumsTrackerClass))
#define FLOATNUMSTRACKER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FLOATNUMSTRACKER_TYPE))
#define FLOATNUMSTRACKER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FLOATNUMSTRACKER_TYPE))
#define FLOATNUMSTRACKER_CAST(src)        ((FloatNumsTracker *)(src))


typedef struct _FloatNumsTrackerItem FloatNumsTrackerItem;
struct _FloatNumsTracker
{
  GObject                  object;
  GRWLock                  rwmutex;
  FloatNumsTrackerItem*    items;
  gdouble                  value_sum;
  guint32                  length;
  GstClock*                sysclock;
  GstClockTime             treshold;
  gint32                   write_index;
  gint32                   read_index;
  gint32                   counter;
};

struct _FloatNumsTrackerItem
{
  gdouble       value;
  GstClockTime  added;
};

struct _FloatNumsTrackerClass{
  GObjectClass parent_class;

};

GType floatnumstracker_get_type (void);
FloatNumsTracker *make_floatnumstracker(guint32 length, GstClockTime obsolation_treshold);
guint32 floatnumstracker_get_num(FloatNumsTracker *this);
guint64 floatnumstracker_get_last(FloatNumsTracker *this);

void
floatnumstracker_get_stats (FloatNumsTracker * this,
                         gdouble *sum,
                         gdouble *avg);
void
floatnumstracker_iterate (FloatNumsTracker * this,
                            void(*process)(gpointer,gdouble),
                            gpointer data);
void floatnumstracker_obsolate (FloatNumsTracker * this);
void floatnumstracker_reset(FloatNumsTracker *this);
void floatnumstracker_add(FloatNumsTracker *this, gdouble value);

#endif /* FLOATNUMSTRACKER_H_ */
