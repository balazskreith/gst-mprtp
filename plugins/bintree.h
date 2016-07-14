/*
 * bintree.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef BINTREE_H_
#define BINTREE_H_

#include <gst/gst.h>
#include "mprtplogger.h"

typedef struct _BinTree BinTree;
typedef struct _BinTreeClass BinTreeClass;

#define BINTREE_TYPE             (bintree_get_type())
#define BINTREE(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),BINTREE_TYPE,BinTree))
#define BINTREE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),BINTREE_TYPE,BinTreeClass))
#define BINTREE_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),BINTREE_TYPE))
#define BINTREE_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),BINTREE_TYPE))
#define BINTREE_CAST(src)        ((BinTree *)(src))

typedef struct _BinTreeNode BinTreeNode;
typedef gint (*BinTreeCmpFunc)(guint64, guint64);
struct _BinTree
{
  GObject                  object;
  BinTreeNode*             root;
  BinTreeNode*             top;
  BinTreeNode*             bottom;
  BinTreeCmpFunc           cmp;
  GRWLock                  rwmutex;
//  GQueue*                  node_pool;
//  PointerPool*             node_pool;
  guint32                  node_counter;
  gint32                   duplicate_counter;

};

struct _BinTreeNode
{
  BinTreeNode *left, *right;
  gint64 value;
  guint ref;
};

struct _BinTreeClass{
  GObjectClass parent_class;
};

GType bintree_get_type (void);
BinTree *make_bintree(BinTreeCmpFunc cmp);
BinTreeNode *bintree_pop_top_node(BinTree *this);
BinTreeNode *bintree_pop_bottom_node(BinTree *this);
gint64 bintree_get_top_data(BinTree *this);
gint64 bintree_get_bottom_data(BinTree *this);
gboolean bintree_has_value(BinTree *this, gint64 value);
void bintree_insert_node(BinTree* this, BinTreeNode* node);
BinTreeNode *make_bintreenode(BinTree *this, gint64 value);
void trash_bintreenode(BinTree *this, BinTreeNode *node);
void bintree_insert_data(BinTree* this, gint64 value);
BinTreeNode* bintree_pop_node(BinTree* this, gint64 value);
void bintree_delete_value(BinTree* this, gint64 value);
void bintree_trash_node(BinTree *this, BinTreeNode *node);
guint32 bintree_get_nodenum(BinTree *this);
guint32 bintree_get_refnum(BinTree *this);



void bintree_reset(BinTree *this);
void bintree_test(void);

#endif /* BINTREE_H_ */
