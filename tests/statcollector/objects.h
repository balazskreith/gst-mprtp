#ifndef MEDIAPIPELINE_OBJECTS_H_
#define MEDIAPIPELINE_OBJECTS_H_

typedef struct {
  guint ref;
  GFreeFunc dtor;
}Object;

void object_init(Object* this, GFreeFunc dtor);
void object_ref(gpointer target);
void object_unref(gpointer target);

typedef struct{
  Object base;
}RTPStatPacket;

RTPStatPacket* make_packet(void);
gint32 packet_get_payload_size(RTPStatPacket* packet);


typedef void(*ListenerCb)(gpointer listener_obj, gpointer argument);

typedef struct{
  gpointer subscriber_obj;
  subscriber subscriber_func;
}Subscriber;

typedef struct{
  Object base;
  GSList* subscribers;
  gchar   name[256];
}Eventer;

Eventer* make_eventer(const gchar* name);
void eventer_add_listener(Eventer* this, ListenerCb listenerCb, gpointer udata);
void eventer_fire(Eventer *this, gpointer argument);

#endif /* MEDIAPIPELINE_OBJECTS_H_ */
