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

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void slidingwindow_finalize (GObject * object);
static gboolean _slidingwindow_default_obsolation(gpointer udata, SlidingWindowItem *item);


//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
slidingwindow_class_init (SlidingWindowClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = slidingwindow_finalize;

  GST_DEBUG_CATEGORY_INIT (slidingwindow_debug_category, "slidingwindow", 0,
      "SlidingWindow");

}

static void _item_dtor(gpointer itemptr)
{
  g_slice_free(SlidingWindowItem, itemptr);
}

void
slidingwindow_finalize (GObject * object)
{
  SlidingWindow* this;

  if(!object){
    return;
  }
  this = (SlidingWindow*)object;
  datapuffer_clear(this->items, _item_dtor);
  datapuffer_dtor(this->items);

  g_list_free_full(this->plugins, swplugin_dtor);
}

void
slidingwindow_init (SlidingWindow * this)
{

}

static void _copyfnc_int32(gpointer udata, gpointer dst, gpointer src)
{
  memcpy(dst, src, sizeof(gint32));
}

static void _deallocfnc_int32(gpointer udata, gpointer data)
{
  g_slice_free(gint32, data);
}

static gpointer _allocfnc_int32(gpointer udata, gpointer data)
{
  gpointer result;
  result = g_slice_new0(gint32);
  memcpy(result, data, sizeof(gint32));
  return result;
}

SlidingWindow* make_slidingwindow_int32(guint32 num_limit, GstClockTime obsolation_treshold)
{
  SlidingWindow* result;
  result = make_slidingwindow_with_allocators(num_limit, obsolation_treshold,
                                              _allocfnc_int32, NULL,
                                              _deallocfnc_int32, NULL,
                                              _copyfnc_int32, NULL);
  return result;
}

static void _copyfnc_int64(gpointer udata, gpointer dst, gpointer src)
{
  memcpy(dst, src, sizeof(gint64));
}

static void _deallocfnc_int64(gpointer udata, gpointer data)
{
  g_slice_free(gint64, data);
}

static gpointer _allocfnc_int64(gpointer udata, gpointer data)
{
  gpointer result;
  result = g_slice_new0(gint64);
  memcpy(result, data, sizeof(gint64));
  return result;
}

SlidingWindow* make_slidingwindow_int64(guint32 num_limit, GstClockTime obsolation_treshold)
{
  SlidingWindow* result;
  result = make_slidingwindow_with_allocators(num_limit, obsolation_treshold,
                                              _allocfnc_int64, NULL,
                                              _deallocfnc_int64, NULL,
                                              _copyfnc_int64, NULL);
  return result;
}

static void _copyfnc_uint64(gpointer udata, gpointer dst, gpointer src)
{
  memcpy(dst, src, sizeof(guint64));
}

static void _deallocfnc_uint64(gpointer udata, gpointer data)
{
  g_slice_free(guint64, data);
}

static gpointer _allocfnc_uint64(gpointer udata, gpointer data)
{
  gpointer result;
  result = g_slice_new0(guint64);
  memcpy(result, data, sizeof(guint64));
  return result;
}

SlidingWindow* make_slidingwindow_uint64(guint32 num_limit, GstClockTime obsolation_treshold)
{
  SlidingWindow* result;
  result = make_slidingwindow_with_allocators(num_limit, obsolation_treshold,
                                              _allocfnc_uint64, NULL,
                                              _deallocfnc_uint64, NULL,
                                              _copyfnc_uint64, NULL);
  return result;
}


static void _copyfnc_double(gpointer udata, gpointer dst, gpointer src)
{
  memcpy(dst, src, sizeof(gdouble));
}

static void _deallocfnc_double(gpointer udata, gpointer data)
{
  g_slice_free(gdouble, data);
}

static gpointer _allocfnc_double(gpointer udata, gpointer data)
{
  gpointer result;
  result = g_slice_new0(gdouble);
  memcpy(result, data, sizeof(gdouble));
  return result;
}

SlidingWindow* make_slidingwindow_double(guint32 num_limit, GstClockTime obsolation_treshold)
{
  SlidingWindow* result;
  result = make_slidingwindow_with_allocators(num_limit, obsolation_treshold,
                                              _allocfnc_double, NULL,
                                              _deallocfnc_double, NULL,
                                              _copyfnc_double, NULL);
  return result;
}



SlidingWindow* make_slidingwindow_with_allocators(guint32 num_limit,
                                                  GstClockTime obsolation_treshold,
                                                  gpointer               (*alloc)(gpointer,gpointer),
                                                  gpointer                 alloc_udata,
                                                  void                   (*dealloc)(gpointer,gpointer),
                                                  gpointer                 dealloc_udata,
                                                  void                   (*copy)(gpointer,gpointer,gpointer),
                                                  gpointer                 copy_udata
                                                  )
{
  SlidingWindow* result;
  result = make_slidingwindow(num_limit, obsolation_treshold);
  result->allocator.alloc         = alloc;
  result->allocator.alloc_udata   = alloc_udata;
  result->allocator.dealloc       = dealloc;
  result->allocator.dealloc_udata = dealloc_udata;
  result->allocator.copy          = copy;
  result->allocator.copy_udata    = copy_udata;
  result->allocator.active        = TRUE;
  return result;
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
  result->allocator.active = FALSE;
  result->min_itemnum      = 1;
  result->obsolate         = _slidingwindow_default_obsolation;
  result->obsolate_udata   = result;
  return result;
}

