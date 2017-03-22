#include "lib_swplugins.h"
#include <math.h>

#define now (gst_clock_get_time (sysclock))
static GstClock* sysclock;
static int _cmpfunc (const void * pa, const void * pb)
{
  return *((guint32*)pa) == *((guint32*)pb) ? 0 : *((guint32*)pa) < *((guint32*)pb) ? -1 : 1;
}

#include "mprtplogger.h"
static void _median_test_with_qsort(const gchar *filename, guint32* src, guint32* dst, gint length, guint32 *result, GstClockTime *elapsed)
{
  *elapsed = now;
  memcpy(dst, src, sizeof(guint32) * length);
  qsort(dst, length, sizeof(guint32), _cmpfunc);

  if(length%2==0) {
      *result = (dst[length/2]+dst[length/2-1])/2;
  }else{
      *result = dst[length/2];
  }

  *elapsed = now - *elapsed;
}

static void _median_test_with_perc(const gchar *filename, SlidingWindow* sw, guint32 *item, GstClockTime *elapsed)
{
  *elapsed = now;
  slidingwindow_add_data(sw, item);
  *elapsed = now - *elapsed;

}


static void _percentile_median_pipe(gpointer udata, swpercentilecandidates_t *candidates)
{
  guint32 *result = udata;
  if(!candidates->processed){
    return;
  }

  if(!candidates->left){
      *result = *(guint32*)candidates->right;
  }else if(!candidates->right){
      *result = *(guint32*)candidates->left;
  }else{
      *result  = *(guint32*)candidates->left;
      *result += *(guint32*)candidates->right;
      *result >>= 1;
  }
}

void swperctest(void)
{
  guint32 repeat = 1000;
  guint32 *array,*array2;
  guint32 result1,result2;
  GstClockTime elapsed1, elapsed2;
  gchar filename[255];
  gint i,j,k;
  guint32 num_limits[8] = {10,100,1000,10000,100000,1000000,10000000,100000000};

  sysclock = gst_system_clock_obtain();
  array  = g_malloc0(sizeof(guint32) * 200000000);
  array2 = g_malloc0(sizeof(guint32) * 200000000);

  for(i = 0; i < 8; ++i){
    SlidingWindow* sw;
    guint num_limit = num_limits[i];
    sw = make_slidingwindow(num_limit, 0);
    slidingwindow_add_plugin(sw,
        make_swpercentile(50, bintree3cmp_uint32, (ListenerFunc)_percentile_median_pipe, &result2));
    slidingwindow_add_plugin(sw, make_swprinter(swprinter_uint32));
    sprintf(filename, "mediantest_%d.csv", num_limit);
    for(j = 0; j < num_limit; ++j){
      array[j] = g_random_int();
      slidingwindow_add_data(sw, &array[j]);
    }

    for(k = 0; k < repeat; ++k,++j){
      array[j] = g_random_int();

      //profile 1 - qsort
      _median_test_with_qsort(filename, array + (j - num_limit + 1), array2, num_limit, &result1, &elapsed1);
      //profile 2 - perc
      _median_test_with_perc(filename, sw, &array[j], &elapsed2);
      g_print("%u,%lu,%u,%lu\n", result1, elapsed1, result2, elapsed2);
      mprtp_logger(filename, "%u,%lu,%u,%lu\n", result1, elapsed1, result2, elapsed2);
    }

    slidingwindow_clear(sw);
    g_object_unref(sw);
  }

  g_object_unref(sysclock);
  g_free(array);
  g_free(array2);
}
//----------------- SWPrinter plugin --------------------------------------

void swprinter_int32(gpointer data, gchar* string)
{
  sprintf(string, "%d", *(gint32*)data);
}

void swprinter_uint32(gpointer data, gchar* string)
{
  sprintf(string, "%u", *(guint32*)data);
}

void swprinter_int64(gpointer data, gchar* string)
{
  sprintf(string, "%ld", *(gint64*)data);
}

void swprinter_uint64(gpointer data, gchar* string)
{
  sprintf(string, "%lu", *(guint64*)data);
}


static void _swprinter_add_pipe(gpointer dataptr, gpointer itemptr)
{
  gchar string[255];
  void (*sprint)(gpointer,gchar*);
  memset(string, 0, 255);

  sprint = dataptr;
  sprint(itemptr, string);
  g_print("Added item %s\n", string);
}

static void _swprinter_rem_pipe(gpointer dataptr, gpointer itemptr)
{
  gchar string[255];
  void (*sprint)(gpointer,gchar*);
  memset(string, 0, 255);

  sprint = dataptr;
  sprint(itemptr, string);
  g_print("Removed item %s\n", string);
}

