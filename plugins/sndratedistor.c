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
  gdouble         variance;
  MPRTPSPath*     path;
  StreamTracker*  sending_rates;
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
  this->media_rate = 0.;
  this->subflows = g_hash_table_new_full (NULL, NULL,
      NULL, (GDestroyNotify) _ruin_subflow);
  for(i=0; i<SNDRATEDISTOR_MAX_NUM; ++i){
    for(j=0; j<SNDRATEDISTOR_MAX_NUM; ++j){
      this->SCM[i][j] = i == j?1:0;
    }
    this->undershooted[i] = 0;
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
  if(!subflow) goto done;
  this->undershooted[id] = 0;
  g_queue_push_tail(this->free_ids, free_id);
  g_hash_table_remove (this->subflows, GINT_TO_POINTER (id));
done:
  return;
}

void sndrate_distor_measurement_update(SendingRateDistributor *this,
                                       guint8 id,
                                       gfloat goodput,
                                       gdouble variance)
{
  Subflow *subflow;
  subflow = (Subflow *) g_hash_table_lookup (this->subflows, GINT_TO_POINTER (id));
  if(!subflow) goto done;
  streamtracker_add(subflow->goodputs, goodput);
  subflow->variance = variance;
done:
  return;
}

void sndrate_distor_undershoot(SendingRateDistributor *this, guint8 id)
{
  gint64 actual_sending_rate;
  gint64 actual_goodput;
  gint64 desired_sending_rate;
  Subflow *subflow;
  if(this->undershooted[id]>0) goto done;
  this->undershooted[id] += 2;
  if(!subflow) goto done;
  subflow = (Subflow *) g_hash_table_lookup (this->subflows, GINT_TO_POINTER (id));
  actual_sending_rate = streamtracker_get_last(subflow->sending_rates);
  actual_goodput = streamtracker_get_last_minus_n(subflow->goodputs, 2);
  desired_sending_rate = actual_sending_rate - (actual_sending_rate-actual_goodput) * 1.+2*subflow->variance;
  if(desired_sending_rate < 0) desired_sending_rate = actual_goodput * .8;
  this->undershoot_delta[id] = actual_sending_rate - desired_sending_rate;
done:
  return;
}


void sndrate_distor_bounce_back(SendingRateDistributor *this, guint8 id)
{
  gint64 actual_sending_rate;
  gint64 measured_goodput;
  gint64 desired_sending_rate;
  Subflow *subflow;
  if(this->bounced_back[id]>0) goto done;
  this->bounced_back[id] += 2;
  if(!subflow) goto done;
  actual_sending_rate = streamtracker_get_last(subflow->sending_rates);
  measured_goodput = streamtracker_get_last_minus_n(subflow->goodputs, 2);
  desired_sending_rate = measured_goodput * .9;
  this->bounced_back_delta[id] = desired_sending_rate - actual_sending_rate;
done:
  return;
}


void sndrate_distor_keep(SendingRateDistributor *this, guint8 id)
{

}

void sndrate_distor_time_update(SendingRateDistributor *this, guint32 media_rate)
{
  this->media_rate = media_rate;
  guint8 C[SNDRATEDISTOR_MAX_NUM];
  guint32 SR[SNDRATEDISTOR_MAX_NUM];
  guint32 bb = 0, u = 0, eb = 0, SR_sum = 0;
  gint i;
  _setup_for_changes(this, C, SR, &bb,&u, &eb, &SR_sum);
  memset(this->delta_sending_rates, 0, sizeof(gint32)*SNDRATEDISTOR_MAX_NUM);
  //1. Subtracts and assign
  if(!bb) goto collect_and_redistribute;
  for(i=0; i<SNDRATEDISTOR_MAX_NUM; ++i){
    if(!C[i]) continue;
    this->delta_sending_rates[i] -= (gfloat)bb * (gfloat)(SR[i]) / (gfloat) SR_sum;
    this->extra_bytes[i] += -1*this->delta_sending_rates[i];
  }

  //2. Collect and redistribute
collect_and_redistribute:
  if(!u) goto keep_and_prosper;
  if(eb>0)
  for(i=0; i<SNDRATEDISTOR_MAX_NUM; ++i){
    if(u < 1) break;
    if(!C[i]) continue;
    if(this->extra_bytes[i]) continue;
    this->delta_sending_rates[i] += this->extra_bytes[i] / eb;
    this->extra_bytes[i] = 0;
    u-=this->extra_bytes;
  }
  if(u < 1) goto keep_and_prosper;
  for(i=0; i<SNDRATEDISTOR_MAX_NUM; ++i){
    if(!C[i]) continue;
    this->delta_sending_rates[i] += (gfloat)u * (gfloat)(SR[i]) / (gfloat) SR_sum;
  }

keep_and_prosper:

  return;
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
  this->sending_rates = make_streamtracker(_cmp_for_min, _cmp_for_max, 32, 50);
  streamtracker_set_treshold(this->sending_rates, 600 * GST_SECOND);
  return this;
}

void _setup_for_changes(SendingRateDistributor *this,
                       guint8  *vector,
                       guint32 *SR,
                       guint32 *undershooted_sum,
                       guint32 *bounced_back_sum,
                       guint32 *extra_bytes_sum,
                       guint32 *sending_rate_sum)
{
  GHashTableIter iter;
  gint           i;
  gpointer       key, val;
  Subflow*       subflow;
  memset(vector, 0, sizeof(guint8)*SNDRATEDISTOR_MAX_NUM);
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) &key, (gpointer) &val))
  {
    subflow = val;
    vector[i] = 1;
    if(undershooted_sum && this->undershoot_delta[subflow->id]){
      *undershooted_sum+=this->undershoot_delta[subflow->id];
    }
    if(bounced_back_sum && this->bounced_back_delta[subflow->id]){
      *bounced_back_sum+=this->bounced_back_delta[subflow->id];
    }
    if(extra_bytes_sum && this->extra_bytes[subflow->id]){
      *extra_bytes_sum+=this->extra_bytes[subflow->id];
    }

    if(0 == this->undershooted[subflow->id] &&
       0 == this->bounced_back[subflow->id])
    {
      continue;
    }

    if(this->undershooted[subflow->id]) --this->undershooted[subflow->id];
    if(this->bounced_back[subflow->id]) --this->bounced_back[subflow->id];

    for(i=0; i<SNDRATEDISTOR_MAX_NUM; ++i){
      if(!this->SCM[subflow->id][i]) continue;
      vector[i] = 0;
    }
  }

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) &key, (gpointer) &val))
  {
    subflow = val;
    if(!vector[subflow->id]) continue;
    SR[subflow->id] = streamtracker_get_last(subflow->sending_rates);
    if(sending_rate_sum){
      *sending_rate_sum+=SR[subflow->id];
    }
  }
}

void _ruin_subflow(Subflow *this)
{
  g_object_unref(this->goodputs);
  g_object_unref(this->sending_rates);
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
