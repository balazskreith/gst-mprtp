/*
 * packetsender.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef PACKETFORWARDERN_H_
#define PACKETFORWARDERN_H_

#include <gst/gst.h>
#include "mprtpspath.h"

typedef struct _PacketForwarder PacketForwarder;
typedef struct _PacketForwarderClass PacketForwarderClass;

#define PACKETFORWARDER_TYPE             (packetforwarder_get_type())
#define PACKETFORWARDER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),PACKETFORWARDER_TYPE,PacketForwarder))
#define PACKETFORWARDER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),PACKETFORWARDER_TYPE,PacketForwarderClass))
#define PACKETFORWARDER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),PACKETFORWARDER_TYPE))
#define PACKETFORWARDER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),PACKETFORWARDER_TYPE))
#define PACKETFORWARDER_CAST(src)        ((PacketForwarder *)(src))

struct _PacketForwarder
{
  GObject              object;
  GstClock*            sysclock;

  GAsyncQueue*         mprtpq;
  GAsyncQueue*         mprtcpq;

  GstTask*             thread;
  GRecMutex            thread_mutex;

  gpointer             priv;

};

struct _PacketForwarderClass{
  GObjectClass parent_class;
};

PacketForwarder* make_packetforwarder(GstPad* rtppad, GstPad* rtcppad);
void packetforwarder_add_rtppad_buffer(PacketForwarder* this, GstBuffer *buffer);
void packetforwarder_add_rtcppad_buffer(PacketForwarder* this, GstBuffer *buffer);
GType packetforwarder_get_type (void);

#endif /* PACKETFORWARDERN_H_ */
