#include "owds.h"

#define _now(this) (gst_clock_get_time (this->sysclock))

OWDs* owds_ctor(void)
{
  OWDs* this;
  this = g_malloc0(sizeof(OWDs));
  this->items = g_queue_new();
  this->sysclock = gst_system_clock_obtain();
  return this;
}

void owds_dtor(OWDs* this)
{
  g_queue_free_full(this->items, g_free);
  g_object_unref(this->sysclock);
  g_free(this);
}

OWDs* make_owds(GstClockTime accumulation_length, GstClockTime sampling_time, OWDLogger logger)
{
  OWDs* this = owds_ctor();
  this->length        = accumulation_length;
  this->sampling_time = sampling_time;
  this->logger        = logger;
  this->last_sampling = 0;
  return this;
}

static void _refresh_by_length(OWDs* this){
  OWDItem* first,*last;
again:
  if(g_queue_is_empty(this->items)){
    return;
  }
  first = g_queue_peek_head(this->items);
  last  = g_queue_peek_tail(this->items);
  if(first->made < last->made - this->length){
    g_free(g_queue_pop_head(this->items));
    goto again;
  }
}

static gint _queue_sort(OWDItem* a, OWDItem* b, gpointer udata){
  if(a->delay == b->delay){
    return 0;
  }
  return a->delay < b->delay ? -1 : 1;
}
static void _refresh_by_time(OWDs* this){
  GQueue* copy;
  guint length;
  GstClockTime result;
  OWDItem *last;
  last = g_queue_peek_tail(this->items);
  if(this->last_sampling == 0){
    this->last_sampling = last->made;
    return;
  }
again:
  if(last->made - this->sampling_time < this->last_sampling){
    return;
  }
  copy = g_queue_copy(this->items);
  g_queue_sort(copy, (GCompareDataFunc)_queue_sort, NULL);
  length = g_queue_get_length(copy);


  if(length % 2 == 1){
    //If n is odd then Median (M) = value of ((n + 1)/2)th item from a sorted array of length n.
    OWDItem* item;
    item = g_queue_peek_nth(copy, length / 2);
    result = item->delay;
  }else{
    //If n is even then Median (M) = value of [((n)/2)th item term + ((n)/2 + 1)th item term ] /2
    OWDItem *left,*right;
    left  = g_queue_peek_nth(copy, length / 2);
    right = g_queue_peek_nth(copy, length / 2 + 1);
    result = (left->delay + right->delay)>>1;
  }
  g_queue_free(copy);

  this->last_sampling += this->sampling_time;
  this->logger(&result);
  goto again;

}

void owds_add_delay(OWDs* this, GstClockTime made, GstClockTime delay)
{
  OWDItem* item = g_malloc0(sizeof(OWDItem));
  item->made   = made;
  item->delay  = delay;

  _refresh_by_length(this);
  g_queue_push_tail(this->items, item);
  _refresh_by_time(this);

}