SlidingWindowPlugin* make_swprinter(void (*sprint)(gpointer,gchar*))
{
  SlidingWindowPlugin* this;
  this = swplugin_ctor();
  this->add_pipe = _swprinter_add_pipe;
  this->add_data = sprint;
  this->rem_pipe = _swprinter_rem_pipe;
  this->rem_data = sprint;
  this->disposer = free;
  this->priv     = NULL;
  return this;
}

//-----------------------------------------------------------------------------------

typedef struct _swminmax{
  SlidingWindowPlugin* base;
  bintree3_t*          tree;
  swminmaxstat_t       stat;
}swminmax_t;


static swminmax_t* _swminmaxpriv_ctor(SlidingWindowPlugin* base, bintree3cmp cmp)
{
  swminmax_t* this;
  this = malloc(sizeof(swminmax_t));
  memset(this, 0, sizeof(swminmax_t));
  this->tree = make_bintree3(cmp);
  this->base = base;
  return this;
}

static void _swminmaxpriv_disposer(gpointer target)
{
  swminmax_t* this = target;
  if(!target){
    return;
  }
  bintree3_dtor(this->tree);
  free(this);
}

static void _swminmax_disposer(gpointer target)
{
  SlidingWindowPlugin* this = target;
  if(!target){
    return;
  }

  _swminmaxpriv_disposer(this->priv);
  this->priv = NULL;
  g_free(this);
}

static void _swminmax_add_pipe(gpointer dataptr, gpointer itemptr)
{
  swminmax_t* this;
  this = dataptr;
  bintree3_insert_data(this->tree, itemptr);
  this->stat.min = bintree3_get_bottom_data(this->tree);
  this->stat.max = bintree3_get_top_data(this->tree);
  swplugin_notify(this->base, &this->stat);
}

static void _swminmax_rem_pipe(gpointer dataptr, gpointer itemptr)
{
  swminmax_t* this;
  this = dataptr;
  bintree3_delete_value(this->tree, itemptr);
  this->stat.min = bintree3_get_bottom_data(this->tree);
  this->stat.max = bintree3_get_top_data(this->tree);
  swplugin_notify(this->base, &this->stat);
}

SlidingWindowPlugin* make_swminmax(bintree3cmp cmp, ListenerFunc on_calculated_cb, gpointer udata)
{
  SlidingWindowPlugin* this;
  this = swplugin_ctor();
  this = make_swplugin(on_calculated_cb, udata);
  this->priv = _swminmaxpriv_ctor(this, cmp);
  this->add_pipe = _swminmax_add_pipe;
  this->add_data = this->priv;
  this->rem_pipe = _swminmax_rem_pipe;
  this->rem_data = this->priv;
  this->disposer = _swminmax_disposer;
  return this;
}



//-----------------------------------------------------------------------------------

typedef struct _swtendency{
  SlidingWindowPlugin*    base;
  SWItemTendencyExtractor extractor;
  gint32                  counter;
  gint32                  sum;
}swtendency_t;


static swtendency_t* _swtendencypriv_ctor(SlidingWindowPlugin* base, SWItemTendencyExtractor extractor)
{
  swtendency_t* this;
  this = malloc(sizeof(swtendency_t));
  memset(this, 0, sizeof(swtendency_t));
  this->extractor = extractor;
  this->counter = 0;
  this->sum     = 0;
  this->base    = base;
  return this;
}

static void _swtendencypriv_disposer(gpointer target)
{
  swtendency_t* this = target;
  if(!target){
    return;
  }
  free(this);
}

static void _swtendency_disposer(gpointer target)
{
  SlidingWindowPlugin* this = target;
  if(!target){
    return;
  }

  _swtendencypriv_disposer(this->priv);
  this->priv = NULL;
  g_free(this);
}

static void _swtendency_add_pipe(gpointer dataptr, gpointer itemptr)
{
  swtendency_t* this;
  gdouble tendency;
  this = dataptr;
  this->sum += this->extractor(itemptr);
  ++this->counter;
  tendency = (gdouble)this->sum / (gdouble)this->counter;
  swplugin_notify(this->base, &tendency);
}

static void _swtendency_rem_pipe(gpointer dataptr, gpointer itemptr)
{
  swtendency_t* this;
  gdouble tendency;
  this = dataptr;
  this->sum -= this->extractor(itemptr);
  --this->counter;
  tendency = (gdouble)this->sum / (gdouble)this->counter;
  swplugin_notify(this->base, &tendency);
}

SlidingWindowPlugin* make_swtendency(bintree3cmp cmp, ListenerFunc on_calculated_cb, gpointer udata,
    SWItemTendencyExtractor extractor)
{
  SlidingWindowPlugin* this;
  this = swplugin_ctor();
  this = make_swplugin(on_calculated_cb, udata);
  this->priv = _swtendencypriv_ctor(this, extractor);
  this->add_pipe = _swtendency_add_pipe;
  this->add_data = this->priv;
  this->rem_pipe = _swtendency_rem_pipe;
  this->rem_data = this->priv;
  this->disposer = _swtendency_disposer;
  return this;
}




