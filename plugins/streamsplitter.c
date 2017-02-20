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
#include "streamsplitter.h"
#include <string.h>
#include <stdio.h>
#include <math.h>


GST_DEBUG_CATEGORY_STATIC (stream_splitter_debug_category);
#define GST_CAT_DEFAULT stream_splitter_debug_category

/* class initialization */
G_DEFINE_TYPE (StreamSplitter, stream_splitter, G_TYPE_OBJECT);



struct _SchNode
{
  //  SchNode* parent;
  GSList*  subflows;
  GSList*  childs;
  gint32   sent_bytes;
  gint16   highest_flag;
};


//
//typedef struct{
//  SndSubflow* subflow;
//  gint32      remained;
//}SubflowSetupHelper;
//
//
//typedef struct{
//  SubflowSetupHelper subflows[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
//  gint32             subflow_num;
//  gint32             total_target;
//}SubflowSetupArray;

#define _now(this) gst_clock_get_time (this->sysclock)
#define _get_splitter_subflow(this, id) ((SplitterSubflow*) (this->splittersubflows_lookup + id))
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------
//Functions realted to stream_splitter
static void
stream_splitter_finalize (
    GObject * object);



static void
_create_nodes(
    StreamSplitter* this,
    SchNode* node,
    guint8 subflow_num,
    guint level);

static SchNode *
_tree_ctor (
    StreamSplitter *this);


//Functions related to tree
static gpointer
_schnode_ctor (void);


static SplitterSubflow*
_get_max_amongst_splitter_subflow(
    StreamSplitter* this);

static GSList*
_merge_subflows_list(
    GSList* target,
    GSList* source);

static void
_setup_nodes(
    StreamSplitter* this,
    SchNode* node,
    guint level);

static void
_clean_tree(
    StreamSplitter* this);

static void
_setup_tree(
    StreamSplitter* this);

static void
_schnode_rdtor (
    StreamSplitter *this,
    SchNode * node);


static SchNode *
_schtree_select_next(
    StreamSplitter* this,
    SchNode *node,
    SndPacket* packet);


static void
_rebuild_tree (
    StreamSplitter *this);

static void
_print_tree (
    SchNode * node,
    gint value,
    gint level);

static void
_logging(
    gpointer data);



//----------------------------------------------------------------------
//---- Private function implementations to Stream Dealer object --------
//----------------------------------------------------------------------


typedef struct{
  gdouble target_sum,actual_sum;
}PrintData;
static void _sim_bitrate(SndSubflow* subflow, PrintData *data){
  data->target_sum +=subflow->target_bitrate;
  data->actual_sum +=subflow->actual_bitrate;
}
static void _print_weight(SndSubflow* subflow, PrintData* data){
  g_print("Subflow %d, target weight: %3.2f actual weight: %3.2f, ",
      subflow->id,
      (gdouble) subflow->target_bitrate / data->target_sum,
      (gdouble) subflow->actual_bitrate / data->actual_sum
      );
}
static GstClockTime last_ratio_printed = 0;
static void _print_ratios(StreamSplitter *this){
  PrintData data = {0.,0.};
  if(_now(this) < last_ratio_printed  + 200 * GST_MSECOND) return;
  last_ratio_printed = _now(this);
  sndsubflows_iterate(this->subflows, (GFunc) _sim_bitrate, &data);
  sndsubflows_iterate(this->subflows, (GFunc) _print_weight, &data);
  g_print("\n");
}


void
stream_splitter_class_init (StreamSplitterClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = stream_splitter_finalize;

  GST_DEBUG_CATEGORY_INIT (stream_splitter_debug_category, "stream_splitter", 0,
      "Stream Splitter");

}

StreamSplitter* make_stream_splitter(SndSubflows* sndsubflows)
{
  StreamSplitter *this;
  this = g_object_new (STREAM_SPLITTER_TYPE, NULL);
  this->subflows = g_object_ref(sndsubflows);
  return this;
}

void
stream_splitter_finalize (GObject * object)
{
  StreamSplitter *this = STREAM_SPLITTER (object);
  g_object_unref (this->sysclock);
  g_object_unref(this->subflows);
  g_free(this->splittersubflows_lookup);
}


