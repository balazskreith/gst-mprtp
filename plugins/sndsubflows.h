/*
 * sndsubflows.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef SNDSUBFLOWSN_H_
#define SNDSUBFLOWSN_H_

#include <gst/gst.h>
#include "mprtpspath.h"

typedef struct _SndSubflows SndSubflows;
typedef struct _SndSubflowsClass SndSubflowsClass;
typedef struct _SndSubflowsPrivate SndSubflowsPrivate;

#define SNDSUBFLOWS_TYPE             (sndsubflows_get_type())
#define SNDSUBFLOWS(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SNDSUBFLOWS_TYPE,SndSubflows))
#define SNDSUBFLOWS_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SNDSUBFLOWS_TYPE,SndSubflowsClass))
#define SNDSUBFLOWS_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SNDSUBFLOWS_TYPE))
#define SNDSUBFLOWS_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SNDSUBFLOWS_TYPE))
#define SNDSUBFLOWS_CAST(src)        ((SndSubflows *)(src))

#define SCHTREE_MAX_VALUE 128

typedef struct _SndSubflow SndSubflow;

typedef enum
{
  MPRTPS_PATH_STATE_OVERUSED     = -1,
  MPRTPS_PATH_STATE_STABLE       = 0,
  MPRTPS_PATH_STATE_UNDERUSED     = 1,
} MPRTPSPathState;

struct _SndSubflow
{
  guint8                     id;
  guint8                     mprtp_ext_header_id;
  guint16                    rtp_seq;
  guint16                    rtp_seq_cycle_num;
  guint16                    fec_seq;
  guint16                    fec_cycle_num;

  gboolean                   lossy;
  gboolean                   congested;
  gboolean                   active;

  gint32                     target_bitrate;
  guint32                    fec_interval;

  guint32                    total_sent_packets_num;
  guint32                    total_sent_payload_bytes;

  MPRTPSPathState            state;

  GstClockTime               pacing_time;
  GstClockTime               keepalive_period;

  GstClockTime               next_regular_rtcp;
  GstClockTime               report_timeout;
  RTCPIntervalMode           rtcp_interval_mode;
  CongestionControllingMode  congestion_controlling_mode;

  guint32                    sent_packet_count;
  guint32                    sent_octet_count;

  struct{
    GSList*                 on_removing;
    GSList*                 on_active_status_changed;
  }notifiers;

};




struct _SndSubflows
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;

  SndSubflow*          subflows[256];
  GSList*              added;
  GSList*              added_notifications;
  gint32               target_rate;
  guint                subflows_num;
};

struct _SndSubflowsClass{
  GObjectClass parent_class;
};

SndSubflows*
make_sndsubflows(void);

GType sndsubflows_get_type (void);

void sndsubflows_add_notifications(SndSubflows* this,void (*callback)(gpointer udata, SndSubflow* subflow), gpointer udata);
SndSubflow* sndsubflows_add(SndSubflows* this, guint8 id);
void sndsubflows_rem(SndSubflows* this, guint8 id);
void sndsubflows_iterate(SndSubflows* this, GFunc process, gpointer udata);
void sndsubflows_set_target_rate(SndSubflows* this, SndSubflow* subflow, gint32 target_rate);
gint32 sndsubflows_get_total_target(SndSubflows* this);
gint32 sndsubflows_get_subflows_num(SndSubflows* this);

void sndsubflow_add_report_arrived_notification(SndSubflow* subflow, void (*callback)(gpointer udata, SndSubflow* subflow), gpointer udata);
void sndsubflow_add_removal_notification(SndSubflow* subflow, void (*callback)(gpointer udata, SndSubflow* subflow), gpointer udata);
void sndsubflow_add_active_status_changed_notification(SndSubflow* subflow, void (*callback)(gpointer udata, SndSubflow* subflow), gpointer udata);

void sndsubflow_set_active_status(SndSubflow* subflow, gboolean active);
guint8 sndsubflow_get_flags_abs_value(SndSubflow* subflow);

#endif /* SNDSUBFLOWSN_H_ */
