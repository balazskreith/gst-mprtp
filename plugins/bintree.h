/*
 * refctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef BINTREE_H_
#define BINTREE_H_

#include <gst/gst.h>
#include "lib_datapuffer.h"

typedef struct _Bintree Bintree;
typedef struct _BintreeClass BintreeClass;

#define BINTREE_TYPE             (bintree_get_type())
#define BINTREE(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),BINTREE_TYPE,Bintree))
#define BINTREE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),BINTREE_TYPE,BintreeClass))
#define BINTREE_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),BINTREE_TYPE))
#define BINTREE_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),BINTREE_TYPE))
#define BINTREE_CAST(src)        ((Bintree *)(src))

typedef struct _BintreeNode{
  struct _BintreeNode *left;
  struct _BintreeNode *right;
  GSList* values;
  gint32 values_length;
}BintreeNode;

struct _Bintree
{
  GObject object;
  BintreeNode* root;
  BintreeNode* top;
  BintreeNode* bottom;
  GCompareFunc cmp;
  gint32 node_counter;
  gint32 duplicate_counter;
  GQueue* recycle;
};

struct _BintreeClass{
  GObjectClass parent_class;
};


GType bintree_get_type (void);

Bintree *make_bintree(GCompareFunc cmp);
gpointer bintree_get_top_value(Bintree* this);
gpointer bintree_get_bottom_value(Bintree* this);
BintreeNode* bintree_get_top_node(Bintree* this) ;
BintreeNode* bintree_get_bottom_node(Bintree* this) ;
gint32 bintree_get_duplicated_coutner(Bintree* this);
gint32 bintree_get_node_counter(Bintree* this);
gint32 bintree_get_size(Bintree* this);
void bintree_insert_node_at_top(Bintree* this, BintreeNode* node);
void bintree_insert_node_at_bottom(Bintree* this, BintreeNode* node);
void bintree_insert_value(Bintree* this, gpointer value);
BintreeNode* bintree_pop_bottom_node(Bintree* this);
BintreeNode* bintree_pop_top_node(Bintree* this);
gboolean bintree_delete_value(Bintree* this, gpointer value);

#endif /* BINTREE_H_ */
