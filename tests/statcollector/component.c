#include "component.h"

Component* component_ctor(void) {
  Component* this = g_malloc0(sizeof(Component));
  return this;
}

void component_init(Component* this, const gchar* name) {
  strcpy(this->name, name);
}

Component* make_component(const gchar* name) {
  Component* this = component_ctor();
  component_init(this, name);
  return this;
}

void component_dtor(Component* this) {
  g_free(this);
}
