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

#define SCHTREE_MAX_VALUE 128
#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

/* class initialization */
G_DEFINE_TYPE (StreamSplitter, stream_splitter, G_TYPE_OBJECT);

typedef struct _Subflow Subflow;

struct _SchNode
{
  SchNode *left;
  SchNode *right;
  //SchNode*     next;
  MPRTPSPath *path;
  guint32 sent_bytes;
  guint32 sent_frames;
  guint32 *decision_value;
};

struct _Subflow
{
  MPRTPSPath *path;
  guint32 actual_bid;
  guint32 new_bid;
};

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------
//Functions realted to stream_splitter
static void stream_splitter_finalize (GObject * object);
//static void _full_tree_commit (StreamSplitter * this, guint32 bid_sum);
//static void _keyframes_tree_commit (StreamSplitter * this, guint32 bid_sum,
//    MPRTPSPathState filter);

static void
_schtree_set_scheduling_mode(SchNode *node, StreamSplittingMode mode);
static void
_tree_commit (StreamSplitter *this,
              SchNode ** tree, GHashTable * subflows,
              guint filter, guint32 bid_sum);
static void _schnode_reduction (SchNode * node, guint reduction);
//Functions related to tree
static SchNode *_schnode_ctor (void);
static void _schtree_insert (SchNode ** node,
    MPRTPSPath * path, gint * change, gint level_value);
static void _schnode_rdtor (SchNode * node);
//static void _print_tree (SchNode * root, gint top, gint level);
//static MPRTPSPath* schtree_get_actual (SchNode * root);
static MPRTPSPath *schtree_get_next (SchNode * root,
                                     guint32 bytes_to_send,
                                     guint32 frames_to_send);
static void stream_splitter_run (void *data);
//static void _print_tree_stat(SchNode *root);

static Subflow *make_subflow (MPRTPSPath * path);


static MPRTPSPath *_get_next_path (StreamSplitter * this, GstRTPBuffer * rtp);
//static void _print_tree (SchNode * node, gint top, gint level);
//void
//_print_tree (SchNode * node, gint top, gint level)
//{
//  gint i;
//  if (node == NULL) {
//    return;
//  }
//  for (i = 0; i < level; ++i)
//    g_print ("--");
//  if (node->path != NULL) {
//    g_print ("%d->%d:%d\n", top >> level, mprtps_path_get_id (node->path),
//        node->sent_bytes);
//  } else {
//    g_print ("%d->C:%d\n", top >> level, node->sent_bytes);
//  }
//  _print_tree (node->left, top, level + 1);
//  _print_tree (node->right, top, level + 1);
//}

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

void
stream_splitter_finalize (GObject * object)
{
  StreamSplitter *this = STREAM_SPLITTER (object);
  g_hash_table_destroy (this->subflows);
  gst_task_stop (this->thread);
  gst_task_join (this->thread);
  gst_object_unref (this->thread);

  g_object_unref (this->sysclock);
}

void
stream_splitter_init (StreamSplitter * this)
{
  this->new_path_added = FALSE;
  this->changes_are_committed = FALSE;
  this->path_is_removed = FALSE;
  this->sysclock = gst_system_clock_obtain ();
  this->active_subflow_num = 0;
  this->ext_header_id = MPRTP_DEFAULT_EXTENSION_HEADER_ID;
  this->subflows = g_hash_table_new_full (NULL, NULL, NULL, g_free);
  this->charge_value = 100;
  this->thread = gst_task_new (stream_splitter_run, this, NULL);
    this->splitting_mode = MPRTP_STREAM_FRAME_BASED_SPLITTING;
//  this->splitting_mode = MPRTP_STREAM_BYTE_BASED_SPLITTING;

  g_rw_lock_init (&this->rwmutex);
  g_rec_mutex_init (&this->thread_mutex);

  gst_task_set_lock (this->thread, &this->thread_mutex);
  gst_task_start (this->thread);


}

void
stream_splitter_add_path (StreamSplitter * this, guint8 subflow_id,
    MPRTPSPath * path)
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
  lookup_result = make_subflow (path);
  g_hash_table_insert (this->subflows, GINT_TO_POINTER (subflow_id),
      lookup_result);
  this->new_path_added = TRUE;
  ++this->active_subflow_num;
  GST_DEBUG ("Subflow is added, the actual number of subflow is: %d",
      this->active_subflow_num);
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
  this->path_is_removed = TRUE;
  --this->active_subflow_num;
  GST_DEBUG ("Subflow is removed, the actual number of subflow is: %d",
      this->active_subflow_num);
