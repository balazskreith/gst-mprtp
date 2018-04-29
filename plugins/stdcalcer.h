/*
 * smanctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef STDCALCER_H_
#define STDCALCER_H_

#include <gst/gst.h>
#include "lib_swplugins.h"
#include "rcvtracker.h"
#include "mediator.h"

typedef struct _StdCalcer StdCalcer;
typedef struct _StdCalcerClass StdCalcerClass;


#define STDCALCER_TYPE             (std_calcer_get_type())
#define STDCALCER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),STDCALCER_TYPE,StdCalcer))
#define STDCALCER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),STDCALCER_TYPE,StdCalcerClass))
#define STDCALCER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),STDCALCER_TYPE))
#define STDCALCER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),STDCALCER_TYPE))
#define STDCALCER_CAST(src)        ((StdCalcer *)(src))

typedef enum{
  STDCALCER_WELFORD_MODE = 0,
  STDCALCER_EWMA_MODE = 1,
}StdCalcerMode;

struct _StdCalcer
{
  GObject object;
  GstClock* sysclock;
  guint checker_size;
  gdouble *checker_array;
  guint count, samples_num;
  gdouble confidence_level;
  gdouble previousMean, newMean, oldS, newS, std;
  gdouble t;
  gdouble last_sample;

  StdCalcerMode mode;

//  guint length;
  GstClockTime window_size;
  GQueue* recycle;
  GQueue* pushed_items;

  GstClockTime last_checked, checking_window;
};

struct _StdCalcerClass{
  GObjectClass parent_class;
};

StdCalcer* make_std_calcer(GstClockTime window_size, GstClockTime checking_window, StdCalcerMode mode);
void std_calcer_set_window_size(StdCalcer* this, GstClockTime size);
void std_calcer_print(StdCalcer* this, const gchar* name);
void std_calcer_set_confidence_level(StdCalcer* this, gdouble value);
gboolean std_calcer_do_t_probe(StdCalcer* this, gdouble assumed_mu, gdouble mu);
void std_calcer_add_value(StdCalcer* this, gdouble value);
void std_calcer_reset(StdCalcer* this);
void std_calcer_do_check(StdCalcer* this);
gdouble std_calcer_get_std(StdCalcer* this);
gdouble std_calcer_get_mean(StdCalcer* this);
gint32 std_calcer_get_total_number(StdCalcer* this);
void std_calcer_clear(StdCalcer* this);
GType
std_calcer_get_type (void);

#endif /* STDCALCER_H_ */
