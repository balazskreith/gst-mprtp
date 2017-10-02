/*
 * smanctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef MKFIFO_H_
#define MKFIFO_H_

#include <gst/gst.h>
#include "lib_swplugins.h"
#include "rcvtracker.h"
#include "mediator.h"

typedef struct _MkFifo MkFifo;
typedef struct _MkFifoClass MkFifoClass;


#define MKFIFO_TYPE             (mkfifo_get_type())
#define MKFIFO(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),MKFIFO_TYPE,MkFifo))
#define MKFIFO_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),MKFIFO_TYPE,MkFifoClass))
#define MKFIFO_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),MKFIFO_TYPE))
#define MKFIFO_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),MKFIFO_TYPE))
#define MKFIFO_CAST(src)        ((MkFifo *)(src))

//typedef struct _FrameNode FrameNode;
//typedef struct _Frame Frame;

typedef enum {
  MKFIFO_MODE_WRITER = 1,
  MKFIFO_MODE_READER = 2
}MkFifoMode;

struct _MkFifo
{
  GObject              object;
  guint                message_size;
  MkFifoMode           mode;
  gchar                path[1024];
  gint                 fd;
};

struct _MkFifoClass{
  GObjectClass parent_class;
};

MkFifo*
make_mkfifo(const gchar* path, MkFifoMode mode, guint message_size);

void
mkfifo_push(MkFifo* this, gpointer message);

gpointer
mkfifo_pop(MkFifo* this);

GType
mkfifo_get_type (void);

#endif /* MKFIFO_H_ */
