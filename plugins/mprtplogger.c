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

//#define THIS_READLOCK(this)
//#define THIS_READUNLOCK(this)
//#define THIS_WRITELOCK(this)
//#define THIS_WRITEUNLOCK(this)

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define LIST_READLOCK g_rw_lock_reader_lock(&list_mutex)
#define LIST_READUNLOCK g_rw_lock_reader_unlock(&list_mutex)
#define LIST_WRITELOCK g_rw_lock_writer_lock(&list_mutex)
#define LIST_WRITEUNLOCK g_rw_lock_writer_unlock(&list_mutex)

#define DATABED_LENGTH 1400

GST_DEBUG_CATEGORY_STATIC (mprtp_logger_debug_category);
#define GST_CAT_DEFAULT mprtp_logger_debug_category

#define _now(this) (gst_clock_get_time (this->sysclock))

G_DEFINE_TYPE (MPRTPLogger, mprtp_logger, G_TYPE_OBJECT);

static MPRTPLogger *loggerptr = NULL;
static GRWLock list_mutex;
static GList* subscriptions = NULL;
static GstClock*  listclock = NULL;
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
mprtp_logger_finalize (GObject * object);

static void
_logging_process(void *data);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

gpointer mprtp_malloc(gsize bytenum){
  gpointer result;
  result = g_malloc0(bytenum);
//  if(!g_hash_table_lookup(this->reserves, result)){
//    g_hash_table_insert(this->reserves, result, NULL);
//    mprtp_logger("logs/memory.log", "malloc: %p\n", result);
//  }else{
//    mprtp_logger("logs/memory.log", "malloc: %p <- doubly reserved\n", result);
//  }
  return result;
}

void mprtp_free(gpointer ptr){

//  if(g_hash_table_lookup(this->reserves, ptr)){
//    g_hash_table_remove(this->reserves, ptr);
//    mprtp_logger("logs/memory.log", "free: %p\n", ptr);
//  }else{
//    mprtp_logger("logs/memory.log", "free: %p <- doubly freed\n", ptr);
//    g_warning("doubly free detected");
//  }
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
  g_rw_lock_init (&this->rwmutex);
  g_rw_lock_init (&list_mutex);
  this->enabled    = FALSE;
  this->sysclock   = gst_system_clock_obtain ();
  listclock        = gst_system_clock_obtain ();
  strcpy(this->path, "logs/");
  this->touches    = g_hash_table_new(g_str_hash, g_str_equal);
//  this->reserves   = g_hash_table_new(g_direct_hash, g_direct_equal);
}

void init_mprtp_logger(void)
{
  if(loggerptr != NULL){
    return;
  }
  loggerptr = g_object_new(MPRTPLOGGER_TYPE, NULL);
}


void enable_mprtp_logger(void)
{
  THIS_WRITELOCK(loggerptr);
  loggerptr->enabled = TRUE;

  loggerptr->thread = gst_task_new (_logging_process, loggerptr, NULL);
  g_rec_mutex_init (&loggerptr->thread_mutex);
  gst_task_set_lock (loggerptr->thread, &loggerptr->thread_mutex);
  gst_task_start (loggerptr->thread);

  THIS_WRITEUNLOCK(loggerptr);
}

void disable_mprtp_logger(void)
{
  THIS_WRITELOCK(loggerptr);
  loggerptr->enabled = FALSE;
  if(loggerptr->thread && gst_task_get_state(loggerptr->thread) == GST_TASK_STARTED){
    gst_task_stop (loggerptr->thread);
    gst_task_join (loggerptr->thread);
  }
  THIS_WRITEUNLOCK(loggerptr);
}

void mprtp_logger_set_target_directory(const gchar *path)
{
  THIS_WRITELOCK(loggerptr);
  strcpy(loggerptr->path, path);
  THIS_WRITEUNLOCK(loggerptr);
}

void mprtp_logger_get_target_directory(gchar* result)
{
  THIS_READLOCK(loggerptr);
  strcpy(result, loggerptr->path);
  THIS_READUNLOCK(loggerptr);
}

