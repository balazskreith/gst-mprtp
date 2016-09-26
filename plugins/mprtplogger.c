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

typedef enum{
  MPRTP_LOGGER_MESSAGE_TYPE_STATE_CHANGING          = 1,
  MPRTP_LOGGER_MESSAGE_TYPE_WRITING                 = 2,
  MPRTP_LOGGER_MESSAGE_TYPE_CHANGE_TARGET_DIRECTORY = 3,
}MessageTypes;

typedef struct{
  MessageTypes type;
}Message;

typedef struct{
  Message  base;
  gboolean enabled;
}StatusMessage;

typedef struct{
  Message  base;
  gchar    string[1024];
  gchar    path[255];
  gboolean overwrite;
}WritingMessage;

typedef struct{
  Message            base;
  gchar              path[255];
}TargetDirectoryMessage;



G_DEFINE_TYPE (MPRTPLogger, mprtp_logger, G_TYPE_OBJECT);

static MPRTPLogger *this = NULL;
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
mprtp_logger_finalize (GObject * object);

static void
_writing(
    MPRTPLogger* this,
    WritingMessage *item);

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
  gst_task_stop (this->process);
  g_async_queue_unref(this->messages);
}

void
mprtp_logger_init (MPRTPLogger * this)
{
  this->enabled    = FALSE;
  this->sysclock   = gst_system_clock_obtain ();
  this->made       = _now(this);
  strcpy(this->path, "logs/");
  this->messages   = g_async_queue_new();

  this->process = gst_task_new (_process, this, NULL);
  g_rec_mutex_init (&this->process_mutex);
  gst_task_set_lock (this->process, &this->process_mutex);
  gst_task_start (this->process);
}

void init_mprtp_logger(void)
{
  if(this != NULL){
    return;
  }
  this = g_object_new(MPRTPLOGGER_TYPE, NULL);
}

static void _process(gpointer udata)
{
  MPRTPLogger* this = udata;
  Message* msg;
again:
  msg = (Message*) g_async_queue_timeout_pop(this->messages, 1000);
  if(!msg){
    goto done;
  }
  switch(msg->type){
    case MPRTP_LOGGER_MESSAGE_TYPE_STATE_CHANGING:
    {
      StatusMessage* status_msg = (StatusMessage*)msg;
      this->enabled = status_msg->enabled;
      g_slice_free(StatusMessage, status_msg);
    }
    break;
    case MPRTP_LOGGER_MESSAGE_TYPE_WRITING:
    {
      _writing(this, (WritingMessage*) msg);
      g_slice_free(WritingMessage, msg);
    }
    break;
    case MPRTP_LOGGER_MESSAGE_TYPE_CHANGE_TARGET_DIRECTORY:
    {
      TargetDirectoryMessage* target_msg = (TargetDirectoryMessage*)msg;
      strcpy(this->path, target_msg->path);
      g_slice_free(TargetDirectoryMessage, target_msg);
    }
    break;
    default:
      g_warning("Unhandled message with type %d", msg->type);
    break;
  }
  goto again;
done:
  return;
}

void mprtp_logger_set_state(gboolean enabled)
{
  StatusMessage *msg;
  msg = g_slice_new0(StatusMessage);
  msg->base.type = MPRTP_LOGGER_MESSAGE_TYPE_STATE_CHANGING;
  msg->enabled = enabled;

  g_async_queue_push(this->messages, msg);
}

void mprtp_logger_set_target_directory(const gchar *path)
{
  TargetDirectoryMessage *msg;
  msg = g_slice_new0(TargetDirectoryMessage);
  msg->base.type = MPRTP_LOGGER_MESSAGE_TYPE_WRITING;

  strcpy(msg->path, path);
  g_async_queue_push(this->messages, msg);
}

void mprtp_logger(const gchar *filename, const gchar * format, ...)
{
  WritingMessage *msg;
  va_list args;
  msg = g_slice_new0(WritingMessage);
  msg->base.type = MPRTP_LOGGER_MESSAGE_TYPE_WRITING;
  va_start (args, format);
  vsprintf(msg->string, format, args);
  va_end (args);

  strcpy(msg->path, this->path);
  strcat(msg->path, filename);
  g_async_queue_push(this->messages, msg);
}

void mprtp_log_one(const gchar *filename, const gchar * format, ...)
{
  WritingMessage *item;
  va_list args;
  item = g_slice_new0(WritingMessage);
  item->base.type = MPRTP_LOGGER_MESSAGE_TYPE_WRITING;

  va_start (args, format);
  vsprintf(item->string, format, args);
  va_end (args);

  item->overwrite = TRUE;

  strcpy(item->path, this->path);
  strcat(item->path, filename);
  g_async_queue_push(this->messages, item);
}


void _writing(MPRTPLogger* this, WritingMessage *item)
{
  FILE *fp;
  if(!this->enabled){
    return;
  }
  fp = fopen(item->path, item->overwrite ? "w" : "a+");
  fprintf (fp, "%s", item->string);
  fclose(fp);
  g_free(item);
}

#undef MAX_RIPORT_INTERVAL
#undef THIS_LOCK
#undef THIS_UNLOCK
