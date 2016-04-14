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
#include "gstmprtpbuffer.h"
#include "percentiletracker.h"

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

  guint                      desired_framenum;
  guint                      clock_rate;
  GstClockTime               highest_playoutrate;
  GstClockTime               lowest_playoutrate;
  gdouble                    spread_factor;

  gboolean                   flush;
  GstClockTime               timestamp_t1;
  GstClockTime               timestamp_t2;

  PercentileTracker*         samplings;
  GstClockTime               mean_rate;
};



struct _PacketsRcvQueueClass{
  GObjectClass parent_class;

};



GType packetsrcvqueue_get_type (void);
PacketsRcvQueue *make_packetsrcvqueue(void);
void packetsrcvqueue_reset(PacketsRcvQueue *this);
void packetsrcvqueue_set_playout_allowed(PacketsRcvQueue *this, gboolean playout_permission);
void packetsrcvqueue_set_desired_framenum(PacketsRcvQueue *this, guint desired_framenum);
void packetsrcvqueue_set_clock_rate(PacketsRcvQueue *this, guint clock_rate);
void packetsrcvqueue_flush(PacketsRcvQueue *this);
void packetsrcvqueue_set_highest_playoutrate(PacketsRcvQueue *this, GstClockTime highest_playoutrate);
void packetsrcvqueue_set_lowest_playoutrate(PacketsRcvQueue *this, GstClockTime lowest_playoutrate);
void packetsrcvqueue_set_spread_factor(PacketsRcvQueue *this, gdouble spread_factor);
GstClockTime packetsrcvqueue_get_playout_point(PacketsRcvQueue *this);
void packetsrcvqueue_push(PacketsRcvQueue *this, GstMpRTPBuffer* buffer);
void packetsrcvqueue_refresh(PacketsRcvQueue *this);
GstMpRTPBuffer * packetsrcvqueue_pop_normal(PacketsRcvQueue *this);
GstMpRTPBuffer* packetsrcvqueue_pop_urgent(PacketsRcvQueue *this);


#endif /* PACKETSRCVQUEUE_H_ */
