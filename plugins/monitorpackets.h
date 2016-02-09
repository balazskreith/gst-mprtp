/*
 * monitorpackets.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef MONITORPACKETS_H_
#define MONITORPACKETS_H_

#include <gst/gst.h>
#include "bintree.h"

typedef struct _MonitorPackets MonitorPackets;
typedef struct _MonitorPacketsClass MonitorPacketsClass;

#define MONITORPACKETS_TYPE             (monitorpackets_get_type())
#define MONITORPACKETS(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),MONITORPACKETS_TYPE,MonitorPackets))
#define MONITORPACKETS_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),MONITORPACKETS_TYPE,MonitorPacketsClass))
#define MONITORPACKETS_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),MONITORPACKETS_TYPE))
#define MONITORPACKETS_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),MONITORPACKETS_TYPE))
#define MONITORPACKETS_CAST(src)        ((MonitorPackets *)(src))

typedef struct _MonitorPacketsNode MonitorPacketsNode;

#define MONITORPACKETS_CONTENTS_LENGTH 3

struct _MonitorPackets
{
  GObject                  object;
  GRWLock                  rwmutex;
  GQueue*                  queue;
  gint                     counter;
  GstClock*                sysclock;
};

struct _MonitorPacketsClass{
  GObjectClass parent_class;

};
GType monitorpackets_get_type (void);
MonitorPackets *make_monitorpackets(void);
void monitorpackets_reset(MonitorPackets *this);
void monitorpackets_push_rtp_packet(MonitorPackets* this, GstBuffer* buf);
GstBuffer *monitorpackets_provide_rtp_packet(MonitorPackets *this);
#endif /* MONITORPACKETS_H_ */
