#include "lib_bintree.h"
#include <math.h>
#include <string.h>

static bintree3node_t * _insert(bintree3_t *this, bintree3node_t *actual, bintree3node_t *insert);
static bintree3node_t * _insert_into_tree(bintree3_t *this, bintree3node_t *actual, bintree3node_t **insert);
static bintree3node_t *
_search_value(bintree3_t *this, gpointer value, bintree3node_t **parent);
static gboolean
_deref_from_tree (bintree3_t * this, gpointer value);
static void _refresh_top(bintree3_t *this);
static void _refresh_bottom(bintree3_t *this);
static bintree3node_t *_get_rightest_value(bintree3node_t *node, bintree3node_t **parent);
static bintree3node_t *_get_leftest_value(bintree3node_t *node, bintree3node_t **parent);
static bintree3node_t *_make_bintree3node(bintree3_t *this, gpointer value);
static void _trash_bintree3node(bintree3_t *this, bintree3node_t *node);
static void _ruin_full(bintree3_t *this, bintree3node_t *node);


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
    sprintf(helper, "(%p)%c ", it->data, it->next ? ',' : ' ');
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
_print_tree (bintree3_t * tree, bintree3node_t* node, gint32 level)
{
  gchar string[255];

  if (!node) {
    return;
  }
  if(tree->top)  {
    _sprint_list_of_pointers(tree->top->ptrs, string, 255);
    //observer_notify(tree->on_print, string);
  }

  if (!level){
    gchar str[255];
    memset(str, 0, 255);
    sprintf(str, "Tree %p TOP is: %s Node Counter: %u Duplicate counter: %d\n", tree, string, tree->node_counter, tree->duplicate_counter);
    notifier_do(tree->on_print, str);
  }
  _setup_indent_level(string, 255, level);
  _sprint_list_of_pointers(node->ptrs, string, 255);
  //observer_notify(tree->on_print, string);

  memset(string, 0, 255);
  sprintf(string, "%d->%p->data:%s  ref: %u ->left:%p ->right:%p\n",
      level, node, string, node->ref, node->left, node->right);
  notifier_do(tree->on_print, string);

  _print_tree (tree, node->left, level + 1);
  _print_tree (tree, node->right, level + 1);
}


//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------


gint32 bintree3cmp_int32(gpointer pa,gpointer pb)
{
  gint32 a,b;
  a = *((gint32*)pa);
  b = *((gint32*)pb);
  return a == b ? 0 : a < b ? -1 : 1;
}

gint32 bintree3cmp_uint32(gpointer pa,gpointer pb)
{
  return *((guint32*)pa) == *((guint32*)pb) ? 0 : *((guint32*)pa) < *((guint32*)pb) ? -1 : 1;
}


gint32 bintree3cmp_uint64(gpointer pa,gpointer pb)
{
  guint64 a,b;
  a = *((guint64*)pa);
  b = *((guint64*)pb);
  return a == b ? 0 : a < b ? -1 : 1;
}

gint32 bintree3cmp_double(gpointer pa,gpointer pb)
{
  gdouble a,b;
  a = *((gdouble*)pa);
  b = *((gdouble*)pb);
  return a == b ? 0 : a < b ? -1 : 1;
}


gint32 bintree3cmp_int64(gpointer pa,gpointer pb)
{
  gint64 a,b;
  a = *((gint64*)pa);
  b = *((gint64*)pb);
  return a == b ? 0 : a < b ? -1 : 1;
}


bintree3_t *make_bintree3(bintree3cmp cmp)
{
  bintree3_t *result;
  result = malloc (sizeof(bintree3_t));
  memset(result, 0, sizeof(bintree3_t));
  result->cmp = cmp;
  result->on_print     = make_notifier();
  result->on_duplicate = make_notifier();
  return result;
}


void bintree3_print(bintree3_t* this)
{
  if(!this->root){
    return;
  }
  _print_tree(this, this->root, 0);
}

void bintree3_test(void)
{
  bintree3_t *tree1;
  bintree3_t *tree2;
  gint32   i;
  gint32   tree1_values[] = {5,7,7,6,8,8,2,2,3,1};
  gint32   tree2_values[] = {5,7,7,2,3,1};
  tree1 = make_bintree3(bintree3cmp_int32);
  tree2 = make_bintree3(bintree3cmp_int32);

  printf("POP CASE. Insert 5,7,7,6,8,8,2,2,3,1 into tree1\n");
  for(i = 0; i < 10; ++i){
    bintree3_insert_data(tree1, &tree1_values[i]);
  }

  printf("POP CASE. Insert 5,7,7,2,3,1 into tree2\n");
  for(i = 0; i < 6; ++i){
    bintree3_insert_data(tree2, &tree2_values[i]);
  }

  printf("Tree1:\n");
  _print_tree(tree1, tree1->root, 0);
  printf("Tree2:\n");
  _print_tree(tree2, tree2->root, 0);

  printf("POP TOP FROM tree1 and insert to tree2\n");
  printf("Tree1:\n");
  _print_tree(tree1, tree1->root, 0);
  printf("Tree2:\n");
  _print_tree(tree2, tree2->root, 0);

  //terminate the program
  bintree3_reset(tree1);
}

