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
#include "mprtpspath.h"
#include <math.h>


GST_DEBUG_CATEGORY_STATIC (stream_splitter_debug_category);
#define GST_CAT_DEFAULT stream_splitter_debug_category


#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

/* class initialization */
G_DEFINE_TYPE (StreamSplitter, stream_splitter, G_TYPE_OBJECT);

typedef struct _Subflow Subflow;

#define SUBFLOW_SCHEDULED_BYTES_LENGTH 100
struct _Subflow
{
  guint8      id;
  MPRTPSPath *path;
  gint32      flags_value;
  gint32      sending_target;
  gint        weight_for_tree;
  gdouble     weight;
  gboolean    valid;

};

struct _SchNode
{
  gint   remained;
  GList* subflows;

  SchNode *parent;
  SchNode *left;
  SchNode *right;
  gint32   sent_bytes;
  gint16   highest_flag;
};

typedef struct{
  gint32 valid;
  gint32 total;
}SumData;

typedef struct{
  guint16 total_weight;
  gint32  valid_sum;
}WeightData;

typedef struct{
  guint16 remained;
  SchNode *root;
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
_iterate_subflows(
    StreamSplitter *this,
    void(*iterator)(Subflow *, gpointer),
    gpointer data);

static void _refresh_flags(
    Subflow *subflow,
    gpointer data);

static void
_summarize_sending_rates(
    Subflow *subflow,
    gpointer data);

static void
_validate_sending_rates(
    Subflow *subflow,
    gpointer data);


static void
_setup_sending_weights(
    Subflow *subflow,
    gpointer data);


static void
_create_nodes(
    Subflow *subflow,
    gpointer data);


static SchNode *
_tree_ctor (
    StreamSplitter *this);


//Functions related to tree
static gpointer
_schnode_ctor (void);


static gint
_schtree_insert (
    SchNode * node,
    gint * value,
    Subflow * subflow,
    gint level_value);

static gboolean
_allowed(
    SchNode *node,
    GstRTPBuffer *rtp,
    guint8 flag_restriction);

static SchNode *
_make_schnode(
    gint remained);


static void
_schnode_rdtor (
    StreamSplitter *this,
    SchNode * node);


static SchNode *
_schtree_select_next (
    SchNode * root,
    GstRTPBuffer * rtp,
    guint8 flag_restriction);


static Subflow *
_schtree_get_next (
    SchNode * root,
    GstRTPBuffer * rtp,
    guint8 key_restriction);

static void
_schtree_approve_next (
    SchNode * selected,
    guint bytes_to_send);

static void
_refresh_splitter (
    StreamSplitter *this);


static Subflow *
_make_subflow (
    MPRTPSPath * path);


static MPRTPSPath *
_get_next_path (
    StreamSplitter * this,
    GstRTPBuffer * rtp);

static void
_logging(
    gpointer data);



//----------------------------------------------------------------------
//---- Private function implementations to Stream Dealer object --------
//----------------------------------------------------------------------

void
stream_splitter_class_init (StreamSplitterClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = stream_splitter_finalize;

  GST_DEBUG_CATEGORY_INIT (stream_splitter_debug_category, "stream_splitter", 0,
      "Stream Splitter");

}

StreamSplitter* make_stream_splitter(PacketsSndQueue *sndqueue)
{
  StreamSplitter *this;
  this = g_object_new (STREAM_SPLITTER_TYPE, NULL);
  this->sndqueue = sndqueue;
  return this;
}

void
stream_splitter_finalize (GObject * object)
{
  StreamSplitter *this = STREAM_SPLITTER (object);
  g_hash_table_destroy (this->subflows);
  g_object_unref (this->sysclock);
}


void
stream_splitter_init (StreamSplitter * this)
{
  this->sysclock = gst_system_clock_obtain ();
  this->subflows               = g_hash_table_new_full (NULL, NULL, NULL, mprtp_free);
  this->made                   = _now(this);

  g_rw_lock_init (&this->rwmutex);
}

void
stream_splitter_add_path (StreamSplitter * this, guint8 subflow_id,
    MPRTPSPath * path, gint32 sending_rate)
{
  Subflow *lookup_result;
  THIS_WRITELOCK (this);
  lookup_result =
      (Subflow *) g_hash_table_lookup (this->subflows,
      GINT_TO_POINTER (subflow_id));
  if (lookup_result != NULL) {
    GST_WARNING_OBJECT (this, "The requested add operation can not be done "
        "due to duplicated subflow id (%d)", subflow_id);
    goto exit;
  }
  lookup_result = _make_subflow (path);
  g_hash_table_insert (this->subflows, GINT_TO_POINTER (subflow_id),
      lookup_result);
  lookup_result->sending_target = sending_rate;
  lookup_result->id = subflow_id;
  ++this->active_subflow_num;
  GST_DEBUG ("Subflow is added, the actual number of subflow is: %d",
      this->active_subflow_num);

  _refresh_splitter(this);

exit:
  THIS_WRITEUNLOCK (this);
}

void
stream_splitter_rem_path (StreamSplitter * this, guint8 subflow_id)
{
  Subflow *lookup_result;
  THIS_WRITELOCK (this);
  lookup_result =
      (Subflow *) g_hash_table_lookup (this->subflows,
      GINT_TO_POINTER (subflow_id));
  if (lookup_result == NULL) {
    GST_WARNING_OBJECT (this, "The requested remove operation can not be done "
        "due to not existed subflow id (%d)", subflow_id);
    goto exit;
  }
  g_hash_table_remove (this->subflows, GINT_TO_POINTER (subflow_id));
  --this->active_subflow_num;
  GST_DEBUG ("Subflow is marked to be removed, the actual number of subflow is: %d",
      this->active_subflow_num);

  _refresh_splitter(this);

exit:
  THIS_WRITEUNLOCK (this);
}


void
stream_splitter_setup_sending_target (StreamSplitter * this, guint8 subflow_id,
    gint32 sending_target)
{
  Subflow *subflow;
  THIS_WRITELOCK (this);
  subflow =
      (Subflow *) g_hash_table_lookup (this->subflows,
      GINT_TO_POINTER (subflow_id));
  if (subflow == NULL) {
    GST_WARNING_OBJECT (this,
        "The requested setup bid operation can not be done "
        "due to not existed subflow id (%d)", subflow_id);
    goto exit;
  }
  subflow->sending_target = sending_target;
exit:
  THIS_WRITEUNLOCK (this);
}

gdouble stream_splitter_get_sending_target(StreamSplitter* this, guint8 subflow_id)
{
  Subflow *subflow;
  gdouble result = 0.;
  THIS_READLOCK(this);
  subflow =
        (Subflow *) g_hash_table_lookup (this->subflows,
        GINT_TO_POINTER (subflow_id));
  if(!subflow) goto done;
  result = subflow->sending_target;
done:
  THIS_READUNLOCK(this);
  return result;
}

gdouble stream_splitter_get_sending_weight(StreamSplitter* this, guint8 subflow_id)
{
  Subflow *subflow;
  gdouble result = 0.;
  THIS_READLOCK(this);
  subflow =
        (Subflow *) g_hash_table_lookup (this->subflows,
        GINT_TO_POINTER (subflow_id));
  if(!subflow) goto done;
  result = subflow->weight;
done:
  THIS_READUNLOCK(this);
  return result;
}

static void _refresh_targets(Subflow *subflow, gpointer data)
{
  subflow->sending_target = mprtps_path_get_target_bitrate(subflow->path);
}

void
stream_splitter_refresh_targets (StreamSplitter * this)
{
  THIS_WRITELOCK (this);
  _iterate_subflows(this, _refresh_targets, NULL);
  _refresh_splitter(this);
  THIS_WRITEUNLOCK (this);
}

void
stream_splitter_commit_changes (StreamSplitter * this)
{
  THIS_WRITELOCK (this);
  _refresh_splitter(this);
  THIS_WRITEUNLOCK (this);
}

gboolean
stream_splitter_approve_buffer(StreamSplitter * this, GstBuffer *buffer, MPRTPSPath **path)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  gboolean result;
  guint8 flag_restriction;
  SchNode *selected;

  result = FALSE;
  *path = NULL;

  THIS_WRITELOCK (this);
  if (this->tree == NULL) {
    GST_WARNING_OBJECT (this, "No active subflow");
    goto done;
  }
  if (G_UNLIKELY (!gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp))) {
    GST_WARNING_OBJECT (this, "The RTP packet is not readable");
    goto done;
  }
  flag_restriction = 0; //Fixme: flag restriction considering the type of buffer here....
  selected = _schtree_select_next(this->tree, &rtp, flag_restriction);
  if(!selected){
    gst_rtp_buffer_unmap (&rtp);
    goto done;
  }

  result = TRUE;
  *path = ((Subflow*)selected->subflows->data)->path;

  _schtree_approve_next(selected, gst_rtp_buffer_get_payload_len(&rtp));
  gst_rtp_buffer_unmap (&rtp);
