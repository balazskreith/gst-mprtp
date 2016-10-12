/*
 * smanctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef PACINGQUEUE_H_
#define PACINGQUEUE_H_

#include <gst/gst.h>
#include "lib_swplugins.h"
#include "rcvtracker.h"
#include "mediator.h"

typedef struct _PacingQueue PacingQueue;
typedef struct _PacingQueueClass PacingQueueClass;


#define PACINGQUEUE_TYPE             (pacing_queue_get_type())
#define PACINGQUEUE(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),PACINGQUEUE_TYPE,PacingQueue))
#define PACINGQUEUE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),PACINGQUEUE_TYPE,PacingQueueClass))
#define PACINGQUEUE_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),PACINGQUEUE_TYPE))
#define PACINGQUEUE_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),PACINGQUEUE_TYPE))
#define PACINGQUEUE_CAST(src)        ((PacingQueue *)(src))

#define MPRTP_SENDER_SCHTREE_MAX_PATH_NUM 32
#define MAX_SKEWS_ARRAY_LENGTH 256

//typedef struct _FrameNode FrameNode;
//typedef struct _Frame Frame;


struct _PacingQueue
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;
  GQueue*              sendingq;
  GstClockTime         pacing_times[256];

};
struct _PacingQueueClass{
  GObjectClass parent_class;
};

PacingQueue*
make_pacing_queue();


void
pacing_queue_push_packet(
    PacingQueue *this,
    SndPacket* packet);

SndPacket*
pacing_queue_pop_packet(
    PacingQueue *this,
    GstClockTime *pacing_time);

GType
pacing_queue_get_type (void);

#endif /* PACINGQUEUE_H_ */
