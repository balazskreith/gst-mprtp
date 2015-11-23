/*
 * sndrate_distor.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef SNDRATEDISTOR_H_
#define SNDRATEDISTOR_H_

#include <gst/gst.h>
#include "mprtpspath.h"
#include "streamtracker.h"

typedef struct _SendingRateDistributor SendingRateDistributor;
typedef struct _SendingRateDistributorClass SendingRateDistributorClass;
#define SNDRATEDISTOR_TYPE             (sndrate_distor_get_type())
#define SNDRATEDISTOR(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SNDRATEDISTOR_TYPE,SendingRateDistributor))
#define SNDRATEDISTOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SNDRATEDISTOR_TYPE,SendingRateDistributorClass))
#define SNDRATEDISTOR_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SNDRATEDISTOR_TYPE))
#define SNDRATEDISTOR_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SNDRATEDISTOR_TYPE))
#define SNDRATEDISTOR_CAST(src)        ((SendingRateDistributor *)(src))

#define SNDRATEDISTOR_MAX_NUM 32
typedef void  (*SignalRequestFunc)(gpointer,gpointer);

struct _SendingRateDistributor
{
  GObject              object;
  GstClock*            sysclock;
  guint8*              subflows;
  guint8               max_id;
  guint32              media_rate;
  GQueue*              free_ids;
  guint8               counter;

  guint32              stable_sr_sum;
  guint32              taken_bytes;
  guint32              supplied_bytes;
  guint32              requested_bytes;
  guint32              fallen_bytes;
  guint32              overused_bytes;

  guint8               load_controlling;
  guint8               prev_controlling;

  SignalRequestFunc    signal_request;
  gpointer             signal_controller;
};

struct _SendingRateDistributorClass{
  GObjectClass parent_class;
};


GType sndrate_distor_get_type (void);
SendingRateDistributor *make_sndrate_distor(SignalRequestFunc signal_request, gpointer controller);
guint8 sndrate_distor_request_id(SendingRateDistributor *this,
                                 MPRTPSPath *path,
                                 guint32 sending_rate);
void sndrate_distor_measurement_update(SendingRateDistributor *this,
                                       guint8 id,
                                       guint32 goodput,
                                       guint32 receiver_rate,
                                       gdouble variance,
                                       gdouble corrh_owd,
                                       gdouble corrl_owd);
void sndrate_distor_remove_id(SendingRateDistributor *this, guint8 id);
void sndrate_distor_time_update(SendingRateDistributor *this, guint32 media_rate);
guint32 sndrate_distor_get_sending_rate(SendingRateDistributor *this, guint8 id);
#endif /* SNDRATEDISTOR_H_ */
