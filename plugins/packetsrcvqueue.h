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
#include "numstracker.h"

typedef struct _PacketsRcvQueue PacketsRcvQueue;
typedef struct _PacketsRcvQueueClass PacketsRcvQueueClass;

#define PACKETSRCVQUEUE_TYPE             (packetsrcvqueue_get_type())
#define PACKETSRCVQUEUE(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),PACKETSRCVQUEUE_TYPE,PacketsRcvQueue))
#define PACKETSRCVQUEUE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),PACKETSRCVQUEUE_TYPE,PacketsRcvQueueClass))
#define PACKETSRCVQUEUE_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),PACKETSRCVQUEUE_TYPE))
#define PACKETSRCVQUEUE_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),PACKETSRCVQUEUE_TYPE))
#define PACKETSRCVQUEUE_CAST(src)        ((PacketsRcvQueue *)(src))

#define PACKETSRCVQUEUE_MAX_ITEMS_NUM 100

struct _PacketsRcvQueue
{
  GObject                    object;
  GstClock*                  sysclock;
  GstClockTime               made;
  GRWLock                    rwmutex;

  GQueue*                    frames;
  GQueue*                    urgent;
  GQueue*                    normal;

  gboolean                   playout_allowed;
};



struct _PacketsRcvQueueClass{
  GObjectClass parent_class;

};



GType packetsrcvqueue_get_type (void);
PacketsRcvQueue *make_packetsrcvqueue(void);
void packetsrcvqueue_reset(PacketsRcvQueue *this);
void packetsrcvqueue_set_playout_allowed(PacketsRcvQueue *this, gboolean playout_permission);
void packetsrcvqueue_set_initial_delay(PacketsRcvQueue *this, GstClockTime init_delay);
void packetsrcvqueue_push(PacketsRcvQueue *this, GstMpRTPBuffer* buffer);
void packetsrcvqueue_refresh(PacketsRcvQueue *this);
GstMpRTPBuffer * packetsrcvqueue_pop_normal(PacketsRcvQueue *this);
GstMpRTPBuffer* packetsrcvqueue_pop_urgent(PacketsRcvQueue *this);


#endif /* PACKETSRCVQUEUE_H_ */
