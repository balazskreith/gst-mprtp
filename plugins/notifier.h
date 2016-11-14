/*
 * refctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef NOTIFIER_H_
#define NOTIFIER_H_

#include <gst/gst.h>
#include "mprtputils.h"

typedef struct _Notifier Notifier;
typedef struct _NotifierClass NotifierClass;

#define NOTIFIER_TYPE             (notifier_get_type())
#define NOTIFIER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),NOTIFIER_TYPE,Notifier))
#define NOTIFIER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),NOTIFIER_TYPE,NotifierClass))
#define NOTIFIER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),NOTIFIER_TYPE))
#define NOTIFIER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),NOTIFIER_TYPE))
#define NOTIFIER_CAST(src)        ((Notifier *)(src))


struct _Notifier
{
  GObject          object;
  gchar            name[256];
  GSList*          listeners;
};

struct _NotifierClass{
  GObjectClass parent_class;
};

typedef gboolean (*ListenerFilterFunc)(gpointer udata, gpointer item);
typedef void (*ListenerFunc)(gpointer udata, gpointer item);


GType notifier_get_type (void);

Notifier *make_notifier(const gchar* name);
void notifier_add_listener(Notifier *this, ListenerFunc callback, gpointer udata);
void notifier_add_listener_with_filter(Notifier *this, ListenerFunc callback, ListenerFilterFunc filter, gpointer udata);
void notifier_rem_listener(Notifier *this, ListenerFunc callback);
void notifier_do(Notifier *this, gpointer subject);


#endif /* NOTIFIER_H_ */
