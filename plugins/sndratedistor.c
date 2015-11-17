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
#include <string.h>


GST_DEBUG_CATEGORY_STATIC (sndrate_distor_debug_category);
#define GST_CAT_DEFAULT sndrate_distor_debug_category

G_DEFINE_TYPE (SendingRateDistributor, sndrate_distor, G_TYPE_OBJECT);

typedef struct _Subflow Subflow;

struct _ChangeableVector{
  guint8      changeable[SNDRATEDISTOR_MAX_NUM];
  guint32     negative_delta_sr_sum;
  guint32     positive_delta_sr_sum;
  guint32     changeable_sr_sum;
  gdouble     log2_changeable_stability_sum;
  gboolean    all_unstable;
  gboolean    all_stable;
};

struct _Subflow{
  guint8          id;
  StreamTracker*  goodputs;
  gdouble         variance;
  MPRTPSPath*     path;
  StreamTracker*  sending_rates;
  gdouble         fallen_rate;
  guint64         fallen_bytes;
};

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void
sndrate_distor_finalize (
    GObject * object);

static void
_setup_changeable_vector(
    SendingRateDistributor *this);

static void
_deprive_and_charge(
    SendingRateDistributor *this);

static void
_discharge_and_divide(
    SendingRateDistributor *this);

static void
_restore_and_obtain(
    SendingRateDistributor *this);

static void
_assign_and_notify(
    SendingRateDistributor *this);

static void
_changeable_shortcut(
    SendingRateDistributor *this,
    ChangeableVector** changeable_vector,
    guint8**        changeable,
    guint32**       positive_delta_sr_sum,
    guint32**       negative_delta_sr_sum,
    guint32**       changeable_sr_sum,
    gint64**        delta_sending_rates,
    gdouble**       log2_changeable_stability_sum);

static Subflow*
_get_next_most_stable(
    SendingRateDistributor *this,
    Subflow *act);

static Subflow*
_make_subflow(
    guint8 subflow_id,
    MPRTPSPath *path);

static void
_ruin_subflow(
    Subflow *this);

static gint
_cmp_for_max (
    guint64 x,
    guint64 y);

static gint
_cmp_for_min (
    guint64 x,
    guint64 y);

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
  g_free(this->changeable_vector);
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
  this->changeable_vector = g_malloc0(sizeof(ChangeableVector));
  this->subflows = g_hash_table_new_full (NULL, NULL,
      NULL, (GDestroyNotify) _ruin_subflow);
  for(i=0; i<SNDRATEDISTOR_MAX_NUM; ++i){
    for(j=0; j<SNDRATEDISTOR_MAX_NUM; ++j){
      this->SCM[i][j] = i == j?1:0;
    }
  }
}

SendingRateDistributor *make_sndrate_distor(void)
{
  SendingRateDistributor *result;
  result = g_object_new (SNDRATEDISTOR_TYPE, NULL);
  return result;
}

guint8 sndrate_distor_request_id(SendingRateDistributor *this,
                                 MPRTPSPath *path,
                                 guint32 sending_rate)
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
    id = &result;
    ++this->max_id;
    goto done;
  }
  id = g_queue_pop_head(this->free_ids);
  result = *id;
  g_free(id);
done:
  this->sending_rates[*id] = sending_rate;
  this->stability[*id] = 0;
  this->monitoring_interval[*id] = 8;
  this->delta_sending_rates[*id] = 0;
  this->extra_bytes[*id] = 0;
  this->restoring[*id] = FALSE;
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
  this->sending_rates[id] = 0;
  this->stability[id] = 0;
  this->monitoring_interval[id] = 0;
  this->delta_sending_rates[id] = 0;
  this->extra_bytes[id] = 0;
  this->restoring[id] = FALSE;
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
  Subflow *subflow;
  subflow = (Subflow *) g_hash_table_lookup (this->subflows, GINT_TO_POINTER (id));
  if(!subflow) goto done;

  this->stability[id] = -2;
  this->monitoring_interval[id] = 0;
  actual_sending_rate = streamtracker_get_last(subflow->sending_rates);
  actual_goodput = streamtracker_get_last(subflow->goodputs);
  subflow->fallen_bytes = actual_sending_rate-actual_goodput;
  subflow->fallen_rate = (gdouble)actual_goodput / (gdouble) actual_sending_rate;
  this->delta_sending_rates[id] = actual_sending_rate;
  this->delta_sending_rates[id]-= subflow->fallen_bytes * (1.+2*subflow->variance);
  this->delta_sending_rates[id] *= -1;
  this->extra_bytes[id] = 0;
  this->restoring[id] = FALSE;
done:
  return;
}

