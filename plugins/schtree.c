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
#include "schtree.h"

GST_DEBUG_CATEGORY_STATIC (schtree_debug_category);
#define GST_CAT_DEFAULT schtree_debug_category

G_DEFINE_TYPE (SchTree, schtree, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------
static void schtree_finalize(GObject *object);
static MPRTPSSubflow* schtree_get_actual(SchTree* tree);
static guint16 schtree_get_max_value(SchTree* tree);
static MPRTPSSubflow* schtree_get_next(SchTree* tree);
static void schtree128_change(SchNode **root,
	MPRTPSSubflow *path, gint change, gint *actual);
static void _schtree_delete(SchNode* parent, SchNode* node,
	MPRTPSSubflow* searched_value, gint *change, gint level_value);
static void _schtree_insert(SchNode** node,
	MPRTPSSubflow* path, gint* change, gint level_value);
static void schtree_commit_changes(SchTree *tree);
static void _schnode_overlap_trees(SchNode *old_node, SchNode *new_node);
static void schtree_setup_change(SchTree *tree,
	MPRTPSSubflow* path, guint32 value);
static void schtree_print(SchTree* tree);

static void _schnode_rdtor(SchNode* node);
static SchNode* _schnode_ctor();
static void _print_tree(SchNode* node, gint top, gint level);
//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

static SchNode *_schnode_ctor();

void
schtree_class_init (SchTreeClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = schtree_finalize;

  GST_DEBUG_CATEGORY_INIT (schtree_debug_category, "schtree", 0, "Scheduling Tree");
}

void
schtree_finalize(GObject *object)
{

}

void
schtree_init(SchTree* tree)
{
  gint i;
  tree->setup_change= schtree_setup_change;
  tree->commit_changes = schtree_commit_changes;
  tree->get_actual = schtree_get_actual;
  //tree->get_max_value = schtree_get_max_value;
  tree->get_next = schtree_get_next;
  tree->print = schtree_print;
  tree->max_value = 128;
  tree->root = NULL;
  for(i=0; i<MPRTP_SENDER_SCHTREE_MAX_PATH_NUM; ++i){
	  tree->paths[i] = NULL;
	  tree->path_values[i] = 0;
  }
}

static void
_schtree_loadup(SchNode* node, MPRTPSSubflow *path)
{

  if(node == NULL || node->path != NULL){
    return;
  }
  if(node->left == NULL){
    node->left = _schnode_ctor();
    node->left->path = path;
  }else{
    _schtree_loadup(node->left, path);
  }

  if(node->right == NULL){
    node->right = _schnode_ctor();
    node->right->path = path;
  }else{
	_schtree_loadup(node->right, path);
  }
}

void
schtree_commit_changes(SchTree *tree)
{
  gint32 index, max_index = 0, path_num = 0;
  guint16 sum = 0;
  gint max_value = 0, *value, actual_value, delta_value;
  SchNode *new_root = NULL;
  MPRTPSSubflow *path;
  g_rw_lock_writer_lock (&tree->rwmutex);

  for(index = 0; index < MPRTP_SENDER_SCHTREE_MAX_PATH_NUM &&
      tree->paths[index] != NULL; ++index, ++path_num)
  {
	actual_value = (gint)tree->path_values[index] + tree->path_delta_values[index];
	tree->path_values[index] = actual_value > 0 ? actual_value : 1;
    if(max_value < actual_value){
      max_index = index;
    }
    sum += actual_value;
  }

  for(index = 0; index < path_num; ++index)
  {

	path = tree->paths[index];

	actual_value = (gfloat) tree->path_values[index] /
				   (gfloat) sum * (gfloat) tree->max_value;

     //g_print("insert: %d / %d * %d = %d\n",
    	//	 tree->path_values[index], sum, tree->max_value, actual_value);

     _schtree_insert(&new_root, path, &actual_value, tree->max_value);
  }

  //check 128 integrity
  _schnode_rdtor(tree->root);
  tree->root = new_root;
  _schtree_loadup(tree->root, tree->paths[max_index]);
  //tree->print(tree);
  g_rw_lock_writer_unlock (&tree->rwmutex);

}

void _schnode_rdtor(SchNode* node)
{
  if(node == NULL){
    return;
  }
  _schnode_rdtor(node->left);
  _schnode_rdtor(node->right);
  g_free(node);
}

void _schnode_overlap_trees(SchNode *old_node, SchNode *new_node)
{
  if(old_node == NULL || new_node == NULL){
    return;
  }
  if(old_node->path != NULL || new_node->path != NULL){
    return;
  }
  if(old_node->next == old_node->right){
    new_node->next = new_node->right;
  }
  _schnode_overlap_trees(old_node->left, new_node->left);
  _schnode_overlap_trees(old_node->right, new_node->right);
}

void
schtree_setup_change(SchTree *tree, MPRTPSSubflow* path, guint32 value)
{
  gint32 index;
  g_rw_lock_writer_lock (&tree->rwmutex);
  for(index = 0;
	  index < MPRTP_SENDER_SCHTREE_MAX_PATH_NUM && tree->paths[index] != path;
	  ++index);

  if(index < MPRTP_SENDER_SCHTREE_MAX_PATH_NUM){
    tree->path_delta_values[index] = value - tree->path_values[index];
    goto schtree_set_path_and_values_done;
  }
  for(index = 0; tree->paths[index] != NULL &&
                 index < MPRTP_SENDER_SCHTREE_MAX_PATH_NUM;
      ++index);

  if(index == MPRTP_SENDER_SCHTREE_MAX_PATH_NUM){
    goto schtree_set_path_and_values_done;
  }

  tree->paths[index] = path;
  tree->path_delta_values[index] = value;

schtree_set_path_and_values_done:
  //g_print("value: %d, %p path value: %d, delta value: %d\n",
	//	  value, path, tree->path_values[index], tree->path_delta_values[index]);
  g_rw_lock_writer_unlock (&tree->rwmutex);
}

void
schtree128_change(SchNode **root, MPRTPSSubflow *path, gint change, gint *actual)
{

  if(actual != NULL){
    *actual += change;
  }

  if(change < 0){
    int value = -1*change;
    _schtree_delete(NULL, *root, path, &value, 128);
    change = -1*value;
  }
  if(change > 0){
    _schtree_insert(root, path, &change, 128);
  }
}

void
_schtree_insert(SchNode** node, MPRTPSSubflow* path, gint* change, gint level_value)
{
  if(*node == NULL){
    *node = _schnode_ctor();
  }
  if((*node)->path != NULL){
    return;
  }
  if(*change >= level_value && (*node)->left == NULL && (*node)->right == NULL){
    *change-=level_value;
    (*node)->path = path;
    return;
  }

  _schtree_insert(&(*node)->left, path, change, level_value>>1);
  if(*change < 1){
    return;
  }
  _schtree_insert(&(*node)->right, path, change, level_value>>1);
}


void
_schtree_delete(SchNode* parent, SchNode* node,
	MPRTPSSubflow* searched_value, gint *change, gint level_value)
{
  if(node == NULL || *change < 1){
    return;
  }
  if(node->path != NULL){
    if(node->path != searched_value){
      return;
    }
    g_free(node);
    *change -= level_value;
    if(parent == NULL){
      return;
    }
    if(parent->left == node){
      parent->left = NULL;
    }else{
      parent->right = NULL;
    }
    return;
  }
  if(node->left != NULL && node->left->path != NULL){
	_schtree_delete(node, node->right, searched_value, change, level_value>>1);
	_schtree_delete(node, node->left, searched_value, change, level_value>>1);
  }else if(node->right != NULL){
	_schtree_delete(node, node->left, searched_value, change, level_value>>1);
	_schtree_delete(node, node->right, searched_value, change, level_value>>1);
  }
  if(node->left != NULL || node->right != NULL || parent == NULL){
    return;
  }
  g_free(node);
  if(parent->left == node){
    parent->left = NULL;
  }else{
    parent->right = NULL;
  }
}

SchNode *_schnode_ctor()
{
  SchNode* result = (SchNode*) g_malloc0(sizeof(SchNode));
  result->left = NULL;
  result->right = NULL;
  result->next = NULL;
  result->path = NULL;
  return result;
}

MPRTPSSubflow*
schtree_get_next(SchTree* tree)
{
	MPRTPSSubflow *result;
	SchNode* selected;
	g_rw_lock_writer_lock (&tree->rwmutex);
	selected = tree->root;
	while(selected->path == NULL){
	  selected->next = (selected->next == selected->left) ? selected->right : selected->left;
	  selected = selected->next;
	  //g_print("node: %p l->%p r->%p p: %p\n",
		//	  selected, selected->left, selected->right, selected->path);
	}
	result = selected->path;
	//g_print("selected: %p->path:%p\n", selected, selected->path);
	g_rw_lock_writer_unlock (&tree->rwmutex);
	return result;
}

MPRTPSSubflow*
schtree_get_actual(SchTree* tree)
{
	MPRTPSSubflow *result;
	SchNode* selected;
	g_rw_lock_writer_lock (&tree->rwmutex);
	selected = tree->root;
	while(selected->path == NULL){
	  selected->next = (selected->next == NULL) ? selected->left : selected->next;
	  selected = selected->next;
	  //g_print("node: %p l->%p r->%p p: %p\n",
		//	  selected, selected->left, selected->right, selected->path);
	}
	result = selected->path;
	//g_print("selected: %p->path:%p\n", selected, selected->path);
	g_rw_lock_writer_unlock (&tree->rwmutex);
	return result;
}

guint16
schtree_get_max_value(SchTree* tree)
{
  guint16 result;
  g_rw_lock_reader_lock (&tree->rwmutex);
  result = tree->max_value;
  g_rw_lock_reader_unlock (&tree->rwmutex);
  return result;
}

void schtree_print(SchTree* tree)
{
  _print_tree(tree->root, 128, 0);
}

void _print_tree(SchNode* node, gint top, gint level)
{
  if(node == NULL){
    return;
  }
  gint i;

  for(i = 0; i < level; ++i) g_print("--");
  g_print("%d:", level);
  g_print("%p (%p)\n", node->path, top>>level);
  _print_tree(node->left, top, level+1);
  _print_tree(node->right, top, level+1);
}


/*

void
schtree_commit_changes(SchTree *tree)
{
  gint32 index, max_index = 0, path_num = 0;
  guint16 sum = 0;
  gint max_value = 0, *value, actual_value, delta_value;
  SchNode *new_root = NULL;
  MPRTPSSubflow *path;
  g_rw_lock_writer_lock (&tree->rwmutex);

  for(index = 0; index < MPRTP_SENDER_SCHTREE_MAX_PATH_NUM &&
      tree->paths[index] != NULL; ++index, ++path_num)
  {
	actual_value = (gint)tree->path_values[index] + tree->path_delta_values[index];
    if(max_value < actual_value){
      max_index = index;
    }
    sum += actual_value;
  }

  for(index = 0; index < path_num; ++index)
  {
    if(tree->path_delta_values[index] >= 0){
      continue;
    }

    path = tree->paths[index];

    actual_value = (gfloat) tree->path_values[index] /
	  	           (gfloat) sum * (gfloat) tree->max_value;

    delta_value = (gfloat) tree->path_delta_values[index] /
  	  	          (gfloat) sum * (gfloat) tree->max_value * -1.0;

    g_print("delete: %d / %d * %d = %d\n",
    	tree->path_delta_values[index], sum, tree->max_value, delta_value);

    _schtree_delete(NULL, tree->root, path, &delta_value, tree->max_value);
    tree->path_values[index] += tree->path_delta_values[index];
  }

  for(index = 0; index < path_num; ++index){
	if(tree->path_delta_values[index] <= 0){
	  continue;
	}

	path = tree->paths[index];

	actual_value = (gfloat) tree->path_values[index] /
				   (gfloat) sum * (gfloat) tree->max_value;

	delta_value = (gfloat) tree->path_delta_values[index] /
				  (gfloat) sum * (gfloat) tree->max_value;

     g_print("insert: %d / %d * %d = %d\n",
        	tree->path_delta_values[index], sum, tree->max_value, delta_value);

     _schtree_insert(&tree->root, path, &delta_value, tree->max_value);
	 tree->path_values[index] += tree->path_delta_values[index];
  }

  //check 128 integrity
  _schtree_loadup(tree, tree->paths + max_index);
  tree->print(tree);
  g_rw_lock_writer_unlock (&tree->rwmutex);

}
 */
