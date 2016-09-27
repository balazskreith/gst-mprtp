/*
 * refctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef REFCTRLER_H_
#define REFCTRLER_H_

#include <gst/gst.h>

#include "mprtprpath.h"
#include "streamjoiner.h"
#include "ricalcer.h"
#include "sndctrler.h"
#include "streamsplitter.h"
#include "reportprod.h"
#include "reportproc.h"
#include "fecdec.h"

typedef struct _Observer Observer;
typedef struct _ObserverClass ObserverClass;

#define OBSERVER_TYPE             (observer_get_type())
#define OBSERVER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),OBSERVER_TYPE,Observer))
#define OBSERVER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),OBSERVER_TYPE,ObserverClass))
#define OBSERVER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),OBSERVER_TYPE))
#define OBSERVER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),OBSERVER_TYPE))
#define OBSERVER_CAST(src)        ((Observer *)(src))


struct _Observer
{
  GObject          object;
  GSList*          notifiers;
  GSList*          collectors;
};

struct _ObserverClass{
  GObjectClass parent_class;
};

typedef void (*NotifierFunc)(gpointer udata, gpointer item);
typedef gpointer (*CollectorFunc)(gpointer udata);


GType observer_get_type (void);

Observer *make_observer(void);
void observer_add_listener(Observer *this, NotifierFunc callback, gpointer udata);
void observer_rem_listener(Observer *this, NotifierFunc callback);
void observer_notify(Observer *this, gpointer subject);


#endif /* REFCTRLER_H_ */
