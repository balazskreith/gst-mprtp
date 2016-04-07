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
#include "streamsplitter.h"
#include "packetssndqueue.h"

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
  GObject                   object;
  GRWLock                   rwmutex;
  GstClock*                 sysclock;
  GHashTable*               subflows;

  StreamSplitter*           splitter;

  gboolean                  urgent_rescheduling;

  GstClockTime              last_subflow_refresh;
  GstClockTime              next_splitter_refresh;

  gint32                    target_media_rate;
};


struct _SendingRateDistributorClass{
  GObjectClass parent_class;
};


GType sndrate_distor_get_type (void);
SendingRateDistributor *make_sndrate_distor(StreamSplitter *splitter);

void sndrate_distor_refresh(SendingRateDistributor* this);
void sndrate_distor_add_subflow(SendingRateDistributor *this, MPRTPSPath *path);
void sndrate_distor_rem_subflow(SendingRateDistributor *this, guint8 subflow_id);
#endif /* SNDRATEDISTOR_H_ */