static void _slidingwindow_rem(SlidingWindow* this)
{
  SlidingWindowItem *item;
  GList* it;
  if(datapuffer_isempty(this->items)){
    return;
  }

  item = datapuffer_read(this->items);

  for(it = this->plugins; it; it = it->next){
      SlidingWindowPlugin *swplugin;
      swplugin = it->data;
      if(swplugin->rem_pipe){
        swplugin->rem_pipe(swplugin->rem_data, item->data);
      }
  }

  if(this->allocator.active){
    this->allocator.dealloc(this->allocator.dealloc_udata, item->data);
  }

  g_slice_free(SlidingWindowItem, item);
}

void slidingwindow_clear(SlidingWindow* this)
{
  while(!datapuffer_isempty(this->items)){
      _slidingwindow_rem(this);
  }
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
  if(datapuffer_readcapacity(this->items) < this->min_itemnum){
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

void slidingwindow_set_treshold(SlidingWindow* this, GstClockTime obsolation_treshold)
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
  GList* it;
  SlidingWindowItem *item;

  slidingwindow_refresh(this);

  item = g_slice_new0(SlidingWindowItem);
  item->added = _now(this);
  if(this->allocator.active){
    item->data = this->allocator.alloc(this->allocator.alloc_udata, data);
  }else{
    item->data = data;
  }

  for(it = this->plugins; it; it = it->next){
      SlidingWindowPlugin *swplugin;
      swplugin = it->data;
      if(swplugin->add_pipe){
        swplugin->add_pipe(swplugin->add_data, item->data);
      }
  }

  datapuffer_write(this->items, item);
}

void slidingwindow_add_plugin(SlidingWindow* this, SlidingWindowPlugin *swplugin)
{
  this->plugins = g_list_append(this->plugins, swplugin);
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


void slidingwindow_add_pipes(SlidingWindow* this, void (*rem_pipe)(gpointer,gpointer),gpointer rem_data, void (*add_pipe)(gpointer,gpointer),gpointer add_data)
{
  SlidingWindowPlugin *swplugin;
  swplugin = g_malloc0(sizeof(SlidingWindowPlugin));
  memset(swplugin, 0, sizeof(SlidingWindowPlugin));
  swplugin->add_pipe = add_pipe;
  swplugin->add_data = add_data;
  swplugin->rem_pipe = rem_pipe;
  swplugin->rem_data = rem_data;
  swplugin->disposer = g_free;
  swplugin->priv     = NULL;
  slidingwindow_add_plugin(this, swplugin);
}

gboolean slidingwindow_is_empty(SlidingWindow* this)
{
  return datapuffer_isempty(this->items);
}


SlidingWindowPlugin* swplugin_ctor(void)
{
  SlidingWindowPlugin* this;
  this = malloc(sizeof(SlidingWindowPlugin));
  memset(this, 0, sizeof(SlidingWindowPlugin));
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

//------------------------------------------------------------------------------

datapuffer_t* datapuffer_ctor(gint32 size)
{
        datapuffer_t* result;
        result = (datapuffer_t*) g_malloc0(sizeof(datapuffer_t));
        result->items = (gpointer*) g_malloc0(sizeof(gpointer) * size);
        result->length = size;
        result->start = 0;
        result->end = 0;
        result->count = 0;
        return result;
}//# datapuffer_ctor end


void datapuffer_dtor(datapuffer_t* puffer)
{
        gint32 index;
        gpointer item;
        index = 0;
        if(puffer == NULL){
                return;
        }
        for(index = 0; index <  puffer->length; index++)
        {
                item = puffer->items[index];
                if(item == NULL)
                {
                  continue;
                }
                g_free(item);
        }
        g_free(puffer->items);
        g_free(puffer);
}//# datapuffer_dtor end

void datapuffer_write(datapuffer_t* puffer, gpointer item)
{
        puffer->items[puffer->end++] = item;
        ++puffer->count;
        if(puffer->length <= puffer->end){
                puffer->end = 0;
        }
}//# datapuffer_write end

gpointer datapuffer_read(datapuffer_t* puffer)
{
        puffer->read = puffer->items[puffer->start];
        puffer->items[puffer->start] = NULL;
        if(puffer->length <= ++puffer->start){
                puffer->start = 0;
        }
        --puffer->count;
        return puffer->read;
}//# datapuffer_read end

gpointer datapuffer_peek_first(datapuffer_t* puffer)
{
        return puffer->items[puffer->start];
}//# datapuffer_read end

gpointer datapuffer_peek_last(datapuffer_t* puffer)
{
  gint32 pos;
  if(puffer->end == 0){
    pos = puffer->length - 1;
  }else{
    pos = puffer->end - 1;
  }
  return puffer->items[pos];
}//# datapuffer_read end

gint32 datapuffer_readcapacity(datapuffer_t *datapuffer)
{
        return datapuffer->count;
}

gint32 datapuffer_writecapacity(datapuffer_t *datapuffer)
{
        return datapuffer->length - datapuffer->count;
}

gboolean datapuffer_isfull(datapuffer_t *datapuffer)
{
        return datapuffer->count == datapuffer->length;
}

gboolean datapuffer_isempty(datapuffer_t *datapuffer)
{
        return datapuffer->count == 0;
}

void datapuffer_clear(datapuffer_t *puffer, void (*dtor)(gpointer))
{
        gint32 i,c;
        void *item;
        for(i = 0, c = datapuffer_readcapacity(puffer); i < c; ++i){
                item = datapuffer_read(puffer);
                if(dtor == NULL){
                        continue;
                }
                dtor(item);
        }
}