void bintree3_dtor(gpointer target)
{
  bintree3_t* this;
  if(!target){
    return;
  }
  this = target;
  bintree3_reset(this);
  g_object_unref(this->on_print);
  g_object_unref(this->on_duplicate);
  free(this);
}

void bintree3_reset(bintree3_t *this)
{
  _ruin_full(this, this->root);
  this->node_counter = 0;
  this->root = NULL;
}


static void _inorder_fillup(bintree3node_t* node, gpointer* dataptr)
{
  GSList *it;
  if(!node)
    return;
  _inorder_fillup(node->left, dataptr);
  for(it = node->ptrs; it; it = it->next, ++dataptr){
    dataptr = it->data;
  }
  _inorder_fillup(node->right, dataptr);
}

gpointer bintree3_get_items_sorted_array(bintree3_t *this, guint *length)
{
  gpointer* result;
  //TODO: test this
  result = g_malloc0(sizeof(gpointer) * (this->node_counter + this->duplicate_counter));
  _inorder_fillup(this->root, result);
  if(length){
      *length = this->node_counter + this->duplicate_counter;
  }
  return result;
}

void bintree3_add_on_duplicate_cb(bintree3_t *this, ListenerFunc callback, gpointer udata)
{
  notifier_add_listener(this->on_duplicate, callback, udata);
}

void bintree3_add_on_print_cb(bintree3_t *this, ListenerFunc callback, gpointer udata)
{
  notifier_add_listener(this->on_print, callback, udata);
}

void _ruin_full(bintree3_t *this, bintree3node_t *node)
{
  if(!node) return;
  _ruin_full(this, node->left);
  _ruin_full(this, node->right);
  _trash_bintree3node(this, node);
}

gpointer bintree3_delete_top_data(bintree3_t *this)
{
  gpointer result = NULL;
  if(!this->top){
    return NULL;
  }
  result = this->top->ptrs->data;
  _deref_from_tree(this, this->top->ptrs->data);
  return result;

  return result;
}

gpointer bintree3_delete_bottom_data(bintree3_t *this)
{
  gpointer result = NULL;
  if(!this->bottom){
    return NULL;
  }
  result = this->bottom->ptrs->data;
  _deref_from_tree(this, this->bottom->ptrs->data);
  return result;
}

gpointer bintree3_get_top_data(bintree3_t *this)
{
  if(!this->top)
    return NULL;
  return this->top->ptrs->data;
}

gpointer bintree3_get_bottom_data(bintree3_t *this)
{
  if(!this->bottom)
    return NULL;
  return this->bottom->ptrs->data;
}

gboolean bintree3_has_value(bintree3_t *this, gpointer data)
{
  bintree3node_t *node,*parent = NULL;
  node = _search_value(this, data, &parent);
  return node != NULL;
}


void bintree3_insert_data(bintree3_t* this, gpointer data)
{
  bintree3node_t* node;
  node = _make_bintree3node(this, data);
  this->root = _insert(this, this->root, node);
}

void bintree3_insert_top_data(bintree3_t* this, gpointer data)
{
  bintree3node_t* node;
  node = _make_bintree3node(this, data);
  if(!this->root){
    this->root = _insert(this, this->root, node);
  }else{
    this->top = _insert(this, this->top, node);
  }
}

void bintree3_insert_bottom_data(bintree3_t* this, gpointer data)
{
  bintree3node_t* node;
  node = _make_bintree3node(this, data);
  if(!this->root){
    this->root = _insert(this, this->root, node);
  }else{
    this->bottom = _insert(this, this->bottom, node);
  }
}

gboolean bintree3_delete_value(bintree3_t* this, gpointer data)
{
  return _deref_from_tree(this, data);
}

gint32 bintree3_get_nodenum(bintree3_t *this)
{
  return this->node_counter;
}

gint32 bintree3_get_refnum(bintree3_t *this)
{
  return this->node_counter + this->duplicate_counter;
}