//-----------------------------------------------------------------------------------

typedef struct _swpercentile{
  SlidingWindowPlugin*      base;
  bintree3_t*               mintree;
  bintree3_t*               maxtree;
  gint32                    percentile;
  double                    ratio;
  gint32                    required;
  bintree3cmp               cmp;
  swpercentilecandidates_t  candidates;
  gint32                    Mxc,Mnc;
  gboolean                  sprinted;
}swpercentile_t;

static void _swpercentile_set_percentile(swpercentile_t* this, gint32 percentile)
{
  this->percentile      = CONSTRAIN(10,90,percentile);
  this->ratio = (double)this->percentile / (double)(100-this->percentile);
  if(this->ratio < 1.){
    this->required = (1./this->ratio) + 1;
  }else if(1. < this->ratio){
    this->required = this->ratio + 1;
  }else{
    this->required = 2;
  }
}

static swpercentile_t* _swpercentilepriv_ctor(SlidingWindowPlugin* base, gint32 percentile, bintree3cmp cmp)
{
  swpercentile_t* this;
  Recycle* bintreenode_recycle = NULL;
  bintreenode_recycle = make_recycle_for_bintreenode(16);

  this = malloc(sizeof(swpercentile_t));
  memset(this, 0, sizeof(swpercentile_t));
  this->base            = base;
  this->cmp             = cmp;

  this->mintree         = make_bintree3_with_recycle(cmp, bintreenode_recycle);
  this->maxtree         = make_bintree3_with_recycle(cmp, bintreenode_recycle);
//  this->maxtree         = make_bintree3(cmp);
//  this->mintree         = make_bintree3(cmp);
  this->Mxc = this->Mnc = 0;

  _swpercentile_set_percentile(this, percentile);

  g_object_unref(bintreenode_recycle);

  return this;
}

static void _swpercentilepriv_disposer(gpointer target)
{
  swpercentile_t* this = target;
  if(!target){
    return;
  }

  bintree3_dtor(this->mintree);
  free(this);
}

static void _swpercentile_disposer(gpointer target)
{
  SlidingWindowPlugin* this = target;
  if(!target){
    return;
  }

  _swpercentilepriv_disposer(this->priv);
  this->priv = NULL;
  free(this);
}


static void _swpercentile_calculate(swpercentile_t *this)
{
  double ratio;
  if(this->Mnc + this->Mxc < this->required){
    this->candidates.processed  = FALSE;
    this->candidates.right = this->candidates.left = NULL;
    goto done;
  }
  this->candidates.processed  = TRUE;
  ratio = (double) this->Mxc / (double) this->Mnc;

  if(this->ratio < ratio){
    this->candidates.left  = bintree3_get_top_data(this->maxtree);
    this->candidates.right = NULL;
  }else if(ratio < this->ratio){
    this->candidates.left  = NULL;
    this->candidates.right = bintree3_get_bottom_data(this->mintree);
  }else{
    this->candidates.left  = bintree3_get_top_data(this->maxtree);
    this->candidates.right = bintree3_get_bottom_data(this->mintree);
  }

done:
  if(0 < bintree3_get_nodenum(this->maxtree)){
    this->candidates.min = bintree3_get_bottom_data(this->maxtree);
  }
  if(0 < bintree3_get_nodenum(this->mintree)){
    this->candidates.max = bintree3_get_top_data(this->mintree);
  }

  swplugin_notify(this->base, &this->candidates);
}


static void _swpercentile_balancer(swpercentile_t *this)
{
  double ratio;

  if(this->Mxc + this->Mnc  < this->required){
    goto done;
  }

  if(!this->Mnc){
    goto reduce_maxtree;
  }
  if(!this->Mxc){
    goto reduce_mintree;
  }

  ratio = (double) this->Mxc / (double) this->Mnc;
  if(this->ratio < ratio)
    goto reduce_maxtree;
  else
    goto reduce_mintree;

reduce_mintree:
  ratio = (double) (this->Mxc + 1) / (double) (this->Mnc - 1);
  if(this->ratio < ratio || this->Mnc < 1) goto done;
  bintree3_insert_data(this->maxtree, bintree3_delete_bottom_data(this->mintree));
  --this->Mnc; ++this->Mxc;
  goto reduce_mintree;

reduce_maxtree:
  ratio = (double) (this->Mxc - 1) / (double) (this->Mnc + 1);
  if(ratio < this->ratio || this->Mxc < 1) goto done;
  bintree3_insert_data(this->mintree, bintree3_delete_top_data(this->maxtree));
  --this->Mxc; ++this->Mnc;
  goto reduce_maxtree;

done:
  return;
}

