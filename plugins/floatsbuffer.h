/*
 * cofloatsbuffer.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FLOATSBUFFER_H_
#define FLOATSBUFFER_H_

#include <gst/gst.h>

typedef struct _FloatsBuffer FloatsBuffer;
typedef struct _FloatsBufferClass FloatsBufferClass;


#include "bintree.h"

#define FLOATSBUFFER_TYPE             (floatsbuffer_get_type())
#define FLOATSBUFFER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FLOATSBUFFER_TYPE,FloatsBuffer))
#define FLOATSBUFFER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FLOATSBUFFER_TYPE,FloatsBufferClass))
#define FLOATSBUFFER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FLOATSBUFFER_TYPE))
#define FLOATSBUFFER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FLOATSBUFFER_TYPE))
#define FLOATSBUFFER_CAST(src)        ((FloatsBuffer *)(src))


typedef struct _FloatsBufferItem FloatsBufferItem;
typedef struct _FloatsBufferPlugin FloatsBufferPlugin;
typedef struct _FloatsBufferEvaluator FloatsBufferEvaluator;
typedef struct _FloatsBufferStatData FloatsBufferStatData;

struct _FloatsBufferStatData{
  gdouble         avg;
  gdouble         sum;
  gdouble         dev;
  gdouble         var;
};

typedef enum{
  FLOATSBUFFER_FIRE_AT_ADD = 1,
  FLOATSBUFFER_FIRE_AT_REM = 2,
}FloatsBufferEvaluatorFire;

struct _FloatsBuffer
{
  GObject                  object;
  GRWLock                  rwmutex;
  FloatsBufferItem*        items;
  gdouble                  sum;
  gdouble                  sq_sum;
  gdouble                  avg;
  gdouble                  dev;
  gdouble                  var;
  guint32                  length;
  GstClock*                sysclock;
  GstClockTime             treshold;
  GList*                   plugins;
  void                   (*stat_pipe)(gpointer, FloatsBufferStatData*);
  gpointer                 stat_pipe_data;
  GList*                   evaluators;
  gint32                   write_index;
  gint32                   read_index;
  gint32                   counter;


};

struct _FloatsBufferItem
{
  gdouble       value;
  GstClockTime  added;
  GstClockTime  remove;
};

struct _FloatsBufferClass{
  GObjectClass parent_class;

};

struct _FloatsBufferPlugin{
  void (*add_activator)(gpointer, gdouble);
  void (*rem_activator)(gpointer, gdouble);
  void (*destroyer)(gpointer);
};

struct _FloatsBufferEvaluator{
  void                    (*destroyer)(gpointer);
  guint                     activator_filter;
  void                    (*iterator)(gpointer, gdouble);
  gpointer                  iterator_data;
};


GType floatsbuffer_get_type (void);
FloatsBuffer *make_floatsbuffer(guint32 length, GstClockTime obsolation_treshold);
guint32 floatsbuffer_get_num(FloatsBuffer *this);
guint64 floatsbuffer_get_last(FloatsBuffer *this);

void
floatsbuffer_get_stats (FloatsBuffer * this,
                         gdouble *sum,
                         gdouble *avg);
void
floatsbuffer_iterate (FloatsBuffer * this,
                            void(*process)(gpointer,gdouble),
                            gpointer data);
void floatsbuffer_obsolate (FloatsBuffer * this);
void floatsbuffer_reset(FloatsBuffer *this);


void floatsbuffer_add_full(FloatsBuffer *this, gdouble value, GstClockTime remove);
#define floatsbuffer_add(this, value) floatsbuffer_add_full(this, value, 0)

void floatsbuffer_set_stats_pipe(FloatsBuffer *this, void (*pipe)(gpointer,FloatsBufferStatData*), gpointer pipe_data);
void floatsbuffer_add_plugin(FloatsBuffer *this, FloatsBufferPlugin *plugin);
void floatsbuffer_rem_plugin(FloatsBuffer *this, FloatsBufferPlugin *plugin);

FloatsBufferEvaluator *
floatsbuffer_add_evaluator(FloatsBuffer *this,
    guint activator_filter,
    void (*iterator)(gpointer, gdouble), gpointer iterator_data);

void floatsbuffer_rem_evaluator(FloatsBuffer *this, FloatsBufferEvaluator *evaluator);

#endif /* FLOATSBUFFER_H_ */
