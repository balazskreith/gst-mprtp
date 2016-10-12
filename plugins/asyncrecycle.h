/*
 * refctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef ASYNCRECYCLE_H_
#define ASYNCRECYCLE_H_

#include <gst/gst.h>

typedef struct _AsyncRecycle AsyncRecycle;
typedef struct _AsyncRecycleClass AsyncRecycleClass;

#define ASYNCRECYCLE_TYPE             (async_recycle_get_type())
#define ASYNCRECYCLE(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),ASYNCRECYCLE_TYPE,AsyncRecycle))
#define ASYNCRECYCLE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),ASYNCRECYCLE_TYPE,AsyncRecycleClass))
#define ASYNCRECYCLE_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),ASYNCRECYCLE_TYPE))
#define ASYNCRECYCLE_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),ASYNCRECYCLE_TYPE))
#define ASYNCRECYCLE_CAST(src)        ((AsyncRecycle *)(src))


#define DEFINE_ASYNCRECYCLE_TYPE(scope, name, type)                      \
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
scope AsyncRecycle* make_async_recycle_##name(AsyncRecycleItemShaper shaper)   \
{                                                                   \
  AsyncRecycle* result;                                                  \
  result = make_async_recycle(_##name##_ctor, _##name##_dtor, shaper);    \
  return result;                                                    \
}





typedef gpointer (*AsyncRecycleItemCtor)(void);
typedef void (*AsyncRecycleItemDtor)(gpointer item);
typedef void (*AsyncRecycleItemShaper)(gpointer result,gpointer udata);

struct _AsyncRecycle
{
  GObject                object;
  GAsyncQueue*           items;
  AsyncRecycleItemCtor   ctor;
  AsyncRecycleItemDtor   dtor;
  AsyncRecycleItemShaper shaper;
};

struct _AsyncRecycleClass{
  GObjectClass parent_class;
};


GType async_recycle_get_type (void);

AsyncRecycle *make_async_recycle(AsyncRecycleItemCtor ctor, AsyncRecycleItemDtor dtor, AsyncRecycleItemShaper shaper);
gpointer async_recycle_retrieve(AsyncRecycle* this);
gpointer async_recycle_retrieve_and_shape(AsyncRecycle *this, gpointer udata);
void async_recycle_add(AsyncRecycle* this, gpointer item);

#endif /* ASYNCRECYCLE_H_ */
