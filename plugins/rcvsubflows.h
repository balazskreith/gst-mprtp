/*
 * rcvsubflows.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef RCVSUBFLOWSN_H_
#define RCVSUBFLOWSN_H_

#include <gst/gst.h>
#include "observer.h"

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
  SndSubflows*               base_db;

  guint32                    total_lost_packets;
  guint32                    total_received_packets;
  guint32                    total_received_payload_bytes;
  guint16                    highest_seq;

  GstClockTime               next_regular_rtcp;
  RTCPIntervalType           rtcp_interval_mode;
  CongestionControllingType  congestion_controlling_mode;

  guint64                    last_SR_report_sent;
  guint64                    last_SR_report_rcvd;

  guint32                    received_packet_count;
  guint32                    received_octet_count;

  GstClockTime               report_timeout;
  RTCPIntervalType           rtcp_interval_type;
  CongestionControllingType  congestion_controlling_type;

  Observer*                  on_rtcp_time_update;
};


struct _RcvSubflows
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;

  RcvSubflow*          subflows[256];
  GSList*              joined;

  GQueue*              changed_subflows;

  Observer*            on_subflow_detached;
  Observer*            on_subflow_joined;
  Observer*            on_congestion_controlling_type_changed;

  guint                subflows_num;

};

struct _RcvSubflowsClass{
  GObjectClass parent_class;
};

RcvSubflows*
make_rcvsubflows(void);

GType rcvsubflows_get_type(void);

void rcvsubflows_iterate(RcvSubflows* this, GFunc process, gpointer udata);

void rcvsubflows_set_congestion_controlling_type(RcvSubflows* this, guint8 subflow_id, CongestionControllingType new_type);
void rcvsubflows_set_path_active(RcvSubflows* this, guint8 subflow_id, gboolean value);
void rcvsubflows_set_rtcp_interval_type(RcvSubflows* this, guint8 subflow_id, RTCPIntervalType new_type);
void rcvsubflows_set_path_lossy(RcvSubflows* this, guint8 subflow_id, gboolean value);
void rcvsubflows_set_path_congested(RcvSubflows* this, guint8 subflow_id, gboolean value);


void rcvsubflows_add_on_subflow_joined_cb(RcvSubflows* this, NotifierFunc callback, gpointer udata);
void rcvsubflows_add_on_subflow_detached_cb(RcvSubflows* this, NotifierFunc callback, gpointer udata);
void rcvsubflows_add_on_congestion_controlling_type_changed_cb(RcvSubflows* this, NotifierFunc callback, gpointer udata);

void rcvsubflow_notify_rtcp_fb_cbs(RcvSubflow* subflow, gpointer udata);
void rcvsubflow_add_on_rtcp_fb_cb(RcvSubflow* subflow, NotifierFunc callback, gpointer udata);
void rcvsubflow_rem_on_rtcp_fb_cb(RcvSubflow* subflow, NotifierFunc callback);

#endif /* RCVSUBFLOWSN_H_ */
