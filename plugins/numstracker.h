/*
 * conumstracker.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef NUMSTRACKER_H_
#define NUMSTRACKER_H_

#include <gst/gst.h>

typedef struct _NumsTracker NumsTracker;
typedef struct _NumsTrackerClass NumsTrackerClass;


#include "bintree.h"

#define NUMSTRACKER_TYPE             (numstracker_get_type())
#define NUMSTRACKER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),NUMSTRACKER_TYPE,NumsTracker))
#define NUMSTRACKER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),NUMSTRACKER_TYPE,NumsTrackerClass))
#define NUMSTRACKER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),NUMSTRACKER_TYPE))
#define NUMSTRACKER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),NUMSTRACKER_TYPE))
#define NUMSTRACKER_CAST(src)        ((NumsTracker *)(src))


typedef struct _NumsTrackerItem NumsTrackerItem;
struct _NumsTracker
{
  GObject                  object;
  GRWLock                  rwmutex;
  NumsTrackerItem*         items;
  gint64                   value_sum;
  BinTree*                 tree;
  guint32                  length;
  GstClock*                sysclock;
  GstClockTime             treshold;
  gint32                   write_index;
  gint32                   read_index;
  gint32                   counter;
};

struct _NumsTrackerItem
{
  gint64        value;
  GstClockTime  added;
};

struct _NumsTrackerClass{
  GObjectClass parent_class;

};

GType numstracker_get_type (void);
NumsTracker *make_numstracker(guint32 length, GstClockTime obsolation_treshold);
NumsTracker *make_numstracker_with_tree(guint32 length, GstClockTime obsolation_treshold);
guint32 numstracker_get_num(NumsTracker *this);
guint64 numstracker_get_last(NumsTracker *this);

void
numstracker_get_stats (NumsTracker * this,
                         gint64 *sum,
                         guint64 *max,
                         guint64 *min);

void numstracker_obsolate (NumsTracker * this);
void numstracker_reset(NumsTracker *this);
void numstracker_add(NumsTracker *this, gint64 value);

#endif /* NUMSTRACKER_H_ */