exit:
  THIS_WRITEUNLOCK (this);
}

void
stream_splitter_set_rtp_ext_header_id (StreamSplitter * this,
    guint8 ext_header_id)
{
  THIS_WRITELOCK (this);
  this->ext_header_id = ext_header_id;
  THIS_WRITEUNLOCK (this);
}

void
stream_splitter_set_splitting_mode (StreamSplitter * this,
                                    StreamSplittingMode mode)
{
  THIS_WRITELOCK (this);
  this->splitting_mode = mode;
  THIS_WRITEUNLOCK (this);
}


void
stream_splitter_setup_sending_bid (StreamSplitter * this, guint8 subflow_id,
    guint32 bid)
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
  subflow->new_bid = bid;
exit:
  THIS_WRITEUNLOCK (this);
}

void
stream_splitter_commit_changes (StreamSplitter * this)
{
  THIS_WRITELOCK (this);
  this->changes_are_committed = TRUE;
  THIS_WRITEUNLOCK (this);
}

//Here the buffer must be writeable and must be rtp
//gboolean
//stream_splitter_process_rtp_packet (StreamSplitter * this, GstRTPBuffer * rtp)
//{
//  MPRTPSPath *path;
//  gboolean result = TRUE;
//  guint32 bytes_to_send;
//  guint32 keytree_value, non_keytree_value;
//  SchNode *tree;
//
//  THIS_WRITELOCK (this);
//  bytes_to_send = gst_rtp_buffer_get_payload_len(rtp);
//  if(this->keyframes_tree == NULL){
//    path = schtree_get_next (this->non_keyframes_tree, bytes_to_send);
//    goto process;
//  }else if(this->non_keyframes_tree == NULL){
//    path = schtree_get_next (this->keyframes_tree, bytes_to_send);
//    goto process;
//  }
//
//  if (!GST_BUFFER_FLAG_IS_SET (rtp->buffer, GST_BUFFER_FLAG_DELTA_UNIT))
//  {
//    path = schtree_get_next (this->keyframes_tree, bytes_to_send);
//    goto process;
//  }
//
//  keytree_value = this->keyframes_tree->sent_bytes * this->non_keyframe_ratio;
//  non_keytree_value = this->non_keyframes_tree->sent_bytes * this->keyframe_ratio;
//  tree = keytree_value <= non_keytree_value ? this->keyframes_tree : this->non_keyframes_tree;
//
//  path = schtree_get_next (tree, bytes_to_send);
//
//
//process:
//  mprtps_path_process_rtp_packet (path, this->ext_header_id, rtp);
//done:
//  THIS_WRITEUNLOCK (this);
//  return result;
//}


MPRTPSPath *
stream_splitter_get_next_path (StreamSplitter * this, GstBuffer * buf)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  MPRTPSPath *path = NULL;

  if (G_UNLIKELY (!gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp))) {
    GST_WARNING_OBJECT (this, "The RTP packet is not writeable");
    goto exit;
  }
  THIS_WRITELOCK (this);
  if (this->keyframes_tree == NULL && this->non_keyframes_tree == NULL) {
    //vagy várni egy kis ideig....
    GST_WARNING_OBJECT (this, "No active subflow");
    goto done;
  }
  path = _get_next_path (this, &rtp);
done:
  gst_rtp_buffer_unmap (&rtp);
  THIS_WRITEUNLOCK (this);
exit:
  return path;
}


