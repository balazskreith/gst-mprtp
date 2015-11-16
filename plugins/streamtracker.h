/*
 * streamtracker.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef STREAMTRACKER_H_
#define STREAMTRACKER_H_

#include <gst/gst.h>

typedef struct _StreamTracker StreamTracker;
typedef struct _StreamTrackerClass StreamTrackerClass;

#include "bintree.h"

#define STREAMTRACKER_TYPE             (streamtracker_get_type())
#define STREAMTRACKER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),STREAMTRACKER_TYPE,StreamTracker))
#define STREAMTRACKER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),STREAMTRACKER_TYPE,StreamTrackerClass))
#define STREAMTRACKER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),STREAMTRACKER_TYPE))
#define STREAMTRACKER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),STREAMTRACKER_TYPE))
#define STREAMTRACKER_CAST(src)        ((StreamTracker *)(src))

typedef struct _StreamTrackerItem StreamTrackerItem;
struct _StreamTracker
{
  GObject                  object;
  BinTree*                 mintree;
  BinTree*                 maxtree;
  GRWLock                  rwmutex;
  StreamTrackerItem*       items;
  guint32                  length;
  GstClock*                sysclock;
  GstClockTime             treshold;
  guint32                  write_index;
  guint32                  read_index;
  gdouble                  max_multiplier;
};

struct _StreamTrackerItem
{
  guint64       value;
  GstClockTime  added;
};

struct _StreamTrackerClass{
  GObjectClass parent_class;

};

GType streamtracker_get_type (void);
StreamTracker *make_streamtracker(BinTreeCmpFunc cmp_min,
                                  BinTreeCmpFunc cmp_max,
                                  guint32 length,
                                  guint percentile);
void streamtracker_test(void);
void streamtracker_set_treshold(StreamTracker *this, GstClockTime treshold);
guint32 streamtracker_get_num(StreamTracker *this);
guint64
streamtracker_get_stats (StreamTracker * this,
                         guint64 *min,
                         guint64 *max);
void streamtracker_reset(StreamTracker *this);
void streamtracker_add(StreamTracker *this, guint64 value);

#endif /* STREAMTRACKER_H_ */
