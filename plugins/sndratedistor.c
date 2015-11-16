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
//#include "mprtpspath.h"
#include <math.h>
#include <gst/gst.h>
#include <stdlib.h>
#include "streamtracker.h"


GST_DEBUG_CATEGORY_STATIC (sndrate_distor_debug_category);
#define GST_CAT_DEFAULT sndrate_distor_debug_category

G_DEFINE_TYPE (SendingRateDistributor, sndrate_distor, G_TYPE_OBJECT);

typedef struct _Subflow Subflow;

struct _Subflow{
  guint8          id;
  StreamTracker*  goodputs;
  MPRTPSPath*     path;
  StreamTracker*  rates;
  guint32         extra_bytes;
  guint32         monitored_bytes;


};

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
sndrate_distor_finalize (GObject * object);
static Subflow*
_make_subflow(guint8 subflow_id, MPRTPSPath *path);
static void
_ruin_subflow(Subflow *this);
static gint
_cmp_for_max (guint64 x, guint64 y);
static gint
_cmp_for_min (guint64 x, guint64 y);
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
  while(!g_queue_is_empty(this->free_ids)){
    g_free(g_queue_pop_head(this->free_ids));
  }
}

void
sndrate_distor_init (SendingRateDistributor * this)
{
  gint i,j;
  this->sysclock = gst_system_clock_obtain();
  this->free_ids = g_queue_new();
  this->counter = 0;
  this->subflows = g_hash_table_new_full (NULL, NULL,
      NULL, (GDestroyNotify) _ruin_subflow);
  for(i=0; i<SNDRATEDISTOR_MAX_NUM; ++i){
    for(j=0; j<SNDRATEDISTOR_MAX_NUM; ++j){
      this->SCM[i][j] = i == j?1:0;
    }
    this->distorted[i] = 0;
  }
}

SendingRateDistributor *make_sndrate_distor(void)
{
  SendingRateDistributor *result;
  result = g_object_new (SNDRATEDISTOR_TYPE, NULL);
  return result;
}

guint8 sndrate_distor_request_id(SendingRateDistributor *this, MPRTPSPath *path)
{
  guint8 result = 0;
  guint8* id;
  Subflow *subflow;
  if(this->counter > SNDRATEDISTOR_MAX_NUM){
    g_error("TOO MANY SUBFLOW IN CONTROLLER. ERROR!");
    goto exit;
  }
  if(g_queue_is_empty(this->free_ids)){
    result = this->max_id;
    ++this->max_id;
    goto done;
  }
  id = g_queue_pop_head(this->free_ids);
  result = *id;
  g_free(id);
done:
  subflow = _make_subflow(result, path);
  g_hash_table_insert (this->subflows, GINT_TO_POINTER (result), subflow);
  ++this->counter;
exit:
  return result;
}

void sndrate_distor_remove_id(SendingRateDistributor *this, guint8 id)
{
  guint8 i;
  guint8 *free_id;
  Subflow *subflow;
  free_id = g_malloc(sizeof(guint8));
  *free_id = id;
  for(i=0; i<SNDRATEDISTOR_MAX_NUM; ++i){
    this->SCM[id][i] = this->SCM[i][id] = 0;
  }
  subflow = (Subflow *) g_hash_table_lookup (this->subflows, GINT_TO_POINTER (id));
  this->distorted[id] = 0;
  g_queue_push_tail(this->free_ids, free_id);
  g_hash_table_remove (this->subflows, GINT_TO_POINTER (id));
}

void sndrate_distor_measurement_update(SendingRateDistributor *this,
                                       guint8 id,
                                       gfloat goodput)
{
  Subflow *subflow;
  subflow = (Subflow *) g_hash_table_lookup (this->subflows, GINT_TO_POINTER (id));
  if(!subflow) goto done;
  streamtracker_add(subflow->goodputs, goodput);
done:
  return;
}

void sndrate_distor_undershoot(SendingRateDistributor *this, guint8 id)
{

}


void sndrate_distor_bounce_back(SendingRateDistributor *this, guint8 id)
{

}


void sndrate_distor_keep(SendingRateDistributor *this, guint8 id)
{

}

void sndrate_distor_time_update(SendingRateDistributor *this)
{

}

guint32 sndrate_distor_get_rate(SendingRateDistributor *this, guint8 id)
{
  return 100;
}

Subflow* _make_subflow(guint8 subflow_id, MPRTPSPath *path)
{
  Subflow* this;
  this = g_malloc0(sizeof(Subflow));
  this->id = subflow_id;
  this->path = g_object_ref(path);
  this->goodputs = make_streamtracker(_cmp_for_min, _cmp_for_max, 32, 50);
  streamtracker_set_treshold(this->goodputs, 600 * GST_SECOND);
  this->rates = make_streamtracker(_cmp_for_min, _cmp_for_max, 32, 50);
  streamtracker_set_treshold(this->rates, 600 * GST_SECOND);
  return this;
}

void _ruin_subflow(Subflow *this)
{
  g_object_unref(this->goodputs);
  g_object_unref(this->rates);
  g_object_unref(this->path);
}

gint
_cmp_for_max (guint64 x, guint64 y)
{
  return x == y ? 0 : x < y ? -1 : 1;
}

gint
_cmp_for_min (guint64 x, guint64 y)
{
  return x == y ? 0 : x < y ? 1 : -1;
}
//
//void f();
//{
//  GHashTableIter iter;
//  g_hash_table_iter_init (&iter, this->subflows);
//    while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
//    {
//
//    }
//}
