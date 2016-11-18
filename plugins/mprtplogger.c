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
  MPRTP_LOGGER_MESSAGE_TYPE_SYSTEM_COMMAND           = 7,
}MessageTypes;

//NOTE: This must be big enough to holds any kind of Message,
//since this type is going to be created and recycled


typedef struct{
  MessageTypes type;
  gchar        content[2048];
}Message;

typedef struct{
  MessageTypes type;
  gboolean     enabled;
}StatusMessage;

typedef struct{
  MessageTypes type;
  gchar        type_name[255];
  gsize        size;
}MemoryConsumptionMessage;

typedef struct{
  MessageTypes  type;
  loggerfnc     logger;
  loggerfnc_obj logger_obj;
  gpointer      udata;
}PrintMemoryConsumptionMessage;

typedef struct{
  MessageTypes type;
  gchar        string[1024];
  gchar        filename[255];
  gboolean     overwrite;
}WritingMessage;

typedef struct{
  MessageTypes       type;
  gchar              filename[255];
}TargetDirectoryMessage;

typedef struct{
  MessageTypes       type;
  gchar              command[255];
}SystemCommandMessage;

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


static gboolean _memory_consumption_item_dtor(gpointer key, Message* msg, Messenger* messenger)
{
  messenger_throw_block(messenger, msg);
  return TRUE;
}

void
mprtp_logger_finalize (GObject * object)
{
  MPRTPLogger *this = MPRTPLOGGER (object);

  g_object_unref (this->sysclock);
  gst_task_stop (this->process);

  g_hash_table_foreach_remove(this->memory_consumptions, (GHRFunc) _memory_consumption_item_dtor, this->messenger);
  g_hash_table_unref(this->memory_consumptions);
  g_object_unref (this->messenger);

}

void
mprtp_logger_init (MPRTPLogger * this)
{
  this->enabled    = FALSE;
  this->sysclock   = gst_system_clock_obtain ();
  this->made       = _now(this);
  this->messenger  = make_messenger(sizeof(Message));
  memset(this->path, 0, 255);

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

  msg = (Message*) messenger_pop_block(this->messenger);

  if(!msg){
    goto exit;
  }

  if(!this->enabled && msg->type != MPRTP_LOGGER_MESSAGE_TYPE_STATE_CHANGING){
    goto done;
  }

  switch(msg->type){
    case MPRTP_LOGGER_MESSAGE_TYPE_STATE_CHANGING:
    {
      StatusMessage* status_msg = (StatusMessage*)msg;
      this->enabled = status_msg->enabled;
    }
    break;
    case MPRTP_LOGGER_MESSAGE_TYPE_WRITING:
    {
      _writing(this, (WritingMessage*) msg);
    }
    break;
    case MPRTP_LOGGER_MESSAGE_TYPE_CHANGE_TARGET_DIRECTORY:
    {
      TargetDirectoryMessage* target_msg = (TargetDirectoryMessage*)msg;
      strcpy(this->path, target_msg->filename);
    }
    break;
    case MPRTP_LOGGER_MESSAGE_TYPE_ADD_MEMORY_ALLOCATION:
    {
      MemoryConsumptionMessage* consumption_msg = (MemoryConsumptionMessage*)msg;
      MemoryConsumptionMessage* actual;
      if(!g_hash_table_contains(this->memory_consumptions, consumption_msg->type_name)){
        actual = messenger_retrieve_block(this->messenger);
        memcpy(actual, consumption_msg, sizeof(MemoryConsumptionMessage));
        g_hash_table_insert(this->memory_consumptions, actual->type_name, actual);
        break;
      }
      actual = g_hash_table_lookup(this->memory_consumptions, consumption_msg->type_name);
      actual->size += consumption_msg->size;
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
    }
    break;
    case MPRTP_LOGGER_MESSAGE_TYPE_PRINT_MEMORY_ALLOCATIONS:
    {
      PrintMemoryConsumptionMessage* casted_msg = (PrintMemoryConsumptionMessage*)msg;
      g_hash_table_foreach(this->memory_consumptions, _print_memory_allocation, casted_msg);
    }
    break;
    case MPRTP_LOGGER_MESSAGE_TYPE_SYSTEM_COMMAND:
    {
      SystemCommandMessage* casted_msg = (SystemCommandMessage*) msg;
      g_print("Command: %s returns %d", casted_msg->command, system(casted_msg->command));
    }
    break;
    default:
      g_warning("Unhandled message with type %d", msg->type);
    break;
  }
done:
  messenger_throw_block(this->messenger, msg);
exit:
  return;
}

gpointer mprtp_slice_alloc(const gchar* type_name, gsize size)
{
  mprtp_logger_add_memory_consumption(type_name, size);
  return g_slice_alloc(size);
}


void mprtp_slice_dealloc(const gchar* type_name, gsize size, gpointer memptr)
{
  mprtp_logger_rem_memory_consumption(type_name, size);
  g_slice_free1(size, memptr);
}

void mprtp_logger_add_memory_consumption(const gchar *type_name, gsize size)
{
  MemoryConsumptionMessage *msg;
  messenger_lock(this->messenger);
  msg = messenger_retrieve_block_unlocked(this->messenger);

  msg->type = MPRTP_LOGGER_MESSAGE_TYPE_ADD_MEMORY_ALLOCATION;
  msg->size = size;
  strcpy(msg->type_name, type_name);

  messenger_push_block_unlocked(this->messenger, msg);
  messenger_unlock(this->messenger);
}

