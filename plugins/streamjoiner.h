/*
 * smanctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef STREAMJOINER_H_
#define STREAMJOINER_H_

#include <gst/gst.h>
#include "lib_swplugins.h"
#include "rcvtracker.h"
#include "mediator.h"

typedef struct _StreamJoiner StreamJoiner;
typedef struct _StreamJoinerClass StreamJoinerClass;


#define STREAMJOINER_TYPE             (stream_joiner_get_type())
#define STREAMJOINER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),STREAMJOINER_TYPE,StreamJoiner))
#define STREAMJOINER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),STREAMJOINER_TYPE,StreamJoinerClass))
#define STREAMJOINER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),STREAMJOINER_TYPE))
#define STREAMJOINER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),STREAMJOINER_TYPE))
#define STREAMJOINER_CAST(src)        ((StreamJoiner *)(src))

#define MAX_JITTER_BUFFER_ALLOWED_SKEW 50 * GST_MSECOND

//typedef struct _FrameNode FrameNode;
//typedef struct _Frame Frame;

typedef struct {
  guint8 subflow_id;
  gboolean active;
  StreamJoiner* stream_joiner;
  gboolean sampled;
  gboolean played;
  GstClockTime waiting_time;
}StreamJoinerSubflow;

struct _StreamJoiner
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;
  guint32              last_played_sent_ts;
  guint32              last_played_out_ts;
  GstClockTime         last_played_out_time;
  guint16              last_played_seq;
  gboolean first_frame_played_out, reference_points_initialized;
  gboolean             last_played_seq_init;
  guint16 last_pushed_seq;
  gint consecutive_good_seq;
  guint32              last_ts;
  gboolean join_delay_initialized, playout_delay_initialized;
  gdouble joining_delay;

  guint sampled_subflows;
  guint joined_subflows;
  gint played_subflows;
  guint16 HSN;
  gboolean joining_delay_first_updated;
  guint16 last_joining_delay_update_HSN;
//  gint32               clock_rate;
  GQueue*              playoutq;
  GQueue*              discardedq;
  GList*               frames;
  Recycle*             frames_recycle;
  TimestampGenerator*  rtp_ts_generator;
  StreamJoinerSubflow* subflows;

  gint32               gap_seq;

  gboolean initial_time_initialized;
  gboolean first_frame_played;
  GstClockTime first_waiting_started, desired_buffer_time;

};
struct _StreamJoinerClass{
  GObjectClass parent_class;
};

StreamJoiner*
make_stream_joiner(TimestampGenerator* rtp_ts_generator);

void
stream_joiner_set_desired_buffer_time(
    StreamJoiner* this,
    GstClockTime value);

GstClockTime stream_joiner_get_max_join_delay_in_ts(
    StreamJoiner* this);

gboolean
stream_joiner_is_packet_discarded(
    StreamJoiner* this,
    RcvPacket* packet);

RcvPacket*
stream_joiner_pop_discarded_packet(
    StreamJoiner* this);

void
stream_joiner_on_subflow_joined(
    StreamJoiner* this,
    RcvSubflow* subflow
    );

void
stream_joiner_on_subflow_detached(
    StreamJoiner* this,
    RcvSubflow* subflow
    );

void
stream_joiner_push_packet(
    StreamJoiner *this,
    RcvPacket* packet);

gboolean
stream_joiner_has_repair_request(
    StreamJoiner *this,
    guint16 *gap_seq);

RcvPacket*
stream_joiner_pop_packet(
    StreamJoiner *this);

GType
stream_joiner_get_type (void);

#endif /* STREAMJOINER_H_ */
