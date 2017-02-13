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
  gint   remained,total,level;
  GList* subflows;

  SchNode *parent;
  SchNode *left;
  SchNode *right;
  gint32   sent_bytes;
  gint16   highest_flag;
};

typedef struct{
  guint16 remained;
  SchNode *root;
  gint32   total;
  gint32   actual;
  gint32   margin;
}CreateData;

#define _now(this) gst_clock_get_time (this->sysclock)
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------
//Functions realted to stream_splitter
static void
stream_splitter_finalize (
    GObject * object);

static void
_create_nodes(
    gpointer item,
    gpointer udata);


static SchNode *
_tree_ctor (
    StreamSplitter *this);


//Functions related to tree
static gpointer
_schnode_ctor (void);


static gint
_schtree_insert (
    SchNode *node,
    gint *value,
    SndSubflow * subflow,
    gint level,
    gint level_value,
    gint margin);

static gboolean
_allowed(
    SchNode *node,
	  SndPacket *packet,
    GstClockTime now,
    SndSubflowState allowed_state);

static SchNode *
_make_schnode(
    gint level,
    gint total);


static void
_schnode_rdtor (
    StreamSplitter *this,
    SchNode * node);


static SchNode *
_schtree_select_next (
    SchNode * root,
	  SndPacket *packet,
    GstClockTime now,
    SndSubflowState allowed_state);


static void
_schtree_approve_next (
    SchNode * selected,
    guint bytes_to_send);

static void
_refresh_splitter (
    StreamSplitter *this);

//TODO: implement it in multipath case
//static guint8
//_get_key_restriction(
//    StreamSplitter *this,
//	SndPacket *packet);
//
//static gboolean
//_vp8_keyframe_filter(
//		SndPacket *rtp);

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
}


void
stream_splitter_init (StreamSplitter * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->made                   = _now(this);
  this->refresh                = TRUE;
  this->keyframe_filtering     = FALSE;
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
  this->refresh = TRUE;
  abs_delta_target *= abs_delta_target < 0 ? -1 : 1;
  if(abs_delta_target < scheduled_target * .05 && abs_delta_target < 50000){
    return;
  }

//  _refresh_splitter(this);
  this->actual_targets[subflow->id] = subflow->target_bitrate;
  this->refresh = TRUE;
}


static void _sndsubflow_min_pacing_helper(SndSubflow *subflow, GstClockTime *next_time)
{
  *next_time = MIN(*next_time, subflow->pacing_time);
}

static void _select_highest_state(SndSubflow *subflow, SndSubflowState *max_state)
{
  *max_state = CONSTRAIN(*max_state, SNDSUBFLOW_STATE_STABLE, subflow->state);
  //*max_state = MAX(*max_state, subflow->state);
}

SndSubflow* stream_splitter_approve_packet(StreamSplitter * this,
    SndPacket *packet, GstClockTime now, GstClockTime *next_time)
{
  SchNode *selected;
  SndSubflow* result = NULL;
  SndSubflowState min_allowed_state;

  min_allowed_state = SNDSUBFLOW_STATE_OVERUSED;
  if(this->keyframe_filtering){
     sndsubflows_iterate(this->subflows, (GFunc)_select_highest_state, &min_allowed_state);
  }

  if(this->refresh){
    _refresh_splitter(this);
    this->refresh = FALSE;
  }

  if (this->tree == NULL) {
    GST_WARNING_OBJECT (this, "No active subflow");
    goto done;
  }

  DISABLE_LINE _print_ratios(this);
//  sndsubflows_iterate(this->subflows, (GFunc) _sndsubflow_min_pacing_helper, next_time);
//  if(now < *next_time){
//    goto done;
//  }

  selected = _schtree_select_next(this->tree, packet, now, min_allowed_state);
  if(!selected){
    if(next_time){
      sndsubflows_iterate(this->subflows, (GFunc) _sndsubflow_min_pacing_helper, next_time);
    }
    goto done;
  }

  result = selected->subflows->data;
  _schtree_approve_next(selected, packet->payload_size);
done:
  return result;
}

void
_refresh_splitter (StreamSplitter *this)
{
  if(this->tree){
    _schnode_rdtor(this, this->tree);
    this->tree = NULL;
  }

  if(!sndsubflows_get_subflows_num(this->subflows)){
    goto done;
  }

  //if(0)
  {

  }

  this->tree = _tree_ctor(this);
  _logging(this);
done:
  return;
}

