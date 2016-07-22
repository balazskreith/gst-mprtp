#ifndef INCGUARD_NTRT_LIBRARY_BINTREE_H_
#define INCGUARD_NTRT_LIBRARY_BINTREE_H_

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>

#include <gst/gst.h>

typedef struct{
  gboolean                 active;
  gpointer               (*alloc)(gpointer,gpointer);
  gpointer                 alloc_udata;
  void                   (*dealloc)(gpointer,gpointer);
  gpointer                 dealloc_udata;
  void                   (*copy)(gpointer, gpointer, gpointer);
  gpointer                 copy_udata;
}sallocator_t;

typedef struct _bintree3node{
//  gpointer   data;
  struct _bintree3node *left;
  struct _bintree3node *right;
  GSList *ptrs;
  gint32  ref;
}bintree3node_t;

typedef gint32 (*bintree3cmp)(gpointer,gpointer);
typedef void    (*bintree3sprint)(gpointer,gchar*);
typedef void    (*bintree3dupnotifier)(gpointer,gpointer);

typedef struct _bintree3 {
  bintree3node_t       *root,*bottom,*top;
  bintree3cmp           cmp;
  bintree3sprint        sprint;
  gint32                node_counter;
  gint32                duplicate_counter;
  bintree3dupnotifier   duplicate_notifier;
  gpointer              duplicate_notifier_data;
} bintree3_t;

gint32 bintree3cmp_int32(gpointer a,gpointer b);
gint32 bintree3cmp_uint64(gpointer pa,gpointer pb);
gint32 bintree3cmp_int64(gpointer pa,gpointer pb);

bintree3_t *make_bintree3(bintree3cmp cmp);
void bintree3_setsprint(bintree3_t* this, bintree3sprint sprint);
void bintree3_print(bintree3_t* this);
void bintree3_test(void);
void bintree3_dtor(gpointer target);
void bintree3_reset(bintree3_t *this);
gpointer bintree3_get_items_sorted_array(bintree3_t *this);
void bintree3_setup_duplicate_notifier(bintree3_t *this, bintree3dupnotifier duplicate_notifier, gpointer duplicate_notifier_data);
gpointer bintree3_delete_top_data(bintree3_t *this);
gpointer bintree3_delete_bottom_data(bintree3_t *this);
gpointer bintree3_get_top_data(bintree3_t *this);
gpointer bintree3_get_bottom_data(bintree3_t *this);
gboolean bintree3_has_value(bintree3_t *this, gpointer data);
void bintree3_insert_data(bintree3_t* this, gpointer data);
gboolean bintree3_delete_value(bintree3_t* this, gpointer data);
gint32 bintree3_get_nodenum(bintree3_t *this);
gint32 bintree3_get_refnum(bintree3_t *this);


#endif /* INCGUARD_NTRT_LIBRARY_VECTOR_H_ */
