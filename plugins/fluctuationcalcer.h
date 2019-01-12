/*
 * fluctuationcalcer.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FLUCTUATIONCALCER_H_
#define FLUCTUATIONCALCER_H_

#include <gst/gst.h>
#include "gstmprtcpbuffer.h"
#include "reportprod.h"
#include "lib_swplugins.h"

typedef struct _FluctuationCalcer      FluctuationCalcer;
typedef struct _FluctuationCalcerClass FluctuationCalcerClass;

#define FLUCTUATIONCALCER_TYPE             (fluctuationcalcer_get_type())
#define FLUCTUATIONCALCER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FLUCTUATIONCALCER_TYPE,FluctuationCalcer))
#define FLUCTUATIONCALCER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FLUCTUATIONCALCER_TYPE,FluctuationCalcerClass))
#define FLUCTUATIONCALCER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FLUCTUATIONCALCER_TYPE))
#define FLUCTUATIONCALCER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FLUCTUATIONCALCER_TYPE))
#define FLUCTUATIONCALCER_CAST(src)        ((FluctuationCalcer *)(src))

typedef struct _CorrBlock CorrBlock;

#define FRACTALPRODUCER_CHUNKS_MAX_LENGTH 64

typedef GstClockTime(*FluctuationCalcerTimeValidityProvider)(gpointer udata);

struct _FluctuationCalcer
{
  GObject                  object;
  GstClock*                sysclock;

  FluctuationCalcerTimeValidityProvider time_validity_provider;
  gpointer time_validity_provider_udata;
  
  double good;
  double bad;
  GQueue* items;
  GQueue* recycle;
};

struct _FluctuationCalcerClass{
  GObjectClass parent_class;
};

GType fluctuationcalcer_get_type (void);
FluctuationCalcer *make_fluctuationcalcer(void);
void fluctuationcalcer_setup_time_threshold_provider(FluctuationCalcer *this, FluctuationCalcerTimeValidityProvider time_validity_provider, gpointer udata);
void fluctuationcalcer_add_good_measurement(FluctuationCalcer *this, double value);
void fluctuationcalcer_add_bad_measurement(FluctuationCalcer *this, double value);
gdouble fluctuationcalcer_get_stability_score(FluctuationCalcer *this);
void fluctuationcalcer_reset(FluctuationCalcer *this);


#endif /* FLUCTUATIONCALCER_H_ */
