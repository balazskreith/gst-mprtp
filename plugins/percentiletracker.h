/*
 * percentiletracker.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef PERCENTILETRACKER_H_
#define PERCENTILETRACKER_H_

#include <gst/gst.h>

typedef struct _PercentileTracker PercentileTracker;
typedef struct _PercentileTrackerClass PercentileTrackerClass;
#include "bintree.h"

#define PERCENTILETRACKER_TYPE             (percentiletracker_get_type())
#define PERCENTILETRACKER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),PERCENTILETRACKER_TYPE,PercentileTracker))
#define PERCENTILETRACKER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),PERCENTILETRACKER_TYPE,PercentileTrackerClass))
#define PERCENTILETRACKER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),PERCENTILETRACKER_TYPE))
#define PERCENTILETRACKER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),PERCENTILETRACKER_TYPE))
#define PERCENTILETRACKER_CAST(src)        ((PercentileTracker *)(src))

typedef struct _PercentileTrackerItem PercentileTrackerItem;
typedef struct _PercentileTrackerPipeData{
  guint64 min,max,sum,percentile;
}PercentileTrackerPipeData;

struct _PercentileTracker
{
  GObject                  object;
  BinTree*                 mintree;
  BinTreeCmpFunc           mintree_cmp;
  BinTree*                 maxtree;
  BinTreeCmpFunc           maxtree_cmp;
  GRWLock                  rwmutex;
  PercentileTrackerItem*   items;
  gboolean                 debug;
  gint64                   sum;
  guint8                   percentile;
  guint32                  length;
  GstClock*                sysclock;
  GstClockTime             treshold;
  gint32                   write_index;
  gint32                   read_index;
  gint32                   counter;
  gint32                   Mxc, Mnc;
  gdouble                  ratio;
  guint64*                 collection;
  gboolean                 median;
  gboolean                 ready;
  guint                    required;

  guint32                (*expandfnc)(GstClockTime);

  void                   (*stats_pipe)(gpointer, PercentileTrackerPipeData*);
  gpointer                 stats_pipe_data;
};

struct _PercentileTrackerItem
{
  gint64        value;
  GstClockTime  added;
};

struct _PercentileTrackerClass{
  GObjectClass parent_class;

};

GType percentiletracker_get_type (void);
PercentileTracker *make_percentiletracker(
                                  guint32 length,
                                  guint percentile);

PercentileTracker *make_percentiletracker_debug(
                                  guint32 length,
                                  guint percentile);

PercentileTracker *make_percentiletracker_full(BinTreeCmpFunc cmp_min,
                                  BinTreeCmpFunc cmp_max,
                                  guint32 length,
                                  guint percentile);

void percentiletracker_test(void);
void percentiletracker_set_treshold(PercentileTracker *this, GstClockTime treshold);
void percentiletracker_set_expandfnc(PercentileTracker *this, guint32 (*expandfnc)(GstClockTime));
void percentiletracker_set_stats_pipe(PercentileTracker *this, void(*stats_pipe)(gpointer, PercentileTrackerPipeData*),gpointer stats_pipe_data);
guint32 percentiletracker_get_num(PercentileTracker *this);
guint64 percentiletracker_get_last(PercentileTracker *this);
guint64
percentiletracker_get_stats (PercentileTracker * this,
                             guint64 *min,
                             guint64 *max,
                             guint64 *sum);
void percentiletracker_obsolate (PercentileTracker * this);
void percentiletracker_reset(PercentileTracker *this);
void percentiletracker_add(PercentileTracker *this, guint64 value);

#endif /* PERCENTILETRACKER_H_ */
