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
  GQueue*              playoutq;
  Mediator*            repair_channel;

};
struct _JitterBufferClass{
  GObjectClass parent_class;
};

JitterBuffer*
make_jitterbuffer(Mediator *repair_channel);


void
jitterbuffer_push_packet(
    JitterBuffer *this,
    RcvPacket* packet);

RcvPacket*
jitterbuffer_pop_packet(
    JitterBuffer *this,
    gboolean *repair_request);

GType
jitterbuffer_get_type (void);

#endif /* JITTERBUFFER_H_ */
