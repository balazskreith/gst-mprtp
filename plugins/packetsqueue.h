/*
 * packetsqueue.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef PACKETSQUEUE_H_
#define PACKETSQUEUE_H_

#include <gst/gst.h>
#include "bintree.h"

typedef struct _PacketsQueue PacketsQueue;
typedef struct _PacketsQueueClass PacketsQueueClass;

#define PACKETSQUEUE_TYPE             (packetsqueue_get_type())
#define PACKETSQUEUE(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),PACKETSQUEUE_TYPE,PacketsQueue))
#define PACKETSQUEUE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),PACKETSQUEUE_TYPE,PacketsQueueClass))
#define PACKETSQUEUE_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),PACKETSQUEUE_TYPE))
#define PACKETSQUEUE_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),PACKETSQUEUE_TYPE))
#define PACKETSQUEUE_CAST(src)        ((PacketsQueue *)(src))

typedef struct _PacketsQueueNode PacketsQueueNode;

struct _PacketsQueue
{
  GObject                  object;
  PacketsQueueNode*        head;
  PacketsQueueNode*        tail;
  gboolean                 gap_arrive;
  gboolean                 discarded_arrive;
  guint32                  counter;
  guint32                  lost;
  GRWLock                  rwmutex;
  GQueue*                  node_pool;
  BinTree*                 node_tree;
  gboolean                 last_seq_init;
  guint16                  last_seq;
  GstClock*                sysclock;
  guint32                  jitter;
};

struct _PacketsQueueNode
{
  PacketsQueueNode* next;
  guint64           skew;
  guint64           rcv_time;
  guint64           snd_time;
  guint16           seq_num;
  GstClockTime      added;
};

struct _PacketsQueueClass{
  GObjectClass parent_class;

};
GType packetsqueue_get_type (void);
PacketsQueue *make_packetsqueue(void);
void packetsqueue_reset(PacketsQueue *this);
guint64 packetsqueue_add(PacketsQueue *this,
                         guint64 snd_time,
                         guint16 seq_num,
                         GstClockTime *delay);
gboolean packetsqueue_head_obsolted(PacketsQueue *this, GstClockTime treshold);
void packetsqueue_get_packets_stat_for_obsolation(PacketsQueue *this,
                                      GstClockTime treshold,
                                      guint16 *lost,
                                      guint16 *received,
                                      guint16 *expected);
guint32 packetsqueue_get_jitter(PacketsQueue *this);
void packetsqueue_remove_head(PacketsQueue *this, guint64 *skew);
#endif /* PACKETSQUEUE_H_ */