void sndrate_distor_bounce_back(SendingRateDistributor *this, guint8 id)
{
  gint64 actual_sending_rate;
  gint64 obtained_goodput;
  Subflow *subflow;
  subflow = (Subflow *) g_hash_table_lookup (this->subflows, GINT_TO_POINTER (id));
  if(!subflow) goto done;

  this->stability[id] = -2;
  actual_sending_rate = streamtracker_get_last(subflow->sending_rates);
  obtained_goodput = streamtracker_get_last_minus_n(subflow->goodputs, 2);
  this->delta_sending_rates[id] = (obtained_goodput - actual_sending_rate) * .9;
  this->monitoring_interval[id] = (-1.*log2(subflow->fallen_rate));
  if(this->monitoring_interval[id] < 2) this->monitoring_interval[id] = 2;
  else if(this->monitoring_interval[id] > 14) this->monitoring_interval[id] = 14;
  this->extra_bytes[id] = subflow->fallen_bytes;
  subflow->fallen_bytes = 0;
  this->restoring[id] = TRUE;
done:
  return;
}

void sndrate_distor_keep(SendingRateDistributor *this, guint8 id)
{
  if(++this->stability[id] > 0) goto done;
  if(!this->restoring[id]) goto done;
  if(!this->stability[id]) this->restoring[id] = FALSE;
  this->delta_sending_rates[id] = this->extra_bytes[id]>>1;
  this->extra_bytes[id]>>=1;
  this->monitoring_interval[id] = MAX(this->monitoring_interval[id]+1,8);
done:
  this->stability[id] = MIN(this->stability[id],8);
  return;
}

void sndrate_distor_time_update(SendingRateDistributor *this, guint32 media_rate)
{
  this->media_rate = media_rate;
  _setup_changeable_vector(this);
//  if(this->changeable_vector->all_stable) goto
  _deprive_and_charge(this);
  _discharge_and_divide(this);
  _restore_and_obtain(this);
  _assign_and_notify(this);
  return;
}

guint32 sndrate_distor_get_rate(SendingRateDistributor *this, guint8 id)
{
  return this->sending_rates[id];
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


//Stability driven approach for flow redistribution
void _setup_changeable_vector(SendingRateDistributor *this)
{
  GHashTableIter iter;
  gpointer       key, val;
  Subflow*       subflow;
  guint8         unstable[SNDRATEDISTOR_MAX_NUM];
  ChangeableVector* changeable_vector;
  guint8*        changeable;
  guint32*       positive_delta_sr_sum;
  guint32*       negative_delta_sr_sum;
  guint32*       changeable_sr_sum;
  gint64*        delta_sending_rates;
  gdouble*       log2_changeable_stability_sum;

  _changeable_shortcut(this,
                       &changeable_vector,
                       &changeable,
                       &positive_delta_sr_sum,
                       &negative_delta_sr_sum,
                       &changeable_sr_sum,
                       &delta_sending_rates,
                       &log2_changeable_stability_sum);

  changeable_vector->all_unstable = changeable_vector->all_stable = TRUE;
  memset(changeable, 0, sizeof(ChangeableVector));
  memset(unstable, 0, sizeof(guint8)*SNDRATEDISTOR_MAX_NUM);
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) &key, (gpointer) &val))
  {
    subflow = val;
    if(this->stability[subflow->id] > 0) goto stable;
    else goto unstable;
  stable:
    changeable[subflow->id] = 1;
    changeable_vector->all_unstable = FALSE;
    continue;
  unstable:
    unstable[subflow->id] = 1;
    changeable_vector->all_stable = FALSE;
    continue;
  }

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) &key, (gpointer) &val))
  {
    guint i;
    subflow = val;
    if(unstable[subflow->id]) continue;
    for(i=0; i<SNDRATEDISTOR_MAX_NUM; ++i){
      if(this->SCM[subflow->id][i]) changeable[i] = 0;
    }
  }

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) &key, (gpointer) &val))
  {
    subflow = val;
    if(changeable[subflow->id]){
      *changeable_sr_sum+=this->sending_rates[subflow->id];
      *log2_changeable_stability_sum+=log2((gdouble)this->stability[subflow->id]);
    }
    else if(delta_sending_rates[subflow->id] < 0)
      *negative_delta_sr_sum+=-1*delta_sending_rates[subflow->id];
    else
      *positive_delta_sr_sum+=delta_sending_rates[subflow->id];
  }

}

void _deprive_and_charge(SendingRateDistributor *this)
{
  guint8            id;
  ChangeableVector* changeable_vector;
  guint8*           changeable;
  guint32*          positive_delta_sr_sum;
  guint32*          changeable_sr_sum;
  gint64*           delta_sending_rates;
  gdouble*          log2_changeable_stability_sum;

  _changeable_shortcut(this,
                       &changeable_vector,
                       &changeable,
                       &positive_delta_sr_sum,
                       NULL,
                       &changeable_sr_sum,
                       &delta_sending_rates,
                       &log2_changeable_stability_sum);

  for(id=0; id < SNDRATEDISTOR_MAX_NUM; ++id){
    guint32 RB;
    gdouble mi1,mi2;
    if(!changeable[id]) continue;
    RB = this->sending_rates[id] - ((gdouble)*positive_delta_sr_sum *
         ((gdouble)this->sending_rates[id] * log2((gdouble)this->stability[id])) /
         ((gdouble)*changeable_sr_sum * (*log2_changeable_stability_sum)));
    delta_sending_rates[id]-=RB;
    this->extra_bytes[id]+=RB;
    if(this->monitoring_interval[id] < 3) continue;
    mi1 = 1./((gdouble)this->monitoring_interval[id]-1.) * (gdouble) this->sending_rates[id];
    mi2 = 1./((gdouble)this->monitoring_interval[id]) * ((gdouble) (this->sending_rates[id]+RB));
    if(mi1 < mi2) --this->monitoring_interval[id];
  }
}