void _create_nodes(gpointer item, gpointer udata)
{

  CreateData *cdata = udata;
  SndSubflow *subflow = item;
  gint value = subflow->target_bitrate >> 3;
  if(!subflow->active){
    return;
  }

  _schtree_insert(cdata->root,
                  &value,      //value we distribute
                  subflow,     //subflow we insert
                  0,           //level
                  cdata->total,//level value
                  cdata->margin
                  );

}

SchNode *
_tree_ctor (StreamSplitter *this)
{
  CreateData cdata;
  cdata.remained      = cdata.total = sndsubflows_get_total_target(this->subflows) >> 3;
  cdata.margin        = (cdata.total >> (SCHTREE_MAX_LEVEL + 3)) + 1;
  cdata.root          = _make_schnode(0, cdata.total);

  sndsubflows_iterate(this->subflows, _create_nodes, &cdata);
  return cdata.root;
}

void
_schnode_rdtor (StreamSplitter *this,SchNode * node)
{
  if (node == NULL) {
    return;
  }
  _schnode_rdtor (this, node->left);
  _schnode_rdtor (this, node->right);
  g_slice_free(SchNode, node);

//  mprtp_free(node);
}


gint
_schtree_insert (SchNode * node, gint *value, SndSubflow * subflow, gint level, gint level_value, gint32 margin)
{
  gint dvalue = 0;
  gint left_level_value,right_level_value;

  if(!node->remained || *value < 1){
    goto done;
  }

  if((node->remained <= *value || *value <= margin) &&
    !node->left &&
    !node->right)
  {
    SchNode* actual = node;
    *value -= node->remained;
    dvalue =  node->remained;
    node->subflows = g_list_prepend(node->subflows, subflow);
    //refresh sent bytes for correct initial value in case of rapid changes
    do{
      actual->sent_bytes += subflow->actual_bitrate >> (level + 3);
      actual = actual->parent;
    }while(actual != NULL);
    goto done;
  }

  left_level_value = level_value>>1;
  if(!node->left){
    node->left = _make_schnode(level, left_level_value);
    node->left->parent = node;
  }
  if(0 < node->left->remained){
    node->left->subflows = g_list_prepend(node->left->subflows, subflow);
    dvalue += _schtree_insert(node->left, value, subflow, level + 1, left_level_value, margin);
  }
  if((*value) < 1){
    goto done;
  }
  right_level_value = level_value - left_level_value;
  if(!node->right){
    node->right = _make_schnode(level, right_level_value);
    node->right->parent = node;
  }
  if(0 < node->right->remained){
    node->right->subflows = g_list_prepend(node->right->subflows, subflow);
    dvalue += _schtree_insert(node->right, value, subflow, level + 1, right_level_value, margin);
  }

done:
  node->remained -= dvalue;
  return dvalue;
}

gboolean _allowed(SchNode *node, SndPacket *packet, GstClockTime now, SndSubflowState min_allowed_state)
{
  GList *it;
  SndSubflow *subflow;

  for(it = node->subflows; it; it = it->next){
    subflow = it->data;
    if(now < subflow->pacing_time){
//      g_print("pacing: %lu\n", subflow->pacing_time - now);
      continue;
    }

    if(packet->keyframe && subflow->state < min_allowed_state){
//      g_print("Packet %hu is keyframe and the min state is %d subflow %d state is %d\n",
//          packet->abs_seq, min_allowed_state, subflow->id, subflow->state);
      continue;
    }
    return TRUE;
  }
  return FALSE;
}


SchNode *_make_schnode(gint level, gint total)
{
  SchNode *result;
  result = _schnode_ctor();
  result->remained = total;
  result->total    = total;
  result->level    = level;
  return result;
}

gpointer
_schnode_ctor (void)
{
  SchNode *result = g_slice_new0(SchNode);
//  g_print("%d schnode created", ++created_);
  result->left       = NULL;
  result->right      = NULL;
  result->subflows   = NULL;
  result->sent_bytes = 0;
  result->remained   = 0;
  return result;
}


