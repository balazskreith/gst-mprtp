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
  GstClock*                 sysclock;
  gpointer                  subflows;
  guint8                    controlled_num;

  guint8                    available_ids[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
  guint8                    available_ids_length;
  GstClockTime              initial_disabling_time;

  gint32                    extra_rate;
  gint32                    target_bitrate;
  gint32                    delta_rate;

  PacketsSndQueue*          pacer;
  StreamSplitter*           splitter;

  gint32                    supplied_bitrate;
  gint32                    requested_bitrate;

  MPRTPPluginUtilization    ur;
  gboolean                  ready;
};


struct _SendingRateDistributorClass{
  GObjectClass parent_class;
};


GType sndrate_distor_get_type (void);
SendingRateDistributor *make_sndrate_distor(void);
void sndrate_distor_setup(SendingRateDistributor *this, StreamSplitter *splitter, PacketsSndQueue *pacer);
void sndrate_setup_report(
    SendingRateDistributor *this,
    guint8 id,
    struct _SubflowUtilizationReport *report);
void sndrate_distor_add_controlled_subflow(SendingRateDistributor *this, guint8 id);
void sndrate_distor_rem_controlled_subflow(SendingRateDistributor *this, guint8 id);
MPRTPPluginUtilization* sndrate_distor_time_update(SendingRateDistributor *this);
void sndrate_set_initial_disabling_time(SendingRateDistributor *this, guint64 initial_disabling_time);
guint32 sndrate_distor_get_sending_rate(SendingRateDistributor *this, guint8 id);
#endif /* SNDRATEDISTOR_H_ */
