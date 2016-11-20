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

#define MPRTP_SENDER_SCHTREE_MAX_PATH_NUM 32
#define MAX_SKEWS_ARRAY_LENGTH 256

//typedef struct _FrameNode FrameNode;
//typedef struct _Frame Frame;


struct _JitterBuffer
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;
  guint16              last_seq;
  gboolean             last_seq_init;

  guint32              last_ts;

  gint32               clock_rate;
  GQueue*              playoutq;

  gint32               gap_seq;

  GstClockTime         playout_delay;
  GstClockTime         playout_time;
  SlidingWindow*       path_skews;
  gpointer             subflows;

};
struct _JitterBufferClass{
  GObjectClass parent_class;
};

JitterBuffer*
make_jitterbuffer(void);

void jitterbuffer_set_clock_rate(
    JitterBuffer *this,
    gint32 clock_rate
    );

gboolean
jitterbuffer_is_packet_discarded(
    JitterBuffer* this,
    RcvPacket* packet);

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
    JitterBuffer *this,
    GstClockTime *playout_time);

GType
jitterbuffer_get_type (void);

#endif /* JITTERBUFFER_H_ */