done:
  THIS_WRITEUNLOCK (this);
  return result;
}

GstBuffer *
stream_splitter_pop(StreamSplitter * this, MPRTPSPath **out_path)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  MPRTPSPath *path = NULL;
  GstBuffer *buffer = NULL;
  THIS_WRITELOCK (this);
  if (this->tree == NULL) {
    //Somewhere, over the rainbow a path may exist
    GST_WARNING_OBJECT (this, "No active subflow");
    goto done;
  }
  buffer = packetssndqueue_peek(this->sndqueue);
  if(!buffer){
    goto done;
  }
  if (G_UNLIKELY (!gst_rtp_buffer_map (buffer, GST_MAP_READ, &rtp))) {
    GST_WARNING_OBJECT (this, "The RTP packet is not readable");
    goto done;
  }

  path = _get_next_path (this, &rtp);
  gst_rtp_buffer_unmap (&rtp);
  *out_path = path;
  buffer = path ? packetssndqueue_pop(this->sndqueue) : NULL;
done:
  THIS_WRITEUNLOCK (this);
  return buffer;
}


void
_refresh_splitter (StreamSplitter *this)
{
  if(this->tree){
    _schnode_rdtor(this, this->tree);
    this->tree = NULL;
  }

  if(!this->active_subflow_num){
    goto done;
  }

  this->tree = _tree_ctor(this);
  _logging(this);
done:
  return;
}