bintree3node_t * _insert(bintree3_t *this, bintree3node_t *actual, bintree3node_t *insert)
{
  bintree3node_t *result;
  result = _insert_into_tree(this, actual, &insert);
  if(this->node_counter == 1) {
    this->top = this->bottom = insert;
  } else {
    bintree3node_t *top,*bottom;
    top = this->top;
    bottom = this->bottom;
    if(this->cmp(top->ptrs->data, insert->ptrs->data) < 0) {
      this->top = insert;
    }
    if(this->cmp(insert->ptrs->data, bottom->ptrs->data) < 0) {
      this->bottom = insert;
    }
  }
  return result;
}

bintree3node_t * _insert_into_tree(bintree3_t *this, bintree3node_t *actual, bintree3node_t **insert)
{
  gint32 cmp_result;
  if (!actual) {
      ++this->node_counter;
      return *insert;
  }
  cmp_result = this->cmp (actual->ptrs->data, (*insert)->ptrs->data);
  if (!cmp_result) {
    actual->ref+=(*insert)->ref;

    notifier_do(this->on_duplicate, (*insert)->ptrs->data);

    actual->ptrs = g_slist_append(actual->ptrs, (*insert)->ptrs->data);
    this->duplicate_counter += (*insert)->ref;
    _trash_bintree3node(this, *insert);
    *insert = actual;
  } else if (cmp_result < 0)
    actual->right = _insert_into_tree (this, actual->right, insert);
  else
    actual->left = _insert_into_tree (this, actual->left, insert);
  return actual;
}


gboolean
_deref_from_tree (bintree3_t * this, gpointer data)
{
  bintree3node_t *candidate = NULL, *candidate_parent = NULL;
  bintree3node_t *node = NULL, *parent = NULL;

  node = _search_value(this, data, &parent);
  if(!node){
    goto not_found;
  }

  node->ptrs = g_slist_remove(node->ptrs, data);

  if(1 < node->ref){
    goto survive;
  }

  --this->node_counter;
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
  return FALSE;
survive:
  --node->ref;
  --this->duplicate_counter;
  return TRUE;
remove:
  if (!parent)
    this->root = candidate;
  else if (parent->left == node)
    parent->left = candidate;
  else
    parent->right = candidate;
  _trash_bintree3node(this, node);
  return TRUE;
replace:
  if(candidate_parent){
    if(candidate_parent->left == candidate){
      candidate_parent->left = candidate->left ? candidate->left : candidate->right;
    }else{
      candidate_parent->right = candidate->left ? candidate->left : candidate->right;
    }
  }

  node->ptrs = candidate->ptrs;
  node->ref = candidate->ref;

  _trash_bintree3node(this, candidate);
  return TRUE;
}



bintree3node_t *_get_rightest_value(bintree3node_t *node, bintree3node_t **parent)
{
  if(!node) return NULL;
  while(node->right) {*parent = node; node = node->right;}
  return node;
}

bintree3node_t *_get_leftest_value(bintree3node_t *node, bintree3node_t **parent)
{
  if(!node) return NULL;
  while(node->left) {*parent = node; node = node->left;}
  return node;
}

bintree3node_t *_search_value(bintree3_t *this, gpointer data, bintree3node_t **parent)
{
  bintree3node_t *node = (*parent) == NULL ? this->root : *parent;
  gint32 cmp;
  while(node){
    cmp = this->cmp(data, node->ptrs->data);
    if(!cmp){
      return g_slist_find(node->ptrs, data) != NULL ? node : NULL;
    }
    *parent = node;
    node =  cmp < 0 ? node->left : node->right;
  }
  return NULL;
}

bintree3node_t *_make_bintree3node(bintree3_t *this, gpointer data)
{
  bintree3node_t *result;
  result = g_slice_new0(bintree3node_t);
  result->ref = 1;
  result->ptrs = g_slist_append(result->ptrs, data);
  return result;
}

void _trash_bintree3node(bintree3_t *this, bintree3node_t *node)
{
  if(!node){
    g_warning("No node to trash");
    return;
  }
  if(node == this->top){
    _refresh_top(this);
  }

  if(node == this->bottom){
    _refresh_bottom(this);
  }

  g_slice_free(bintree3node_t, node);
}

void _refresh_top(bintree3_t *this)
{
  bintree3node_t *top, *parent = NULL;
  top = this->top;
  if(top->left) this->top = _get_rightest_value(top->left, &parent);
  else this->top = _get_rightest_value(this->root, &parent);
}

void _refresh_bottom(bintree3_t *this)
{
  bintree3node_t *bottom, *parent = NULL;
  bottom = this->bottom;
  if(bottom->right) this->bottom = _get_leftest_value(bottom->right, &parent);
  else this->bottom = _get_leftest_value(this->root, &parent);
}

