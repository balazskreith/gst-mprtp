/*
 * smanctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef JITTERBUFFER_H_
#define JITTERBUFFER_H_

#include <gst/gst.h>
#include "lib_swplugins.h"
#include "rcvtracker.h"
#include "mediator.h"

typedef struct _JitterBuffer JitterBuffer;
typedef struct _JitterBufferClass JitterBufferClass;


#define JITTERBUFFER_TYPE             (jitterbuffer_get_type())
#define JITTERBUFFER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),JITTERBUFFER_TYPE,JitterBuffer))
#define JITTERBUFFER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),JITTERBUFFER_TYPE,JitterBufferClass))
#define JITTERBUFFER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),JITTERBUFFER_TYPE))
#define JITTERBUFFER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),JITTERBUFFER_TYPE))
#define JITTERBUFFER_CAST(src)        ((JitterBuffer *)(src))

#define MAX_JITTER_BUFFER_ALLOWED_SKEW 50 * GST_MSECOND

//typedef struct _FrameNode FrameNode;
//typedef struct _Frame Frame;

typedef struct {
  guint8 subflow_id;
  SlidingWindowPlugin* percentile_tracker;
  guint32 last_rcv_ts;
  guint32 last_snd_ts;
  guint16 last_subflow_seq;
  gboolean active, initialized;
  gint64 median_skew;
  JitterBuffer* jitter_buffer;
}JitterBufferSubflow;

struct _JitterBuffer
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
  gint64               max_skew;
  gboolean max_skew_initialized, playout_delay_initialized;
  gdouble playout_delay;

//  gint32               clock_rate;
  GQueue*              playoutq;
  GQueue*              discardedq;
  GList*               frames;
  Recycle*             frames_recycle;
  Recycle*             skews_recycle;
  TimestampGenerator*  rtp_ts_generator;
  SlidingWindow*       skews;
  JitterBufferSubflow* subflows;

  gint32               gap_seq;

  gboolean initial_time_initialized;
  gboolean first_frame_played;
  GstClockTime first_waiting_started, initial_buffer_time;

//  GstClockTime         playout_time;

};
struct _JitterBufferClass{
  GObjectClass parent_class;
};

JitterBuffer*
make_jitterbuffer(TimestampGenerator* rtp_ts_generator);

void
jitterbuffer_set_initial_buffer_time(
    JitterBuffer* this,
    GstClockTime value);

gboolean
jitterbuffer_is_packet_discarded(
    JitterBuffer* this,
    RcvPacket* packet);

RcvPacket*
jitterbuffer_pop_discarded_packet(
    JitterBuffer* this);

void
jitterbuffer_on_subflow_joined(
    JitterBuffer* this,
    RcvSubflow* subflow
    );

void
jitterbuffer_on_subflow_detached(
    JitterBuffer* this,
    RcvSubflow* subflow
    );

void
jitterbuffer_push_packet(
    JitterBuffer *this,
    RcvPacket* packet);

gboolean
jitterbuffer_has_repair_request(
    JitterBuffer *this,
    guint16 *gap_seq);

RcvPacket*
jitterbuffer_pop_packet(
    JitterBuffer *this);

GType
jitterbuffer_get_type (void);

#endif /* JITTERBUFFER_H_ */
