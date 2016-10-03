/*
 * refctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef MEDIATOR_H_
#define MEDIATOR_H_

#include <gst/gst.h>
#include "mprtpdefs.h"
#include "observer.h"

typedef struct _Mediator Mediator;
typedef struct _MediatorClass MediatorClass;

#define MEDIATOR_TYPE             (mediator_get_type())
#define MEDIATOR(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),MEDIATOR_TYPE,Mediator))
#define MEDIATOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),MEDIATOR_TYPE,MediatorClass))
#define MEDIATOR_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),MEDIATOR_TYPE))
#define MEDIATOR_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),MEDIATOR_TYPE))
#define MEDIATOR_CAST(src)        ((Mediator *)(src))


struct _Mediator
{
  GObject          object;
  Observer*        on_request;
  Observer*        on_response;
};

struct _MediatorClass{
  GObjectClass parent_class;
};


GType mediator_get_type (void);

Mediator *make_mediator(void);
void mediator_set_request_handler(Mediator *this, NotifierFunc response_cb, gpointer udata);
void mediator_set_response_handler(Mediator *this, NotifierFunc request_cb, gpointer udata);
void mediator_set_request(Mediator* this, gpointer request);
void mediator_set_response(Mediator* this, gpointer response);


#endif /* MEDIATOR_H_ */
