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

  GQueue*                    discarded;
  GQueue*                    packets;

  gboolean                   playout_allowed;

  gint                       desired_framenum;
  GstClockTime               high_watermark;
  GstClockTime               low_watermark;
  gdouble                    spread_factor;

  gboolean                   hwmark_reached;

  guint32                    playout_ts;
  guint16                    HSSN;
  gboolean                   HSSN_initialized;

  guint32                    played_timestamp;

  gboolean                   flush;

};



struct _PacketsRcvQueueClass{
  GObjectClass parent_class;

};



GType packetsrcvqueue_get_type (void);
PacketsRcvQueue *make_packetsrcvqueue(void);
void packetsrcvqueue_reset(PacketsRcvQueue *this);
void packetsrcvqueue_set_playout_allowed(PacketsRcvQueue *this, gboolean playout_permission);
void packetsrcvqueue_set_desired_framenum(PacketsRcvQueue *this, guint desired_framenum);
void packetsrcvqueue_flush(PacketsRcvQueue *this);
void packetsrcvqueue_set_high_watermark(PacketsRcvQueue *this, GstClockTime min_playoutrate);
void packetsrcvqueue_set_low_watermark(PacketsRcvQueue *this, GstClockTime max_playoutrate);
void packetsrcvqueue_push_discarded(PacketsRcvQueue *this, GstMpRTPBuffer *mprtp);
void packetsrcvqueue_push(PacketsRcvQueue *this, GstMpRTPBuffer* buffer);
GstMpRTPBuffer *packetsrcvqueue_pop(PacketsRcvQueue *this);
GstMpRTPBuffer *packetsrcvqueue_pop_discarded(PacketsRcvQueue *this);


#endif /* PACKETSRCVQUEUE_H_ */
