/*
 * packetsender.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef PACKETSENDERN_H_
#define PACKETSENDERN_H_

#include <gst/gst.h>
#include "mprtpspath.h"

typedef struct _PacketSender PacketSender;
typedef struct _PacketSenderClass PacketSenderClass;

#define PACKETSENDER_TYPE             (packetsender_get_type())
#define PACKETSENDER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),PACKETSENDER_TYPE,PacketSender))
#define PACKETSENDER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),PACKETSENDER_TYPE,PacketSenderClass))
#define PACKETSENDER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),PACKETSENDER_TYPE))
#define PACKETSENDER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),PACKETSENDER_TYPE))
#define PACKETSENDER_CAST(src)        ((PacketSender *)(src))

struct _PacketSender
{
  GObject              object;
  GstClock*            sysclock;

  GAsyncQueue*         mprtpq;
  GAsyncQueue*         mprtcpq;

  GstTask*             thread;
  GRecMutex            thread_mutex;

  gpointer             priv;

};

struct _PacketSenderClass{
  GObjectClass parent_class;
};

PacketSender* make_packetsender(GstPad* rtppad, GstPad* rtcppad);
void packetsender_add_rtppad_buffer(PacketSender* this, GstBuffer *buffer);

GType packetsender_get_type (void);

#endif /* PACKETSENDERN_H_ */