void
stream_splitter_init (StreamSplitter * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->made                   = _now(this);
  this->refresh                = TRUE;
  this->keyframe_filtering     = FALSE;

  this->splittersubflows_lookup       = g_malloc0(sizeof(SplitterSubflow) * MPRTP_PLUGIN_MAX_SUBFLOW_NUM);
  this->splittersubflows_list          = NULL;

}


void
stream_splitter_set_mpath_keyframe_filtering(StreamSplitter * this, guint keyframe_filtering)
{
  this->keyframe_filtering = keyframe_filtering;
}

void
stream_splitter_on_target_bitrate_changed(StreamSplitter* this, SndSubflow* subflow)
{
  gint32 scheduled_target = this->actual_targets[subflow->id];
  gint32 abs_delta_target = subflow->target_bitrate - scheduled_target;
  abs_delta_target *= abs_delta_target < 0 ? -1 : 1;
//  this->refresh = TRUE;

  if(this->last_regular_refresh < _now(this) - GST_SECOND){
    this->refresh = TRUE;
    return;
  }

  if(abs_delta_target < scheduled_target * .1 && abs_delta_target < 100000){
    return;
  }

  this->actual_targets[subflow->id] = subflow->target_bitrate;
  this->refresh                     = TRUE;
  this->last_regular_refresh        = _now(this);
}

void
stream_splitter_on_subflow_detached(StreamSplitter* this, SndSubflow* subflow)
{
  SplitterSubflow* splitter_subflow;
  splitter_subflow = _get_splitter_subflow(this, subflow->id);

  this->refresh    = TRUE;
  this->splittersubflows_list = g_slist_remove(this->splittersubflows_list, splitter_subflow);
  _rebuild_tree(this);
}

void
stream_splitter_on_subflow_joined(StreamSplitter* this, SndSubflow* subflow)
{
  SplitterSubflow* splitter_subflow;
  splitter_subflow = _get_splitter_subflow(this, subflow->id);
  splitter_subflow->subflow = subflow;

  this->refresh    = TRUE;
  this->splittersubflows_list = g_slist_prepend(this->splittersubflows_list, splitter_subflow);
  _rebuild_tree(this);
}

static void _select_highest_state(SndSubflow *subflow, SndSubflowState *max_state)
{
  *max_state = CONSTRAIN(*max_state, SNDSUBFLOW_STATE_STABLE, subflow->state);
}

void
stream_splitter_on_subflow_state_changed(StreamSplitter* this, SndSubflow* subflow)
{
  if(subflow->state != SNDSUBFLOW_STATE_OVERUSED){
    return;
  }
  this->refresh = TRUE;
  sndsubflows_iterate(this->subflows, (GFunc)_select_highest_state, &this->max_state);
}

SndSubflow* stream_splitter_approve_packet(StreamSplitter * this,
    SndPacket *packet, GstClockTime now, GstClockTime *next_time)
{
  SchNode *selected;
  SndSubflow* result = NULL;
  guint8 min_allowed_state;
  GstClockTime next_allowed_time = 0;
  gboolean has_allowed_subflow = FALSE;
  GSList* it;

  min_allowed_state = this->keyframe_filtering ? this->max_state: SNDSUBFLOW_STATE_OVERUSED;

  if(this->refresh){
    _clean_tree(this);
    _setup_tree(this);
    this->refresh = FALSE;
  }

  if (this->tree == NULL) {
    GST_WARNING_OBJECT (this, "No active subflow");
    goto done;
  }

  for(it = this->splittersubflows_list; it; it = it->next){
    SplitterSubflow* splitter_subflow = it->data;
    SndSubflow* subflow = splitter_subflow->subflow;

    if(!next_allowed_time || subflow->pacing_time < next_allowed_time){
      next_allowed_time = subflow->pacing_time;
    }
    if(now < subflow->pacing_time){
      splitter_subflow->allowed = FALSE;
      continue;
    }
    if(packet->keyframe && subflow->state < min_allowed_state){
      splitter_subflow->allowed = FALSE;
      continue;
    }

    splitter_subflow->allowed = TRUE;
    has_allowed_subflow = TRUE;
  }

  DISABLE_LINE _print_ratios(this);

  if(!has_allowed_subflow){
    if(next_time){
      *next_time = next_allowed_time;
    }
    goto done;
  }
  selected = _schtree_select_next(this, this->tree, packet);
  result = selected->subflows->data;
done:
  return result;
}

