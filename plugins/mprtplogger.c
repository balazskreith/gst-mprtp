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


#define DATABED_LENGTH 1400

GST_DEBUG_CATEGORY_STATIC (mprtp_logger_debug_category);
#define GST_CAT_DEFAULT mprtp_logger_debug_category

#define _now(this) (gst_clock_get_time (this->sysclock))

typedef enum{
  MPRTP_LOGGER_MESSAGE_TYPE_STATE_CHANGING           = 1,
  MPRTP_LOGGER_MESSAGE_TYPE_WRITING                  = 2,
  MPRTP_LOGGER_MESSAGE_TYPE_CHANGE_TARGET_DIRECTORY  = 3,
  MPRTP_LOGGER_MESSAGE_TYPE_ADD_MEMORY_ALLOCATION    = 4,
  MPRTP_LOGGER_MESSAGE_TYPE_REM_MEMORY_ALLOCATION    = 5,
  MPRTP_LOGGER_MESSAGE_TYPE_PRINT_MEMORY_ALLOCATIONS = 6,
}MessageTypes;

//NOTE: This must be big enough to holds any kind of Message,
//since this type is going to be created and recycled
typedef struct {
  gchar    bytes[2048];
}MessageBlock;

typedef struct{
  MessageTypes type;
}Message;

typedef struct{
  Message  base;
  gboolean enabled;
}StatusMessage;

typedef struct{
  Message  base;
  gchar    type_name[255];
  gsize    size;
}MemoryConsumptionMessage;

typedef struct{
  Message  base;
  gchar    string[1024];
  gchar    filename[255];
  gboolean overwrite;
}WritingMessage;

typedef struct{
  Message            base;
  gchar              filename[255];
}TargetDirectoryMessage;

#define alloc_message(type) (type*) _messageblock_ctor()
#define throw_message(msg) _throw_messageblock((MessageBlock*) msg)

G_DEFINE_TYPE (MPRTPLogger, mprtp_logger, G_TYPE_OBJECT);

static MPRTPLogger *this = NULL;
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
mprtp_logger_finalize (GObject * object);

static void
_process(gpointer udata);

static MessageBlock*
_messageblock_ctor(void);

static void
_throw_messageblock(
    MessageBlock* messageblock);

static void
_messageblock_dtor(
    gpointer mem);

static void
_writing(
    MPRTPLogger* this,
    WritingMessage *item);

static void
_print_memory_allocation(
    gpointer key,
    gpointer value,
    gpointer udata);

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
  MessageBlock* msg_block;
  while((msg_block = g_async_queue_try_pop(this->messages)) != NULL){
    _messageblock_dtor(msg_block);
  }
  while((msg_block = g_async_queue_try_pop(this->recycle)) != NULL){
    _messageblock_dtor(msg_block);
  }
  g_object_unref (this->sysclock);
  gst_task_stop (this->process);
  g_async_queue_unref(this->messages);
  g_async_queue_unref(this->recycle);

  g_hash_table_unref(this->memory_consumptions);
}

void
mprtp_logger_init (MPRTPLogger * this)
{
  this->enabled    = FALSE;
  this->sysclock   = gst_system_clock_obtain ();
  this->made       = _now(this);
  strcpy(this->path, "logs/");
  this->messages   = g_async_queue_new();
  this->recycle   = g_async_queue_new();

  this->memory_consumptions = g_hash_table_new(g_str_hash, g_str_equal);

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
//      g_slice_free(StatusMessage, status_msg);
      throw_message(msg);
    }
    break;
    case MPRTP_LOGGER_MESSAGE_TYPE_WRITING:
    {
      _writing(this, (WritingMessage*) msg);
//      g_slice_free(WritingMessage, (WritingMessage*) msg);
      throw_message(msg);
    }
    break;
    case MPRTP_LOGGER_MESSAGE_TYPE_CHANGE_TARGET_DIRECTORY:
    {
      TargetDirectoryMessage* target_msg = (TargetDirectoryMessage*)msg;
      strcpy(this->path, target_msg->filename);
//      g_slice_free(TargetDirectoryMessage, target_msg);
      throw_message(msg);
    }
    break;
    case MPRTP_LOGGER_MESSAGE_TYPE_ADD_MEMORY_ALLOCATION:
    {
      MemoryConsumptionMessage* consumption_msg = (MemoryConsumptionMessage*)msg;
      MemoryConsumptionMessage* actual;
      if(!g_hash_table_contains(this->memory_consumptions, consumption_msg->type_name)){
        g_hash_table_insert(this->memory_consumptions, consumption_msg->type_name, consumption_msg);
        break;
      }
      actual = g_hash_table_lookup(this->memory_consumptions, consumption_msg->type_name);
      actual->size += consumption_msg->size;
//      g_slice_free(MemoryConsumptionMessage, consumption_msg);
      throw_message(msg);
    }
    break;
    case MPRTP_LOGGER_MESSAGE_TYPE_REM_MEMORY_ALLOCATION:
    {
      MemoryConsumptionMessage* consumption_msg = (MemoryConsumptionMessage*)msg;
      MemoryConsumptionMessage* actual;
      if(!g_hash_table_contains(this->memory_consumptions, consumption_msg->type_name)){
        break;
      }
      actual = g_hash_table_lookup(this->memory_consumptions, consumption_msg->type_name);
      actual->size -= consumption_msg->size;
//      g_slice_free(MemoryConsumptionMessage, consumption_msg);
      throw_message(msg);
    }
    break;
    case MPRTP_LOGGER_MESSAGE_TYPE_PRINT_MEMORY_ALLOCATIONS:
      g_hash_table_foreach(this->memory_consumptions, _print_memory_allocation, NULL);
      throw_message(msg);
      break;
    default:
      g_warning("Unhandled message with type %d", msg->type);
    break;
  }
  goto again;
