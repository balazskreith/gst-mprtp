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
#include "sndratedistor.h"
#include <math.h>
#include <gst/gst.h>
#include <stdlib.h>
#include <string.h>
#include "streamsplitter.h"
#include "sndctrler.h"


GST_DEBUG_CATEGORY_STATIC (sndrate_distor_debug_category);
#define GST_CAT_DEFAULT sndrate_distor_debug_category

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define _now(this) gst_clock_get_time (this->sysclock)

G_DEFINE_TYPE (SendingRateDistributor, sndrate_distor, G_TYPE_OBJECT);


typedef struct _Subflow Subflow;

struct _Subflow{
  guint8                 id;
  MPRTPSPath*            path;

  gint32                 target_bitrate_t1;
  gint32                 target_bitrate;
  guint8                 flags;
};

//----------------------------------------------------------------------
//-------- Private functions belongs to the object ----------
//----------------------------------------------------------------------

static void
sndrate_distor_finalize (
    GObject * object);

static Subflow *_subflow_ctor (void);
static void _subflow_dtor (Subflow * this);
static void _ruin_subflow (gpointer subflow);
static Subflow *_make_subflow (guint8 id, MPRTPSPath * path);
static void _reset_subflow (Subflow * this);

//static Subflow* _get_subflow(SendingRateDistributor* this, gint subflow_id)
//{
//  Subflow* result;
//  result =
//        (Subflow *) g_hash_table_lookup (this->subflows,
//        GINT_TO_POINTER (subflow_id));
//  return result;
//}


//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
sndrate_distor_class_init (SendingRateDistributorClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = sndrate_distor_finalize;

  GST_DEBUG_CATEGORY_INIT (sndrate_distor_debug_category, "sndrate_distor", 0,
      "MpRTP Sending Rate Distributor");
}

void
sndrate_distor_finalize (GObject * object)
{
  SendingRateDistributor * this;
  this = SNDRATEDISTOR(object);
  g_object_unref(this->sysclock);
  mprtp_free(this->subflows);
}


void
sndrate_distor_init (SendingRateDistributor * this)
{
  this->sysclock = gst_system_clock_obtain();
  this->subflows = g_hash_table_new_full (NULL, NULL, NULL, _ruin_subflow);
  g_rw_lock_init (&this->rwmutex);

}


static void
_iterate_subflows (SendingRateDistributor * this, void(*process)(Subflow *,gpointer), gpointer data)
{
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    subflow = (Subflow *) val;
    process(subflow, data);
  }
}

SendingRateDistributor *make_sndrate_distor(StreamSplitter *splitter)
{
  SendingRateDistributor *result;
  result = g_object_new (SNDRATEDISTOR_TYPE, NULL);
  result->splitter = splitter;
  return result;
}


static void _refresh_subflows_helper(Subflow *subflow, gpointer data)
{
  SendingRateDistributor* this;
  guint8 flags;

  this = data;
  flags = mprtps_path_get_flags(subflow->path);
  subflow->target_bitrate_t1 = subflow->target_bitrate;
  subflow->target_bitrate = mprtps_path_get_target_bitrate(subflow->path);
  this->urgent_rescheduling |= subflow->flags ^ flags;
  this->urgent_rescheduling |= subflow->target_bitrate < subflow->target_bitrate_t1 * .9;
  this->urgent_rescheduling |= subflow->target_bitrate_t1 * 1.1 < subflow->target_bitrate;
  subflow->flags = flags;
  this->target_media_rate += subflow->target_bitrate;
}

static void _refresh_splitter_helper(Subflow *subflow, gpointer data)
{
  SendingRateDistributor* this;
  this = data;
  stream_splitter_setup_sending_target(this->splitter, subflow->id, subflow->target_bitrate);
}

gint32 sndrate_distor_refresh(SendingRateDistributor* this)
{
  gint32 result;
  THIS_WRITELOCK(this);

  if(this->last_subflow_refresh < _now(this) - 100 * GST_MSECOND){
    this->target_media_rate_t1 = this->target_media_rate;
    this->target_media_rate = 0;
    _iterate_subflows(this, _refresh_subflows_helper, this);
    this->urgent_rescheduling |= this->target_media_rate_t1 * 1.05 < this->target_media_rate;
    this->urgent_rescheduling |= this->target_media_rate < this->target_media_rate_t1 * .95;
    this->last_subflow_refresh = _now(this);
  }

  result = this->target_media_rate;
  if(_now(this) < this->next_splitter_refresh && !this->urgent_rescheduling){
    goto done;
  }
  this->urgent_rescheduling = FALSE;
  //Todo: consider and implement flow redistribution here.
  _iterate_subflows(this, _refresh_splitter_helper, this);
  stream_splitter_commit_changes(this->splitter);
  this->next_splitter_refresh = _now(this) + (g_random_double() + .5) * GST_SECOND;
done:
  THIS_WRITEUNLOCK(this);
  return result;
}

void sndrate_distor_add_subflow(SendingRateDistributor *this, MPRTPSPath *path)
{
  Subflow *subflow;
  subflow = _make_subflow(mprtps_path_get_id(path), path);
  g_hash_table_insert (this->subflows, GINT_TO_POINTER (subflow->id), subflow);
}

void sndrate_distor_rem_subflow(SendingRateDistributor *this, guint8 subflow_id)
{
  g_hash_table_remove (this->subflows, GINT_TO_POINTER (subflow_id));
}

Subflow *
_subflow_ctor (void)
{
  Subflow *result;
  result = mprtp_malloc (sizeof (Subflow));
  return result;
}

void
_subflow_dtor (Subflow * this)
{
  g_return_if_fail (this);
  mprtp_free (this);
}

void
_ruin_subflow (gpointer subflow)
{
  Subflow *this;
  g_return_if_fail (subflow);
  this = (Subflow *) subflow;
  g_object_unref (this->path);
  _subflow_dtor (this);
}

Subflow *
_make_subflow (guint8 id, MPRTPSPath * path)
{
  Subflow *result;

  result                  = _subflow_ctor ();
  result->path            = g_object_ref (path);
  result->id              = id;
  _reset_subflow (result);
  return result;
}


void
_reset_subflow (Subflow * this)
{

}

