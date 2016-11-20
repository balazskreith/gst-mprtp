/*
 * refctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef RECYCLE_H_
#define RECYCLE_H_

#include <gst/gst.h>
#include "lib_datapuffer.h"

typedef struct _Recycle Recycle;
typedef struct _RecycleClass RecycleClass;

#define RECYCLE_TYPE             (recycle_get_type())
#define RECYCLE(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),RECYCLE_TYPE,Recycle))
#define RECYCLE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),RECYCLE_TYPE,RecycleClass))
#define RECYCLE_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),RECYCLE_TYPE))
#define RECYCLE_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),RECYCLE_TYPE))
#define RECYCLE_CAST(src)        ((Recycle *)(src))


#define DEFINE_RECYCLE_TYPE(scope, name, type)                      \
static void _##name##_dtor(gpointer item)                           \
{                                                                   \
  g_slice_free(type, item);                                         \
}                                                                   \
                                                                    \
static gpointer _##name##_ctor(void)                                \
{                                                                   \
  return g_slice_new0(type);                                        \
}                                                                   \
                                                                    \
scope Recycle* make_recycle_##name(gint32 size, RecycleItemShaper shaper)   \
{                                                                   \
  Recycle* result;                                                  \
  result = make_recycle(size, _##name##_ctor, _##name##_dtor, shaper);    \
  return result;                                                    \
}





typedef gpointer (*RecycleItemCtor)(void);
typedef void (*RecycleItemDtor)(gpointer item);
typedef void (*RecycleItemShaper)(gpointer result,gpointer udata);
typedef gboolean (*RecycleItemUnrefAndTest)(gpointer item);

struct _Recycle
{
  GObject                 object;
  datapuffer_t*           items;
  RecycleItemCtor         ctor;
  RecycleItemDtor         dtor;

  RecycleItemShaper       shaper;

};

struct _RecycleClass{
  GObjectClass parent_class;
};


GType recycle_get_type (void);

Recycle *make_recycle(gint32 size, RecycleItemCtor ctor, RecycleItemDtor dtor, RecycleItemShaper shaper);
void recycle_set_unref_tester(Recycle* this, RecycleItemUnrefAndTest unref_and_test);
gpointer recycle_retrieve(Recycle* this);
gpointer recycle_retrieve_and_shape(Recycle *this, gpointer udata);
void recycle_add(Recycle* this, gpointer item);

Recycle* make_recycle_uint16(gint32 size, RecycleItemShaper shaper);
Recycle* make_recycle_int32(gint32 size, RecycleItemShaper shaper);
Recycle* make_recycle_int64(gint32 size, RecycleItemShaper shaper);
Recycle* make_recycle_uint32(gint32 size, RecycleItemShaper shaper);
Recycle* make_recycle_uint64(gint32 size, RecycleItemShaper shaper);
Recycle* make_recycle_double(gint32 size, RecycleItemShaper shaper);

#endif /* RECYCLE_H_ */