void
stream_splitter_run (void *data)
{
  StreamSplitter *this;
  GstClockTime now;
  GstClockID clock_id;
  GstClockTime next_scheduler_time;
  GstClockTime interval;
  MPRTPSPathState path_state;
  gdouble rand;
  GHashTableIter iter;
  gpointer key, val;
  Subflow *subflow;
  MPRTPSPath *path;
  guint32 bid_total_sum = 0, bid_nc_sum = 0, bid_mc_sum = 0, bid_c_sum = 0;

  this = STREAM_SPLITTER (data);
  now = gst_clock_get_time (this->sysclock);

  THIS_WRITELOCK (this);

  if (!this->new_path_added &&
      !this->path_is_removed && !this->changes_are_committed) {
    goto done;
  }

  g_hash_table_iter_init (&iter, this->subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    //printf("key %u ---> %u\n", (guint8)*subflow_id, (MPRTPSPath*)*subflow);
    //subflow_id = *((guint8*) key);
    subflow = (Subflow *) val;
    path = subflow->path;
    if (mprtps_path_is_new (path)) {
      mprtps_path_set_not_new (path);
      mprtps_path_set_non_congested (path);
      mprtps_path_set_non_lossy (path);
      subflow->actual_bid = this->charge_value;
    }
    if (subflow->new_bid > 0) {
      subflow->actual_bid = subflow->new_bid;
      subflow->new_bid = 0;
    }
    if (subflow->actual_bid == 0) {
      GST_WARNING_OBJECT (this, "The actual bid can not be 0, so it set to 1.");
      subflow->actual_bid = 1;
    }
    bid_total_sum += subflow->actual_bid;
    path_state = mprtps_path_get_state (path);
    if (path_state == MPRTPS_PATH_STATE_NON_CONGESTED) {
      bid_nc_sum += subflow->actual_bid;
    } else if (path_state == MPRTPS_PATH_STATE_LOSSY) {
      bid_mc_sum += subflow->actual_bid;
    } else if (path_state == MPRTPS_PATH_STATE_CONGESTED) {
      bid_c_sum += subflow->actual_bid;
    }

//    g_print("Subflow %d actual bid is: %u\n",mprtps_path_get_id(path), subflow->actual_bid);
  }

  if (bid_nc_sum > 0) {
    GST_DEBUG_OBJECT (this, "Non-congested paths exists, "
        "the bid is: %d the total bid is: %d", bid_nc_sum, bid_total_sum);
    _tree_commit (this, &this->keyframes_tree, this->subflows,
        MPRTPS_PATH_STATE_NON_CONGESTED, bid_nc_sum);
    if (bid_mc_sum > 0 || bid_c_sum > 0) {
      GST_DEBUG_OBJECT (this, "Middly or congested paths exists, "
          "the bid for mc is: %d, for c is: %d", bid_mc_sum, bid_c_sum);
      _tree_commit (this, &this->non_keyframes_tree,
          this->subflows,
          MPRTPS_PATH_STATE_CONGESTED | MPRTPS_PATH_STATE_LOSSY,
          bid_c_sum + bid_mc_sum);
      this->non_keyframe_ratio = (bid_mc_sum + bid_c_sum) / bid_total_sum;
      this->keyframe_ratio = 1. - this->non_keyframe_ratio;
    } else {
      GST_DEBUG_OBJECT (this, "Neither middly nor congested paths exists");
      _schnode_rdtor (this->non_keyframes_tree);
      this->non_keyframes_tree = NULL;
      this->non_keyframe_ratio = 0.;
      this->keyframe_ratio = 1.;
    }
  } else if (bid_mc_sum > 0) {
    GST_DEBUG_OBJECT (this, "Middly-congested paths exists but "
        "no non-congested available, "
        "the bid is: %d the total bid is: %d", bid_mc_sum, bid_total_sum);
    _tree_commit (this, &this->keyframes_tree, this->subflows,
        MPRTPS_PATH_STATE_LOSSY, bid_mc_sum);
    if (bid_c_sum > 0) {
      GST_DEBUG_OBJECT (this, "Congested paths exists, the bid is: %d",
          bid_c_sum);
      _tree_commit (this, &this->non_keyframes_tree,
          this->subflows, MPRTPS_PATH_STATE_CONGESTED, bid_c_sum);
      this->non_keyframe_ratio = bid_c_sum / bid_total_sum;
      this->keyframe_ratio = 1. - this->non_keyframe_ratio;
    } else {
      GST_DEBUG_OBJECT (this, "No congested paths exists");
      _schnode_rdtor (this->non_keyframes_tree);
      this->non_keyframes_tree = NULL;
      this->non_keyframe_ratio = 0.;
      this->keyframe_ratio = 1.;
    }
  } else if (bid_c_sum > 0) {
    GST_DEBUG_OBJECT (this, "Only congested path exists the bid is: %d",
        bid_c_sum);
    _tree_commit (this, &this->non_keyframes_tree, this->subflows,
        MPRTPS_PATH_STATE_CONGESTED, bid_c_sum);
    _schnode_rdtor (this->keyframes_tree);
    this->keyframes_tree = NULL;
    this->non_keyframe_ratio = 1.;
    this->keyframe_ratio = 0.;
  } else {
    _schnode_rdtor (this->keyframes_tree);
    this->keyframes_tree = NULL;
    _schnode_rdtor (this->non_keyframes_tree);
    this->non_keyframes_tree = NULL;
  }
//  g_print ("NON_KEYFRAMES_TREE\n");
//  _print_tree (this->non_keyframes_tree, 128, 0);
//  g_print ("KEYFRAMES_TREE\n");
//  _print_tree (this->keyframes_tree, 128, 0);
//  g_print("full_tree:\n"); _print_tree(this->full_tree, SCHTREE_MAX_VALUE, 0);
//  g_print("keyframes_tree:\n"); _print_tree(this->keyframes_tree, SCHTREE_MAX_VALUE, 0);

  this->new_path_added = FALSE;
  this->path_is_removed = FALSE;
  this->changes_are_committed = FALSE;

done:
  if (this->active_subflow_num > 0) {
    rand = g_random_double ();
    interval = GST_SECOND * (0.5 + rand);
    next_scheduler_time = now + interval;

    GST_DEBUG_OBJECT (this, "Next scheduling interval time is %lu",
        GST_TIME_AS_MSECONDS (interval));

  } else {
    next_scheduler_time = now + GST_MSECOND * 10;
  }
  clock_id = gst_clock_new_single_shot_id (this->sysclock, next_scheduler_time);

  THIS_WRITEUNLOCK (this);

  if (gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED) {
    GST_WARNING_OBJECT (this, "The scheduler clock wait is interrupted");
  }
  gst_clock_id_unref (clock_id);
}



