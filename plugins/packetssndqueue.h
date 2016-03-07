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
typedef void (*BufferProxy)(gpointer,GstBuffer*);

struct _PacketsSndQueue
{
  GObject                  object;
  PacketsSndQueueNode*     head;
  PacketsSndQueueNode*     tail;
  guint32                  counter;
  GRWLock                  rwmutex;
  GstClock*                sysclock;
  guint                    pacing;
  gdouble                  bandwidth;
  guint                    allowed_rate_per_ms;
  GstClockTime             obsolation_treshold;
  BufferProxy              proxy;
  gpointer                 proxydata;
  gboolean                 expected_lost;
  GstTask*                 ticking_thread;
  GRecMutex                ticking_mutex;
};

struct _PacketsSndQueueNode
{
  PacketsSndQueueNode* next;
  GstClockTime         added;
  GstBuffer*           buffer;
  guint                size;
  guint                allowed_size;
};

struct _PacketsSndQueueClass{
  GObjectClass parent_class;

};



GType packetssndqueue_get_type (void);
PacketsSndQueue *make_packetssndqueue(BufferProxy proxy, gpointer proxydata);
void packetssndqueue_set_bandwidth(PacketsSndQueue *this, gdouble bandwidth);
void packetssndqueue_reset(PacketsSndQueue *this);
gboolean packetssndqueue_expected_lost(PacketsSndQueue *this);
void packetssndqueue_push(PacketsSndQueue *this,
                          GstBuffer* buffer);
#endif /* PACKETSSNDQUEUE_H_ */