done:
  return;
}

void mprtp_logger_add_memory_consumption(gchar *type_name, gsize size)
{
  MemoryConsumptionMessage *msg;
//  msg = g_slice_new0(MemoryConsumptionMessage);
  msg = alloc_message(MemoryConsumptionMessage);
  msg->base.type = MPRTP_LOGGER_MESSAGE_TYPE_ADD_MEMORY_ALLOCATION;
  msg->size = size;
  strcpy(msg->type_name, type_name);
  g_async_queue_push(this->messages, msg);
}

void mprtp_logger_rem_memory_consumption(gchar *type_name, gsize size)
{
  MemoryConsumptionMessage *msg;
//  msg = g_slice_new0(MemoryConsumptionMessage);
  msg = alloc_message(MemoryConsumptionMessage);
  msg->base.type = MPRTP_LOGGER_MESSAGE_TYPE_REM_MEMORY_ALLOCATION;
  msg->size = size;
  strcpy(msg->type_name, type_name);
  g_async_queue_push(this->messages, msg);
}

void mprtp_logger_print_memory_consumption(void)
{
  Message *msg;
//  msg = g_slice_new0(Message);
  msg = alloc_message(Message);
  msg->type = MPRTP_LOGGER_MESSAGE_TYPE_PRINT_MEMORY_ALLOCATIONS;
  g_async_queue_push(this->messages, msg);
}

void mprtp_logger_set_state(gboolean enabled)
{
  StatusMessage *msg;
//  msg = g_slice_new0(StatusMessage);
  msg = alloc_message(StatusMessage);
  msg->base.type = MPRTP_LOGGER_MESSAGE_TYPE_STATE_CHANGING;
  msg->enabled = enabled;

  g_async_queue_push(this->messages, msg);
}

void mprtp_logger_set_target_directory(const gchar *path)
{
  TargetDirectoryMessage *msg;
//  msg = g_slice_new0(TargetDirectoryMessage);
  msg = alloc_message(TargetDirectoryMessage);
  msg->base.type = MPRTP_LOGGER_MESSAGE_TYPE_CHANGE_TARGET_DIRECTORY;

  strcpy(msg->filename, path);
  g_async_queue_push(this->messages, msg);
}

void mprtp_logger(const gchar *filename, const gchar * format, ...)
{
  WritingMessage *msg;
  va_list args;
//  msg = g_slice_new0(WritingMessage);
  msg = alloc_message(WritingMessage);
  msg->base.type = MPRTP_LOGGER_MESSAGE_TYPE_WRITING;
  va_start (args, format);
  vsprintf(msg->string, format, args);
  va_end (args);

  strcpy(msg->filename, filename);
  g_async_queue_push(this->messages, msg);
}

void mprtp_log_one(const gchar *filename, const gchar * format, ...)
{
  WritingMessage *item;
  va_list args;
//  item = g_slice_new0(WritingMessage);
  item = alloc_message(WritingMessage);
  item->base.type = MPRTP_LOGGER_MESSAGE_TYPE_WRITING;

  va_start (args, format);
  vsprintf(item->string, format, args);
  va_end (args);

  item->overwrite = TRUE;

  strcpy(item->filename, filename);
  g_async_queue_push(this->messages, item);
}


MessageBlock* _messageblock_ctor(void)
{
  MessageBlock *result;
  result = g_async_queue_try_pop(this->recycle);
  if(!result){
    result = g_slice_new0(MessageBlock);
  }else{
    memset(result, 0, sizeof(MessageBlock));
  }
  return result;
}

void _throw_messageblock(MessageBlock* messageblock)
{
  g_async_queue_push(this->recycle, messageblock);
}

void _messageblock_dtor(gpointer mem)
{
  g_slice_free(MessageBlock, (MessageBlock*)mem);
}

void _writing(MPRTPLogger* this, WritingMessage *item)
{
  FILE *fp;
  gchar path[255];

  if(!this->enabled){
    return;
  }
  memset(path, 0, 255);
  strcpy(path, this->path);
  strcpy(path, item->filename);
  fp = fopen(path, item->overwrite ? "w" : "a+");
  fprintf (fp, "%s", item->string);
  fclose(fp);
}

void _print_memory_allocation(gpointer key, gpointer value, gpointer udata)
{
  MemoryConsumptionMessage* msg = value;
  g_print("%30s: %lu\n", msg->type_name, msg->size);
}


#undef MAX_RIPORT_INTERVAL
#undef THIS_LOCK
#undef THIS_UNLOCK
