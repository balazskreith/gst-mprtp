/*
 * sndtracker.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef SNDTRACKER_H_
#define SNDTRACKER_H_


#include <gst/gst.h>
#include "mprtpspath.h"

typedef struct _SndTracker SndTracker;
typedef struct _SndTrackerClass SndTrackerClass;
#define SNDTRACKER_TYPE             (sndtracker_get_type())
#define SNDTRACKER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SNDTRACKER_TYPE,SndTracker))
#define SNDTRACKER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SNDTRACKER_TYPE,SndTrackerClass))
#define SNDTRACKER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SNDTRACKER_TYPE))
#define SNDTRACKER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SNDTRACKER_TYPE))
#define SNDTRACKER_CAST(src)        ((SndTracker *)(src))

typedef struct _SndTrackerStat{
  gint32                    sent_bytes_in_1s;
  gint16                    sent_packets_in_1s;
  guint32                   total_sent_bytes;
  guint32                   total_sent_packets;
}SndTrackerStat;

typedef struct _SndTrackerStatNotifier{
  void (*callback)(gpointer udata, SndTrackerStat* stat);
  gpointer udata;
}SndTrackerStatNotifier;

struct _SndTracker
{
  GObject                   object;
  GstClock*                 sysclock;
  SlidingWindow*            packets_sw;
  GSList*                   notifiers;

  gpointer                  priv;
  SndTrackerStat            stat;
};


struct _SndTrackerClass{
  GObjectClass parent_class;
};


GType sndtracker_get_type (void);
SndTracker *make_sndtracker(void);
void sndtracker_refresh(SndTracker * this);

void sndtracker_add_packet_notifier(SndTracker * this,
                                        void (*add_callback)(gpointer udata, gpointer item),
                                        gpointer add_udata,
                                        void (*rem_callback)(gpointer udata, gpointer item),
                                        gpointer rem_udata);

void sndtracker_add_stat_notifier(SndTracker * this,
                                    void (*callback)(gpointer udata, SndTrackerStat* stat),
                                    gpointer udata);

void sndtracker_add_stat_subflow_notifier(SndTracker * this,
                                    guint8 subflow_id,
                                    void (*callback)(gpointer udata, SndTrackerStat* stat),
                                    gpointer udata);

void sndtracker_add_packet(SndTracker * this, RTPPacket* packet);
SndTrackerStat* sndtracker_get_accumulated_stat(SndTracker * this);
SndTrackerStat* sndtracker_get_subflow_stat(SndTracker * this, guint8 subflow_id);

#endif /* SNDTRACKER_H_ */
