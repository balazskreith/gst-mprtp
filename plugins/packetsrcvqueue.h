/*
 * packetsrcvqueue.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef PACKETSRCVQUEUE_H_
#define PACKETSRCVQUEUE_H_

#include <gst/gst.h>
#include "bintree.h"

typedef struct _PacketsRcvQueue PacketsRcvQueue;
typedef struct _PacketsRcvQueueClass PacketsRcvQueueClass;

#define PACKETSRCVQUEUE_TYPE             (packetsrcvqueue_get_type())
#define PACKETSRCVQUEUE(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),PACKETSRCVQUEUE_TYPE,PacketsRcvQueue))
#define PACKETSRCVQUEUE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),PACKETSRCVQUEUE_TYPE,PacketsRcvQueueClass))
#define PACKETSRCVQUEUE_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),PACKETSRCVQUEUE_TYPE))
#define PACKETSRCVQUEUE_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),PACKETSRCVQUEUE_TYPE))
#define PACKETSRCVQUEUE_CAST(src)        ((PacketsRcvQueue *)(src))

typedef struct _PacketsRcvQueueNode PacketsRcvQueueNode;

struct _PacketsRcvQueue
{
  GObject                  object;
  PacketsRcvQueueNode*        head;
  PacketsRcvQueueNode*        tail;
  gboolean                 gap_arrive;
  gboolean                 discarded_arrive;
  guint32                  counter;
  guint32                  lost;
  GRWLock                  rwmutex;
  PointerPool*             node_pool;
  BinTree*                 node_tree;
  gboolean                 last_seq_init;
  guint16                  last_seq;
  GstClock*                sysclock;
  guint32                  jitter;
};

struct _PacketsRcvQueueNode
{
  PacketsRcvQueueNode* next;
  guint64           skew;
  guint64           rcv_time;
  guint64           snd_time;
  guint16           seq_num;
  GstClockTime      added;
};

struct _PacketsRcvQueueClass{
  GObjectClass parent_class;

};
GType packetsrcvqueue_get_type (void);
PacketsRcvQueue *make_packetsrcvqueue(void);
void packetsrcvqueue_reset(PacketsRcvQueue *this);
guint64 packetsrcvqueue_add(PacketsRcvQueue *this,
                         guint64 snd_time,
                         guint16 seq_num,
                         GstClockTime *delay);
gboolean packetsrcvqueue_head_obsolted(PacketsRcvQueue *this, GstClockTime treshold);
void packetsrcvqueue_get_packets_stat_for_obsolation(PacketsRcvQueue *this,
                                      GstClockTime treshold,
                                      guint16 *lost,
                                      guint16 *received,
                                      guint16 *expected);
guint32 packetsrcvqueue_get_jitter(PacketsRcvQueue *this);
void packetsrcvqueue_remove_head(PacketsRcvQueue *this, guint64 *skew);
#endif /* PACKETSRCVQUEUE_H_ */