void
_rebuild_tree (StreamSplitter *this)
{
  if(this->tree){
    _schnode_rdtor(this, this->tree);
  }

  this->tree = _tree_ctor(this);
  _logging(this);
//  _print_tree(this->tree, this->total_target, 0);
  return;
}


void _create_nodes(StreamSplitter* this,
                       SchNode* node,
                       guint8 subflow_num,
                       guint level)
{
  gdouble actual_accuracy = 1./pow(subflow_num, level + 1);
  guint i;

  if(actual_accuracy <= SCHTREE_DISTRIBUTION_ACCURACY){
    return;
  }
  for(i = 0; i < subflow_num; ++i){
    node->childs = g_slist_prepend(node->childs, _schnode_ctor());
  }

  //last child become a parent
  _create_nodes(this, g_slist_last(node->childs)->data, subflow_num, level + 1);
}

SchNode *
_tree_ctor (StreamSplitter *this)
{
  guint8 subflow_num = g_slist_length(this->splittersubflows_list);
  SchNode* root;

  if(!subflow_num){
    return NULL;
  }

  root = _schnode_ctor();
  if(subflow_num < 2){
    return root;
  }

  _create_nodes(this, root, subflow_num, 0);

  return root;
}

void
_schnode_rdtor (StreamSplitter *this,SchNode * node)
{
  SchNode* child;
  GSList* it;

  if (node == NULL) {
    return;
  }
  for(it = node->childs; it; it = it->next){
    child = it->data;
    _schnode_rdtor(this, child);
  }
  g_slice_free(SchNode, node);
//  mprtp_free(node);
}


gpointer
_schnode_ctor (void)
{
  SchNode *result = g_slice_new0(SchNode);

  result->subflows   = NULL;
  result->sent_bytes = 0;
  result->childs     = NULL;
  return result;
}


static gboolean _allowed_node(StreamSplitter* this, SchNode* node){
  GSList *it;
  SndSubflow *subflow;
  for(it = node->subflows; it; it = it->next){
    subflow = it->data;
    if(_get_splitter_subflow(this, subflow->id)->allowed){
      return TRUE;
    }
  }
  return FALSE;
}

SchNode* _schtree_select_next(StreamSplitter* this, SchNode *node, SndPacket* packet)
{
  SchNode *child = NULL, *selected = NULL;
  GSList* it;

  if(node->childs == NULL){
    return _allowed_node(this, node) ? node : NULL;
  }

  for(it = node->childs; it; it = it->next){
    child = it->data;
    if(!_allowed_node(this, child)){
      continue;
    }
    if(!selected || child->sent_bytes < selected->sent_bytes){
      selected = child;
    }
  }

  if(!selected){
    return NULL;
  }

  selected->sent_bytes += packet->payload_size;
  if(selected->childs){
    return _schtree_select_next(this, selected, packet);
  }
//  if(1 < g_slist_length(selected->subflows)){
//    return _schtree_select_next(this, selected, packet);
//  }
  return selected;
}



GSList* _merge_subflows_list(GSList* target, GSList* source)
{
  GSList *it, *it2;
  SndSubflow *selected, *actual;
  gboolean contains;
  if(!target && !source){
    return NULL;
  }
  if(!target || !target){
    return g_slist_copy(source ? source : target);
  }
  for(it = source; it; it = it->next){
    selected = it->data;
    contains = FALSE;
    for(it2 = target; it2; it2 = it2->next){
      actual = it2->data;
      if(actual->id == selected->id){
        contains = TRUE;
      }
    }
    if(!contains){
      target = g_slist_prepend(target, selected);
    }
  }
  return target;
}

