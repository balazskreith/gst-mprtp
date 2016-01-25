/*
 * bintree2.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef BINTREE2_H_
#define BINTREE2_H_

#include <gst/gst.h>
#include "pointerpool.h"

typedef struct _BinTree2 BinTree2;
typedef struct _BinTree2Class BinTree2Class;

#define BINTREE2_TYPE             (bintree2_get_type())
#define BINTREE2(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),BINTREE2_TYPE,BinTree2))
#define BINTREE2_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),BINTREE2_TYPE,BinTree2Class))
#define BINTREE2_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),BINTREE2_TYPE))
#define BINTREE2_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),BINTREE2_TYPE))
#define BINTREE2_CAST(src)        ((BinTree2 *)(src))

typedef struct _BinTree2Node BinTree2Node;
typedef gint (*BinTree2CmpFunc)(gint64, gint64);
struct _BinTree2
{
  GObject                   object;
  BinTree2Node*             root;
  BinTree2Node*             top;
  BinTree2Node*             bottom;
  BinTree2CmpFunc           cmp;
  GRWLock                   rwmutex;
//  GQueue*                  node_pool;
  PointerPool*              node_pool;
  guint32                   counter;

};

struct _BinTree2Node
{
  BinTree2Node *left, *right;
  gint64 value;
  guint ref;
};

struct _BinTree2Class{
  GObjectClass parent_class;
};

GType bintree2_get_type (void);
BinTree2 *make_bintree2(BinTree2CmpFunc cmp);
BinTree2Node *bintree2_pop_top_node(BinTree2 *this);
BinTree2Node *bintree2_pop_bottom_node(BinTree2 *this);
gint64 bintree2_get_top_value(BinTree2 *this);
gint64 bintree2_get_bottom_value(BinTree2 *this);
gboolean bintree2_has_value(BinTree2 *this, gint64 value);
void bintree2_insert_node(BinTree2* this, BinTree2Node* node);
BinTree2Node *make_bintree2node(BinTree2 *this, gint64 value);
void trash_bintree2node(BinTree2 *this, BinTree2Node *node);
void bintree2_insert_value(BinTree2* this, gint64 value);
BinTree2Node* bintree2_pop_node(BinTree2* this, gint64 value);
void bintree2_delete_value(BinTree2* this, gint64 value);
void bintree2_trash_node(BinTree2 *this, BinTree2Node *node);
guint32 bintree2_get_num(BinTree2 *this);



void bintree2_reset(BinTree2 *this);
void bintree2_test(void);

#endif /* BINTREE2_H_ */