MPRTPSPath *
_get_next_path (StreamSplitter * this, GstRTPBuffer * rtp)
{
  MPRTPSPath *result = NULL;
  guint32 new_frame, bytes_to_send;
  guint32 keytree_value, non_keytree_value;
  SchNode *tree;

  new_frame = this->last_rtp_timestamp != gst_rtp_buffer_get_timestamp(rtp) ? 1 : 0;
  bytes_to_send = gst_rtp_buffer_get_payload_len (rtp);
//  g_print("bytes to send: %u\n",bytes_to_send);
  if (this->keyframes_tree == NULL) {
    result = schtree_get_next (this->non_keyframes_tree, bytes_to_send, new_frame);
    goto done;
  } else if (this->non_keyframes_tree == NULL) {
    result = schtree_get_next (this->keyframes_tree, bytes_to_send, new_frame);
    goto done;
  }

  if (!GST_BUFFER_FLAG_IS_SET (rtp->buffer, GST_BUFFER_FLAG_DELTA_UNIT)) {
      result = schtree_get_next (this->keyframes_tree, bytes_to_send, new_frame);
    goto done;
  }

  keytree_value = this->keyframes_tree->sent_bytes * this->non_keyframe_ratio;
  non_keytree_value =
      this->non_keyframes_tree->sent_bytes * this->keyframe_ratio;
  tree =
      keytree_value <=
      non_keytree_value ? this->keyframes_tree : this->non_keyframes_tree;

    result = schtree_get_next (tree, bytes_to_send, new_frame);

done:
  return result;
}


//tree functions
static guint32
_schtree_loadup (SchNode * node,
    MPRTPSPath * max_bid_path, guint32 * min_sent_bytes,
    guint level, guint32 *sent_frames)
{
  guint32 sent_bytes = 0;
  guint32 frames_num = 0;
  if (node == NULL) {
    return 0;
  }

  if (node->path != NULL) {
    if (sent_bytes < *min_sent_bytes) {
      *min_sent_bytes = sent_bytes;
    }
    node->sent_bytes =
        mprtps_path_get_total_sent_payload_bytes (node->path) >> level;
    node->sent_frames =
        mprtps_path_get_total_sent_frames_num(node->path) >> level;
    *sent_frames += node->sent_frames;
    return node->sent_bytes;
  }

  if (node->left == NULL) {
    node->left = _schnode_ctor ();
    node->left->path = max_bid_path;
    node->left->sent_bytes =
        mprtps_path_get_total_sent_payload_bytes (max_bid_path) >> level;
    node->left->sent_frames =
            mprtps_path_get_total_sent_frames_num(max_bid_path) >> level;
    g_warning ("Schtree is not full");
  } else {
    sent_bytes += _schtree_loadup (node->left, max_bid_path,
        min_sent_bytes, level + 1, &frames_num);
  }

  if (node->right == NULL) {
    node->right = _schnode_ctor ();
    node->right->path = max_bid_path;
    node->right->sent_bytes =
        mprtps_path_get_total_sent_payload_bytes (max_bid_path) >> level;
    node->right->sent_frames =
            mprtps_path_get_total_sent_frames_num(max_bid_path) >> level;
    g_warning ("Schtree is not full");
  } else {
    sent_bytes += _schtree_loadup (node->right, max_bid_path,
        min_sent_bytes, level + 1, &frames_num);
  }

  node->sent_bytes = sent_bytes;
  node->sent_frames = frames_num;
  return sent_bytes;
}

void
_schnode_reduction (SchNode * node, guint reduction)
{
  if (node == NULL) {
    return;
  }
  node->sent_bytes >>= reduction;
  _schnode_reduction (node->left, reduction);
  _schnode_reduction (node->right, reduction);
}


