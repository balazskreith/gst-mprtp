#include "monitor.h"

static void
_monitor_process(
    Monitor* this,
    gpointer item
    );



Monitor* monitor_ctor(void) {
  Monitor* this = g_malloc0(sizeof(Monitor));
  return this;
}

void monitor_init(Monitor* this, gchar* name) {
  component_init(&this->base, name);
}

Monitor* make_monitor(const gchar* name) {
  Monitor* this = component_ctor();
  monitor_init(this, name);
  return this;
}

void monitor_dtor(Monitor* this) {
  g_free(this);
}

void monitor_connect_output(Monitor* this, guint output_num, PushPort output, gpointer output_udata) {

}


static void _monitor_process(Monitor* this, gpointer item) {

}