void _iterate_subflows(StreamSplitter *this, void(*iterator)(Subflow *, gpointer), gpointer data)
{
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    subflow = (Subflow *) val;
    iterator(subflow, data);
  }
}

void _refresh_flags(Subflow *subflow, gpointer data)
{
  subflow->flags_value = mprtps_path_get_flags(subflow->path);
}

MPRTPSPath *
_get_next_path (StreamSplitter * this, GstRTPBuffer * rtp)
{
  Subflow *subflow = NULL;

  gboolean flag_restriction;
  flag_restriction = 0; //Fixme: flag restriction considering the type of buffer here....

  subflow = _schtree_get_next(this->tree, rtp, flag_restriction);
  return subflow ? subflow->path : NULL;
}

void _summarize_sending_rates(Subflow *subflow, gpointer data)
{
  SumData *sumd = data;
  sumd->total+= subflow->sending_target;
}

void _validate_sending_rates(Subflow *subflow, gpointer data)
{
  SumData *sumd = data;
  gdouble exp_ratio = (gdouble) subflow->sending_target / (gdouble)sumd->total;
  gdouble treshold = 1. / (gdouble) SCHTREE_MAX_VALUE;
  if(exp_ratio < treshold) return;
  subflow->valid = TRUE;
  sumd->valid += subflow->sending_target;
}


