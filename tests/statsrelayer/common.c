#include "common.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <gst/gst.h>
#include <glib.h>
#include <string.h>

PushPort* make_pushport(PushCb push_cb, gpointer udata) {
  PushPort* this = g_malloc0(sizeof(PushPort));
  this->push_cb = push_cb;
  this->udata = udata;
  return this;
}

void pushport_send(PushPort* this, gpointer item) {
  if (!this) {
    fprintf(stderr, "Pushport does not exists");
    return;
  }
  this->push_cb(this->udata, item);
}

Process* make_process(ProcessCb process, gpointer udata) {
  Process* this = g_malloc0(sizeof(PushPort));
  this->process = process;
  this->udata = udata;
  return this;
}

void process_call(Process* this) {
  if (!this) {
    fprintf(stderr, "Process does not exists");
    return;
  }
  this->process(this->udata);
}

gint32 common_assign_string_to_int(gchar* type, ...)
{
  va_list arguments;
   gchar* token = NULL;
  gint32 result = 1;

  va_start ( arguments, type );
  for(token = va_arg( arguments,  gchar*); token; token = va_arg(arguments,  gchar*), ++result){
//    g_print("comparing |%s|%s|\n", token, type);
    if(!strcmp(type, token)){
      return result;
    }
  }
  va_end ( arguments );
  return -1;
}
