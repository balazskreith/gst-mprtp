/* GStreamer Scheduling tree
 * Copyright (C) 2015 Balázs Kreith (contact: balazs.kreith@gmail.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be ureful,
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
#include "mprtplogger.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

//#define THIS_LOCK(this)
//#define THIS_UNLOCK(this)
//#define THIS_LOCK(this)
//#define THIS_UNLOCK(this)

#define THIS_LOCK(this) g_mutex_lock(&this->mutex)
#define THIS_UNLOCK(this) g_mutex_unlock(&this->mutex)

#define LIST_READLOCK g_rw_lock_reader_lock(&list_mutex)
#define LIST_READUNLOCK g_rw_lock_reader_unlock(&list_mutex)
#define LIST_WRITELOCK g_rw_lock_writer_lock(&list_mutex)
#define LIST_WRITEUNLOCK g_rw_lock_writer_unlock(&list_mutex)

#define DATABED_LENGTH 1400

GST_DEBUG_CATEGORY_STATIC (mprtp_logger_debug_category);
#define GST_CAT_DEFAULT mprtp_logger_debug_category

#define _now(this) (gst_clock_get_time (this->sysclock))

typedef struct{
  void             (*logging_fnc)(gpointer,gchar*);
  gpointer           data;
  gchar              path[255];
}Subscription;

typedef struct{
  gchar    string[1024];
  gchar    path[255];
}WriterQueueItem;


G_DEFINE_TYPE (MPRTPLogger, mprtp_logger, G_TYPE_OBJECT);

static MPRTPLogger *this = NULL;
static GRWLock list_mutex;
static GList* subscriptions = NULL;
static GstClock*  listclock = NULL;
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
mprtp_logger_finalize (GObject * object);

static void
_caller_process(void *data);


static void
_writer_process(void *data);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

gpointer mprtp_malloc(gsize bytenum){
  gpointer result;
  result = g_malloc0(bytenum);
  return result;
}

void mprtp_free(gpointer ptr){
  g_free(ptr);
}


void
mprtp_logger_class_init (MPRTPLoggerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = mprtp_logger_finalize;

  GST_DEBUG_CATEGORY_INIT (mprtp_logger_debug_category, "mprtp_logger", 0,
      "MpRTP Receiving Controller");

}

void
mprtp_logger_finalize (GObject * object)
{
  MPRTPLogger *this = MPRTPLOGGER (object);
  g_object_unref (this->sysclock);
  g_object_unref(listclock);
  g_list_free_full(subscriptions, mprtp_free);
}

void
mprtp_logger_init (MPRTPLogger * this)
{
  g_mutex_init(&this->mutex);
  g_rw_lock_init (&list_mutex);
  this->enabled    = FALSE;
  this->sysclock   = gst_system_clock_obtain ();
  listclock        = gst_system_clock_obtain ();
  this->made       = _now(this);
  strcpy(this->path, "logs/");
  this->writer_queue = g_queue_new();
//  this->reserves   = g_hash_table_new(g_direct_hash, g_direct_equal);
}

void init_mprtp_logger(void)
{
  if(this != NULL){
    return;
  }
  this = g_object_new(MPRTPLOGGER_TYPE, NULL);
}


void enable_mprtp_logger(void)
{
  THIS_LOCK(this);
  this->enabled = TRUE;

  this->caller = gst_task_new (_caller_process, this, NULL);
  g_rec_mutex_init (&this->caller_mutex);
  gst_task_set_lock (this->caller, &this->caller_mutex);
  gst_task_start (this->caller);

  g_cond_init(&this->writer_cond);
  this->writer = gst_task_new (_writer_process, this, NULL);
  g_rec_mutex_init (&this->writer_mutex);
  gst_task_set_lock (this->writer, &this->writer_mutex);
  gst_task_start (this->writer);

  THIS_UNLOCK(this);
}

void disable_mprtp_logger(void)
{
  THIS_LOCK(this);
  this->enabled = FALSE;
  if(this->caller && gst_task_get_state(this->caller) == GST_TASK_STARTED){
    gst_task_stop (this->caller);
    gst_task_join (this->caller);
  }

  if(this->writer && gst_task_get_state(this->writer) == GST_TASK_STARTED){
    gst_task_stop (this->writer);
    gst_task_join (this->writer);
  }
  THIS_UNLOCK(this);
}

void mprtp_logger_set_target_directory(const gchar *path)
{
  THIS_LOCK(this);
  strcpy(this->path, path);
  THIS_UNLOCK(this);
}

void mprtp_logger_get_target_directory(gchar* result)
{
  THIS_LOCK(this);
  strcpy(result, this->path);
  THIS_UNLOCK(this);
}


void mprtp_logger(const gchar *filename, const gchar * format, ...)
{
  WriterQueueItem *item;
  va_list args;
  THIS_LOCK(this);
  if(!this->enabled){
    goto done;
  }
  item = g_malloc0(sizeof(WriterQueueItem));
  va_start (args, format);
  vsprintf(item->string, format, args);
  va_end (args);

  strcpy(item->path, this->path);
  strcat(item->path, filename);
  g_queue_push_tail(this->writer_queue, item);
  if(this->writer_wait){
    g_cond_signal(&this->writer_cond);
  }
done:
  THIS_UNLOCK(this);
}

void mprtp_logger_add_logging_fnc(void(*logging_fnc)(gpointer,gchar*),gpointer data, const gchar* filename)
{
  Subscription *subscription;
  LIST_WRITELOCK;
  subscription = mprtp_malloc(sizeof(Subscription));
  subscription->logging_fnc = logging_fnc;
  subscription->data        = data;

  strcpy(subscription->path, this->path);
  strcat(subscription->path, filename);
  unlink(subscription->path);
  subscriptions = g_list_prepend(subscriptions, subscription);
  LIST_WRITEUNLOCK;
}

void _caller_process(void *data)
{
  GstClockTime next_scheduler_time;
  GstClockID clock_id;
  GList *it;
  Subscription *subscription;
  WriterQueueItem* item;

  THIS_LOCK(this);

  for(it = subscriptions; it; it = it->next){
      subscription = it->data;
      if(!subscription->data){
        g_warning("subscripted logging function data param is NULL. Potentional nightmare might ended up with segfault.");
        continue;
      }
      item = g_malloc0(sizeof(WriterQueueItem));
      subscription->logging_fnc(subscription->data, item->string);
      strcpy(item->path, subscription->path);
      g_queue_push_tail(this->writer_queue, item);
  }
  next_scheduler_time = gst_clock_get_time (listclock) + 100 * GST_MSECOND;
  if(this->writer_wait){
    g_cond_signal(&this->writer_cond);
  }

  THIS_UNLOCK(this);

  clock_id = gst_clock_new_single_shot_id (this->sysclock, next_scheduler_time);

  if (gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED) {
    GST_WARNING_OBJECT (this, "The clock wait is interrupted");
  }
  gst_clock_id_unref (clock_id);
}





void _writer_process(void *data)
{
  WriterQueueItem *item;
  FILE *fp;

  THIS_LOCK(this);
wait:
  this->writer_wait = TRUE;
  g_cond_wait (&this->writer_cond, &this->mutex);

again:
  if(g_queue_is_empty(this->writer_queue)){
    goto wait;
  }
  item = g_queue_pop_head(this->writer_queue);
  this->writer_wait = FALSE;
  THIS_UNLOCK(this);

  fp = fopen(item->path, "a+");
  fprintf (fp, "%s", item->string);
  fclose(fp);
  g_free(item);

  THIS_LOCK(this);
  goto again;
}

#undef MAX_RIPORT_INTERVAL
#undef THIS_LOCK
#undef THIS_UNLOCK