void _schtree_set_scheduling_mode(SchNode *node, StreamSplittingMode mode)
{
  if(!node) return;
  node->decision_value = mode == MPRTP_STREAM_FRAME_BASED_SPLITTING ?
      &node->sent_frames : &node->sent_bytes;
  _schtree_set_scheduling_mode(node->left, mode);
  _schtree_set_scheduling_mode(node->right, mode);
}

void
_tree_commit (StreamSplitter *this, SchNode ** tree,
    GHashTable * subflows, guint filter, guint32 bid_sum)
{
  gdouble actual_value;
  gint insert_value;
  SchNode *new_root = NULL;
  MPRTPSPath *path, *path_with_largest_bid = NULL;
  guint32 largest_bid = 0;
  Subflow *subflow;
  gpointer val, key;
  GHashTableIter iter;
  guint32 min_bytes_sent = 4294967295;
  //guint8 subflow_id;

  g_hash_table_iter_init (&iter, subflows);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    subflow = (Subflow *) val;
    path = subflow->path;

    if ((mprtps_path_get_state (path) & filter) == 0) {
      continue;
    }
    if (largest_bid < subflow->actual_bid) {
      largest_bid = subflow->actual_bid;
      path_with_largest_bid = path;
    }
    actual_value =
        (gdouble) subflow->actual_bid /
        (gdouble) bid_sum *(gdouble) SCHTREE_MAX_VALUE;
    insert_value = (gint) roundf (actual_value);
//    g_print("SCHTREE INSERT: %u<-%d\n", subflow->actual_bid, subflow->path->id);
    _schtree_insert (&new_root, path, &insert_value, SCHTREE_MAX_VALUE);
  }


  //check 128 integrity
  _schnode_rdtor (*tree);
  *tree = new_root;
  new_root->sent_bytes = _schtree_loadup (*tree,
      path_with_largest_bid, &min_bytes_sent, 0, &new_root->sent_frames);
  if ((1 << 23) < min_bytes_sent) {
    _schnode_reduction (*tree, 23);
  }
  _schtree_set_scheduling_mode(new_root, this->splitting_mode);
}


void
_schnode_rdtor (SchNode * node)
{
  if (node == NULL) {
    return;
  }
  _schnode_rdtor (node->left);
  _schnode_rdtor (node->right);
  g_free (node);
}

void
_schtree_insert (SchNode ** node, MPRTPSPath * path, gint * change,
    gint level_value)
{
  if (*node == NULL) {
    *node = _schnode_ctor ();
  }
  if ((*node)->path != NULL || level_value < 1) {
    return;
  }
  if (*change >= level_value && (*node)->left == NULL && (*node)->right == NULL) {
    *change -= level_value;
    (*node)->path = path;
    return;
  }

  _schtree_insert (&(*node)->left, path, change, level_value >> 1);
  if (*change < 1) {
    return;
  }
  _schtree_insert (&(*node)->right, path, change, level_value >> 1);
}

SchNode *
_schnode_ctor (void)
{
  SchNode *result = (SchNode *) g_malloc0 (sizeof (SchNode));
  result->left = NULL;
  result->right = NULL;
//  result->next = NULL;
  result->path = NULL;
  return result;
}


MPRTPSPath *
schtree_get_next (SchNode * root, guint32 bytes_to_send, guint32 frames_to_send)
{
  MPRTPSPath *result;
  SchNode *selected, *left, *right;
  selected = root;
  while (selected->path == NULL) {
    left = selected->left;
    right = selected->right;
    selected->sent_bytes += bytes_to_send;
    selected->sent_frames += frames_to_send;
    selected = (*(left->decision_value) <= *(right->decision_value)) ? left : right;
  }
//  g_print("selected path: %d bytes: %u\n", selected->path->id, selected->sent_bytes);
  result = selected->path;
//  g_print("%d->", result->id);
  return result;
}

//
//
//MPRTPSPath *
//schtree_get_next (SchNode * root)
//{
//  MPRTPSPath *result;
//  SchNode *selected;
//  selected = root;
//  while (selected->path == NULL) {
//    selected->next =
//        (selected->next == selected->left) ? selected->right : selected->left;
//    selected = selected->next;
//  }
//  result = selected->path;
//  return result;
//}


Subflow *
make_subflow (MPRTPSPath * path)
{
  Subflow *result = g_malloc0 (sizeof (Subflow));
  result->path = path;
  return result;
}

#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef SCHTREE_MAX_VALUE
