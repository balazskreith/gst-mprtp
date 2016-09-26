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
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "sndsubflows.h"


GST_DEBUG_CATEGORY_STATIC (sndsubflows_debug_category);
#define GST_CAT_DEFAULT sndsubflows_debug_category

G_DEFINE_TYPE (SndSubflows, sndsubflows, G_TYPE_OBJECT);

#define _now(this) gst_clock_get_time (this->sysclock)
typedef struct{
  void    (*callback)(gpointer udata, SndSubflow* subflow);
  gpointer  udata;
}Notifier;
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------
//Functions realted to sndsubflows
static void
sndsubflows_finalize (
    GObject * object);

void _add_notifier(
    GSList **list,
    void (*callback)(gpointer udata, SndSubflow* subflow),
    gpointer udata);

static Notifier*
_make_notifier(
    void (*callback)(gpointer udata, SndSubflow* subflow),
    gpointer udata);

static void
_dispose_notifier(
    gpointer data);

static void
_call_notifiers(
    GSList *notifiers,
    SndSubflow *subflow);

static void
_setup_rtp2mprtp (SndSubflow *subflow,
                  GstRTPBuffer *rtp);

static void
_setup_mprtpfec (SndSubflow *subflow,
                  GstRTPBuffer *rtp);

//----------------------------------------------------------------------
//---- Private function implementations to Stream Dealer object --------
//----------------------------------------------------------------------

void
sndsubflows_class_init (SndSubflowsClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = sndsubflows_finalize;

  GST_DEBUG_CATEGORY_INIT (sndsubflows_debug_category, "sndsubflows", 0,
      "Stream Splitter");

}

SndSubflows* make_sndsubflows(void)
{
  SndSubflows *this;
  this = g_object_new (SNDSUBFLOWS_TYPE, NULL);
  return this;
}

void
sndsubflows_finalize (GObject * object)
{
  SndSubflows *this = SNDSUBFLOWS (object);
  g_hash_table_destroy (this->subflows);
  g_object_unref (this->sysclock);
  g_free(this->subflows);
}


void
sndsubflows_init (SndSubflows * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->subflows               = g_malloc0(sizeof(SndSubflow) * 256);
  this->made                   = _now(this);
}

void sndsubflows_add_notifications(SndSubflows* this,void (*callback)(gpointer udata, SndSubflow* subflow), gpointer udata)
{
  _add_notifier(&this->added_notifications, callback, udata);
}

SndSubflow* sndsubflows_add(SndSubflows* this, guint8 id)
{
  SndSubflow *result;
  if(this->subflows[id] != NULL){
    GST_LOG_OBJECT(this, "The subflow is already exists");
    return this->subflows[id];
  }
  ++this->subflows_num;
  this->added = g_slist_prepend(this->added, this->subflows[id]);

  this->subflows[id] = result = g_malloc0(sizeof(SndSubflow));

  _call_notifiers(this->added_notifications, result);

  return result;

}

void sndsubflows_rem(SndSubflows* this, guint8 id)
{
  SndSubflow* subflow;
  if(this->subflows[id] == NULL){
    GST_LOG_OBJECT(this, "The subflow is not exists");
  }
  subflow = this->subflows[id];
  this->added = g_slist_remove(this->added, subflow);

  _call_notifiers(subflow->notifiers.on_removing);

  g_free(this->subflows[id]);
  this->subflows[id] = NULL;
  --this->subflows_num;
}

void sndsubflows_iterate(SndSubflows* this, GFunc process, gpointer udata)
{
  if(!this->added){
    return;
  }
  g_slist_foreach(this->added, process, udata);
}

void sndsubflows_set_target_rate(SndSubflows* this, SndSubflow* subflow, gint32 target_rate)
{
  this->target_rate -= subflow->target_bitrate;
  this->target_rate += subflow->target_bitrate = target_rate;
}

gint32 sndsubflows_get_total_target(SndSubflows* this)
{
  return this->target_rate;
}

gint32 sndsubflows_get_subflows_num(SndSubflows* this)
{
  return this->subflows_num;
}