void _setup_sending_weights(Subflow *subflow, gpointer data)
{
  WeightData *wdata = data;
  gdouble weight;
  if(!subflow->valid) return;
  weight = (gdouble) subflow->sending_target / (gdouble) wdata->valid_sum;
  weight *= (gdouble) SCHTREE_MAX_VALUE;
  wdata->total_weight+=subflow->weight_for_tree = weight;
}


void _create_nodes(Subflow *subflow, gpointer data)
{
  CreateData *cdata = data;
  if(!subflow->valid) return;
  if(cdata->remained){
    subflow->weight_for_tree += cdata->remained;
    cdata->remained = 0;
  }
  subflow->weight = (gdouble)subflow->weight_for_tree / (gdouble)SCHTREE_MAX_VALUE;
  cdata->root->remained -= _schtree_insert(cdata->root, &subflow->weight_for_tree, subflow, SCHTREE_MAX_VALUE);
}

SchNode *
_tree_ctor (StreamSplitter *this)
{
  CreateData cdata;
  WeightData wdata;
  SumData sdata;

  sdata.valid = wdata.total_weight = 0;
  cdata.root = NULL;
  sdata.total = 0;
  _iterate_subflows(this, _refresh_flags, NULL);
  _iterate_subflows(this, _summarize_sending_rates, &sdata);
  sdata.valid = 0;
  _iterate_subflows(this, _validate_sending_rates, &sdata);
  wdata.valid_sum = sdata.valid;
  _iterate_subflows(this, _setup_sending_weights, &wdata);
  cdata.remained = SCHTREE_MAX_VALUE - wdata.total_weight;
  cdata.root = _make_schnode(SCHTREE_MAX_VALUE);
  _iterate_subflows(this, _create_nodes, &cdata);
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
//  pointerpool_add(this->pointerpool, node);
//  g_slice_free(SchNode, node);

  mprtp_free(node);
}


gint
_schtree_insert (SchNode * node, gint * value, Subflow * subflow, gint level_value)
{
  gint dvalue = 0;
  if(!node->remained || *value == 0){
    goto done;
  }

  if(node->remained <= *value && !node->left && !node->right){
    *value -= node->remained;
    dvalue =  node->remained;
    node->subflows = g_list_prepend(node->subflows, subflow);
    goto done;
  }

  if(!node->left){
    node->left = _make_schnode(level_value>>1);
    node->left->parent = node;
  }
  if(0 < node->left->remained){
    node->left->subflows = g_list_prepend(node->left->subflows, subflow);
    dvalue += _schtree_insert(node->left, value, subflow, level_value>>1);
  }

  if(*value == 0){
    goto done;
  }

  if(!node->right){
    node->right = _make_schnode(level_value>>1);
    node->right->parent = node;
  }
  if(0 < node->right->remained){
    node->right->subflows = g_list_prepend(node->right->subflows, subflow);
    dvalue += _schtree_insert(node->right, value, subflow, level_value>>1);
  }

done:
  node->remained -= dvalue;
  return dvalue;
}

gboolean _allowed(SchNode *node, GstRTPBuffer *rtp, guint8 flag_restriction)
{
  GList *it;
  Subflow *subflow;

  for(it = node->subflows; it; it = it->next){
    subflow = it->data;
    if(flag_restriction <= subflow->flags_value && mprtps_path_approve_request(subflow->path, rtp->buffer)){
      return TRUE;
    }
  }
  return FALSE;
}


SchNode *_make_schnode(gint remained)
{
  SchNode *result;
  result = _schnode_ctor();
  result->remained = remained;
  return result;
}

gpointer
_schnode_ctor (void)
{
//  SchNode *result = g_slice_new0(SchNode);
  SchNode *result    = mprtp_malloc(sizeof(SchNode));
  result->left       = NULL;
  result->right      = NULL;
  result->subflows   = NULL;
  result->sent_bytes = 0;
  result->remained   = 0;
  return result;
}


