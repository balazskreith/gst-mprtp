/*
 * packetholder.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef PACKETHOLDER_H_
#define PACKETHOLDER_H_

#include <gst/gst.h>
#include "bintree.h"

typedef struct _PacketHolder PacketHolder;
typedef struct _PacketHolderClass PacketHolderClass;

#define PACKETHOLDER_TYPE             (packetholder_get_type())
#define PACKETHOLDER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),PACKETHOLDER_TYPE,PacketHolder))
#define PACKETHOLDER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),PACKETHOLDER_TYPE,PacketHolderClass))
#define PACKETHOLDER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),PACKETHOLDER_TYPE))
#define PACKETHOLDER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),PACKETHOLDER_TYPE))
#define PACKETHOLDER_CAST(src)        ((PacketHolder *)(src))

typedef struct _PacketHolderNode PacketHolderNode;

struct _PacketHolder
{
  GObject                  object;
  GRWLock                  rwmutex;
  GstBuffer*               content;
  GstClock*                sysclock;
};

struct _PacketHolderClass{
  GObjectClass parent_class;

};
GType packetholder_get_type (void);
PacketHolder *make_packetholder(void);
void packetholder_reset(PacketHolder *this);
void packetholder_push(PacketHolder *this,
                         GstBuffer *buf);
GstBuffer *packetholder_pop(PacketHolder *this);
#endif /* PACKETHOLDER_H_ */
