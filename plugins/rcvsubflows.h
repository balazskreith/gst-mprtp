/*
 * rcvsubflows.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef RCVSUBFLOWSN_H_
#define RCVSUBFLOWSN_H_

#include <gst/gst.h>
#include "mprtpspath.h"

typedef struct _RcvSubflows RcvSubflows;
typedef struct _RcvSubflowsClass RcvSubflowsClass;
typedef struct _RcvSubflowsPrivate RcvSubflowsPrivate;

#define RCVSUBFLOWS_TYPE             (rcvsubflows_get_type())
#define RCVSUBFLOWS(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),RCVSUBFLOWS_TYPE,RcvSubflows))
#define RCVSUBFLOWS_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),RCVSUBFLOWS_TYPE,RcvSubflowsClass))
#define RCVSUBFLOWS_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),RCVSUBFLOWS_TYPE))
#define RCVSUBFLOWS_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),RCVSUBFLOWS_TYPE))
#define RCVSUBFLOWS_CAST(src)        ((RcvSubflows *)(src))

#define SCHTREE_MAX_VALUE 128

typedef struct _RcvSubflow RcvSubflow;

struct _RcvSubflow
{
  guint8                     id;

  guint32                    total_received_packets_num;
  guint32                    total_received_payload_bytes;

  GstClockTime               next_regular_rtcp;
  RTCPIntervalMode           rtcp_interval_mode;
  CongestionControllingMode  congestion_controlling_mode;

  guint64                    last_SR_report_sent;
  guint64                    last_SR_report_rcvd;

  guint32                    received_packet_count;
  guint32                    received_octet_count;

  struct{
    GSList*                 on_removing;
  }notifiers;

};




struct _RcvSubflows
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;

  RcvSubflow*          subflows[256];
  GSList*              added;
  GSList*              on_add_notifications;
  guint                subflows_num;
};

struct _RcvSubflowsClass{
  GObjectClass parent_class;
};

RcvSubflows*
make_rcvsubflows(void);

GType rcvsubflows_get_type (void);

void rcvsubflows_on_add_notifications(RcvSubflows* this,void (*callback)(gpointer udata, RcvSubflow* subflow), gpointer udata);
RcvSubflow* rcvsubflows_add(RcvSubflows* this, guint8 id);
void rcvsubflows_rem(RcvSubflows* this, guint8 id);
void rcvsubflows_iterate(RcvSubflows* this, GFunc process, gpointer udata);

void rcvsubflow_add_removal_notification(RcvSubflow* subflow, void (*callback)(gpointer udata, RcvSubflow* subflow), gpointer udata);


#endif /* RCVSUBFLOWSN_H_ */