void mprtp_logger(const gchar *filename, const gchar * format, ...)
{
  FILE *file;
  va_list args;
  gchar writable[255];
  THIS_WRITELOCK(loggerptr);
  if(!loggerptr->enabled){
    goto done;
  }
  strcpy(writable, loggerptr->path);
  strcat(writable, filename);
//  strcpy(writable, filename);
  if(!g_hash_table_lookup(loggerptr->touches, writable)){
    g_hash_table_insert(loggerptr->touches, writable, writable);
    file = fopen(writable, "w");
  }else{
    file = fopen(writable, "a");
  }
  va_start (args, format);
  vfprintf (file, format, args);
  va_end (args);
  fclose(file);
done:
  THIS_WRITEUNLOCK(loggerptr);
}

void mprtp_logger_rewrite(const gchar *filename, const gchar * format, ...)
{
  FILE *file;
  va_list args;
  gchar writable[255];
  THIS_WRITELOCK(loggerptr);
  if(!loggerptr->enabled){
    goto done;
  }
  strcpy(writable, loggerptr->path);
  strcat(writable, filename);
//  strcpy(writable, filename);
  if(!g_hash_table_lookup(loggerptr->touches, writable)){
    g_hash_table_insert(loggerptr->touches, writable, writable);
  }
  file = fopen(writable, "w");
  va_start (args, format);
  vfprintf (file, format, args);
  va_end (args);
  fclose(file);
done:
  THIS_WRITEUNLOCK(loggerptr);
}

void mprtp_logger_open_collector(const gchar *filename)
{
  THIS_WRITELOCK(loggerptr);
  if(!loggerptr->enabled){
    goto done;
  }
  if(loggerptr->collector_string){
    g_string_free(loggerptr->collector_string, TRUE);
  }
  loggerptr->collector_string = g_string_new(NULL);
  strcpy(loggerptr->collector_filename, filename);
done:
  THIS_WRITEUNLOCK(loggerptr);
}

void mprtp_logger_close_collector(void)
{
  gchar *string = NULL;
  gchar filename[255];
  THIS_WRITELOCK(loggerptr);
  if(!loggerptr->enabled){
    goto done;
  }
  string = g_string_free(loggerptr->collector_string, FALSE);
  loggerptr->collector_string = NULL;
  memcpy(filename, loggerptr->collector_filename, 255);
  memset(loggerptr->collector_filename, 0, 255);
done:
  THIS_WRITEUNLOCK(loggerptr);
  if(string){
    mprtp_logger(filename, "%s", string);
  }
}

void mprtp_logger_collect(const gchar * format, ...)
{
  va_list args;
  THIS_WRITELOCK(loggerptr);
  if(!loggerptr->enabled){
    goto done;
  }
  va_start (args, format);
  g_string_append_vprintf(loggerptr->collector_string, format, args);
  va_end (args);
done:
  THIS_WRITEUNLOCK(loggerptr);

}

typedef struct{
  void      (*logging_fnc)(gpointer);
  gpointer    data;
  guint       tick_th;
  guint       tick_count;
  GRWLock*    rwmutex;
}Subscription;


void mprtp_logger_add_logging_fnc(void(*logging_fnc)(gpointer),gpointer data, guint tick_th, GRWLock *rwmutex)
{
  Subscription *subscription;
  LIST_WRITELOCK;
  subscription = mprtp_malloc(sizeof(Subscription));
  subscription->logging_fnc = logging_fnc;
  subscription->data = data;
  subscription->tick_th = tick_th;
  subscription->rwmutex = rwmutex;
  subscriptions = g_list_prepend(subscriptions, subscription);
  LIST_WRITEUNLOCK;
}

void _logging_process(void *data)
{
  GstClockTime next_scheduler_time;
  GstClockID clock_id;
  GList *it;
  Subscription *subscription;
  LIST_WRITELOCK;

  for(it = subscriptions; it; it = it->next){
      subscription = it->data;
      if(++subscription->tick_count < subscription->tick_th){
        continue;
      }
      subscription->tick_count=0;
      g_rw_lock_writer_lock(subscription->rwmutex);
      subscription->logging_fnc(subscription->data);
      g_rw_lock_writer_unlock(subscription->rwmutex);
  }

  next_scheduler_time = gst_clock_get_time (listclock) + 100 * GST_MSECOND;
  LIST_WRITEUNLOCK;
  clock_id = gst_clock_new_single_shot_id (loggerptr->sysclock, next_scheduler_time);

  if (gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED) {
    GST_WARNING_OBJECT (loggerptr, "The clock wait is interrupted");
  }
  gst_clock_id_unref (clock_id);
}

#undef MAX_RIPORT_INTERVAL
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
