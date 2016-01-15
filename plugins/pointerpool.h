/*
 * pointerpool.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef POINTERPOOL_H_
#define POINTERPOOL_H_

#include <gst/gst.h>

typedef struct _PointerPool PointerPool;
typedef struct _PointerPoolClass PointerPoolClass;

#define DISABLE_LINE if(0)

#include "bintree.h"

#define POINTERPOOL_TYPE             (pointerpool_get_type())
#define POINTERPOOL(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),POINTERPOOL_TYPE,PointerPool))
#define POINTERPOOL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),POINTERPOOL_TYPE,PointerPoolClass))
#define POINTERPOOL_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),POINTERPOOL_TYPE))
#define POINTERPOOL_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),POINTERPOOL_TYPE))
#define POINTERPOOL_CAST(src)        ((PointerPool *)(src))

struct _PointerPool
{
  GObject                  object;
  GRWLock                  rwmutex;
  gpointer**               items;
  gpointer               (*item_ctor)(void);
  void                   (*item_dtor)(gpointer);
  guint32                  length;
  gint32                   write_index;
  gint32                   read_index;
  gint32                   counter;
};

struct _PointerPoolClass{
  GObjectClass parent_class;

};

GType pointerpool_get_type (void);
PointerPool *make_pointerpool(guint32 length,
                              gpointer (*item_maker)(void),
                              void (*item_dtor)(gpointer));
void pointerpool_add(PointerPool *this, gpointer item);
gpointer pointerpool_get(PointerPool* this);
void pointerpool_reset(PointerPool *this);

#endif /* POINTERPOOL_H_ */
