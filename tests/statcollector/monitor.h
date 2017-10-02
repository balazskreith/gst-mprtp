#ifndef MEDIAPIPELINE_MONITOR_H_
#define MEDIAPIPELINE_MONITOR_H_

#include <gst/gst.h>
#include <string.h>
#include "objects.h"

typedef gboolean (*MonitorQueueIsFull)(GQueue* queue);

typedef struct {
  Component base;
  PushPort input;
  GQueue* items;
  MonitorQueueIsFull is_full;
  Eventer* on_add;
  Eventer* on_rem;
}MonitorQueue;

MonitorQueue* monitorqueue_ctor(void);
void monitorqueue_dtor(Monitor* this);
void monitorqueue_init(Monitor* this, const gchar* name);


typedef struct {
  Component base;
  MonitorQueue* queue;
  ListenerCb on_add;
  ListenerCb on_rem;
}Monitor;

Monitor* monitor_ctor(void);
void monitor_dtor(Monitor* this);
void monitor_init(Monitor* this, gchar* name, MonitorQueue* monitorqueue,
    ListenerCb on_add, gpointer on_add_udata, ListenerCb on_rem, gpointer on_rem_udata);

typedef gint32 (*SumMonitorExtractor)(gpointer item);

typedef struct {
  Monitor base;
  SumMonitorExtractor extractor;
  gint32 result;
  PushPort output;
  gpointer output_udata;
}SumMonitor;

SumMonitor* make_summonitor(const gchar* name, MonitorQueue* monitorqueue, SumMonitorExtractor extractor);


typedef struct {
  Monitor base;
  GCompareFunc cmp;
  PushPort output;
  gpointer output_udata;
}PercentileMonitor;

SumMonitor* make_percentilemonitor(const gchar* name, MonitorQueue* monitorqueue, GCompareFunc cmp);

#endif /* MEDIAPIPELINE_MONITOR_H_ */
