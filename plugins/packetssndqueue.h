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

typedef struct _PacketsSndQueueItem PacketsSndQueueItem;

struct _PacketsSndQueueItem
{
  GstClockTime         added;
  GstBuffer*           buffer;
  guint32              timestamp;
  gint32               size;
};

#define PACKETSSNDQUEUE_MAX_ITEMS_NUM 2000

struct _PacketsSndQueue
{
  GObject                  object;
  GstClock*                sysclock;
  GRWLock                  rwmutex;
  PacketsSndQueueItem      items[PACKETSSNDQUEUE_MAX_ITEMS_NUM];
  gint32                   items_read_index;
  gint32                   items_write_index;
  guint32                  counter;
  gboolean                 pacing;
  guint32                  last_timestamp;
  gint32                   approved_bytes;
  gint32                   allowed_rate_per_ms;
  gint32                   target_rate;
  GstClockTime             obsolation_treshold;
  gboolean                 expected_lost;
};



struct _PacketsSndQueueClass{
  GObjectClass parent_class;

};



GType packetssndqueue_get_type (void);
PacketsSndQueue *make_packetssndqueue(void);
void packetssndqueue_setup(PacketsSndQueue *this, gint32 target_rate, gboolean pacing);
void packetssndqueue_reset(PacketsSndQueue *this);
gboolean packetssndqueue_expected_lost(PacketsSndQueue *this);
void packetssndqueue_push(PacketsSndQueue *this, GstBuffer* buffer);
GstBuffer * packetssndqueue_pop(PacketsSndQueue *this);
void packetssndqueue_approve(PacketsSndQueue *this);
#endif /* PACKETSSNDQUEUE_H_ */
