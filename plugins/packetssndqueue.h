/*
 * packetssndqueue.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef PACKETSSNDQUEUE_H_
#define PACKETSSNDQUEUE_H_

#include <gst/gst.h>
#include "bintree.h"

typedef struct _PacketsSndQueue PacketsSndQueue;
typedef struct _PacketsSndQueueClass PacketsSndQueueClass;

#define PACKETSSNDQUEUE_TYPE             (packetssndqueue_get_type())
#define PACKETSSNDQUEUE(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),PACKETSSNDQUEUE_TYPE,PacketsSndQueue))
#define PACKETSSNDQUEUE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),PACKETSSNDQUEUE_TYPE,PacketsSndQueueClass))
#define PACKETSSNDQUEUE_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),PACKETSSNDQUEUE_TYPE))
#define PACKETSSNDQUEUE_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),PACKETSSNDQUEUE_TYPE))
#define PACKETSSNDQUEUE_CAST(src)        ((PacketsSndQueue *)(src))

typedef struct _PacketsSndQueueNode PacketsSndQueueNode;

struct _PacketsSndQueue
{
  GObject                  object;
  PacketsSndQueueNode*     head;
  PacketsSndQueueNode*     tail;
  guint32                  counter;
  guint32                  bytes_in_queue;
  GRWLock                  rwmutex;
  PointerPool*             node_pool;
  GstClock*                sysclock;
};

struct _PacketsSndQueueNode
{
  PacketsSndQueueNode* next;
  GstClockTime         added;
  GstBuffer*           buffer;
  guint32              payload_bytes;
};

struct _PacketsSndQueueClass{
  GObjectClass parent_class;

};
GType packetssndqueue_get_type (void);
PacketsSndQueue *make_packetssndqueue(void);
void packetssndqueue_reset(PacketsSndQueue *this);
guint32 packetssndqueue_get_num(PacketsSndQueue *this, guint32 *bytes_in_queue);
void packetssndqueue_push(PacketsSndQueue *this,
                          GstBuffer* buffer,
                          guint32 payload_bytes);
GstBuffer* packetssndqueue_pop(PacketsSndQueue *this);
gboolean packetssndqueue_has_buffer(PacketsSndQueue *this, guint32 *payload_bytes);
#endif /* PACKETSSNDQUEUE_H_ */
