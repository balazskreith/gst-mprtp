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
#include "packetholder.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>
#include "bintree.h"

//#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
//#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
//#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
//#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define THIS_READLOCK(this)
#define THIS_READUNLOCK(this)
#define THIS_WRITELOCK(this)
#define THIS_WRITEUNLOCK(this)

GST_DEBUG_CATEGORY_STATIC (packetholder_debug_category);
#define GST_CAT_DEFAULT packetholder_debug_category

G_DEFINE_TYPE (PacketHolder, packetholder, G_TYPE_OBJECT);



//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void packetholder_finalize (GObject * object);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
packetholder_class_init (PacketHolderClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = packetholder_finalize;

  GST_DEBUG_CATEGORY_INIT (packetholder_debug_category, "packetholder", 0,
      "MpRTP Manual Sending Controller");

}

void
packetholder_finalize (GObject * object)
{
  PacketHolder *this;
  this = PACKETHOLDER(object);
  g_object_unref(this->sysclock);
}


void
packetholder_init (PacketHolder * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sysclock = gst_system_clock_obtain();
}


void packetholder_reset(PacketHolder *this)
{
  THIS_WRITELOCK(this);
  THIS_WRITEUNLOCK(this);
}

PacketHolder *make_packetholder(void)
{
  PacketHolder *result;
  result = g_object_new (PACKETHOLDER_TYPE, NULL);
  return result;
}

void packetholder_push(PacketHolder *this,
                         GstBuffer *buf)
{
  THIS_WRITELOCK(this);
  if(this->content){
    gst_buffer_unref(this->content);
  }
  this->content = gst_buffer_copy_deep(buf);
  THIS_WRITEUNLOCK(this);
}

GstBuffer * packetholder_pop(PacketHolder *this)
{
  GstBuffer * result = NULL;
  THIS_WRITELOCK(this);
  if(this->content){
    result = this->content;
    this->content = NULL;
  }else{

  }
  THIS_WRITEUNLOCK(this);
  return result;
}



#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