SchNode *
_schtree_select_next (SchNode * root, GstRTPBuffer * rtp, guint8 flag_restriction)
{
  SchNode *selected, *left, *right;
  gboolean left_allowed,right_allowed;

  selected = root;
  while (selected->left != NULL && selected->right != NULL) {
    left          = selected->left;
    right         = selected->right;
    left_allowed  = _allowed(left, rtp, flag_restriction);
    right_allowed = _allowed(right, rtp, flag_restriction);

    if(!left_allowed && !right_allowed){
      selected = NULL;
      goto done;
    }
    if(!left_allowed){
      selected = right;
    }else if(!right_allowed){
      selected = left;
    }else{
      selected = left->sent_bytes <= right->sent_bytes ? left : right;
    }
  }
  if(!selected->subflows){
    g_warning("Problems with subflows at stream splitter");
  }else if(!_allowed(selected, rtp, flag_restriction)){
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


Subflow *
_schtree_get_next (SchNode * root, GstRTPBuffer * rtp, guint8 flag_restriction)
{
  Subflow *result = NULL;
  SchNode *selected, *left, *right;
  gboolean left_allowed,right_allowed;
  guint32 bytes_to_send;

  bytes_to_send = gst_rtp_buffer_get_payload_len (rtp);
  selected = root;
  while (selected->left != NULL && selected->right != NULL) {
    left          = selected->left;
    right         = selected->right;
    left_allowed  = _allowed(left, rtp, flag_restriction);
    right_allowed = _allowed(right, rtp, flag_restriction);

    if(!left_allowed && !right_allowed){
      goto done;
    }
    selected->sent_bytes += bytes_to_send;
    if(!left_allowed){
      selected = right;
    }else if(!right_allowed){
      selected = left;
    }else{
      selected = left->sent_bytes <= right->sent_bytes ? left : right;
    }
  }
  selected->sent_bytes += bytes_to_send;
  if(!selected->subflows){
    g_warning("Problems with subflows at stream splitter");
  }
  result = selected->subflows->data;
done:
  return result;
}


Subflow *
_make_subflow (MPRTPSPath * path)
{

  Subflow *result; // = g_slice_new0(Subflow);
  result = mprtp_malloc(sizeof(Subflow));
  result->path = path;
  result->flags_value = 0;
  return result;
}



static void _log_tree (SchNode * node, gint top, gint level)
{
  gint i;
  if (node == NULL) {
    return;
  }
  for (i = 0; i < level; ++i)
    mprtp_logger ("streamsplitter.log","--");
  if (node->subflows != NULL) {
      GList *it;
      mprtp_logger ("streamsplitter.log",
             "%d->sent_bytes:%d (L:%p,R:%p) subflows:",
             top >> level,
             node->sent_bytes,
             node->left,
             node->right);
    for(it = node->subflows; it; it = it->next){
        mprtp_logger ("streamsplitter.log","%d ", ((Subflow*)it->data)->id);
    }
    mprtp_logger ("streamsplitter.log","\n");
  } else {
      mprtp_logger ("streamsplitter.log","%d->C:%d\n", top >> level, node->sent_bytes);
  }
  _log_tree (node->left, top, level + 1);
  _log_tree (node->right, top, level + 1);
}


static void _log_subflow(Subflow *subflow, gpointer data)
{
  mprtp_logger("streamsplitter.log",
               "----------------------------------------------------------------\n"
               "Subflow id: %d\n"
               "Sending target: %d | weight: %f\n",

               subflow->id,
               subflow->sending_target,
               subflow->weight
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
               this->active_subflow_num
               );

  _iterate_subflows(this, _log_subflow, this);
  _log_tree(this->tree, SCHTREE_MAX_VALUE, 0);
}


#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef SCHTREE_MAX_VALUE
