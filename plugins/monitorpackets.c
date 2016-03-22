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
#include "monitorpackets.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>
#include "bintree.h"

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define _content(this) this->contents[this->contents_actual]

//#define THIS_READLOCK(this)
//#define THIS_READUNLOCK(this)
//#define THIS_WRITELOCK(this)
//#define THIS_WRITEUNLOCK(this)

GST_DEBUG_CATEGORY_STATIC (monitorpackets_debug_category);
#define GST_CAT_DEFAULT monitorpackets_debug_category

G_DEFINE_TYPE (MonitorPackets, monitorpackets, G_TYPE_OBJECT);



//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void monitorpackets_finalize (GObject * object);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
monitorpackets_class_init (MonitorPacketsClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = monitorpackets_finalize;

  GST_DEBUG_CATEGORY_INIT (monitorpackets_debug_category, "monitorpackets", 0,
      "MpRTP Manual Sending Controller");

}

void
monitorpackets_finalize (GObject * object)
{
  MonitorPackets *this;
  this = MONITORPACKETS(object);
  g_object_unref(this->sysclock);
}


void
monitorpackets_init (MonitorPackets * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sysclock          = gst_system_clock_obtain();
  this->queue             = g_queue_new();
  this->protected_packets_num = 0;
  this->max_protected_packets_num = 128;
  monitorpackets_reset(this);
}


void monitorpackets_reset(MonitorPackets *this)
{
  THIS_WRITELOCK(this);
  while(g_queue_is_empty(this->queue) == FALSE){
      gst_buffer_unref(g_queue_pop_tail(this->queue));
  }
  THIS_WRITEUNLOCK(this);
}

void monitorpackets_set_fec_payload_type(MonitorPackets *this, guint8 payload_type)
{
  THIS_WRITELOCK(this);
  this->fec_payload_type = payload_type;
  THIS_WRITEUNLOCK(this);
}

MonitorPackets *make_monitorpackets(void)
{
  MonitorPackets *result;
  result = g_object_new (MONITORPACKETS_TYPE, NULL);
  return result;
}

void monitorpackets_add_outgoing_rtp_packet(MonitorPackets *this,
                         GstBuffer *buf)
{

}

void monitorpackets_add_incoming_rtp_packet(MonitorPackets *this, GstBuffer *buf)
{
  THIS_WRITELOCK(this);

  THIS_WRITEUNLOCK(this);
}

GstBuffer *monitorpackets_process_FEC_packet(MonitorPackets *this, GstBuffer *buf)
{
  GstBuffer *result = NULL;
  THIS_WRITELOCK(this);

  //add fec packet to a list;
  gst_buffer_unref(buf);

  THIS_WRITEUNLOCK(this);
  return result;
}

GstBuffer * monitorpackets_provide_FEC_packet(MonitorPackets *this,
                                              guint8 mprtp_ext_header_id,
                                              guint8 subflow_id)
{
  GstBuffer*                   result = NULL;
  return result;
}





#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
