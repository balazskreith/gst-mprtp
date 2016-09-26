/*
 * coslidingwindow.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef SLIDINGWINDOW_H_
#define SLIDINGWINDOW_H_

#include <gst/gst.h>
#include "lib_bintree.h"

typedef struct _SlidingWindow SlidingWindow;
typedef struct _SlidingWindowClass SlidingWindowClass;


#define SLIDINGWINDOW_TYPE             (slidingwindow_get_type())
#define SLIDINGWINDOW(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SLIDINGWINDOW_TYPE,SlidingWindow))
#define SLIDINGWINDOW_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SLIDINGWINDOW_TYPE,SlidingWindowClass))
#define SLIDINGWINDOW_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SLIDINGWINDOW_TYPE))
#define SLIDINGWINDOW_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SLIDINGWINDOW_TYPE))
#define SLIDINGWINDOW_CAST(src)        ((SlidingWindow *)(src))

typedef struct datapuffer_struct_t
{
        gpointer                *items;                ///< A pointer array of data the puffer will uses for storing
        gint32                   length;               ///< The maximal amount of data the puffer can store
        gint32                   start;        ///< index for read operations. It points to the next element going to be read
        gint32                   end;      ///< index for write operations. It points to the last element, which was written by the puffer
        gint32                   count;
        gpointer                 read;
} datapuffer_t;

struct _SlidingWindowItem
{
  gpointer      data;
  GstClockTime  added;
};

typedef struct _SlidingWindowItem SlidingWindowItem;
struct _SlidingWindow
{
  GObject                  object;
  datapuffer_t*            items;
  gint                     min_itemnum;
  GstClock*                sysclock;
  GstClockTime             treshold;
  gint32                   num_limit;
  gint32                   num_act_limit;

  gboolean               (*obsolate)(gpointer, SlidingWindowItem*);
  gpointer                 obsolate_udata;
  GList*                   plugins;

  sallocator_t            allocator;

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
  gpointer    priv;
}SlidingWindowPlugin;



GType slidingwindow_get_type (void);
SlidingWindow* make_slidingwindow_int32(guint32 num_limit, GstClockTime obsolation_treshold);
SlidingWindow* make_slidingwindow_int64(guint32 num_limit, GstClockTime obsolation_treshold);
SlidingWindow* make_slidingwindow_uint64(guint32 num_limit, GstClockTime obsolation_treshold);
SlidingWindow* make_slidingwindow_double(guint32 num_limit, GstClockTime obsolation_treshold);
SlidingWindow* make_slidingwindow_with_allocators(guint32 num_limit,
                                                  GstClockTime obsolation_treshold,
                                                  gpointer               (*alloc)(gpointer,gpointer),
                                                  gpointer                 alloc_udata,
                                                  void                   (*dealloc)(gpointer,gpointer),
                                                  gpointer                 dealloc_udata,
                                                  void                   (*copy)(gpointer,gpointer,gpointer),
                                                  gpointer                 copy_udata
                                                  );
SlidingWindow* make_slidingwindow(guint32 num_limit, GstClockTime obsolation_treshold);
void slidingwindow_clear(SlidingWindow* this);
void slidingwindow_dtor(gpointer target);
void slidingwindow_refresh(SlidingWindow *this);
void slidingwindow_set_treshold(SlidingWindow* this, GstClockTime obsolation_treshold);
gpointer slidingwindow_peek_oldest(SlidingWindow* this);
gpointer slidingwindow_peek_latest(SlidingWindow* this);
gpointer slidingwindow_peek_custom(SlidingWindow* this, gint (*comparator)(gpointer item, gpointer udata), gpointer udata);
void slidingwindow_set_act_limit(SlidingWindow* this, gint32 act_limit);
void slidingwindow_add_int(SlidingWindow* this, gint data);
void slidingwindow_add_data(SlidingWindow* this, gpointer data);
void slidingwindow_set_min_itemnum(SlidingWindow* this, gint min_itemnum);
void slidingwindow_setup_custom_obsolation(SlidingWindow* this, gboolean (*custom_obsolation)(gpointer,SlidingWindowItem*),gpointer custom_obsolation_udata);
void slidingwindow_add_plugin(SlidingWindow* this, SlidingWindowPlugin *plugin);
void slidingwindow_add_plugins (SlidingWindow* this, ... );
void slidingwindow_add_pipes(SlidingWindow* this, void (*rem_pipe)(gpointer,gpointer),gpointer rem_data, void (*add_pipe)(gpointer,gpointer),gpointer add_data);
gboolean slidingwindow_is_empty(SlidingWindow* this);


SlidingWindowPlugin* swplugin_ctor(void);
void swplugin_dtor(gpointer target);

datapuffer_t* datapuffer_ctor(gint32 items_num);
void datapuffer_dtor(datapuffer_t *datapuffer);
gpointer datapuffer_read(datapuffer_t *datapuffer);
gpointer datapuffer_peek_first(datapuffer_t* puffer);
gpointer datapuffer_peek_last(datapuffer_t* puffer);
gpointer datapuffer_peek_custom(datapuffer_t* puffer, gint (*comparator)(gpointer item, gpointer udata), gpointer udata);
void datapuffer_write(datapuffer_t *datapuffer, void *item);
gint32 datapuffer_capacity(datapuffer_t *datapuffer);
gint32 datapuffer_readcapacity(datapuffer_t *datapuffer);
gint32 datapuffer_writecapacity(datapuffer_t *datapuffer);
gboolean datapuffer_isfull(datapuffer_t *datapuffer);
gboolean datapuffer_isempty(datapuffer_t *datapuffer);
void datapuffer_clear(datapuffer_t *datapuffer, void (*dtor)(gpointer));

#endif /* SLIDINGWINDOW_H_ */
