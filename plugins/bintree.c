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
#include "bintree.h"
#include <math.h>
#include <string.h>

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)


GST_DEBUG_CATEGORY_STATIC (bintree_debug_category);
#define GST_CAT_DEFAULT bintree_debug_category

G_DEFINE_TYPE (BinTree, bintree, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void bintree_finalize (GObject * object);
static BinTreeNode * _insert(BinTree *this, BinTreeNode *actual, BinTreeNode *insert);
static BinTreeNode * _insert_into_tree(BinTree *this, BinTreeNode *actual, BinTreeNode **insert);
static BinTreeNode *
_search_value(BinTree *this, guint64 value, BinTreeNode **parent);
static void
_deref_from_tree (BinTree * this, guint64 value);
static void _refresh_top(BinTree *this);
static void _refresh_bottom(BinTree *this);
static BinTreeNode *
_pop_from_tree (BinTree * this, guint64 value);
static BinTreeNode *_get_rightest_value(BinTreeNode *node, BinTreeNode **parent);
static BinTreeNode *_get_leftest_value(BinTreeNode *node, BinTreeNode **parent);
static BinTreeNode *_make_bintreenode(BinTree *this, guint64 value);
static void _trash_bintreenode(BinTree *this, BinTreeNode *node);
static void _ruin_full(BinTree *this, BinTreeNode *node);


void static
_print_tree (BinTree * tree, BinTreeNode* node, gint level)
{
  gint i;
  if (node == NULL) {
    return;
  }
  if (!level)
    g_print ("Tree %p TOP is: %lu Counter: %u\n", tree, tree->top?tree->top->value : 0,
        tree->counter);
  for (i = 0; i < level && i < 10; ++i)
    g_print ("--");
  g_print ("%d->%p->value:%lu ->ref: %u ->left:%p ->right:%p\n",
      level, node, node->value, node->ref, node->left, node->right);
  _print_tree (tree, node->left, level + 1);
  _print_tree (tree, node->right, level + 1);
}

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
bintree_class_init (BinTreeClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = bintree_finalize;

  GST_DEBUG_CATEGORY_INIT (bintree_debug_category, "bintree", 0,
      "MpRTP Manual Sending Controller");

}

void
bintree_finalize (GObject * object)
{
  BinTree *this;
  this = BINTREE(object);
  while(!g_queue_is_empty(this->node_pool)){
    g_free(g_queue_pop_head(this->node_pool));
  }
  _ruin_full(this, this->root);
}

void
bintree_init (BinTree * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->node_pool = g_queue_new();
}

BinTree *make_bintree(BinTreeCmpFunc cmp)
{
  BinTree *result;
  result = g_object_new (BINTREE_TYPE, NULL);
  THIS_WRITELOCK (result);
  result->cmp = cmp;
  THIS_WRITEUNLOCK (result);

  return result;
}

static gint test_cmp(guint64 a, guint64 b)
{
  return a == b ? 0 : a < b ? -1 : 1;
}

void bintree_test(void)
{
  BinTree *tree1;
  BinTree *tree2;
  BinTreeNode *node;
  tree1 = make_bintree(test_cmp);
//  g_print("CREATED\n");
//  _print_tree(tree1, tree1->root, 0);
//  g_print("BASIC CASE. Insert 5,7,6,8,2,3,1\n");
//  bintree_insert_value(tree1, 5);
//  bintree_insert_value(tree1, 7);
//  bintree_insert_value(tree1, 6);
//  bintree_insert_value(tree1, 8);
//  bintree_insert_value(tree1, 3);
//  bintree_insert_value(tree1, 4);
//  bintree_insert_value(tree1, 2);
//  _print_tree(tree1, tree1->root, 0);
//  g_print("DELETE LEAF ITEM 2\n");
//  bintree_delete_value(tree1, 2);
//  _print_tree(tree1, tree1->root, 0);
//  g_print("DELETE ITEM 3\n");
//  bintree_delete_value(tree1, 3);
//  _print_tree(tree1, tree1->root, 0);
//  g_print("DELETE NODE 7\n");
//  bintree_delete_value(tree1, 7);
//  _print_tree(tree1, tree1->root, 0);
//  g_print("DELETE 5\n");
//  bintree_delete_value(tree1, 5);
//  _print_tree(tree1, tree1->root, 0);
//  bintree_reset(tree1);


//  g_print("DUPLICATED CASE. Insert 5,7,7,6,8,2,2,3,1\n");
//  bintree_insert_value(tree1, 5);
//  bintree_insert_value(tree1, 7);
//  bintree_insert_value(tree1, 7);
//  bintree_insert_value(tree1, 6);
//  bintree_insert_value(tree1, 8);
//  bintree_insert_value(tree1, 2);
//  bintree_insert_value(tree1, 2);
//  bintree_insert_value(tree1, 3);
//  bintree_insert_value(tree1, 1);
//  _print_tree(tree1, tree1->root, 0);
//  g_print("DELETE 2\n");
//  bintree_delete_value(tree1, 2);
//  _print_tree(tree1, tree1->root, 0);
//  g_print("DELETE 7\n");
//  bintree_delete_value(tree1, 7);
//  _print_tree(tree1, tree1->root, 0);
//  g_print("DELETE 7\n");
//  bintree_delete_value(tree1, 7);
//  _print_tree(tree1, tree1->root, 0);
//  g_print("DELETE 8\n");
//  bintree_delete_value(tree1, 8);
//  _print_tree(tree1, tree1->root, 0);
//  g_print("DELETE 5\n");
//  bintree_delete_value(tree1, 5);
//  _print_tree(tree1, tree1->root, 0);
//  g_print("DELETE: 23\n");
//  bintree_delete_value(tree1, 23);
//  _print_tree(tree1, tree1->root, 0);

  tree2 = make_bintree(test_cmp);
  bintree_reset(tree1);
  g_print("POP CASE. Insert 5,7,7,6,8,8,2,2,3,1 into tree1\n");
  bintree_insert_value(tree1, 5);
  bintree_insert_value(tree1, 7);
  bintree_insert_value(tree1, 7);
  bintree_insert_value(tree1, 6);
  bintree_insert_value(tree1, 8);
  bintree_insert_value(tree1, 8);
  bintree_insert_value(tree1, 2);
  bintree_insert_value(tree1, 2);
  bintree_insert_value(tree1, 3);
  bintree_insert_value(tree1, 1);

  g_print("POP CASE. Insert 5,7,7,2,3,1 into tree2\n");
  bintree_insert_value(tree2, 5);
  bintree_insert_value(tree2, 7);
  bintree_insert_value(tree2, 7);
  bintree_insert_value(tree2, 2);
  bintree_insert_value(tree2, 3);
  bintree_insert_value(tree2, 1);

  g_print("POP TOP FROM tree1 and insert to tree2\n");
  node = bintree_pop_top_node(tree1);
  bintree_insert_node(tree2, node);
  g_print("Tree1:\n");
  _print_tree(tree1, tree1->root, 0);
  g_print("Tree2:\n");
  _print_tree(tree2, tree2->root, 0);

  //terminate the program
  bintree_reset(tree1);
  tree1->root->value = 5;
}

void bintree_reset(BinTree *this)
{
  THIS_WRITELOCK (this);
  _ruin_full(this, this->root);
  this->root = NULL;
  THIS_WRITEUNLOCK (this);
}

void _ruin_full(BinTree *this, BinTreeNode *node)
{
  if(!node) return;
  _ruin_full(this, node->left);
  _ruin_full(this, node->right);
  _trash_bintreenode(this, node);
  --this->counter;
}

BinTreeNode *bintree_pop_top_node(BinTree *this)
{
  BinTreeNode *result = NULL;
  THIS_WRITELOCK (this);
  if(!this->top) result = NULL;
  else result = _pop_from_tree(this, this->top->value);
  THIS_WRITEUNLOCK (this);
  return result;
}

guint64 bintree_get_top_value(BinTree *this)
{
  guint64 result;
  THIS_READLOCK (this);
  if(!this->top) result = 0;
  else result = this->top->value;
  THIS_READUNLOCK (this);
  return result;
}

guint64 bintree_get_bottom_value(BinTree *this)
{
  guint64 result;
  THIS_READLOCK (this);
  if(!this->bottom) result = 0;
  else result = this->bottom->value;
  THIS_READUNLOCK (this);
  return result;
}

gboolean bintree_has_value(BinTree *this, guint64 value)
{
  BinTreeNode *node,*parent;
  THIS_READLOCK (this);
  node = _search_value(this, value, &parent);
  THIS_READUNLOCK (this);
  return node != NULL;
}


void bintree_insert_node(BinTree* this, BinTreeNode* node)
{
  THIS_WRITELOCK (this);
  this->root = _insert(this, this->root, node);
  THIS_WRITEUNLOCK (this);
}

void bintree_insert_value(BinTree* this, guint64 value)
{
  BinTreeNode* node;
  THIS_WRITELOCK (this);
  node = _make_bintreenode(this, value);
  node->ref = 1;
  this->root = _insert(this, this->root, node);
  THIS_WRITEUNLOCK (this);
}

void bintree_delete_value(BinTree* this, guint64 value)
{
  THIS_WRITELOCK (this);
  _deref_from_tree(this, value);
  THIS_WRITEUNLOCK (this);
}

guint32 bintree_get_num(BinTree *this)
{
  guint32 result;
  THIS_READLOCK(this);
  result = this->counter;
  THIS_READUNLOCK(this);
  return result;
}

BinTreeNode* bintree_pop_node(BinTree* this, guint64 value)
{
  BinTreeNode* result;
  THIS_WRITELOCK (this);
  result = _pop_from_tree(this, value);
  THIS_WRITEUNLOCK (this);
  return result;
}

BinTreeNode *make_bintreenode(BinTree *this, guint64 value)
{
  BinTreeNode *result;
  THIS_WRITELOCK (this);
  result = _make_bintreenode(this, value);
  THIS_WRITEUNLOCK (this);
  return result;
}

void trash_bintreenode(BinTree *this, BinTreeNode *node)
{
  THIS_WRITELOCK (this);
  _trash_bintreenode(this, node);
  THIS_WRITEUNLOCK (this);
}

BinTreeNode * _insert(BinTree *this, BinTreeNode *actual, BinTreeNode *insert)
{
  BinTreeNode *result;
  result = _insert_into_tree(this, actual, &insert);
  if(this->counter == 1) this->top = this->bottom = insert;
  else {
    BinTreeNode *top,*bottom;
    top = this->top;
    bottom = this->bottom;
    if(this->cmp(top->value, insert->value) < 0) this->top = insert;
    else if(this->cmp(bottom->value, insert->value) > 0) this->bottom = insert;
  }
  return result;
}

BinTreeNode * _insert_into_tree(BinTree *this, BinTreeNode *actual, BinTreeNode **insert)
{
  gint cmp_result;
  if (!actual) {++this->counter; return *insert;}
  cmp_result = this->cmp (actual->value, (*insert)->value);
  if (!cmp_result) {
    GST_DEBUG_OBJECT (this, "DUPLICATED: %lu, %p will be merged and dropped",
                      (*insert)->value, *insert);
    actual->ref+=(*insert)->ref;
    _trash_bintreenode(this, *insert);
    *insert = actual;
  } else if (cmp_result < 0)
    actual->right = _insert_into_tree (this, actual->right, insert);
  else
    actual->left = _insert_into_tree (this, actual->left, insert);
  return actual;
}


void
_deref_from_tree (BinTree * this, guint64 value)
{
  BinTreeNode *candidate = NULL, *candidate_parent = NULL;
  BinTreeNode *node = NULL, *parent = NULL;

  node = _search_value(this, value, &parent);
  if(!node)
    goto not_found;
  if(node->ref > 1)
    goto survive;
  if (!node->left && !node->right) {
    candidate = NULL;
    goto remove;
  } else if (!node->left) {
    candidate = node->right;
    goto remove;
  } else if (!node->right) {
    candidate = node->left;
    goto remove;
  } else {
    candidate_parent = node;
    candidate = _get_rightest_value (node->left, &candidate_parent);
    goto replace;
  }

not_found:
  GST_DEBUG_OBJECT (this, "%lu NOT FOUND in %p\n", node->value, this);
//  g_print("%lu NOT FOUND in %p\n", value, this);
  return;
survive:
  GST_DEBUG_OBJECT (this, "%lu SURVIVE in %p\n", node->value, this);
//  g_print("%lu SURVIVE in %p\n", node->value, this);
  --node->ref;
  return;
remove:
  GST_DEBUG_OBJECT (this, "ELEMENT FOUND TO REMOVE: %lu\n", node->value);
//  g_print("ELEMENT FOUND TO REMOVE: %lu\n", node->value);
//  g_print("PARENT: %p\n", parent);
  if (!parent)
    this->root = candidate;
  else if (parent->left == node)
    parent->left = candidate;
  else
    parent->right = candidate;
  _trash_bintreenode(this, node);
  --this->counter;
  return;
replace:
//g_print("ELEMENT FOUND TO REPLACE: %lu->%lu\n", node->value, candidate->value);
  GST_DEBUG_OBJECT (this, "ELEMENT FOUND TO REPLACE: %lu->%lu\n",
      node->value,
      candidate->value);

  candidate = _pop_from_tree(this, candidate->value);
  {
      //copy values
      node->value = candidate->value;
      node->ref = candidate->ref;
  }
  _trash_bintreenode(this, candidate);
  return;
}



BinTreeNode *
_pop_from_tree (BinTree * this, guint64 value)
{
  BinTreeNode *candidate = NULL, *candidate_parent = NULL;
  BinTreeNode *node, *parent;
  node = _search_value(this, value, &parent);
  if(!node)
    goto not_found;
  else if (!node->left && !node->right) {
    candidate = NULL;
    goto remove;
  } else if (!node->left) {
    candidate = node->right;
    goto remove;
  } else if (!node->right) {
    candidate = node->left;
    goto remove;
  } else {
    candidate_parent = node;
    candidate = _get_rightest_value (node->left, &candidate_parent);
    goto replace;
  }
not_found:
  GST_DEBUG_OBJECT (this, "ELEMENT NOT FOUND TO REMOVE: %lu\n", node->value);
  return NULL;
remove:
  GST_DEBUG_OBJECT (this, "ELEMENT FOUND TO REMOVE: %lu\n", node->value);
  if (!parent)
    this->root = candidate;
  else if (parent->left == node)
    parent->left = candidate;
  else
    parent->right = candidate;
  --this->counter;
  node->left = node->right = NULL;
  if(node == this->top) _refresh_top(this);
  else if(candidate == this->bottom) _refresh_bottom(this);
  return node;
replace:
  GST_DEBUG_OBJECT (this, "ELEMENT FOUND TO REPLACE: %lu->%lu\n",
      node->value,
      candidate->value);
  candidate = _pop_from_tree (this, candidate->value);
  {
      guint32 ref;
      guint64 value;

      //copy values
      ref = node->ref;
      value = node->value;
      node->value = candidate->value;
      node->ref = candidate->ref;
      candidate->ref = ref;
      candidate->value = value;
      candidate->left = candidate->right = NULL;
  }
  if(candidate == this->top) _refresh_top(this);
  else if(candidate == this->bottom) _refresh_bottom(this);
  return candidate;
}


BinTreeNode *_get_rightest_value(BinTreeNode *node, BinTreeNode **parent)
{
  if(!node) return NULL;
  while(node->right) {*parent = node; node = node->right;}
  return node;
}

BinTreeNode *_get_leftest_value(BinTreeNode *node, BinTreeNode **parent)
{
  if(!node) return NULL;
  while(node->left) {*parent = node; node = node->left;}
  return node;
}

BinTreeNode *_search_value(BinTree *this, guint64 value, BinTreeNode **parent)
{
  BinTreeNode *node = this->root;
  gint cmp;
  *parent = NULL;
  while(node){
    cmp = this->cmp(value, node->value);
    if(!cmp) break;
    *parent = node;
    node =  cmp < 0? node->left : node->right;
  }
  return node;
}

BinTreeNode *_make_bintreenode(BinTree *this, guint64 value)
{
  BinTreeNode *result;
  if(!g_queue_is_empty(this->node_pool))
    result = g_queue_pop_head(this->node_pool);
  else
    result = g_malloc0(sizeof(BinTreeNode));
  memset((gpointer)result, 0, sizeof(BinTreeNode));
  result->value = value;
  return result;
}

void _trash_bintreenode(BinTree *this, BinTreeNode *node)
{
  if(node){
    if(node == this->top) _refresh_top(this);
    else if(node == this->bottom) _refresh_bottom(this);
  }
  if(g_queue_get_length(this->node_pool) > 1024){
    g_free(node);
  }else{
    g_queue_push_tail(this->node_pool, node);
  }
}

void _refresh_top(BinTree *this)
{
  BinTreeNode *top, *parent = NULL;
  top = this->top;
  if(top->left) this->top = _get_rightest_value(top->left, &parent);
  else this->top = _get_rightest_value(this->root, &parent);
}

void _refresh_bottom(BinTree *this)
{
  BinTreeNode *top, *parent = NULL;
  top = this->bottom;
  if(top->right) this->bottom = _get_leftest_value(top->right, &parent);
  else this->bottom = _get_leftest_value(this->root, &parent);
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
