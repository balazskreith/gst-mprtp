#ifndef MEDIAPIPELINE_COMPONENT_H_
#define MEDIAPIPELINE_COMPONENT_H_

#include <gst/gst.h>
#include <string.h>

typedef void (*PushPort)(gpointer udata, gpointer item);

typedef struct{
  gchar          name[256];
}Component;

Component* component_ctor(void);
void component_dtor(Component* this);
void component_init(Component* this, const gchar* name);
Component* make_component(const gchar *name);


#endif /* MEDIAPIPELINE_COMPONENT_H_ */
