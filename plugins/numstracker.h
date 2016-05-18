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


#include "bintree2.h"

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
  guint32                  length;
  GstClock*                sysclock;
  GstClockTime             treshold;
  gint32                   write_index;
  gint32                   read_index;
  gint32                   counter;
  void                    (*rem_pipe)(gpointer, gint64);
  gpointer                 rem_pipe_data;
  GList*                   plugins;
};

struct _NumsTrackerItem
{
  gint64        value;
  GstClockTime  added;
  GstClockTime  remove;
};

struct _NumsTrackerClass{
  GObjectClass parent_class;

};

typedef struct _NumsTrackerPlugin NumsTrackerPlugin;
typedef struct _NumsTrackerMinMaxPlugin NumsTrackerMinMaxPlugin;
typedef struct _NumsTrackerEWMAPlugin NumsTrackerEWMAPlugin;
typedef struct _NumsTrackerVariancePlugin NumsTrackerVariancePlugin;
typedef struct _NumsTrackerKalmanFilterPlugin NumsTrackerKalmanFilterPlugin ;
typedef struct _NumsTrackerStatPlugin NumsTrackerStatPlugin;
typedef struct _NumsTrackerStatData NumsTrackerStatData;
typedef struct _NumsTrackerTrendPlugin NumsTrackerTrendPlugin;
typedef struct _NumsTrackerEvaluatorPlugin NumsTrackerEvaluatorPlugin;

struct _NumsTrackerPlugin{
  void (*add_activator)(gpointer, gint64);
  void (*rem_activator)(gpointer, gint64);
  void (*destroyer)(gpointer);
};


struct _NumsTrackerMinMaxPlugin{
  NumsTrackerPlugin base;
  BinTree2         *tree;
  void            (*max_pipe)(gpointer, gint64);
  void            (*min_pipe)(gpointer, gint64);
  gpointer          max_pipe_data;
  gpointer          min_pipe_data;
};

struct _NumsTrackerEWMAPlugin{
  NumsTrackerPlugin base;
  gdouble           factor;
  gdouble           avg;
  void            (*avg_pipe)(gpointer, gdouble);
  gpointer          avg_pipe_data;
};

struct _NumsTrackerVariancePlugin{
  NumsTrackerPlugin base;
  gint64            sum;
  gint64            squere_sum;
  guint32           counter;
  void            (*var_pipe)(gpointer, gdouble);
  gpointer          var_pipe_data;
};

struct _NumsTrackerStatPlugin{
  NumsTrackerPlugin base;
  gint64            sum;
  gdouble           avg;
  gdouble           var;
  gdouble           dev;
  gint64            sq_sum;
  guint32           counter;
  void            (*stat_pipe)(gpointer, NumsTrackerStatData*);
  gpointer          stat_pipe_data;
};

struct _NumsTrackerStatData{
  gint64            sum;
  gdouble           avg;
  gdouble           dev;
  gdouble           var;
  guint32           num;
};


GType numstracker_get_type (void);
NumsTracker *make_numstracker(guint32 length, GstClockTime obsolation_treshold);
guint32 numstracker_get_num(NumsTracker *this);

void
numstracker_get_stats (NumsTracker * this,
                         gint64 *sum);

void numstracker_obsolate (NumsTracker * this);
void numstracker_reset(NumsTracker *this);
void numstracker_set_treshold(NumsTracker *this, GstClockTime treshold);
gboolean numstracker_find(NumsTracker *this, gint64 value);
void numstracker_add(NumsTracker *this, gint64 value);
void numstracker_add_rem_pipe(NumsTracker *this, void (*rem_pipe)(gpointer, gint64), gpointer rem_pipe_data);
void numstracker_add_with_removal(NumsTracker *this, gint64 value, GstClockTime removal);

void numstracker_iterate (NumsTracker * this,
                          void(*process)(gpointer,gint64),
                          gpointer data);

void numstracker_add_plugin(NumsTracker *this, NumsTrackerPlugin *plugin);
void numstracker_rem_plugin(NumsTracker *this, NumsTrackerPlugin *plugin);


NumsTrackerMinMaxPlugin *
make_numstracker_minmax_plugin(void (*max_pipe)(gpointer,gint64), gpointer max_data,
                               void (*min_pipe)(gpointer,gint64), gpointer min_data);

void get_numstracker_minmax_plugin_stats(NumsTrackerMinMaxPlugin *this, gint64 *max, gint64 *min);


NumsTrackerEWMAPlugin *
make_numstracker_ewma_plugin(void (*avg_pipe)(gpointer,gdouble), gpointer avg_data,
                               gdouble factor);

void get_numstracker_ewma_plugin_stats(NumsTrackerEWMAPlugin *this,
                                       gdouble *avg, gdouble *dev);


NumsTrackerStatPlugin *
make_numstracker_stat_plugin(void (*sum_pipe)(gpointer,NumsTrackerStatData*), gpointer sum_data);

void get_numstracker_stat_plugin_stats(NumsTrackerStatPlugin *this, gint64 *variance);



#endif /* NUMSTRACKER_H_ */
