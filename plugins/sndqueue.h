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


struct _SndQueue
{
  GObject                   object;
  GstClock*                 sysclock;
  SndSubflows*              subflows;
  GQueue*                   queues[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];

  GQueue*                   next_stack;
};


struct _SndQueueClass{
  GObjectClass parent_class;
};


GType sndqueue_get_type (void);
SndQueue *make_sndqueue(SndSubflows* subflows_db);

void sndqueue_on_subflow_joined(SndQueue* this, SndSubflow* subflow);
void sndqueue_on_subflow_detached(SndQueue* this, SndSubflow* subflow);

void sndqueue_clear(SndQueue * this);
void sndqueue_push_packet(SndQueue * this, SndPacket* packet);
SndPacket* sndqueue_pop_packet(SndQueue* this, GstClockTime* next_approve);
gboolean sndqueue_is_empty(SndQueue* this);

#endif /* SNDQUEUE_H_ */

