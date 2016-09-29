/*
 * sndsubflows.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef SNDSUBFLOWSN_H_
#define SNDSUBFLOWSN_H_

#include <gst/gst.h>
#include "mprtpdefs.h"

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
  SNDSUBFLOW_STATE_OVERUSED     = -1,
  SNDSUBFLOW_STATE_STABLE       = 0,
  SNDSUBFLOW_STATE_UNDERUSED     = 1,
} SndSubflowState;

struct _SndSubflow
{
  guint8                     id;
  SndSubflows*               base_db;
  SubflowSeqTrack            seqtracker;

  gboolean                   lossy;
  gboolean                   congested;
  gboolean                   active;

  gint32                     target_bitrate;
  guint32                    fec_interval;
  guint32                    packet_counter_for_fec;

  guint32                    total_sent_packets_num;
  guint32                    total_sent_payload_bytes;

  SndSubflowState            state;

  GstClockTime               pacing_time;

  GstClockTime               next_regular_rtcp;
  GstClockTime               report_timeout;
  RTCPIntervalType           rtcp_interval_type;
  CongestionControllingType  congestion_controlling_type;

  guint32                    sent_packet_count;
  guint32                    sent_octet_count;

  Observer*                  on_stat_changed;
  Observer*                  on_packet_sent;
};




struct _SndSubflows
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;

  SndSubflow*          subflows[256];

  GQueue*              changed_subflows;
  GSList*              joined;

  Observer*            on_subflow_detached;
  Observer*            on_subflow_joined;
  Observer*            on_congestion_controlling_type_changed;
  Observer*            on_path_active_changed;

  gint32               target_rate;
  guint                subflows_num;
};

struct _SndSubflowsClass{
  GObjectClass parent_class;
};

SndSubflows*
make_sndsubflows(void);

GType sndsubflows_get_type (void);

void sndsubflows_join(SndSubflows* this, guint8 id);
void sndsubflows_detach(SndSubflows* this, guint8 id);
void sndsubflows_iterate(SndSubflows* this, GFunc process, gpointer udata);

void sndsubflows_set_target_rate(SndSubflows* this, SndSubflow* subflow, gint32 target_rate);
gint32 sndsubflows_get_total_target(SndSubflows* this);
gint32 sndsubflows_get_subflows_num(SndSubflows* this);
SndSubflow* sndsubflows_get_subflow(SndSubflows* this, guint8 subflow_id);

void sndsubflows_set_mprtp_ext_header_id(SndSubflows* this, guint8 mprtp_ext_header_id);
guint8 sndsubflows_get_mprtp_ext_header_id(SndSubflows* this);


void sndsubflows_set_congestion_controlling_type(SndSubflows* this, guint8 subflow_id, CongestionControllingType new_type);
void sndsubflows_set_path_active(SndSubflows* this, guint8 subflow_id, gboolean value);
void sndsubflows_set_rtcp_interval_type(SndSubflows* this, guint8 subflow_id, RTCPIntervalType new_type);
void sndsubflows_set_path_lossy(SndSubflows* this, guint8 subflow_id, gboolean value);
void sndsubflows_set_path_congested(SndSubflows* this, guint8 subflow_id, gboolean value);

void sndsubflows_add_on_subflow_joined_cb(SndSubflows* this, NotifierFunc callback, gpointer udata);
void sndsubflows_add_on_subflow_detached_cb(SndSubflows* this, NotifierFunc callback, gpointer udata);
void sndsubflows_add_on_congestion_controlling_type_changed_cb(SndSubflows* this, NotifierFunc callback, gpointer udata);
void sndsubflows_add_on_path_active_changed_cb(SndSubflows* this, NotifierFunc callback, gpointer udata);



//------------------------------------------------------------------------------------------------
void sndsubflow_on_stat_changed_cb(SndSubflow* subflow, NotifierFunc callback, gpointer udata);
void sndsubflow_add_on_packet_sent_cb(SndSubflow* subflow, NotifierFunc callback, gpointer udata);
void sndsubflow_set_state(SndSubflow* this, SndSubflowState state);
SndSubflowState sndsubflow_get_state(SndSubflow* subflow);
guint16 sndsubflow_get_next_subflow_seq(SndSubflow* subflow);

guint8 sndsubflow_get_flags_abs_value(SndSubflow* subflow);

gboolean sndsubflow_fec_requested(SndSubflow* subflow);


#endif /* SNDSUBFLOWSN_H_ */
