/*
 * smanctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef STREAM_JOINER_H_
#define STREAM_JOINER_H_

#include <gst/gst.h>
#include "lib_swplugins.h"
#include "rcvtracker.h"
#include "mediator.h"

typedef struct _StreamJoiner StreamJoiner;
typedef struct _StreamJoinerClass StreamJoinerClass;


#define STREAM_JOINER_TYPE             (stream_joiner_get_type())
#define STREAM_JOINER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),STREAM_JOINER_TYPE,StreamJoiner))
#define STREAM_JOINER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),STREAM_JOINER_TYPE,StreamJoinerClass))
#define STREAM_JOINER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),STREAM_JOINER_TYPE))
#define STREAM_JOINER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),STREAM_JOINER_TYPE))
#define STREAM_JOINER_CAST(src)        ((StreamJoiner *)(src))

#define MPRTP_SENDER_SCHTREE_MAX_PATH_NUM 32
#define MAX_SKEWS_ARRAY_LENGTH 256

//typedef struct _FrameNode FrameNode;
//typedef struct _Frame Frame;


struct _StreamJoiner
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;
  GstClockTime         join_delay;
  GstClockTime         last_playout_time_refreshed;

  SlidingWindow*       joinq;
  GQueue*              playoutq;
  guint16              last_seq;
  gboolean             last_seq_init;
  guint32              last_ts;

  GstClockTime         playout_time;

  gdouble              playout_delay;

  Mediator*            repair_channel;

};
struct _StreamJoinerClass{
  GObjectClass parent_class;
};

StreamJoiner*
make_stream_joiner(Mediator* repair_channel);

void
stream_joiner_set_join_delay (
    StreamJoiner * this,
    GstClockTime join_delay);

void
stream_joiner_push_packet(
    StreamJoiner *this,
    RTPPacket* packet);

void
stream_joiner_on_rcvtracker_stat_change(
    StreamJoiner *this,
    RcvTrackerStat* stat);

RTPPacket*
stream_joiner_pop_packet(
    StreamJoiner *this);

GType
stream_joiner_get_type (void);

#endif /* STREAM_JOINER_H_ */
