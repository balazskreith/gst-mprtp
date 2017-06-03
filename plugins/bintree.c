/* GStreamer Scheduling tree
 * Copyright (C) 2015 Balázs Kreith (contact: balazs.kreith@gmail.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be ureful,
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

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "bintree.h"


#define _now(this) (gst_clock_get_time (this->sysclock))

GST_DEBUG_CATEGORY_STATIC (bintree_debug_category);
#define GST_CAT_DEFAULT bintree_debug_category

G_DEFINE_TYPE (Bintree, bintree, G_TYPE_OBJECT);

static void _insert(Bintree* this, BintreeNode* insert, BintreeNode* node);
static void _replace_node(Bintree* this, BintreeNode* node, BintreeNode* parent, BintreeNode* candidate);
static BintreeNode* _search_node(Bintree* this, gpointer value, BintreeNode** parent);
static BintreeNode* _bintree_get_leftist(Bintree*this, BintreeNode* node, BintreeNode** parent);
static BintreeNode* _bintree_get_rightist(Bintree*this, BintreeNode* node, BintreeNode** parent);
static BintreeNode* _integrate_node(Bintree* this, BintreeNode* node);
static void _desintegrate_node(Bintree* this, BintreeNode* node);
static void _trash_node(Bintree* this, BintreeNode* node);
static BintreeNode* _create_node(Bintree* this, gpointer value);
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------



static void
_sprint_list_of_pointers(GSList* it, gchar *result, guint result_length)
{
  gchar helper[255];
  if(!it){
    return;
  }
  memset(result, 0, result_length);
  for(; it; it = it->next){
    memset(helper, 0, 255);
    sprintf(helper, "(%p)[%d]%c ", it->data, *(gint32*)(it->data), it->next ? ',' : ' ');
    strcat(result, helper);
  }
}

static void
_setup_indent_level(gchar *result, guint result_length, guint level)
{
  gint i;
  memset(result, 0, result_length);
  for(i = 0; i < level; ++i){
    strcat(result, "--");
  }
}

void static
_print_tree (Bintree * tree, BintreeNode* node, gint32 level)
{
  gchar string[255];
  gchar node_values_string[255];

  if (!node) {
    return;
  }
  if(tree->top)  {
    _sprint_list_of_pointers(tree->top->values, string, 255);
    //observer_notify(tree->on_print, string);
  }

  if (!level){
    gchar str[255];
    memset(str, 0, 255);
    sprintf(str, "Tree %p TOP is: %s Node Counter: %u Duplicate counter: %d\n", tree, string, tree->node_counter, tree->duplicate_counter);
    g_print("%s", str);
    //notifier_do(tree->on_print, str);
  }
  _setup_indent_level(string, 255, level);
  _sprint_list_of_pointers(node->values, node_values_string, 255);

  memset(string, 0, 255);
  sprintf(string, "%d->%p->data:%s  ref: %u ->left:%p ->right:%p\n",
      level, node, node_values_string, node->values_length, node->left, node->right);
  g_print("%s", string);
//  notifier_do(tree->on_print, string);

  _print_tree (tree, node->left, level + 1);
  _print_tree (tree, node->right, level + 1);
}

void bintree_print(Bintree* this) {
  _print_tree(this, this->root, 0);
}

//========================================================================

static void
bintree_finalize (GObject * object);

void
bintree_class_init (BintreeClass * klass)
{
  GObjectClass *gobject_class;
  gobject_class = (GObjectClass *) klass;
  gobject_class->finalize = bintree_finalize;
  GST_DEBUG_CATEGORY_INIT (bintree_debug_category, "bintree", 0, "Bintree");
}


void
bintree_finalize (GObject * object)
{
  Bintree *this = BINTREE (object);
  while(this->root) {
    bintree_delete_value(this, this->root->values->data);
  }
  g_queue_clear(this->recycle);

}

void
bintree_init (Bintree * this)
{

}

Bintree *make_bintree(GCompareFunc cmp) {
  Bintree *this;
  this = g_object_new (BINTREE_TYPE, NULL);
  this->cmp = cmp;
  this->recycle = g_queue_new();
  return this;
}

gpointer bintree_get_top_value(Bintree* this) {
  if (!this->top) {
    return NULL;
  }
  return this->top->values->data;
}

gpointer bintree_get_bottom_value(Bintree* this) {
  if (!this->bottom) {
    return NULL;
  }
  return this->bottom->values->data;
}

BintreeNode* bintree_get_top_node(Bintree* this) {
 return this->top;
}

BintreeNode* bintree_get_bottom_node(Bintree* this) {
  return this->bottom;
}

gint32 bintree_get_duplicated_coutner(Bintree* this) {
  return this->duplicate_counter;
}

gint32 bintree_get_node_counter(Bintree* this) {
  return this->node_counter;
}

gint32 bintree_get_size(Bintree* this) {
  return this->node_counter + this->duplicate_counter;
}

void bintree_insert_node_at_top(Bintree* this, BintreeNode* node) {
  _insert(this, node, this->top);
}

void bintree_insert_node_at_bottom(Bintree* this, BintreeNode* node) {
  _insert(this, node, this->bottom);
}

void bintree_insert_value(Bintree* this, gpointer value) {
  _insert(this, _create_node(this, value), this->root);
}

BintreeNode* bintree_pop_bottom_node(Bintree* this) {
  BintreeNode *parent;
  BintreeNode *result;
  if (!this->bottom) {
    return NULL;
  }
  result = this->bottom;
  _search_node(this, this->bottom->values->data, &parent);
  _replace_node(this, this->bottom, parent, this->bottom->right);
  _desintegrate_node(this, this->bottom);
  this->duplicate_counter -= result->values_length - 1;
  --this->node_counter;
  result->right = NULL;
  return result;
}

BintreeNode* bintree_pop_top_node(Bintree* this) {
  BintreeNode *parent;
  BintreeNode *result;
  if (!this->top) {
    return NULL;
  }
  result = this->top;
  _search_node(this, this->top->values->data, &parent);
  _replace_node(this, this->top, parent, this->top->left);
  _desintegrate_node(this, this->top);
  this->duplicate_counter -= result->values_length - 1;
  --this->node_counter;
  result->left = NULL;
  return result;
}


void _insert(Bintree* this, BintreeNode* insert, BintreeNode* node) {
  gint cmp = 0;
  BintreeNode* parent = NULL; // This seems to be wrong, but actually not.
  if (!this->root) {
    this->root = _integrate_node(this, insert);
    this->duplicate_counter = insert->values_length - 1;
    this->node_counter = 1;
    return;
  }

  while(node) {
    cmp = this->cmp(node->values->data, insert->values->data);
    if (!cmp) {
      this->duplicate_counter += insert->values_length;
      node->values = g_slist_concat(node->values, insert->values);
      node->values_length += insert->values_length;
      _trash_node(this, insert);
      return;
    }
    parent = node;
    node = cmp < 0 ? node->right : node->left;
  }
  ++this->node_counter;
  this->duplicate_counter += insert->values_length - 1;
  if (cmp < 0) {
    parent->right = _integrate_node(this, insert);
  } else {
    parent->left = _integrate_node(this, insert);
  }
}

gboolean bintree_delete_value(Bintree* this, gpointer value) {
  BintreeNode* parent;
  BintreeNode* node = _search_node(this, value, &parent);
  if (!node) {
    return FALSE;
  }

  if (1 < node->values_length) {
    node->values = g_slist_remove(node->values, value);
    --node->values_length;
    --this->duplicate_counter;
    return TRUE;
  }

  --this->node_counter;
  node->values = g_slist_remove(node->values, value);
  node->values_length = 0;
  if (!node->left && !node->right) {
    _replace_node(this, node, parent, NULL);
    _desintegrate_node(this, node);
    _trash_node(this, node);
  } else if (!node->left) {
    _replace_node(this, node, parent, node->right);
    _desintegrate_node(this, node);
    _trash_node(this, node);
  } else if (!node->right) {
    _replace_node(this, node, parent, node->left);
    _desintegrate_node(this, node);
    _trash_node(this, node);
  } else {
    BintreeNode* candidate = node->left;
    BintreeNode* candidate_parent = node;
    BintreeNode* candidate_left;
    for(; candidate->right; candidate_parent = candidate, candidate = candidate->right);
    candidate_left = candidate->left;
    if (candidate_parent == node) {
      candidate_parent->left = candidate_left;
    } else {
      candidate_parent->right = candidate_left;
    }
    node->values = candidate->values;
    node->values_length = candidate->values_length;
    _desintegrate_node(this, candidate);
    _trash_node(this, candidate);
  }
  return TRUE;
}

void _replace_node(Bintree* this, BintreeNode* node, BintreeNode* parent, BintreeNode* candidate) {
  if (!parent) {
    this->root = candidate;
    return;
  }
  if (parent->left == node) {
    parent->left = candidate;
  } else {
    parent->right = candidate;
  }
}

BintreeNode* _search_node(Bintree* this, gpointer value, BintreeNode** parent) {
  BintreeNode* node;
  gint cmp;
  *parent = NULL;
  node = this->root;
  while(node) {
    cmp = this->cmp(node->values->data, value);
    if (cmp == 0) {
      return node;
    }
    *parent = node;
    node = cmp < 0 ? node->right : node->left;
  }
  return NULL;
}

BintreeNode* _bintree_get_leftist(Bintree*this, BintreeNode* node, BintreeNode** parent) {
  *parent = NULL;
  while (node->left) {
    *parent = node;
    node = node->left;
  }
  return node;
}

BintreeNode* _bintree_get_rightist(Bintree*this, BintreeNode* node, BintreeNode** parent) {
  *parent = NULL;
  while (node->right) {
    *parent = node;
    node = node->right;
  }
  return node;
}

BintreeNode* _integrate_node(Bintree* this, BintreeNode* node) {
  if(!this->top) {
    this->top = node;
  } else if (this->cmp(this->top->values->data, node->values->data) < 0) {
    this->top = node;
  }

  if (!this->bottom) {
    this->bottom = node;
  } else if (this->cmp(node->values->data, this->bottom->values->data) < 0) {
    this->bottom = node;
  }
  return node;
}

void _desintegrate_node(Bintree* this, BintreeNode* node) {
  if (!this->root) {
    this->top = this->bottom = NULL;
    return;
  }
  if (this->top == node) {
    BintreeNode* parent;
    this->top = _bintree_get_rightist(this, this->root, &parent);
  }
  if (this->bottom == node) {
    BintreeNode* parent;
    this->bottom = _bintree_get_leftist(this, this->root, &parent);
  }
}

void _trash_node(Bintree* this, BintreeNode* node) {
  memset(node, 0, sizeof(BintreeNode));
  g_queue_push_tail(this->recycle, node);
}

BintreeNode* _create_node(Bintree* this, gpointer value) {
  BintreeNode* node = g_queue_is_empty(this->recycle) ? g_malloc0(sizeof(BintreeNode)) : g_queue_pop_head(this->recycle);
  node->values = g_slist_append(node->values, value);
  node->values_length = 1;
  return node;
}
