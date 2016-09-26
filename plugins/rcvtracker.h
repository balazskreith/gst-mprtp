/*
 * rcvtracker.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef RCVTRACKER_H_
#define RCVTRACKER_H_


#include <gst/gst.h>
#include "mprtpspath.h"

typedef struct _RcvTracker RcvTracker;
typedef struct _RcvTrackerClass RcvTrackerClass;
#define RCVTRACKER_TYPE             (rcvtracker_get_type())
#define RCVTRACKER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),RCVTRACKER_TYPE,RcvTracker))
#define RCVTRACKER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),RCVTRACKER_TYPE,RcvTrackerClass))
#define RCVTRACKER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),RCVTRACKER_TYPE))
#define RCVTRACKER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),RCVTRACKER_TYPE))
#define RCVTRACKER_CAST(src)        ((RcvTracker *)(src))

typedef struct _RcvTrackerStat{
  gdouble    min_skew;
  gdouble    max_skew;
}RcvTrackerStat;


typedef struct _RcvTrackerSubflowStat{
  guint16                   highest_seq;
  guint32                   total_received_bytes;
  guint32                   total_received_packets;
  guint32                   jitter;
}RcvTrackerSubflowStat;

typedef struct _RcvTrackerStatNotifier{
  void (*callback)(gpointer udata, RcvTrackerSubflowStat* stat);
  gpointer udata;
}RcvTrackerStatNotifier;

typedef struct _RcvTrackerPacketNotifier{
  void (*callback)(gpointer udata, RcvTrackerSubflowStat* stat);
  gpointer udata;
}RcvTrackerPacketNotifier;

struct _RcvTracker
{
  GObject                   object;
  GstClock*                 sysclock;
  RcvTrackerStat            stat;
  GSList*                   joined_subflows;
  GSList*                   stat_notifiers;
  GSList*                   packet_notifiers;

  GstClockTime              skew_minmax_updated;

  SlidingWindow*            path_skews;

  gpointer                  priv;
};


struct _RcvTrackerClass{
  GObjectClass parent_class;
};


GType rcvtracker_get_type (void);
RcvTracker *make_rcvtracker(void);
void rcvtracker_deinit_subflow(RcvTracker *this, guint8 subflow_id);
void rcvtracker_init_subflow(RcvTracker *this, guint8 subflow_id);
void rcvtracker_refresh(RcvTracker * this);

void rcvtracker_add_packet_notifier(RcvTracker * this,
                                        void (*callback)(gpointer udata, gpointer item),
                                        gpointer udata);

void rcvtracker_add_stat_notifier(RcvTracker * this,
                                    void (*callback)(gpointer udata, RcvTrackerStat* stat),
                                    gpointer udata);

void rcvtracker_add_stat_subflow_notifier(RcvTracker * this,
                                    guint8 subflow_id,
                                    void (*callback)(gpointer udata, RcvTrackerSubflowStat* stat),
                                    gpointer udata);

void rcvtracker_add_packet_on_subflow_notifier(RcvTracker * this,
                                    guint8 subflow_id,
                                    void (*callback)(gpointer udata, RcvTrackerSubflowStat* stat),
                                    gpointer udata);

void rcvtracker_add_packet(RcvTracker * this, RTPPacket* packet);
RcvTrackerSubflowStat* rcvtracker_get_subflow_stat(RcvTracker * this, guint8 subflow_id);

#endif /* RCVTRACKER_H_ */