void mprtp_logger_rem_memory_consumption(const gchar *type_name, gsize size)
{
  MemoryConsumptionMessage *msg;
  messenger_lock(this->messenger);
  msg = messenger_retrieve_block_unlocked(this->messenger);

  msg->type = MPRTP_LOGGER_MESSAGE_TYPE_REM_MEMORY_ALLOCATION;
  msg->size = size;
  strcpy(msg->type_name, type_name);

  messenger_push_block_unlocked(this->messenger, msg);
  messenger_unlock(this->messenger);
}

void mprtp_logger_print_memory_consumption(loggerfnc fnc)
{
  PrintMemoryConsumptionMessage *msg;
  messenger_lock(this->messenger);
  msg = messenger_retrieve_block_unlocked(this->messenger);

  msg->type   = MPRTP_LOGGER_MESSAGE_TYPE_PRINT_MEMORY_ALLOCATIONS;
  msg->logger = fnc;

  messenger_push_block_unlocked(this->messenger, msg);
  messenger_unlock(this->messenger);
}

void mprtp_logger_print_obj_memory_consumption(loggerfnc_obj fnc, gpointer udata)
{
  PrintMemoryConsumptionMessage *msg;
  messenger_lock(this->messenger);
  msg = messenger_retrieve_block_unlocked(this->messenger);

  msg->type       = MPRTP_LOGGER_MESSAGE_TYPE_PRINT_MEMORY_ALLOCATIONS;
  msg->logger_obj = fnc;
  msg->udata      = udata;

  messenger_push_block_unlocked(this->messenger, msg);
  messenger_unlock(this->messenger);
}

void mprtp_logger_set_state(gboolean enabled)
{
  StatusMessage *msg;
  messenger_lock(this->messenger);
  msg = messenger_retrieve_block_unlocked(this->messenger);

  msg->type = MPRTP_LOGGER_MESSAGE_TYPE_STATE_CHANGING;
  msg->enabled = enabled;

  messenger_push_block_unlocked(this->messenger, msg);
  messenger_unlock(this->messenger);
}

void mprtp_logger_set_target_directory(const gchar *path)
{
  TargetDirectoryMessage *msg;
  messenger_lock(this->messenger);
  msg = messenger_retrieve_block_unlocked(this->messenger);

  msg->type = MPRTP_LOGGER_MESSAGE_TYPE_CHANGE_TARGET_DIRECTORY;
  strcpy(msg->filename, path);

  messenger_push_block_unlocked(this->messenger, msg);
  messenger_unlock(this->messenger);
}

void mprtp_logger_set_system_command(const gchar *command)
{
  SystemCommandMessage *msg;
  messenger_lock(this->messenger);
  msg = messenger_retrieve_block_unlocked(this->messenger);

  msg->type = MPRTP_LOGGER_MESSAGE_TYPE_SYSTEM_COMMAND;
  strcpy(msg->command, command);

  messenger_push_block_unlocked(this->messenger, msg);
  messenger_unlock(this->messenger);
}

void mprtp_logger(const gchar *filename, const gchar * format, ...)
{
  WritingMessage *msg;
  va_list args;
  messenger_lock(this->messenger);
  msg = messenger_retrieve_block_unlocked(this->messenger);

  msg->type = MPRTP_LOGGER_MESSAGE_TYPE_WRITING;
  va_start (args, format);
  vsprintf(msg->string, format, args);
  va_end (args);

  strcpy(msg->filename, filename);
  messenger_push_block_unlocked(this->messenger, msg);
  messenger_unlock(this->messenger);
}

void mprtp_log_one(const gchar *filename, const gchar * format, ...)
{
  WritingMessage *msg;
  va_list args;
  messenger_lock(this->messenger);
  msg = messenger_retrieve_block_unlocked(this->messenger);

  msg->type = MPRTP_LOGGER_MESSAGE_TYPE_WRITING;

  va_start (args, format);
  vsprintf(msg->string, format, args);
  va_end (args);

  msg->overwrite = TRUE;

  strcpy(msg->filename, filename);

  messenger_push_block_unlocked(this->messenger, msg);
  messenger_unlock(this->messenger);
}


void _writing(MPRTPLogger* this, WritingMessage *item)
{
  FILE *fp;
  gchar path[255];

  memset(path, 0, 255);
  if(this->path){
    strcpy(path, this->path);
    strcat(path, item->filename);
  }else{
    strcpy(path, item->filename);
  }

  fp = fopen(path, item->overwrite ? "w" : "a+");
  fprintf (fp, "%s", item->string);
  fclose(fp);
}

void _print_memory_allocation(gpointer key, gpointer value, gpointer udata)
{
  PrintMemoryConsumptionMessage* casted_msg = udata;
  MemoryConsumptionMessage* msg = value;
  if(casted_msg->logger_obj){
    casted_msg->logger_obj(casted_msg->udata, "%-20s %-10lu\n", msg->type_name, msg->size);
  }else if(casted_msg->logger){
    casted_msg->logger("%-20s %-10lu\n", msg->type_name, msg->size);
  }else{
    g_print("%-20s %-10lu\n", msg->type_name, msg->size);
  }
}


