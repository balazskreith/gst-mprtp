/*
 * percentiletracker2.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef PERCENTILETRACKER2_H_
#define PERCENTILETRACKER2_H_

#include <gst/gst.h>

typedef struct _PercentileTracker2 PercentileTracker2;
typedef struct _PercentileTracker2Class PercentileTracker2Class;
#include "bintree2.h"

#define PERCENTILETRACKER2_TYPE             (percentiletracker2_get_type())
#define PERCENTILETRACKER2(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),PERCENTILETRACKER2_TYPE,PercentileTracker2))
#define PERCENTILETRACKER2_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),PERCENTILETRACKER2_TYPE,PercentileTracker2Class))
#define PERCENTILETRACKER2_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),PERCENTILETRACKER2_TYPE))
#define PERCENTILETRACKER2_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),PERCENTILETRACKER2_TYPE))
#define PERCENTILETRACKER2_CAST(src)        ((PercentileTracker2 *)(src))

typedef struct _PercentileTracker2Item PercentileTracker2Item;
typedef struct _PercentileTracker2PipeData{
  gint64 min,max,sum,percentile;
}PercentileTracker2PipeData;

struct _PercentileTracker2
{
  GObject                  object;
  BinTree2*                 mintree;
  BinTree2*                 maxtree;
  GRWLock                  rwmutex;
  PercentileTracker2Item*   items;
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
  gint64*                 collection;
  gboolean                 median;
  gboolean                 ready;
  guint                    required;

  void                   (*stats_pipe)(gpointer, PercentileTracker2PipeData*);
  gpointer                 stats_pipe_data;
};

struct _PercentileTracker2Item
{
  gint64        value;
  GstClockTime  added;
};

struct _PercentileTracker2Class{
  GObjectClass parent_class;

};

GType percentiletracker2_get_type (void);
PercentileTracker2 *make_percentiletracker2(
                                  guint32 length,
                                  guint percentile);

PercentileTracker2 *make_percentiletracker2_debug(
                                  guint32 length,
                                  guint percentile);

PercentileTracker2 *make_percentiletracker2_full(BinTree2CmpFunc cmp_min,
                                  BinTree2CmpFunc cmp_max,
                                  guint32 length,
                                  guint percentile);

void percentiletracker2_test(void);
void percentiletracker2_set_treshold(PercentileTracker2 *this, GstClockTime treshold);
void percentiletracker2_set_stats_pipe(PercentileTracker2 *this, void(*stats_pipe)(gpointer, PercentileTracker2PipeData*),gpointer stats_pipe_data);
guint32 percentiletracker2_get_num(PercentileTracker2 *this);
gint64 percentiletracker2_get_last(PercentileTracker2 *this);
gint64
percentiletracker2_get_stats (PercentileTracker2 * this,
                             gint64 *min,
                             gint64 *max,
                             gint64 *sum);
void percentiletracker2_obsolate (PercentileTracker2 * this);
void percentiletracker2_reset(PercentileTracker2 *this);
void percentiletracker2_add(PercentileTracker2 *this, gint64 value);

#endif /* PERCENTILETRACKER2_H_ */
