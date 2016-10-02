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

  GstTask*             thread;
  GRecMutex            thread_mutex;

  SlidingWindow*       joinq;
  GQueue*              playoutq;
  guint16              last_seq;
  gboolean             last_seq_init;
  guint32              last_ts;

  gdouble              betha;
  GAsyncQueue*         rtppackets_out;
  GAsyncQueue*         discarded_packets_out;
  GAsyncQueue*         messages_in;
  GAsyncQueue*         repair_request_out;
  GAsyncQueue*         repair_response_in;

  GstClockTime         playout_time;

  gdouble              playout_delay;

};
struct _StreamJoinerClass{
  GObjectClass parent_class;
};

StreamJoiner*
make_stream_joiner(void);

void stream_joiner_setup_and_start(StreamJoiner *this,
    GAsyncQueue* rtppackets_out,
    GAsyncQueue *repair_request_out,
    GAsyncQueue *discarded_packets_out);

void
stream_joiner_add_stat(
    StreamJoiner *this,
    RcvTrackerStat* stat);

void
stream_joiner_add_packet(
    StreamJoiner *this,
    RTPPacket* packet);

void
stream_joiner_set_join_delay (
    StreamJoiner * this,
    GstClockTime join_delay);


GType
stream_joiner_get_type (void);

#endif /* STREAM_JOINER_H_ */
