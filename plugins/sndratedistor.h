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

typedef struct _SendingRateDistributor SendingRateDistributor;
typedef struct _SendingRateDistributorClass SendingRateDistributorClass;
#define SNDRATEDISTOR_TYPE             (sndrate_distor_get_type())
#define SNDRATEDISTOR(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SNDRATEDISTOR_TYPE,SendingRateDistributor))
#define SNDRATEDISTOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SNDRATEDISTOR_TYPE,SendingRateDistributorClass))
#define SNDRATEDISTOR_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SNDRATEDISTOR_TYPE))
#define SNDRATEDISTOR_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SNDRATEDISTOR_TYPE))
#define SNDRATEDISTOR_CAST(src)        ((SendingRateDistributor *)(src))

typedef void  (*SignalRequestFunc)(gpointer,gpointer);

struct _SendingRateDistributor
{
  GObject              object;
  GstClock*            sysclock;
  guint8*              subflows;
  guint8               controlled_num;

  guint8               available_ids[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
  guint8               available_ids_length;

  gint32               supplied_bytes;
  gint32               requested_bytes;
  gint32               movable_bytes;

  guint32              max_rate;
  guint32              min_rate;

  SignalRequestFunc    signal_request;
  gpointer             signal_controller;
};


struct _SendingRateDistributorClass{
  GObjectClass parent_class;
};


GType sndrate_distor_get_type (void);
SendingRateDistributor *make_sndrate_distor(SignalRequestFunc signal_request, gpointer controller);
void sndrate_distor_add_controllable_path(SendingRateDistributor *this,
                                 MPRTPSPath *path,
                                 guint32 sending_rate);
void sndrate_distor_measurement_update(SendingRateDistributor *this,
                                       guint8 id,
                                       RRMeasurement *measurement,
                                       guint32 media_rate);
void sndrate_distor_extract_stats(SendingRateDistributor *this,
                                  guint8 id,
                                  guint64 *median_delay,
                                  gint32  *sender_rate,
                                  gdouble *target_rate,
                                  gdouble *goodput,
                                  gdouble *next_target,
                                  gdouble *media_target);

void sndrate_distor_remove_id(SendingRateDistributor *this, guint8 id);
guint32 sndrate_distor_time_update(SendingRateDistributor *this);
guint32 sndrate_distor_get_sending_rate(SendingRateDistributor *this, guint8 id);
#endif /* SNDRATEDISTOR_H_ */
