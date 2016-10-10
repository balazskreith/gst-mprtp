/*
 * rcvtracker.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef RCVTRACKER_H_
#define RCVTRACKER_H_


#include <gst/gst.h>
#include "rcvpackets.h"
#include "mprtpdefs.h"

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
  gint32     discarded_packets;
  gint32     recovered_packets;
}RcvTrackerStat;


typedef struct _RcvTrackerSubflowStat{
  guint16                   highest_seq;
  guint32                   total_received_bytes;
  guint32                   total_received_packets;
  guint32                   jitter;
  guint16                   cycle_num;

  gint64                    skew_median;
  gint64                    skew_min;
  gint64                    skew_max;
}RcvTrackerSubflowStat;

struct _RcvTracker
{
  GObject                   object;
  GstClock*                 sysclock;
  RcvTrackerStat            stat;
  GSList*                   joined_subflows;

  Notifier*                 on_received_packet;
  Notifier*                 on_discarded_packet;
  Notifier*                 on_lost_packet;

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

void rcvtracker_add_discarded_packet(RcvTracker* this, DiscardedPacket* discarded_packet);

void rcvtracker_add_on_received_packet_listener(RcvTracker * this,
                                        ListenerFunc callback,
                                        gpointer udata);

void rcvtracker_add_on_received_packet_listener_with_filter(RcvTracker * this,
                                                            ListenerFunc callback,
                                                            ListenerFilterFunc filter,
                                                            gpointer udata);

void rcvtracker_rem_on_received_packet_listener(RcvTracker * this, ListenerFunc callback);

void rcvtracker_add_on_discarded_packet_cb(RcvTracker * this,
                                    guint8 subflow_id,
                                    ListenerFunc callback,
                                    gpointer udata);

void rcvtracker_add_packet(RcvTracker * this, RcvPacket* packet);
RcvTrackerSubflowStat* rcvtracker_get_subflow_stat(RcvTracker * this, guint8 subflow_id);

#endif /* RCVTRACKER_H_ */
