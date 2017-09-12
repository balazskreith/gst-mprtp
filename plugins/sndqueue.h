/*
 * sndqueue.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef SNDQUEUE_H_
#define SNDQUEUE_H_


#include <gst/gst.h>
#include "sndsubflows.h"
#include "sndpackets.h"
#include "slidingwindow.h"

typedef struct _SndQueue SndQueue;
typedef struct _SndQueueClass SndQueueClass;
#define SNDQUEUE_TYPE             (sndqueue_get_type())
#define SNDQUEUE(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SNDQUEUE_TYPE,SndQueue))
#define SNDQUEUE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SNDQUEUE_TYPE,SndQueueClass))
#define SNDQUEUE_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SNDQUEUE_TYPE))
#define SNDQUEUE_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SNDQUEUE_TYPE))
#define SNDQUEUE_CAST(src)        ((SndQueue *)(src))

typedef struct _RTPQueueStat{
  gint32                    bytes_in_queue;
  gint16                    total_packets_in_queue;
  gdouble                   rtpq_delay;
  volatile gint32           queued_bytes[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
  volatile gint32           actual_bitrates[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
  volatile gint32           packets_in_queue[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
  volatile gint32           total_bitrate;
  gint32                    total_pushed_packets;
}RTPQueueStat;

struct _SndQueue
{
  GObject                   object;
  GstClock*                 sysclock;
  SndSubflows*              subflows;
  SndSubflowState           tracked_states[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
  gint32                    num_subflow_overused;
  gboolean                  queued_bytes_considered;
//  RTPQueueStat*             rtpqstat;
  guint                     dropping_policy;
  RTPQueueStat              stat;
  GQueue*                   packets[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
  GQueue*                   unqueued_packets;
  gdouble                   threshold;
//  volatile gint32           total_queued_bytes;
  volatile gint32           actual_targets[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
  volatile gint32           pacing_bitrate[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
  volatile gboolean         empty;
  volatile gint32           total_bitrate;
  volatile gint32           total_target;
  guint32                   clear_end_ts;
  Notifier*                 on_packet_queued;
};


struct _SndQueueClass{
  GObjectClass parent_class;
};


GType sndqueue_get_type (void);
SndQueue *make_sndqueue(SndSubflows* subflows_db);

void sndqueue_add_on_packet_queued(SndQueue * this, ListenerFunc callback, gpointer udata);
void sndqueue_on_subflow_joined(SndQueue* this, SndSubflow* subflow);
void sndqueue_on_subflow_detached(SndQueue* this, SndSubflow* subflow);
void sndqueue_on_subflow_state_changed(SndQueue* this, SndSubflow* subflow);
void sndqueue_on_subflow_target_bitrate_changed(SndQueue* this, SndSubflow* subflow);

void sndqueue_push_packet(SndQueue * this, SndPacket* packet);
SndPacket* sndqueue_pop_packet(SndQueue* this, GstClockTime* next_approve);
gboolean sndqueue_is_empty(SndQueue* this);
RTPQueueStat* sndqueue_get_stat(SndQueue* this);
#endif /* SNDQUEUE_H_ */

