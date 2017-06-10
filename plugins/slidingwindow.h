/*
 * coslidingwindow.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef SLIDINGWINDOW_H_
#define SLIDINGWINDOW_H_

#include <gst/gst.h>
#include "lib_datapuffer.h"
#include "lib_bintree.h"
#include "notifier.h"
#include "recycle.h"

typedef struct _SlidingWindow SlidingWindow;
typedef struct _SlidingWindowClass SlidingWindowClass;


#define SLIDINGWINDOW_TYPE             (slidingwindow_get_type())
#define SLIDINGWINDOW(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SLIDINGWINDOW_TYPE,SlidingWindow))
#define SLIDINGWINDOW_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SLIDINGWINDOW_TYPE,SlidingWindowClass))
#define SLIDINGWINDOW_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SLIDINGWINDOW_TYPE))
#define SLIDINGWINDOW_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SLIDINGWINDOW_TYPE))
#define SLIDINGWINDOW_CAST(src)        ((SlidingWindow *)(src))

struct _SlidingWindowItem
{
  gpointer      data;
  GstClockTime  added;
};

typedef void (*SlidingWindowItemSprintf)(gpointer item_data, gchar* item_to_str);
typedef void (*SlidingWindowItemLogger)(const gchar* format, ...);

typedef struct _SlidingWindowItem SlidingWindowItem;
struct _SlidingWindow
{
  GObject                  object;
  GstClockTime             made;
  datapuffer_t*            items;
  gint                     min_itemnum;
  GstClock*                sysclock;
  GstClockTime             treshold;
  gint32                   num_limit;
  gint32                   num_act_limit;

  gboolean               (*obsolate)(gpointer, SlidingWindowItem*);
  gpointer                 obsolate_udata;
  GList*                   plugins;

  Notifier*                on_add_item;
  Notifier*                on_rem_item;

  Notifier*                preprocessors;
  Notifier*                postprocessors;

  Recycle*                 data_recycle;
  Recycle*                 items_recycle;

  struct{
    gboolean                 active;
    SlidingWindowItemLogger  logger;
    SlidingWindowItemSprintf sprintf;
  }debug;
};

struct _SlidingWindowClass{
  GObjectClass parent_class;

};

typedef struct _SlidingWindowPlugin{
  void      (*rem_pipe)(gpointer,gpointer);
  gpointer    rem_data;
  void      (*add_pipe)(gpointer,gpointer);
  gpointer    add_data;
  void      (*disposer)(gpointer);
  void      (*clear)(gpointer);
  Notifier*   on_calculated;
  gpointer    priv;
}SlidingWindowPlugin;


GType slidingwindow_get_type (void);
SlidingWindow* make_slidingwindow_uint16(guint32 num_limit, GstClockTime obsolation_treshold);
SlidingWindow* make_slidingwindow_int32(guint32 num_limit, GstClockTime obsolation_treshold);
SlidingWindow* make_slidingwindow_int64(guint32 num_limit, GstClockTime obsolation_treshold);
SlidingWindow* make_slidingwindow_uint32(guint32 num_limit, GstClockTime obsolation_treshold);
SlidingWindow* make_slidingwindow_uint64(guint32 num_limit, GstClockTime obsolation_treshold);
SlidingWindow* make_slidingwindow_double(guint32 num_limit, GstClockTime obsolation_treshold);
SlidingWindow* make_slidingwindow_with_data_recycle(guint32 num_limit,
                                                  GstClockTime obsolation_treshold,
                                                  Recycle* data_recycle
                                                  );
SlidingWindow* make_slidingwindow(guint32 num_limit, GstClockTime obsolation_treshold);
void slidingwindow_clear(SlidingWindow* this);
void slidingwindow_dtor(gpointer target);
void slidingwindow_refresh(SlidingWindow *this);
void slidingwindow_set_threshold(SlidingWindow* this, GstClockTime obsolation_treshold);
gpointer slidingwindow_peek_oldest(SlidingWindow* this);
gpointer slidingwindow_peek_latest(SlidingWindow* this);
gpointer slidingwindow_peek_custom(SlidingWindow* this, gint (*comparator)(gpointer item, gpointer udata), gpointer udata);
void slidingwindow_set_act_limit(SlidingWindow* this, gint32 act_limit);
void slidingwindow_add_int(SlidingWindow* this, gint data);
void slidingwindow_add_data(SlidingWindow* this, gpointer data);
void slidingwindow_set_data_recycle(SlidingWindow* this, Recycle* data_recycle);

void slidingwindow_add_preprocessor(SlidingWindow* this, ListenerFunc callback, gpointer udata);
void slidingwindow_add_postprocessor(SlidingWindow* this, ListenerFunc callback, gpointer udata);
void slidingwindow_add_processors(SlidingWindow* this, ListenerFunc preprocess_cb, ListenerFunc postprocess_cb, gpointer udata);

void slidingwindow_setup_debug(SlidingWindow* this, SlidingWindowItemSprintf sprintf, SlidingWindowItemLogger logger);
void slidingwindow_set_min_itemnum(SlidingWindow* this, gint min_itemnum);
void slidingwindow_setup_custom_obsolation(SlidingWindow* this, gboolean (*custom_obsolation)(gpointer,SlidingWindowItem*),gpointer custom_obsolation_udata);
gint32 slidingwindow_get_counter(SlidingWindow* this);
void slidingwindow_add_plugin(SlidingWindow* this, SlidingWindowPlugin *plugin);
void slidingwindow_add_plugins (SlidingWindow* this, ... );

void slidingwindow_add_on_change(SlidingWindow* this, ListenerFunc add_callback, ListenerFunc rem_callback, gpointer udata);
void slidingwindow_add_on_add_item_cb(SlidingWindow* this, ListenerFunc callback, gpointer udata);
void slidingwindow_add_on_rem_item_cb(SlidingWindow* this, ListenerFunc callback, gpointer udata);
gboolean slidingwindow_is_empty(SlidingWindow* this);

void swplugin_notify(SlidingWindowPlugin* this, gpointer subject);
SlidingWindowPlugin* make_swplugin(ListenerFunc on_calculated_cb, gpointer udata);
SlidingWindowPlugin* swplugin_ctor(void);
void swplugin_dtor(gpointer target);

#endif /* SLIDINGWINDOW_H_ */
