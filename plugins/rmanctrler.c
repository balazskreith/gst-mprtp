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
#include "smanctrler.h"
#include "rmanctrler.h"
#include "streamjoiner.h"
#include <math.h>


GST_DEBUG_CATEGORY_STATIC (rmanctrler_debug_category);
#define GST_CAT_DEFAULT rmanctrler_debug_category

G_DEFINE_TYPE (RcvManualController, rmanctrler, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void rmanctrler_finalize (GObject * object);
static void rmanctrler_mprtcp_receiver (gpointer this, GstBuffer * buf);
static void rmanctrler_riport_can_flow (gpointer this);
static void rmanctrler_add_path (gpointer controller_ptr, guint8 subflow_id,
    MpRTPRPath * path);
static void rmanctrler_rem_path (gpointer controller_ptr, guint8 subflow_id);
//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
rmanctrler_class_init (RcvManualControllerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = rmanctrler_finalize;

  GST_DEBUG_CATEGORY_INIT (rmanctrler_debug_category, "rmanctrler", 0,
      "MpRTP Manual Sending Controller");
}

void
rmanctrler_finalize (GObject * object)
{

}

void
rmanctrler_init (RcvManualController * this)
{

}

static void
rmanctrler_add_path (gpointer controller_ptr,
    guint8 subflow_id, MpRTPRPath * path)
{
  RcvManualController *this;
  this = RMANCTRLER (controller_ptr);
  GST_DEBUG_OBJECT (this, "Receive Manual Controller add path is called"
      "for joining subflow %d for path %p", subflow_id, path);
}

static void
rmanctrler_rem_path (gpointer controller_ptr, guint8 subflow_id)
{
  RcvManualController *this;
  this = RMANCTRLER (controller_ptr);
  GST_DEBUG_OBJECT (this, "Receive Manual Controller add path is called"
      "for detaching subflow %d", subflow_id);
}

void
rmanctrler_set_callbacks (void (**riport_can_flow_indicator) (gpointer),
    void (**controller_add_path) (gpointer, guint8, MpRTPRPath *),
    void (**controller_rem_path) (gpointer, guint8))
{
  if (riport_can_flow_indicator) {
    *riport_can_flow_indicator = rmanctrler_riport_can_flow;
  }
  if (controller_add_path) {
    *controller_add_path = rmanctrler_add_path;
  }
  if (controller_rem_path) {
    *controller_rem_path = rmanctrler_rem_path;
  }
}

GstBufferReceiverFunc
rmanctrler_setup_mprtcp_exchange (RcvManualController * this,
    gpointer mprtcp_send_func_data, GstBufferReceiverFunc mprtcp_send_func)
{
  GST_DEBUG_OBJECT (this, "An mprtcp exchange function is called. "
      "Not used variables are: %p %p", mprtcp_send_func, mprtcp_send_func_data);
  return (GstBufferReceiverFunc) rmanctrler_mprtcp_receiver;
}

void
rmanctrler_mprtcp_receiver (gpointer this, GstBuffer * buf)
{
  GST_DEBUG_OBJECT (SMANCTRLER (this),
      "A riport is received by receiver, which doesn't do anything with it. Buffer: %p",
      buf);
}

void
rmanctrler_setup (gpointer ptr, StreamJoiner * joiner)
{
  RcvManualController *this;
  this = RMANCTRLER (ptr);
  this->joiner = joiner;
}


void
rmanctrler_riport_can_flow (gpointer this)
{
  GST_DEBUG_OBJECT (RMANCTRLER (this), "RTCP riport can now flowable");
}