SndSubflow* sndsubflows_get_subflow(SndSubflows* this, guint8 subflow_id)
{
  return this->subflows + subflow_id;
}

void sndsubflow_add_removal_notification(SndSubflow* subflow, void (*callback)(gpointer udata, SndSubflow* subflow), gpointer udata)
{
  _add_notifier(&subflow->notifiers.on_removing, callback, udata);
}

void sndsubflow_add_active_status_changed_notification(SndSubflow* subflow, void (*callback)(gpointer udata, SndSubflow* subflow), gpointer udata)
{
  _add_notifier(&subflow->notifiers.on_active_status_changed, callback, udata);
}

guint8 sndsubflow_get_flags_abs_value(SndSubflow* subflow)
{
  return (subflow->active ? 4 : 0) + (subflow->lossy ? 0 : 2) + (subflow->congested ? 0 : 1);
}

void sndsubflow_set_active_status(SndSubflow* subflow, gboolean active)
{
  subflow->active = active;
  _call_notifiers(subflow->notifiers.on_active_status_changed, subflow);
}



GstBuffer*
sndsubflow_process_rtp_buffer(SndSubflow *subflow,
                              GstBuffer* buffer,
                              gboolean *fec_request)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  buffer = gst_buffer_make_writable(buffer);
  gst_rtp_buffer_map(buffer, GST_MAP_READWRITE, &rtp);
  _setup_rtp2mprtp(subflow, &rtp);
  gst_rtp_buffer_unmap(&rtp);

  if(!fec_request || !subflow->fec_interval){
    goto done;
  }
  if(subflow->fec_interval <= ++subflow->packet_counter_for_fec){
    *fec_request = TRUE;
  }
done:
  return buffer;
}

GstBuffer*
sndsubflow_process_fec_buffer(SndSubflow *subflow,
                              GstBuffer* buffer)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  buffer = gst_buffer_make_writable(buffer);
  gst_rtp_buffer_map(buffer, GST_MAP_READWRITE, &rtp);
  _setup_mprtpfec(subflow, &rtp);
  gst_rtp_buffer_unmap(&rtp);
  return buffer;
}









void _add_notifier(GSList **list, void (*callback)(gpointer udata, SndSubflow* subflow), gpointer udata)
{
  *list = g_slist_prepend(*list, _make_notifier(callback, udata));
}

Notifier* _make_notifier(void (*callback)(gpointer udata, SndSubflow* subflow), gpointer udata)
{
  Notifier* result =  g_slice_new0(Notifier);
  result->udata = udata;
  result->callback = callback;
  return result;
}

void _dispose_notifier(gpointer data)
{
  if(!data){
    return;
  }
  Notifier* notifier = data;
  g_slice_free(Notifier, notifier);
}

static void _call_notifiers(GSList *notifiers, SndSubflow *subflow)
{
  GSList *it;
  if(!notifiers){
    return;
  }

  for(it = notifiers; it; it = it->next){
    Notifier* notifier = it->data;
    notifier->callback(notifier->udata, subflow);
  }
}



void
_setup_rtp2mprtp (SndSubflow *subflow,
                  GstRTPBuffer *rtp)
{
  MPRTPSubflowHeaderExtension data;
  data.id = subflow->id;
  if (++(subflow->rtp_seq) == 0) {
    ++(subflow->rtp_seq_cycle_num);
  }
  data.seq = subflow->rtp_seq;

  gst_rtp_buffer_add_extension_onebyte_header (rtp, subflow->mprtp_ext_header_id,
      (gpointer) & data, sizeof (data));

  return;
}


void
_setup_mprtpfec (SndSubflow *subflow,
                  GstRTPBuffer *rtp)
{
  MPRTPSubflowHeaderExtension data;
  data.id = subflow->id;
  if (++(subflow->fec_seq) == 0) {
    ++(subflow->fec_seq_cycle_num);
  }
  data.seq = subflow->fec_seq;

  gst_rtp_buffer_add_extension_onebyte_header (rtp, subflow->fec_ext_header_id,
      (gpointer) & data, sizeof (data));

  return;
}

