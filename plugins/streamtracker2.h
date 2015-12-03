/*
 * streamtracker2.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef STREAMTRACKER2_H_
#define STREAMTRACKER2_H_

#include <gst/gst.h>

typedef struct _StreamTracker2 StreamTracker2;
typedef struct _StreamTracker2Class StreamTracker2Class;

#include "bintree.h"

#define STREAMTRACKER2_TYPE             (streamtracker2_get_type())
#define STREAMTRACKER2(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),STREAMTRACKER2_TYPE,StreamTracker2))
#define STREAMTRACKER2_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),STREAMTRACKER2_TYPE,StreamTracker2Class))
#define STREAMTRACKER2_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),STREAMTRACKER2_TYPE))
#define STREAMTRACKER2_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),STREAMTRACKER2_TYPE))
#define STREAMTRACKER2_CAST(src)        ((StreamTracker2 *)(src))

typedef struct _StreamTracker2Item StreamTracker2Item;
struct _StreamTracker2
{
  GObject                  object;
  GRWLock                  rwmutex;
  StreamTracker2Item*      items;
  guint64                  sum;
  guint32                  length;
  GstClock*                sysclock;
  GstClockTime             treshold;
  gint32                   write_index;
  gint32                   read_index;
  gint32                   counter;
};

struct _StreamTracker2Item
{
  guint64       value;
  GstClockTime  added;
};

struct _StreamTracker2Class{
  GObjectClass parent_class;

};

GType streamtracker2_get_type (void);
StreamTracker2 *make_streamtracker2(guint32 length);
void streamtracker2_set_treshold(StreamTracker2 *this, GstClockTime treshold);
guint32 streamtracker2_get_num(StreamTracker2 *this);
guint64 streamtracker2_get_last(StreamTracker2 *this);
void streamtracker2_get_stats (StreamTracker2 * this,
                         guint64 *sum,
                         guint32 *items_num);
void streamtracker2_obsolate (StreamTracker2 * this);
void streamtracker2_reset(StreamTracker2 *this);
void streamtracker2_add(StreamTracker2 *this, guint64 value);

#endif /* STREAMTRACKER2_H_ */
