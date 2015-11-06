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
#include <math.h>
#include <gst/gst.h>


GST_DEBUG_CATEGORY_STATIC (smanctrler_debug_category);
#define GST_CAT_DEFAULT smanctrler_debug_category

G_DEFINE_TYPE (SndManualController, smanctrler, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void smanctrler_finalize (GObject * object);
static void smanctrler_mprtcp_receiver (gpointer this, GstBuffer * buf);
static void smanctrler_riport_can_flow (gpointer this);
static void smanctrler_add_path (gpointer controller_ptr, guint8 subflow_id,
    MPRTPSPath * path);
static void smanctrler_rem_path (gpointer controller_ptr, guint8 subflow_id);
static void smanctrler_pacing (gpointer controller_ptr, gboolean allowed);
static gboolean smanctrler_is_pacing (gpointer controller_ptr);
static GstStructure* smanctrler_state(gpointer controller_ptr);
//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
smanctrler_class_init (SndManualControllerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = smanctrler_finalize;

  GST_DEBUG_CATEGORY_INIT (smanctrler_debug_category, "smanctrler", 0,
      "MpRTP Manual Sending Controller");
}

void
smanctrler_finalize (GObject * object)
{

}

void
smanctrler_init (SndManualController * this)
{

}


void
smanctrler_add_path (gpointer controller_ptr,
    guint8 subflow_id, MPRTPSPath * path)
{
  SndManualController *this;
  this = SMANCTRLER (controller_ptr);
  GST_DEBUG_OBJECT (this, "Sending Manual Controller add path is called"
      "for joining subflow %d for path %p", subflow_id, path);
  stream_splitter_add_path (this->splitter, subflow_id, path, 1000);
}

void
smanctrler_rem_path (gpointer controller_ptr, guint8 subflow_id)
{
  SndManualController *this;
  this = SMANCTRLER (controller_ptr);
  GST_DEBUG_OBJECT (this, "Sending Manual Controller add path is called "
      "for detaching subflow %d", subflow_id);
  stream_splitter_rem_path (this->splitter, subflow_id);
}

void
smanctrler_pacing (gpointer controller_ptr, gboolean allowed)
{
  SndManualController *this;
  this = SMANCTRLER (controller_ptr);
  GST_DEBUG_OBJECT (this, "Sending Manual Controller pacing is%s allowed ",
      allowed ? " " : " NOT");
}

gboolean
smanctrler_is_pacing (gpointer controller_ptr)
{
  SndManualController *this;
  this = SMANCTRLER (controller_ptr);
  GST_DEBUG_OBJECT (this, "Sending Manual Controller is pacing");
  return FALSE;
}

GstStructure* smanctrler_state(gpointer controller_ptr)
{
  SndManualController *this;
  this = SMANCTRLER (controller_ptr);
  GST_DEBUG_OBJECT (this, "Sending Manual Controller state");
  return NULL;
}

void
smanctrler_setup (SndManualController * this, StreamSplitter * splitter)
{
  this->splitter = splitter;
}

void
smanctrler_set_callbacks (void (**riport_can_flow_indicator) (gpointer),
    void (**controller_add_path) (gpointer, guint8, MPRTPSPath *),
    void (**controller_rem_path) (gpointer, guint8),
    void (**controller_pacing) (gpointer, gboolean),
    gboolean (**controller_is_pacing)(gpointer),
    GstStructure* (**controller_state)(gpointer))
{
  if (riport_can_flow_indicator) {
    *riport_can_flow_indicator = smanctrler_riport_can_flow;
  }
  if (controller_add_path) {
    *controller_add_path = smanctrler_add_path;
  }
  if (controller_rem_path) {
    *controller_rem_path = smanctrler_rem_path;
  }
  if (controller_pacing) {
    *controller_pacing = smanctrler_pacing;
  }
  if(controller_is_pacing){
    *controller_is_pacing = smanctrler_is_pacing;
  }
  if(controller_state){
    *controller_state = smanctrler_state;
  }
}

GstBufferReceiverFunc
smanctrler_setup_mprtcp_exchange (SndManualController * this,
    gpointer mprtcp_send_func_data, GstBufferReceiverFunc mprtcp_send_func)
{
  GST_DEBUG_OBJECT (this, "An mprtcp exchange function is called. "
      "Not used variables are: %p %p", mprtcp_send_func, mprtcp_send_func_data);
  return (GstBufferReceiverFunc) smanctrler_mprtcp_receiver;
}

void
smanctrler_mprtcp_receiver (gpointer this, GstBuffer * buf)
{
  GST_DEBUG_OBJECT (SMANCTRLER (this),
      "A riport is received by receiver, which doesn't do anything with it. Buffer: %p",
      buf);
}

void
smanctrler_riport_can_flow (gpointer this)
{
  GST_DEBUG_OBJECT (SMANCTRLER (this), "RTCP riport can now flowable");
}
