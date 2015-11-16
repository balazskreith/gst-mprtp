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

struct _SendingRateDistributor
{
  GObject           object;
  GstClock*         sysclock;
  GHashTable*       subflows;
  guint8            max_id;
  guint32           media_rate;
  GQueue*           free_ids;
  guint8            SCM[SNDRATEDISTOR_MAX_NUM][SNDRATEDISTOR_MAX_NUM];
  guint8            undershooted[SNDRATEDISTOR_MAX_NUM];
  gint64            undershoot_delta[SNDRATEDISTOR_MAX_NUM];
  guint8            bounced_back[SNDRATEDISTOR_MAX_NUM];
  gint64            bounced_back_delta[SNDRATEDISTOR_MAX_NUM];
  guint32           extra_bytes[SNDRATEDISTOR_MAX_NUM];
  gint32            delta_sending_rates[SNDRATEDISTOR_MAX_NUM];
  guint8            counter;
};

struct _SendingRateDistributorClass{
  GObjectClass parent_class;
};


GType sndrate_distor_get_type (void);
SendingRateDistributor *make_sndrate_distor(void);
guint8 sndrate_distor_request_id(SendingRateDistributor *this, MPRTPSPath *path);
void sndrate_distor_measurement_update(SendingRateDistributor *this,
                                       guint8 id,
                                       gfloat goodput,
                                       gdouble variance);
void sndrate_distor_remove_id(SendingRateDistributor *this, guint8 id);
void sndrate_distor_undershoot(SendingRateDistributor *this, guint8 id);
void sndrate_distor_bounce_back(SendingRateDistributor *this, guint8 id);
void sndrate_distor_keep(SendingRateDistributor *this, guint8 id);
void sndrate_distor_time_update(SendingRateDistributor *this, guint32 media_rate);
guint32 sndrate_distor_get_rate(SendingRateDistributor *this, guint8 id);
#endif /* SNDRATEDISTOR_H_ */
