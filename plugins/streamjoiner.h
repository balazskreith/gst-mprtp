/*
 * smanctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef STREAM_JOINER_H_
#define STREAM_JOINER_H_

#include <gst/gst.h>
#include "packetsrcvqueue.h"

typedef struct _StreamJoiner StreamJoiner;
typedef struct _StreamJoinerClass StreamJoinerClass;

#include "mprtprpath.h"

#define STREAM_JOINER_TYPE             (stream_joiner_get_type())
#define STREAM_JOINER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),STREAM_JOINER_TYPE,StreamJoiner))
#define STREAM_JOINER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),STREAM_JOINER_TYPE,StreamJoinerClass))
#define STREAM_JOINER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),STREAM_JOINER_TYPE))
#define STREAM_JOINER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),STREAM_JOINER_TYPE))
#define STREAM_JOINER_CAST(src)        ((StreamJoiner *)(src))

#define MPRTP_SENDER_SCHTREE_MAX_PATH_NUM 32
#define MAX_SKEWS_ARRAY_LENGTH 256

typedef struct _FrameNode FrameNode;
typedef struct _Frame Frame;


struct _StreamJoiner
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;
  GHashTable*          subflows;
  gint                 subflow_num;
  GRWLock              rwmutex;
  GstClockTime         join_delay;
  GstClockTime         join_min_treshold;
  GstClockTime         join_max_treshold;
  GstClockTime         last_join_refresh;
  PercentileTracker*   delays;

  guint                bytes_in_queue;
  guint                packets_in_queue;

  PacketsRcvQueue*     rcvqueue;
  GQueue*              retained_buffers;

};
struct _StreamJoinerClass{
  GObjectClass parent_class;
};

StreamJoiner*
make_stream_joiner(PacketsRcvQueue *rcvqueue);

void
stream_joiner_set_min_treshold (
    StreamJoiner * this,
    GstClockTime treshold);

void
stream_joiner_set_max_treshold (
    StreamJoiner * this,
    GstClockTime treshold);

void
stream_joiner_add_path(
    StreamJoiner * this,
    guint8 subflow_id,
    MpRTPRPath *path);

void
stream_joiner_rem_path(
    StreamJoiner * this,
    guint8 subflow_id);

void
stream_joiner_push(
    StreamJoiner * this,
    GstMpRTPBuffer *mprtp);

void
stream_joiner_transfer(
    StreamJoiner *this);


void
stream_joiner_set_playout_halt_time(
    StreamJoiner *this,
    GstClockTime halt_time);

void
stream_joiner_set_playout_allowed(
    StreamJoiner *this,
    gboolean playout_permission);


GType
stream_joiner_get_type (void);

#endif /* STREAM_JOINER_H_ */