void _discharge_and_divide(SendingRateDistributor *this)
{
  Subflow*          subflow;
  guint8            id;
  ChangeableVector* changeable_vector;
  guint8*           changeable;
  guint32*          negative_delta_sr_sum;
  guint32*          changeable_sr_sum;
  gint64*           delta_sending_rates;

  _changeable_shortcut(this,
                       &changeable_vector,
                       &changeable,
                       NULL,
                       &negative_delta_sr_sum,
                       &changeable_sr_sum,
                       &delta_sending_rates,
                       NULL);
  subflow = NULL;
  while((subflow = _get_next_most_stable(this, subflow))){
    guint32 AB;
    if(!(*negative_delta_sr_sum)) break;
    AB = MIN(*negative_delta_sr_sum, this->extra_bytes[subflow->id]);

    delta_sending_rates[subflow->id]+=AB;
    *negative_delta_sr_sum-=AB;
    this->extra_bytes[subflow->id]-=AB;
    if(this->monitoring_interval[subflow->id] < 14)
      ++this->monitoring_interval[subflow->id];
    --this->stability[subflow->id];
  }
  if(!(*negative_delta_sr_sum)) goto done;
  //overused
  this->overused_bytes += *negative_delta_sr_sum;

  for(id=0; id < SNDRATEDISTOR_MAX_NUM; ++id){
    if(!changeable[id]) continue;
    delta_sending_rates[id]+=*negative_delta_sr_sum / *changeable_sr_sum;
    this->monitoring_interval[id] = MIN(this->monitoring_interval[id]+1,14);
  }
  done:
  return;
}

void _restore_and_obtain(SendingRateDistributor *this)
{

}

void _assign_and_notify(SendingRateDistributor *this)
{
  GHashTableIter iter;
  gpointer       key, val;
  Subflow*       subflow;
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) &key, (gpointer) &val))
  {
    subflow = val;
    this->sending_rates[subflow->id] += this->delta_sending_rates[subflow->id];
  }
}

void _changeable_shortcut(
    SendingRateDistributor *this,
    ChangeableVector** changeable_vector,
    guint8**        changeable,
    guint32**       positive_delta_sr_sum,
    guint32**       negative_delta_sr_sum,
    guint32**       changeable_sr_sum,
    gint64**        delta_sending_rates,
    gdouble**       log2_changeable_stability_sum)
{
  if(changeable_vector)
    *changeable_vector = this->changeable_vector;
  if(changeable)
    *changeable =  (*changeable_vector)->changeable;
  if(delta_sending_rates)
    *delta_sending_rates = this->delta_sending_rates;
  if(positive_delta_sr_sum)
    *positive_delta_sr_sum = &this->changeable_vector->positive_delta_sr_sum;
  if(negative_delta_sr_sum)
    *negative_delta_sr_sum = &this->changeable_vector->negative_delta_sr_sum;
  if(changeable_sr_sum)
    *changeable_sr_sum= &this->changeable_vector->changeable_sr_sum;
  if(log2_changeable_stability_sum)
    *log2_changeable_stability_sum = &this->changeable_vector->log2_changeable_stability_sum;
}

Subflow* _get_next_most_stable(SendingRateDistributor *this, Subflow *act)
{
  Subflow* result,*subflow;
  GHashTableIter iter;
  gpointer       key, val;
  guint8         max;
  if(act)  goto next;
  result = NULL;
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) &key, (gpointer) &val))
  {
      subflow = val;
      if(this->stability[subflow->id] > 0)
        continue;
      if(!result)
        result = subflow;
      else if(this->stability[result->id] < this->stability[subflow->id])
        result = subflow;
  }
  return result;

next:
  result = NULL;
  max = this->stability[act->id];
  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) &key, (gpointer) &val))
  {
      subflow = val;
      if(subflow == act) continue;
      if(this->stability[subflow->id] < 0) continue;
      if(!this->extra_bytes[subflow->id]) continue;
      if(!result){
        if(this->stability[subflow->id] <= max) result = subflow;
      }else if(this->stability[result->id] < this->stability[subflow->id]){
        if(this->stability[subflow->id] <= max) result = subflow;
      }
  }
  return result;
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