SplitterSubflow* _get_max_amongst_splitter_subflow(StreamSplitter* this)
{
  GSList* it;
  SplitterSubflow *selected = NULL, *actual;
  for(it = this->splittersubflows_list; it; it = it->next){
    actual = it->data;
    if(!selected || selected->remained < actual->remained){
      selected = actual;
    }
  }
  return selected;
}
static void _print_splitter_subflows(StreamSplitter* this){
  GSList* it;
  for(it = this->splittersubflows_list; it; it = it->next){
    SplitterSubflow* splitter_subflow = it->data;
    g_print("Splitter subflow %d, remained: %d\n", splitter_subflow->subflow->id, splitter_subflow->remained);
  }
}

void _setup_nodes(StreamSplitter* this,
                         SchNode* node,
                         guint level)
{
  SchNode* child;
  GSList* it;

  if(node->childs == NULL){
    SplitterSubflow* splitter_subflow;
    gint32 portion;

    DISABLE_LINE _print_splitter_subflows(this);
    splitter_subflow = _get_max_amongst_splitter_subflow(this);
    portion          = this->total_target/pow(this->active_subflow_num, level);
    splitter_subflow->remained -= portion;
    node->subflows = g_slist_prepend(node->subflows, splitter_subflow->subflow);
    DISABLE_LINE _print_splitter_subflows(this);
    DISABLE_LINE g_print("PORTION: %d\n", portion);

    return;
  }

  for(it = node->childs; it; it = it->next){
    child = it->data;
    _setup_nodes(this, child, level + 1);
    node->subflows = _merge_subflows_list(node->subflows, child->subflows);
  }
}

static void _clean_node(SchNode* node){
  SchNode* child;
  GSList*  it;
  if(!node) return;

  for(it = node->childs; it; it = it->next){
    child = it->data;
    if(!child) break;
    _clean_node(child);
  }
  g_slist_free(node->subflows);
  node->subflows = NULL;
}

void _clean_tree(StreamSplitter* this)
{
  _clean_node(this->tree);
}


static void _setup_tree_helper(SndSubflow* subflow, StreamSplitter* this){
  if(!subflow->active){
    return;
  }

  _get_splitter_subflow(this, subflow->id)->remained = subflow->target_bitrate;
  this->total_target += subflow->target_bitrate;
  ++this->active_subflow_num;
}

void _setup_tree(StreamSplitter* this){
  this->total_target = 0;
  this->active_subflow_num = 0;
  sndsubflows_iterate(this->subflows, (GFunc)_setup_tree_helper, this);
  _setup_nodes(this, this->tree, 0);
  DISABLE_LINE _print_tree(this->tree, this->total_target, 0);
}



void _print_tree (SchNode * node, gint value, gint level)
{
  gint i;
  gint subflow_num = 0;
  SchNode* child;
  GSList *it;

  if (node == NULL) {
    return;
  }
  for (i = 0; i < level; ++i){
    g_print ("--");
  }

  g_print (
         "%p: %d sent_bytes:%d subflows:",
         node,
         value,
         node->sent_bytes);
  for(it = node->subflows; it; it = it->next){
    g_print ("%d ", ((SndSubflow*)it->data)->id);
  }
  g_print(" childs: ");
  for(it = node->childs; it; it = it->next){
    child = it->data;
    g_print("%p ", child);
    ++subflow_num;
  }
  g_print ("\n");

  for(it = node->childs; it; it = it->next){
     child = it->data;
    _print_tree (child,  value / subflow_num,  level + 1);
  }
}


static void _log_subflow(gpointer item, gpointer udata)
{
  SndSubflow *subflow = item;
  StreamSplitter *this = udata;
  mprtp_logger("streamsplitter.log",
               "----------------------------------------------------------------\n"
               "Subflow id: %d\n"
               "Target bittarget: %d | weight: %f\n",

               subflow->id,
               subflow->target_bitrate,
               (gdouble) subflow->target_bitrate / (gdouble) sndsubflows_get_total_target(this->subflows)
               );

}


void _logging(gpointer data)
{
  StreamSplitter *this = data;
  mprtp_logger("streamsplitter.log",
               "###############################################################\n"
               "Seconds: %lu\n"
               "Active subflow num: %d\n"
               ,
               GST_TIME_AS_SECONDS(_now(this) - this->made),
               sndsubflows_get_subflows_num(this->subflows)
               );

  sndsubflows_iterate(this->subflows, _log_subflow, this);
}

