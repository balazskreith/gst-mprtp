#include "objects.h"

void object_init(Object* this, GFreeFunc dtor) {
  this->ref = 1;
  this->dtor = dtor;
}

void object_ref(gpointer target) {
  ++((Object*) target)->ref;
}

void object_unref(gpointer target) {
  if (1 < --((Object*) target)) {
    return;
  }
  ((Object*) target)->dtor(target);
}


RTPStatPacket* make_packet(void) {
  RTPStatPacket* this;
  this = g_malloc0(sizeof(RTPStatPacket));
  object_init(&this->base, g_free);
  return this;
}

Eventer* make_eventer(const gchar* name) {
  Eventer* this;
  this = g_malloc0(sizeof(Eventer));
  object_init(&this->base, g_free);
  return this;
}

void eventer_add_listener(Eventer* this, ListenerCb listenerCb, gpointer udata) {
  Subscriber* subscriber = g_malloc0(sizeof(Subscriber));
  subscriber->subscriber_func = listenerCb;
  subscriber->subscriber_obj = udata;
  this->subscribers = g_slist_prepend(this->subscribers, subscriber);
}

void eventer_fire(Eventer *this, gpointer argument) {
  GSList* it;
  for (it = this->subscribers; it; it = it->next) {
    Subscriber* subscriber = it->data;
    subscriber->subscriber_func(subscriber->subscriber_obj, argument);
  }
}

