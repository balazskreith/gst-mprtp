/*
 * packetssndqueue.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef PACKETSSNDQUEUE_H_
#define PACKETSSNDQUEUE_H_

#include <gst/gst.h>

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

#define PACKETSSNDQUEUE_MAX_ITEMS_NUM 100

struct _PacketsSndQueue
{
  GObject                    object;
  GstClock*                  sysclock;
  GstClockTime               made;
  GRWLock                    rwmutex;

  GstClockTime               obsolation_treshold;
  gboolean                   expected_lost;
  gint32                     bytes;

  GQueue*                    items;

};



struct _PacketsSndQueueClass{
  GObjectClass parent_class;

};



GType packetssndqueue_get_type (void);
PacketsSndQueue *make_packetssndqueue(void);
void packetssndqueue_reset(PacketsSndQueue *this);
gboolean packetssndqueue_expected_lost(PacketsSndQueue *this);
gint32 packetssndqueue_get_encoder_bitrate(PacketsSndQueue *this);
gint32 packetssndqueue_get_bytes_in_queue(PacketsSndQueue *this);
void packetssndqueue_push(PacketsSndQueue *this, GstBuffer* buffer);
void packetssndqueue_set_obsolation_treshold(PacketsSndQueue *this, GstClockTime treshold);
GstClockTime packetssndqueue_get_obsolation_treshold(PacketsSndQueue *this);
GstBuffer * packetssndqueue_peek(PacketsSndQueue *this);
GstBuffer * packetssndqueue_pop(PacketsSndQueue *this);


#endif /* PACKETSSNDQUEUE_H_ */
