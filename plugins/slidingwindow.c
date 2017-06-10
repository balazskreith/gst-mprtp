/* GStreamer Scheduling tree
 * Copyright (C) 2015 Balázs Kreith (contact: balazs.kreith@gmail.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include "slidingwindow.h"
#include <math.h>
#include <string.h>

#define _now(this) (gst_clock_get_time (this->sysclock))

GST_DEBUG_CATEGORY_STATIC (slidingwindow_debug_category);
#define GST_CAT_DEFAULT coslidingwindow_debug_category

G_DEFINE_TYPE (SlidingWindow, slidingwindow, G_TYPE_OBJECT);

#define RECYCLE_SHAPER_COPY(name, type) \
static void name(gpointer result, gpointer udata) \
{                                                 \
  memcpy(result, udata, sizeof(type));            \
}                                                 \


//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void slidingwindow_finalize (GObject * object);
static gboolean _slidingwindow_default_obsolation(gpointer udata, SlidingWindowItem *item);

RECYCLE_SHAPER_COPY(_uint16_shaper, guint16);
RECYCLE_SHAPER_COPY(_int32_shaper, gint32);
RECYCLE_SHAPER_COPY(_int64_shaper, gint64);
RECYCLE_SHAPER_COPY(_uint32_shaper, guint32);
RECYCLE_SHAPER_COPY(_uint64_shaper, guint64);
RECYCLE_SHAPER_COPY(_double_shaper, gdouble);

SlidingWindow* make_slidingwindow_uint16(guint32 num_limit, GstClockTime obsolation_treshold)
{
  SlidingWindow* result;
  result = make_slidingwindow_with_data_recycle(num_limit, obsolation_treshold,
                                                    make_recycle_uint16(num_limit, _uint16_shaper)
                                                    );
  return result;
}

SlidingWindow* make_slidingwindow_int32(guint32 num_limit, GstClockTime obsolation_treshold)
{
  SlidingWindow* result;
  result = make_slidingwindow_with_data_recycle(num_limit, obsolation_treshold,
                                              make_recycle_int32(num_limit, _int32_shaper)
                                              );
  return result;
}

SlidingWindow* make_slidingwindow_int64(guint32 num_limit, GstClockTime obsolation_treshold)
{
  SlidingWindow* result;
  result = make_slidingwindow_with_data_recycle(num_limit, obsolation_treshold,
                                                make_recycle_int64(num_limit, _int64_shaper)
                                              );
  return result;
}

SlidingWindow* make_slidingwindow_uint32(guint32 num_limit, GstClockTime obsolation_treshold)
{
  SlidingWindow* result;
  result = make_slidingwindow_with_data_recycle(num_limit, obsolation_treshold,
                                                    make_recycle_uint32(num_limit, _uint32_shaper)
                                                    );
  return result;
}

SlidingWindow* make_slidingwindow_uint64(guint32 num_limit, GstClockTime obsolation_treshold)
{
  SlidingWindow* result;
  result = make_slidingwindow_with_data_recycle(num_limit, obsolation_treshold,
                                                    make_recycle_uint64(num_limit, _uint64_shaper)
                                                    );
  return result;
}

SlidingWindow* make_slidingwindow_double(guint32 num_limit, GstClockTime obsolation_treshold)
{
  SlidingWindow* result;
  result = make_slidingwindow_with_data_recycle(num_limit, obsolation_treshold,
                                                make_recycle_double(num_limit, _double_shaper)
                                              );
  return result;
}

DEFINE_RECYCLE_TYPE(static, switem, SlidingWindowItem);
RECYCLE_SHAPER_COPY(_switem_shaper, SlidingWindowItem);

SlidingWindow* make_slidingwindow_with_data_recycle(guint32 num_limit,
                                                  GstClockTime obsolation_treshold,
                                                  Recycle* data_recycle
                                                  )
{
  SlidingWindow* result;
  result = make_slidingwindow(num_limit, obsolation_treshold);
  result->data_recycle = data_recycle;
  return result;
}


void
slidingwindow_class_init (SlidingWindowClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = slidingwindow_finalize;

  GST_DEBUG_CATEGORY_INIT (slidingwindow_debug_category, "slidingwindow", 0,
      "SlidingWindow");

}

void
slidingwindow_finalize (GObject * object)
{
  SlidingWindow* this;

  if(!object){
    return;
  }
  this = (SlidingWindow*)object;
  datapuffer_clear(this->items, _switem_dtor);
  datapuffer_dtor(this->items);

  if(this->data_recycle){
    g_object_unref(this->data_recycle);
  }
  g_object_unref(this->items_recycle);

  g_list_free_full(this->plugins, swplugin_dtor);
  g_object_unref(this->on_add_item);
  g_object_unref(this->on_rem_item);

  if(this->preprocessors){
    g_object_unref(this->preprocessors);
  }

  if(this->postprocessors){
    g_object_unref(this->postprocessors);
  }

}

void
slidingwindow_init (SlidingWindow * this)
{

}

SlidingWindow* make_slidingwindow(guint32 num_limit, GstClockTime obsolation_treshold)
{
  SlidingWindow* result;
  result = g_object_new (SLIDINGWINDOW_TYPE, NULL);
  if(!num_limit){
    g_warning("Num limit can not be zero");
    num_limit = 32;
  }

  result->items            = datapuffer_ctor(num_limit);
  result->sysclock         = gst_system_clock_obtain();
  result->treshold         = obsolation_treshold;
  result->num_limit        = result->num_act_limit = num_limit;
  result->min_itemnum      = 1;
  result->obsolate         = _slidingwindow_default_obsolation;
  result->obsolate_udata   = result;
  result->on_add_item      = make_notifier("SW: on-add-item");
  result->on_rem_item      = make_notifier("SW: on-rem-item");
  result->data_recycle     = NULL;
  result->items_recycle    = make_recycle_switem( MAX(4, num_limit>>2), _switem_shaper);
  result->made             = _now(result);
  result->preprocessors      = NULL;
  result->postprocessors    = NULL;
  result->debug.active     = FALSE;

  return result;
}

static void _slidingwindow_rem(SlidingWindow* this)
{
  SlidingWindowItem *item;
  if(datapuffer_isempty(this->items)){
    return;
  }

  item = datapuffer_read(this->items);

  notifier_do(this->on_rem_item, item->data);
  notifier_do(this->postprocessors, item->data);

  if(this->data_recycle) {
    recycle_add(this->data_recycle, item->data);
  }

  if(this->debug.active){
    gchar item_data_str[1024];
    this->debug.sprintf(item->data, item_data_str);
    this->debug.logger("Item removed: %p->%s t(%2.1f), c(%d)\n",
        item,
        item_data_str,
        (gdouble) (_now(this) - this->made) / (gdouble) GST_SECOND,
        datapuffer_readcapacity(this->items)
    );
  }

  recycle_add(this->items_recycle, item);
//  g_slice_free(SlidingWindowItem, item);
}

void slidingwindow_clear(SlidingWindow* this)
{
  while(!datapuffer_isempty(this->items)){
      _slidingwindow_rem(this);
  }
}

void slidingwindow_set_data_recycle(SlidingWindow* this, Recycle* data_recycle)
{
  this->data_recycle = data_recycle;
}

void slidingwindow_add_preprocessor(SlidingWindow* this, ListenerFunc callback, gpointer udata)
{
  if(!this->preprocessors){
    this->preprocessors = make_notifier("SW: on-data-ref");
  }
  notifier_add_listener(this->preprocessors, callback, udata);

}

void slidingwindow_add_postprocessor(SlidingWindow* this, ListenerFunc callback, gpointer udata)
{
  if(!this->postprocessors){
    this->postprocessors = make_notifier("SW: on-data-unref");
  }
  notifier_add_listener(this->postprocessors, callback, udata);
}

void slidingwindow_add_processors(SlidingWindow* this, ListenerFunc preprocess, ListenerFunc postprocess, gpointer udata)
{
  slidingwindow_add_preprocessor(this,  preprocess,  udata);
  slidingwindow_add_postprocessor(this, postprocess, udata);
}

void slidingwindow_setup_debug(SlidingWindow* this, SlidingWindowItemSprintf sprintf, SlidingWindowItemLogger logger)
{
  this->debug.sprintf = sprintf;
  this->debug.logger = logger;
  this->debug.active = TRUE;
}

void slidingwindow_set_min_itemnum(SlidingWindow* this, gint min_itemnum)
{
  this->min_itemnum = min_itemnum;
}

void slidingwindow_setup_custom_obsolation(SlidingWindow* this, gboolean (*custom_obsolation)(gpointer,SlidingWindowItem*),gpointer custom_obsolation_udata)
{
  this->obsolate = custom_obsolation_udata;
  this->obsolate_udata = custom_obsolation_udata;
}

gint32 slidingwindow_get_counter(SlidingWindow* this){
  return datapuffer_readcapacity(this->items);
}

static void _slidingwindow_obsolate_num_limit(SlidingWindow* this)
{
  while(datapuffer_isfull(this->items) || this->num_act_limit < datapuffer_readcapacity(this->items)){
    _slidingwindow_rem(this);
  }
}

gboolean _slidingwindow_default_obsolation(gpointer udata, SlidingWindowItem *item)
{
  SlidingWindow *this = udata;
  if(!this->treshold){
    return FALSE;
  }
  return item->added < _now(this) - this->treshold;
}

static void _slidingwindow_obsolate_time_limit(SlidingWindow* this)
{
  SlidingWindowItem *item;
again:
  if(datapuffer_readcapacity(this->items) <= this->min_itemnum){
    return;
  }
  item = datapuffer_peek_first(this->items);
  if(!item){
    return;
  }
  if(!this->obsolate(this->obsolate_udata, item)){
    return;
  }
  _slidingwindow_rem(this);
  goto again;
}

void slidingwindow_refresh(SlidingWindow *this)
{
  if(datapuffer_isempty(this->items)){
    return;
  }
  _slidingwindow_obsolate_num_limit(this);
  _slidingwindow_obsolate_time_limit(this);
}

gpointer slidingwindow_peek_oldest(SlidingWindow* this)
{
  SlidingWindowItem *item;
  if(datapuffer_isempty(this->items)){
    return NULL;
  }
  item = datapuffer_peek_first(this->items);
  return item->data;
}

gpointer slidingwindow_peek_latest(SlidingWindow* this)
{
  SlidingWindowItem *item;
  if(datapuffer_isempty(this->items)){
    return NULL;
  }
  item = datapuffer_peek_last(this->items);
  return item->data;
}

typedef struct{
  gint (*comparator)(gpointer item, gpointer udata);
  gpointer udata;
}PeekCustomHelper;

static gint _slidingwindow_peek_custom_helper(gpointer item, gpointer udata)
{
  PeekCustomHelper* helper = udata;
  SlidingWindowItem* sitem = item;
  return helper->comparator(sitem->data, helper->udata);
}

gpointer slidingwindow_peek_custom(SlidingWindow* this, gint (*comparator)(gpointer item, gpointer udata), gpointer udata)
{
  PeekCustomHelper helper;
  SlidingWindowItem* result;
  if(datapuffer_isempty(this->items)){
    return NULL;
  }
  helper.comparator = comparator;
  helper.udata = udata;
  result = datapuffer_peek_custom(this->items, _slidingwindow_peek_custom_helper, &helper);
  return result ? result->data : NULL;
}

void slidingwindow_set_threshold(SlidingWindow* this, GstClockTime obsolation_treshold)
{
  this->treshold = obsolation_treshold;
}

void slidingwindow_set_act_limit(SlidingWindow* this, gint32 act_limit)
{
  this->num_act_limit = MIN(act_limit, this->num_limit);
}

void slidingwindow_add_int(SlidingWindow* this, gint data)
{
  slidingwindow_add_data(this, &data);
}

void slidingwindow_add_data(SlidingWindow* this, gpointer data)
{
  SlidingWindowItem *item;

  slidingwindow_refresh(this);

//  item = g_slice_new0(SlidingWindowItem);
  item = recycle_retrieve(this->items_recycle);

  item->added = _now(this);
  if(this->data_recycle){
    item->data = recycle_retrieve_and_shape(this->data_recycle, data);
  }else{
    item->data = data;
  }

  notifier_do(this->preprocessors, item->data);
  notifier_do(this->on_add_item, item->data);

  datapuffer_write(this->items, item);

  if(this->debug.active){
      gchar item_data_str[1024];
      this->debug.sprintf(item->data, item_data_str);
      this->debug.logger("Item added: %p->%s t(%2.1f), c(%d)\n",
          item,
          item_data_str,
          (gdouble) (_now(this) - this->made) / (gdouble) GST_SECOND,
          datapuffer_readcapacity(this->items)
      );
  }
}

void slidingwindow_add_plugin(SlidingWindow* this, SlidingWindowPlugin *swplugin)
{
  this->plugins = g_list_append(this->plugins, swplugin);
  if(swplugin->add_pipe){
    notifier_add_listener(this->on_add_item, (ListenerFunc) swplugin->add_pipe, swplugin->add_data);
  }

  if(swplugin->rem_pipe){
    notifier_add_listener(this->on_rem_item, (ListenerFunc) swplugin->rem_pipe, swplugin->rem_data);
  }
}


void slidingwindow_add_plugins (SlidingWindow* this, ... )
{
  va_list arguments;
  SlidingWindowPlugin* swplugin = NULL;
  va_start ( arguments, this );
  for(swplugin = va_arg( arguments, SlidingWindowPlugin*); swplugin; swplugin = va_arg(arguments, SlidingWindowPlugin*)){
      slidingwindow_add_plugin(this, swplugin);
  }
  va_end ( arguments );
}


void slidingwindow_add_on_change(SlidingWindow* this, ListenerFunc add_callback, ListenerFunc rem_callback, gpointer udata)
{
  notifier_add_listener(this->on_add_item, add_callback, udata);
  notifier_add_listener(this->on_rem_item, rem_callback, udata);
}

void slidingwindow_add_on_add_item_cb(SlidingWindow* this, ListenerFunc callback, gpointer udata)
{
  notifier_add_listener(this->on_add_item, callback, udata);
}

void slidingwindow_add_on_rem_item_cb(SlidingWindow* this, ListenerFunc callback, gpointer udata)
{
  notifier_add_listener(this->on_rem_item, callback, udata);
}

gboolean slidingwindow_is_empty(SlidingWindow* this)
{
  return datapuffer_isempty(this->items);
}

void swplugin_notify(SlidingWindowPlugin* this, gpointer subject)
{
  notifier_do(this->on_calculated, subject);
}

SlidingWindowPlugin* make_swplugin(ListenerFunc on_calculated_cb, gpointer udata)
{
  SlidingWindowPlugin* this = swplugin_ctor();
  notifier_add_listener(this->on_calculated, on_calculated_cb, udata);
  return this;
}

SlidingWindowPlugin* swplugin_ctor(void)
{
  SlidingWindowPlugin* this;
  this = malloc(sizeof(SlidingWindowPlugin));
  memset(this, 0, sizeof(SlidingWindowPlugin));
  this->on_calculated = make_notifier("SWPlugin: on-calculated");
  this->disposer = swplugin_dtor;
  return this;
}

void swplugin_dtor(gpointer target)
{
  SlidingWindowPlugin* swplugin;
  if(!target){
    return;
  }
  swplugin = target;
  swplugin->disposer(swplugin);
}

