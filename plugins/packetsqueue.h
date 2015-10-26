/*
 * packetsqueue.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef PACKETSQUEUE_H_
#define PACKETSQUEUE_H_

#include <gst/gst.h>

typedef struct _PacketsQueue PacketsQueue;
typedef struct _PacketsQueueClass PacketsQueueClass;

#define PACKETSQUEUE_TYPE             (packetsqueue_get_type())
#define PACKETSQUEUE(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),PACKETSQUEUE_TYPE,PacketsQueue))
#define PACKETSQUEUE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),PACKETSQUEUE_TYPE,PacketsQueueClass))
#define PACKETSQUEUE_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),PACKETSQUEUE_TYPE))
#define PACKETSQUEUE_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),PACKETSQUEUE_TYPE))
#define PACKETSQUEUE_CAST(src)        ((PacketsQueue *)(src))

typedef struct _PacketsQueueNode PacketsQueueNode;
typedef struct _Gap Gap;
typedef struct _GapNode GapNode;

struct _PacketsQueue
{
  GObject                  object;
  PacketsQueueNode*        head;
  PacketsQueueNode*        tail;
  gboolean                 gap_arrive;
  gboolean                 discarded_arrive;
  guint32                  counter;
  GRWLock                  rwmutex;
  GQueue*                  node_pool;
  GQueue*                  gapnodes_pool;
  GQueue*                  gaps_pool;
  GstClock*                sysclock;
  GList*                   gaps;
};

struct _PacketsQueueNode
{
  PacketsQueueNode *next;
  guint64 skew;
  guint64 rcv_time;
  guint64 snd_time;
  guint16 seq_num;
  GstClockTime added;
  GapNode *gapnode;
};

struct _PacketsQueueClass{
  GObjectClass parent_class;

};
void packetsqueue_test(void);
GType packetsqueue_get_type (void);
PacketsQueue *make_packetsqueue(void);
void packetsqueue_reset(PacketsQueue *this);
guint64 packetsqueue_add(PacketsQueue *this, guint64 snd_time, guint16 seq_num);
void packetsqueue_prepare_gap(PacketsQueue *this);
void packetsqueue_prepare_discarded(PacketsQueue *this);
gboolean packetsqueue_try_found_a_gap(PacketsQueue *this, guint16 seq_num, gboolean *duplicated);
gboolean packetsqueue_try_fill_gap(PacketsQueue *this, guint16 seq_num);
gboolean packetsqueue_head_obsolted(PacketsQueue *this, GstClockTime treshold);
void packetsqueue_remove_head(PacketsQueue *this, guint64 *skew);
#endif /* PACKETSQUEUE_H_ */
