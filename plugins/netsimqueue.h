/*
 * netsimqueue.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef NETSIMQUEUE_H_
#define NETSIMQUEUE_H_

#include <gst/gst.h>

typedef struct _NetsimQueue NetsimQueue;
typedef struct _NetsimQueueClass NetsimQueueClass;

#define NETSIMQUEUE_TYPE             (netsimqueue_get_type())
#define NETSIMQUEUE(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),NETSIMQUEUE_TYPE,NetsimQueue))
#define NETSIMQUEUE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),NETSIMQUEUE_TYPE,NetsimQueueClass))
#define NETSIMQUEUE_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),NETSIMQUEUE_TYPE))
#define NETSIMQUEUE_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),NETSIMQUEUE_TYPE))
#define NETSIMQUEUE_CAST(src)        ((NetsimQueue *)(src))

#define MAX_NETSIMQUEUEBUFFERS_NUM 4096

typedef struct _NetsimQueueBuffer NetsimQueueBuffer;

typedef enum{
  NETSIMQUEUE_DROP_POLICY_MILK,
  NETSIMQUEUE_DROP_POLICY_WINE,
}NetsimQueueDropPolicy;

struct _NetsimQueue
{
  GObject                  object;

  NetsimQueueBuffer*       buffers[MAX_NETSIMQUEUEBUFFERS_NUM];
  guint16                  buffers_write_index;
  guint16                  buffers_read_index;
  guint16                  buffers_counter;
  guint16                  buffers_allowed_max_num;

  NetsimQueueDropPolicy    drop_policy;
  GstClockTime             max_time;
  GstClockTime             min_time;
  GstClock*                sysclock;
};

struct _NetsimQueueClass{
  GObjectClass parent_class;
};

GType netsimqueue_get_type (void);
void netsimqueue_push_buffer(NetsimQueue * this, GstBuffer *buffer);
GstBuffer *netsimqueue_pop_buffer(NetsimQueue * this);
void netsimqueue_set_min_time(NetsimQueue * this, gint min_time_in_ms);
void netsimqueue_set_max_time(NetsimQueue * this, gint max_time_in_ms);
void netsimqueue_set_max_packets(NetsimQueue * this, guint16 allowed_max_num);
void netsimqueue_set_drop_policy(NetsimQueue * this, NetsimQueueDropPolicy policy);

#endif /* NETSIMQUEUE_H_ */