SchNode *
_schtree_select_next (SchNode * root, SndPacket *packet, GstClockTime now, SndSubflowState min_allowed_state)
{
  SchNode *selected, *left, *right;
  gboolean left_allowed,right_allowed;

  selected = root;
  while (selected->left != NULL && selected->right != NULL) {
    left          = selected->left;
    right         = selected->right;
    left_allowed  = _allowed(left, packet, now, min_allowed_state);
    right_allowed = _allowed(right, packet, now, min_allowed_state);

    if(!left_allowed && !right_allowed){
      selected = NULL;
      goto done;
    }
    if(!left_allowed){
      selected = right;
      continue;
    }

    if(!right_allowed){
      selected = left;
      continue;
    }
    selected = left->sent_bytes <= right->sent_bytes ? left : right;
  }
  if(!selected->subflows){
    g_warning("Problems with subflows at stream splitter");
    _print_tree(root, SCHTREE_MAX_VALUE, 0);
  }else if(!_allowed(selected, packet, now, min_allowed_state)){
    selected = NULL;
  }
done:
  return selected;
}


void
_schtree_approve_next (SchNode * selected, guint bytes_to_send)
{
  while (selected != NULL) {
    selected->sent_bytes += bytes_to_send;
    selected = selected->parent;
  }
}

//TODO: implement it in multipath case for more sophistacated distribution
//guint8 _get_key_restriction(StreamSplitter *this, SndPacket *packet)
//{
//  switch(this->keyframe_filtering){
//    case 1:
//      return _vp8_keyframe_filter(packet) ? this->max_flag : 0;
//    case 0:
//    default:
//      return 0;
//  }
//  return 0;
//}
//
//gboolean
//_vp8_keyframe_filter(SndPacket *packet)
//{
//  gboolean is_keyframe;
//  guint8 *p;
//  unsigned long raw;
//  p = packet->payload;
//  /* The frame header is defined as a three byte little endian
//  * value
//  */
//  raw = p[0] | (p[1] << 8) | (p[2] << 16);
//  is_keyframe     = !BITS_GET(raw, 0, 1);
//  return is_keyframe;
//}


void _print_tree (SchNode * node, gint value, gint level)
{
  gint i;
  gint right_value, left_value;
  if (node == NULL) {
    return;
  }
  for (i = 0; i < level; ++i)
    g_print ("--");
  if (node->subflows != NULL) {
      GList *it;
      g_print (
             "R: %d | %d->sent_bytes:%d (L:%p,R:%p) subflows:",
             node->remained,
             value,
             node->sent_bytes,
             node->left,
             node->right);
    for(it = node->subflows; it; it = it->next){
      g_print ("%d ", ((SndSubflow*)it->data)->id);
    }
    g_print ("\n");
  } else {
    GList *it;
    g_print ("R: %d | %d->C:%d, (L:%p;R:%p) Subflows: ", node->remained, value, node->sent_bytes, node->left, node->right);
    for(it = node->subflows; it; it = it->next){
      g_print ("%d ", ((SndSubflow*)it->data)->id);
    }
    g_print("\n");
  }
  left_value  = value >> 1;
  right_value = value - left_value;
  _print_tree (node->left,  left_value,  level + 1);
  _print_tree (node->right, right_value, level + 1);
}

static void _log_tree (SchNode * node, gint value, gint level)
{
  gint i;
  gint right_value, left_value;
  if (node == NULL) {
    return;
  }
  for (i = 0; i < level; ++i)
    mprtp_logger ("streamsplitter.log","--");
  if (node->subflows != NULL) {
      GList *it;
      mprtp_logger ("streamsplitter.log",
             "%d->sent_bytes:%d (L:%p,R:%p) subflows:",
             value,
             node->sent_bytes,
             node->left,
             node->right);
    for(it = node->subflows; it; it = it->next){
        mprtp_logger ("streamsplitter.log","%d ", ((SndSubflow*)it->data)->id);
    }
    mprtp_logger ("streamsplitter.log","\n");
  } else {
    mprtp_logger ("streamsplitter.log","%d->C:%d\n", value, node->sent_bytes);
  }
  left_value  = value >> 1;
  right_value = value - left_value;
  _log_tree (node->left,  left_value,  level + 1);
  _log_tree (node->right, right_value, level + 1);
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

//static void _log_subflow_csv(SndSubflow *subflow, gpointer data)
//{
//  gchar appendum[255];
//  sprintf(appendum, "%f,", subflow->weight);
//  strcat((gchar*)data, appendum);
//}
//
//void _logging_csv(gpointer data, gchar* string)
//{
//  StreamSplitter *this = data;
//  _iterate_subflows(this, _log_subflow_csv, string);
//  strcat(string, "\n");
//}

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
  _log_tree(this->tree, sndsubflows_get_total_target(this->subflows) >> 3, 0);
}

