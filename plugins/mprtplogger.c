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

#define DATABED_LENGTH 1400

GST_DEBUG_CATEGORY_STATIC (mprtp_logger_debug_category);
#define GST_CAT_DEFAULT mprtp_logger_debug_category

G_DEFINE_TYPE (MPRTPLogger, mprtp_logger, G_TYPE_OBJECT);

static MPRTPLogger *this = NULL;
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
mprtp_logger_finalize (GObject * object);


//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------


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
}

void
mprtp_logger_init (MPRTPLogger * this)
{
  g_rw_lock_init (&this->rwmutex);

  this->sysclock   = gst_system_clock_obtain ();
  this->touches    = g_hash_table_new(g_str_hash, g_str_equal);
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
  THIS_WRITELOCK(this);
  this->enabled = TRUE;
  THIS_WRITEUNLOCK(this);
}

void disable_mprtp_logger(void)
{
  THIS_WRITELOCK(this);
  this->enabled = FALSE;
  THIS_WRITEUNLOCK(this);
}

void mprtp_logger(const gchar *filename, const gchar * format, ...)
{
  FILE *file;
  va_list args;
  gchar writable[255];
  THIS_READLOCK(this);
  if(!this->enabled){
    goto done;
  }

  strcpy(writable, filename);
  if(!g_hash_table_lookup(this->touches, filename)){
    THIS_READUNLOCK(this);
    THIS_WRITELOCK(this);
    g_hash_table_insert(this->touches, writable, writable);
    THIS_WRITEUNLOCK(this);
    THIS_READLOCK(this);
    file = fopen(filename, "w");
  }else{
    file = fopen(filename, "a");
  }
  va_start (args, format);
  vfprintf (file, format, args);
  va_end (args);
  fclose(file);
done:
  THIS_READUNLOCK(this);
}

#undef MAX_RIPORT_INTERVAL
#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
