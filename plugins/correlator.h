/*
 * smanctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef CORRELATOR_H_
#define CORRELATOR_H_

#include <gst/gst.h>
#include "lib_datapuffer.h"
#include "notifier.h"


typedef struct _Correlator Correlator;
typedef struct _CorrelatorClass CorrelatorClass;


#define CORRELATOR_TYPE             (correlator_get_type())
#define CORRELATOR(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),CORRELATOR_TYPE,Correlator))
#define CORRELATOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),CORRELATOR_TYPE,CorrelatorClass))
#define CORRELATOR_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),CORRELATOR_TYPE))
#define CORRELATOR_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),CORRELATOR_TYPE))
#define CORRELATOR_CAST(src)        ((Correlator *)(src))


#define correlator_define_on_calculated_listener(this_type, on_calculated_fnc, on_calculated_field) \
    static void on_calculated_fnc(gpointer udata, gpointer result){ \
      ((this_type*)udata)->on_calculated_field = *(gdouble*)result; \
    }

#define correlator_define_dataextractor(extractor_fnc, item_type, extracted_field) \
    static gdouble extractor_fnc(gpointer itemptr){ \
      return ((item_type*)itemptr)->extracted_field; \
    }


typedef gdouble (*CorrelatorDataExtractor)(gpointer);


typedef struct _CorrelatorChannel{
  gint          counter;
  datapuffer_t* samples;
  datapuffer_t* variances;
  guint64       samples_sum;
  gdouble       samples_avg;
  gdouble       variance_sum;
  gdouble       variance;
  gdouble       std;

}CorrelatorChannel;


typedef struct _CorrelationFuncPoint{
  gdouble covariance_x;
  gdouble covariance_y;
  gdouble covariance_xy;
  gdouble count;
  gdouble tau;
}CorrelatorPoint;

typedef gdouble (*CorrelatorNormalizer)(CorrelatorPoint*);

struct _Correlator
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;

  gdouble              covariance;
  gdouble              covariance_sum;
  datapuffer_t*        covariances;
  datapuffer_t*        delay_puffer;
  CorrelatorChannel    x_ch;
  CorrelatorChannel    y_ch;
  GQueue*              uint_recycle;
  GQueue*              double_recycle;
  Notifier*            on_correlation_calculated;

  gint                 tau;
  gint                 max_length;
  gint32               accumulation_length;

  CorrelatorPoint point;

  gdouble Gxy,g;

  CorrelatorDataExtractor Ix_extractor;
  CorrelatorDataExtractor Iy_extractor;

};
struct _CorrelatorClass{
  GObjectClass parent_class;
};

Correlator*
make_correlator(gint32 tau, gint32 length);

void
correlator_set_tau(Correlator* this, gint32 tau);

void
correlator_set_accumulation_length(Correlator* this, gint32 accumulation_length);

void
correlator_add_on_correlation_calculated_listener(Correlator* this, ListenerFunc listener, gpointer udata);

void
correlator_add_extractors(Correlator* this, CorrelatorDataExtractor Ix_extractor, CorrelatorDataExtractor Iy_extractor);

void
correlator_add_data(Correlator* this, gpointer data);

void
correlator_add_sample(Correlator* this, guint x);

void
correlator_add_samples(Correlator* this, guint x, guint y);

GType
correlator_get_type (void);

#endif /* CORRELATOR_H_ */