static void _swpercentile_on_add(gpointer dataptr, gpointer itemptr)
{
  swpercentile_t* this;
  this = dataptr;

  if(!this->Mxc){
    bintree3_insert_data(this->maxtree, itemptr);
    ++this->Mxc;
    goto done;
  }

  if(this->cmp(itemptr, bintree3_get_top_data(this->maxtree)) <= 0){
    bintree3_insert_data(this->maxtree, itemptr);
    ++this->Mxc;
  }else{
    bintree3_insert_data(this->mintree, itemptr);
    ++this->Mnc;
  }
done:
  _swpercentile_balancer(this);
  _swpercentile_calculate(this);
}

static void _swpercentile_on_rem(gpointer dataptr, gpointer itemptr)
{
  swpercentile_t* this;
  this = dataptr;

  if(bintree3_delete_value(this->maxtree, itemptr)){
    --this->Mxc;
  }else if(bintree3_delete_value(this->mintree, itemptr)){
    --this->Mnc;
  }else{
    GST_WARNING("No data with ptr%p registered by percentiletracker", itemptr);
  }

  _swpercentile_balancer(this);
  _swpercentile_calculate(this);

}

void swpercentile_set_percentile(SlidingWindowPlugin* plugin, gint32 percentile)
{
  _swpercentile_set_percentile(plugin->priv, percentile);
}

SlidingWindowPlugin* make_swpercentile(
                              gint32     percentile,
                              bintree3cmp  cmp,
                              ListenerFunc on_calculated_cb,
                              gpointer     udata
                              )
{

  SlidingWindowPlugin* this;
  this = make_swplugin(on_calculated_cb, udata);
  this->priv = _swpercentilepriv_ctor(this, percentile, cmp);

  this->add_pipe          = _swpercentile_on_add;
  this->add_data          = this->priv;
  this->rem_pipe          = _swpercentile_on_rem;
  this->rem_data          = this->priv;
  this->disposer          = _swpercentile_disposer;
  return this;

}



typedef struct _swint32stater{
  SlidingWindowPlugin* base;
  swint32stat_t        stat;
  gint32               sumsqavg;
  gint32               sqsum;
}swint32stater;



static swint32stater* _swint32summer_ctor(SlidingWindowPlugin *base)
{
  swint32stater* this;
  this = malloc(sizeof(swint32stater));
  memset(this, 0, sizeof(swint32stater));
  this->base = base;
  return this;
}


static void _swint32summer_disposer(gpointer target)
{
  SlidingWindowPlugin* this = target;
  if(!target){
    return;
  }

  free(this->priv);
  this->priv = NULL;
  free(this);
}

static void _swint32stat_calc(swint32stater* this)
{
  if(0 < this->stat.counter){
    this->stat.avg = (gdouble)(this->stat.sum) / (gdouble)this->stat.counter;
    this->sumsqavg = (gdouble)(this->stat.sum * this->stat.sum) / (gdouble)this->stat.counter;
  }

  if(1 < this->stat.counter){
    this->stat.var = (gdouble)(this->sqsum - this->sumsqavg) / (gdouble)(this->stat.counter - 1);
    this->stat.dev = sqrt(this->stat.var);
  }

  swplugin_notify(this->base, &this->stat);
}

static void _swint32stat_add_pipe(gpointer dataptr, gpointer itemptr)
{
  swint32stater* this;
  this = dataptr;
  this->stat.sum    += *(gint32*)itemptr;
  this->stat.sum_2  += *(gint32*)itemptr * *(gint32*)itemptr;
  ++this->stat.counter;

  _swint32stat_calc(this);
}

static void _swint32stat_rem_pipe(gpointer dataptr, gpointer itemptr)
{
  swint32stater* this;
  this = dataptr;
  this->stat.sum    -= *(gint32*)itemptr;
  this->stat.sum_2  -= *(gint32*)itemptr * *(gint32*)itemptr;
  --this->stat.counter;

  _swint32stat_calc(this);
}

SlidingWindowPlugin* make_swint32_stater(ListenerFunc on_calculated_cb, gpointer on_calculated_udata)
{
  SlidingWindowPlugin* this;
  this = make_swplugin(on_calculated_cb, on_calculated_udata);
  this->priv = _swint32summer_ctor(this);
  this->add_pipe = _swint32stat_add_pipe;
  this->add_data = this->priv;
  this->rem_pipe = _swint32stat_rem_pipe;
  this->rem_data = this->priv;
  this->disposer = _swint32summer_disposer;
  return this;
}





