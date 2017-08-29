#include "lib_swplugins.h"
#include <math.h>
#include "bintree.h"

#define now (gst_clock_get_time (sysclock))
static GstClock* sysclock;
static int _cmpfunc (const void * pa, const void * pb)
{
  return *((guint32*)pa) == *((guint32*)pb) ? 0 : *((guint32*)pa) < *((guint32*)pb) ? -1 : 1;
}

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
  this->disposer = g_free;
  this->priv     = NULL;
  return this;
}

//-----------------------------------------------------------------------------------

typedef struct _swminmax{
  SlidingWindowPlugin* base;
  Bintree*             tree;
  swminmaxstat_t       stat;
  SWPluginFilterFunc   filter;
}swminmax_t;


static swminmax_t* _swminmaxpriv_ctor(SlidingWindowPlugin* base, GCompareFunc cmp)
{
  swminmax_t* this;
  this = malloc(sizeof(swminmax_t));
  memset(this, 0, sizeof(swminmax_t));
  this->tree = make_bintree(cmp);
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
  if (this->filter && !this->filter(itemptr)) {
    return;
  }
  bintree_insert_value(this->tree, itemptr);
  this->stat.min = bintree_get_bottom_value(this->tree);
  this->stat.max = bintree_get_top_value(this->tree);
  swplugin_notify(this->base, &this->stat);
}

static void _swminmax_rem_pipe(gpointer dataptr, gpointer itemptr)
{
  swminmax_t* this;
  this = dataptr;
  if (this->filter && !this->filter(itemptr)) {
      return;
    }
  bintree_delete_value(this->tree, itemptr);
  this->stat.min = bintree_get_bottom_value(this->tree);
  this->stat.max = bintree_get_top_value(this->tree);
  swplugin_notify(this->base, &this->stat);
}

void swminmax_set_filter(SlidingWindowPlugin* plugin,  SWPluginFilterFunc filter)
{
  swminmax_t* this = plugin->priv;
  this->filter = filter;
}

SlidingWindowPlugin* make_swminmax(GCompareFunc cmp, ListenerFunc on_calculated_cb, gpointer udata)
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

typedef struct _swavg{
  SlidingWindowPlugin*  base;
  SWDataExtractor       extractor;
  gint32                counter;
  gdouble               sum;
}swavg_t;


static swavg_t* _swavgpriv_ctor(SlidingWindowPlugin* base, SWDataExtractor extractor)
{
  swavg_t* this;
  this = malloc(sizeof(swavg_t));
  memset(this, 0, sizeof(swavg_t));
  this->extractor = extractor;
  this->counter = 0;
  this->sum     = 0;
  this->base    = base;
  return this;
}

static void _swavgpriv_disposer(gpointer target)
{
  swavg_t* this = target;
  if(!target){
    return;
  }
  free(this);
}

static void _swavg_disposer(gpointer target)
{
  SlidingWindowPlugin* this = target;
  if(!target){
    return;
  }

  _swavgpriv_disposer(this->priv);
  this->priv = NULL;
  g_free(this);
}

static gdouble _swavg_get_avg(swavg_t* this, gpointer itemptr, gint change){
  gdouble new_item = this->extractor(itemptr);
  if(isnan(new_item) || isinf(new_item)){
    goto done;
  }
  this->sum += new_item * change;
  this->counter+= 1 * change;
done:
  return 0 < this->counter ? this->sum / (gdouble)this->counter : 0.;
}

static void _swavg_add_pipe(gpointer dataptr, gpointer itemptr)
{
  swavg_t* this = dataptr;
  gdouble avg = _swavg_get_avg(this, itemptr, 1);
  swplugin_notify(this->base, &avg);
}

static void _swavg_rem_pipe(gpointer dataptr, gpointer itemptr)
{
  swavg_t* this = dataptr;
  gdouble avg = _swavg_get_avg(this, itemptr, -1);
  swplugin_notify(this->base, &avg);
}

SlidingWindowPlugin* make_swavg(ListenerFunc on_calculated_cb, gpointer udata,
    SWDataExtractor extractor)
{
  SlidingWindowPlugin* this;
  this = swplugin_ctor();
  this = make_swplugin(on_calculated_cb, udata);
  this->priv = _swavgpriv_ctor(this, extractor);
  this->add_pipe = _swavg_add_pipe;
  this->add_data = this->priv;
  this->rem_pipe = _swavg_rem_pipe;
  this->rem_data = this->priv;
  this->disposer = _swavg_disposer;
  return this;
}



//-----------------------------------------------------------------------------------

typedef struct _swsum{
  SlidingWindowPlugin*  base;
  SWDataExtractor       extractor;
  gdouble               sum;
}swsum_t;


static swsum_t* _swsumpriv_ctor(SlidingWindowPlugin* base, SWDataExtractor extractor)
{
  swsum_t* this;
  this = malloc(sizeof(swsum_t));
  memset(this, 0, sizeof(swsum_t));
  this->extractor = extractor;
  this->base    = base;
  this->sum     = 0;
  return this;
}

static void _swsumpriv_disposer(gpointer target)
{
  swsum_t* this = target;
  if(!target){
    return;
  }
  free(this);
}

static void _swsum_disposer(gpointer target)
{
  SlidingWindowPlugin* this = target;
  if(!target){
    return;
  }

  _swsumpriv_disposer(this->priv);
  this->priv = NULL;
  g_free(this);
}

static void _swsum_add_pipe(gpointer dataptr, gpointer itemptr)
{
  swsum_t* this = dataptr;
  this->sum += this->extractor(itemptr);
  swplugin_notify(this->base, &this->sum);
}

static void _swsum_rem_pipe(gpointer dataptr, gpointer itemptr)
{
  swsum_t* this = dataptr;
  this->sum -= this->extractor(itemptr);
  swplugin_notify(this->base, &this->sum);
}

SlidingWindowPlugin* make_swsum(ListenerFunc on_calculated_cb, gpointer udata,
    SWDataExtractor extractor)
{
  SlidingWindowPlugin* this;
  this = swplugin_ctor();
  this = make_swplugin(on_calculated_cb, udata);
  this->priv = _swsumpriv_ctor(this, extractor);
  this->add_pipe = _swsum_add_pipe;
  this->add_data = this->priv;
  this->rem_pipe = _swsum_rem_pipe;
  this->rem_data = this->priv;
  this->disposer = _swsum_disposer;
  return this;
}

//-----------------------------------------------------------------------------------

typedef struct _swstd{
  SlidingWindowPlugin*  base;
  SWDataExtractor       extractor;
  gint32                counter;
  gdouble               mean;
  gdouble               var;
  gdouble               emp;
  gdouble               sum;
  datapuffer_t*         variances;
  datapuffer_t*         items;
  GQueue*               double_recycle;
}swstd_t;


static swstd_t* _swstdpriv_ctor(SlidingWindowPlugin* base, SWDataExtractor extractor)
{
  swstd_t* this;
  this = g_malloc(sizeof(swstd_t));
  memset(this, 0, sizeof(swstd_t));
  this->extractor      = extractor;
  this->counter        = 0;
  this->emp            = 0;
  this->var            = 0;
  this->mean           = 0;
  this->base           = base;
  this->sum            = 0.;
  this->double_recycle = g_queue_new();
  return this;
}

static void _swstdpriv_disposer(gpointer target)
{
  swstd_t* this = target;
  if(!target){
    return;
  }
  if(this->variances){
    datapuffer_clear(this->variances, g_free);
    datapuffer_dtor(this->variances);
    this->variances = NULL;
  }

  if(this->items){
    datapuffer_clear(this->items, g_free);
    datapuffer_dtor(this->items);
    this->items = NULL;
  }
  g_queue_free_full(this->double_recycle, g_free);
  this->double_recycle = NULL;
  free(this);
}

static void _swstd_disposer(gpointer target)
{
  SlidingWindowPlugin* this = target;
  if(!target){
    return;
  }

  _swstdpriv_disposer(this->priv);
  this->priv = NULL;
  g_free(this);
}



static void _swstd_add_knuth_pipe(gpointer dataptr, gpointer itemptr)
{
  swstd_t* this = dataptr;
  gdouble new_item = this->extractor(itemptr);
  gdouble prev_mean = this->mean;
  gdouble dprev = new_item - prev_mean;
  gdouble dact;
  gdouble n;
  gdouble result = 0.;
  gdouble alpha = 0.;
  if(isnan(new_item) || isinf(new_item)){
    goto done;
  }

  n = ++this->counter;
  this->mean += dprev / n;
  dact = new_item - this->mean;
  if(this->counter < 2){
    goto done;
  }
  this->emp *= (n - 2.) / (n - 1.);
  this->emp += pow(dprev, 2) / n;

  alpha = MAX(.1, 1. / (gdouble) n);
//  this->var = ( (n-1.) * this->var + dprev * dact ) / n;
  this->var = alpha * dprev * dact + (1.-alpha) * this->var;
  result = sqrt(this->var);
done:
  swplugin_notify(this->base, &result);
}

static void _swstd_rem_pipe(gpointer dataptr, gpointer itemptr)
{
  swstd_t* this = dataptr;
  --this->counter;

}


static void _swstd_add_windowed_pipe(gpointer dataptr, gpointer itemptr)
{
  swstd_t* this = dataptr;
  gdouble new_item = this->extractor(itemptr);
  gdouble avg;
  gdouble result = 0.;
  gdouble n;
  gdouble new_variance;
  gdouble old_variance = 0.;
  gdouble* tmp;
  if(isnan(new_item) || isinf(new_item)){
    goto done;
  }
  if(datapuffer_isfull(this->items)){
    tmp = datapuffer_read(this->items);
    this->sum -= *tmp;
    g_queue_push_tail(this->double_recycle, tmp);
  }

  this->sum += new_item;
  tmp = g_queue_is_empty(this->double_recycle) ? g_malloc(sizeof(gdouble)) : g_queue_pop_head(this->double_recycle);
  memcpy(tmp, &new_item, sizeof(gdouble));
  datapuffer_write(this->items, tmp);

  n = datapuffer_readcapacity(this->items);
  if (n < 1.) {
    goto done;
  }

  avg = this->sum / n;
  new_variance = pow(new_item - avg, 2);
  if(datapuffer_isfull(this->variances)){
    tmp = datapuffer_read(this->variances);
    old_variance   = *tmp;
    g_queue_push_tail(this->double_recycle, tmp);
  }

  this->var += new_variance - old_variance;
  if (this->var < 1.) {
    goto done;
  }
  tmp = g_queue_is_empty(this->double_recycle) ? g_malloc(sizeof(gdouble)) : g_queue_pop_head(this->double_recycle);
  memcpy(tmp, &new_variance, sizeof(gdouble));
  datapuffer_write(this->variances, tmp);
  if(++this->counter < 2){
    result = 0.;
  }else{
    result = sqrt(this->var / n);
  }
done:
  swplugin_notify(this->base, &result);
}

SlidingWindowPlugin* make_swstd(ListenerFunc on_calculated_cb, gpointer udata,
    SWDataExtractor extractor, gint32 window_size)
{
  SlidingWindowPlugin* this;
  swstd_t* priv;
  this = swplugin_ctor();
  this = make_swplugin(on_calculated_cb, udata);
  this->priv = priv = _swstdpriv_ctor(this, extractor);
  this->add_data = this->priv;
  this->rem_data = this->priv;
  this->disposer = _swstd_disposer;
  if(window_size){
    priv->variances = datapuffer_ctor(window_size);
    priv->items     = datapuffer_ctor(window_size);
    this->add_pipe  = _swstd_add_windowed_pipe;
    this->rem_pipe  = _swstd_rem_pipe;
  }else{
    priv->variances = NULL;
    this->add_pipe  = _swstd_add_knuth_pipe;
    this->rem_pipe  = _swstd_rem_pipe;
  }
  return this;
}




//-----------------------------------------------------------------------------------

typedef struct _swcorr{
  SlidingWindowPlugin*  base;
  SWDataExtractor       extractor_1;
  SWDataExtractor       extractor_2;
  gdouble               sum_1;
  gdouble               sum_2;
  gdouble               sum_12;
  gint32                counter_1;
  gint32                counter_2;
  SlidingWindow*        delay_in_sw;
  SlidingWindow*        delay_out_sw;
  gdouble               I_1_add,I_1_rem;
}swcorr_t;

typedef struct _swcorr_item_t{
  gdouble I_1;
  gdouble I_2;
}swcorr_item_t;

static void _swcorr_delay_in_rem_pipe(swcorr_t*  dataptr, gdouble* itemptr);
static void _swcorr_delay_out_rem_pipe(swcorr_t* dataptr, gdouble* itemptr);

static swcorr_t* _swcorrpriv_ctor(SlidingWindowPlugin* base,
    SWDataExtractor extractor1,
    SWDataExtractor extractor2,
    GstClockTime tau,
    gint         max_length)
{
  swcorr_t* this;
  this = malloc(sizeof(swcorr_t));
  memset(this, 0, sizeof(swcorr_t));
  this->extractor_1   = extractor1;
  this->extractor_2   = extractor2;
  this->base          = base;
  this->delay_in_sw   = make_slidingwindow_double(max_length, tau);
  this->delay_out_sw  = make_slidingwindow_double(max_length, tau);

  slidingwindow_add_on_rem_item_cb(this->delay_in_sw,  (ListenerFunc) _swcorr_delay_in_rem_pipe,  this);
  slidingwindow_add_on_rem_item_cb(this->delay_out_sw, (ListenerFunc) _swcorr_delay_out_rem_pipe, this);

  return this;
}

static void _swcorrpriv_disposer(gpointer target)
{
  swcorr_t* this = target;
  if(!target){
    return;
  }
  g_object_unref(this->delay_in_sw);
  g_object_unref(this->delay_out_sw);
  free(this);
}

static void _swcorr_disposer(gpointer target)
{
  SlidingWindowPlugin* this = target;
  if(!target){
    return;
  }

  _swcorrpriv_disposer(this->priv);
  this->priv = NULL;
  g_free(this);
}

static gdouble _get_swcorr_g(swcorr_t* this){
  gdouble c_1 = (gdouble)(1./this->counter_1);
  gdouble c_2 = (gdouble)(1./this->counter_2);
  if(!this->counter_1 || !this->counter_2 || this->sum_1 == 0. || this->sum_2 == 0.){
    return 0.;
  }
  return (gdouble) (c_2 * this->sum_12) / (gdouble) ((c_1 * this->sum_1) * (c_2 * this->sum_2)) - 1.;
}

void _swcorr_delay_in_rem_pipe(swcorr_t* this, gdouble* I_2){
  ++this->counter_2;
  this->sum_1 += this->I_1_add;
  this->sum_2 += *I_2;
  this->sum_12 += this->I_1_add * (*I_2);
}

static void _swcorr_add_pipe(gpointer dataptr, gpointer itemptr)
{
  gdouble g = 0.;
  swcorr_t* this = dataptr;
  gdouble I_2    = this->extractor_2(itemptr);
  this->I_1_add  = this->extractor_1(itemptr);
  ++this->counter_1;
  slidingwindow_add_data(this->delay_in_sw, &I_2);

  g = _get_swcorr_g(this);
  swplugin_notify(this->base, &g);
}



void _swcorr_delay_out_rem_pipe(swcorr_t* this, gdouble* I_2){
  --this->counter_2;
  this->sum_1 -= this->I_1_rem;
  this->sum_2 -= *I_2;
  this->sum_12 -= this->I_1_rem * (*I_2);
}

static void _swcorr_rem_pipe(gpointer dataptr, gpointer itemptr)
{
  gdouble g = 0.;
  swcorr_t* this = dataptr;
  gdouble I_2    = this->extractor_2(itemptr);
  this->I_1_rem  = this->extractor_1(itemptr);
  --this->counter_1;
  slidingwindow_add_data(this->delay_out_sw, &I_2);

  g = _get_swcorr_g(this);
  swplugin_notify(this->base, &g);
}

SlidingWindowPlugin* make_swcorr(ListenerFunc on_calculated_cb,
    gpointer udata,
    SWDataExtractor extractor1,
    SWDataExtractor extractor2,
    GstClockTime tau,
    gint         max_length)
{
  SlidingWindowPlugin* this;
  this = swplugin_ctor();
  this = make_swplugin(on_calculated_cb, udata);
  this->priv = _swcorrpriv_ctor(this, extractor1, extractor2, tau, max_length);
  this->add_pipe = _swcorr_add_pipe;
  this->add_data = this->priv;
  this->rem_pipe = _swcorr_rem_pipe;
  this->rem_data = this->priv;
  this->disposer = _swcorr_disposer;
  return this;
}

void swcorr_set_tau(SlidingWindowPlugin* plugin, GstClockTime tau)
{
  swcorr_t* this = plugin->priv;
  slidingwindow_set_threshold(this->delay_in_sw,  tau);
  slidingwindow_set_threshold(this->delay_out_sw, tau);
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







//-----------------------------------------------------------------------------------

typedef struct _swpercentile2{
  SlidingWindowPlugin* base;
  Bintree* mintree;
  Bintree* maxtree;
  gint32 percentile;
  GCompareFunc cmp;
  SWExtractorFunc extractor;
  gdouble ratio;
  gint32 required;
  SWMeanCalcer mean_calcer;
  SWEstimator estimator;
}swpercentile2_t;

static swpercentile2_t* _swpercentile2priv_ctor(SlidingWindowPlugin* base, gint32 percentile,
    GCompareFunc cmp, SWExtractorFunc extractor, SWMeanCalcer mean_calcer, SWEstimator estimator)
{
  swpercentile2_t* this;
  this = malloc(sizeof(swpercentile2_t));
  memset(this, 0, sizeof(swpercentile2_t));
  this->base = base;
  this->cmp = cmp;
  this->mintree = make_bintree(cmp);
  this->maxtree = make_bintree(cmp);
  this->percentile = percentile;
  this->extractor = extractor;
  this->mean_calcer = mean_calcer;
  this->estimator = estimator;
  this->ratio = (gdouble)percentile / (gdouble)(100 - percentile);
  if (this->ratio < 1.) {
    this->required = 1./this->ratio + 1;
  } else if (1. < this->ratio) {
    this->required = this->ratio + 1;
  } else {
    this->required = 2;
  }
  return this;
}

static void _swpercentile2priv_disposer(gpointer target)
{
  swpercentile2_t* this = target;
  if(!target){
    return;
  }

  g_object_unref(this->maxtree);
  g_object_unref(this->mintree);
  free(this);
}

static void _swpercentile2_disposer(gpointer target)
{
  SlidingWindowPlugin* this = target;
  if(!target){
    return;
  }

  _swpercentile2priv_disposer(this->priv);
  this->priv = NULL;
  free(this);
}

static void _swpercentile2_get_thresholds(swpercentile2_t* this, gint32* maxtreeThreshold, gint32* mintreeThreshold) {
  gint32 mincounter = bintree_get_size(this->mintree);
  gint32 maxcounter = bintree_get_size(this->maxtree);

  if (1. < this->ratio) {
    *maxtreeThreshold = ceil((gdouble) mincounter * this->ratio);
    *mintreeThreshold = floor((gdouble) maxcounter / this->ratio) + 1;
  } else if (this->ratio < 1.) {
    *maxtreeThreshold = floor((gdouble) mincounter * this->ratio) + 1;
    *mintreeThreshold = ceil((gdouble) maxcounter / this->ratio);
  } else {
    *maxtreeThreshold = mincounter + 1;
    *mintreeThreshold = maxcounter + 1;
  }
}

static void _swpercentile2_balancing(swpercentile2_t* this) {
  Bintree* mintree = this->mintree;
  Bintree* maxtree = this->maxtree;
  gint32 mincounter = bintree_get_size(mintree);
  gint32 maxcounter = bintree_get_size(maxtree);
  gint32 total = mincounter + maxcounter;

  if (total < this->required) {
    return;
  }

  if (mincounter < 1) {
    bintree_insert_node_at_bottom(mintree, bintree_pop_top_node(maxtree));
    return;
  }

  if (maxcounter < 1) {
    bintree_insert_node_at_top(maxtree, bintree_pop_bottom_node(mintree));
    return;
  }

  do{
    gint32 mintreeNodeLength, maxtreeNodeLength;
    gint32 maxtreeThreshold, mintreeThreshold;
    gboolean popFromMinAllowed = 1 < bintree_get_node_counter(mintree);
    gboolean popFromMaxAllowed = 1 < bintree_get_node_counter(maxtree);
    if (!popFromMaxAllowed || !popFromMaxAllowed) {
      break;
    }
    mintreeNodeLength = bintree_get_bottom_node(mintree)->values_length;
    maxtreeNodeLength = bintree_get_top_node(maxtree)->values_length;
    _swpercentile2_get_thresholds(this, &maxtreeThreshold, &mintreeThreshold);
    popFromMaxAllowed &= mincounter + maxtreeNodeLength <= mintreeThreshold;
    popFromMinAllowed &= maxcounter + mintreeNodeLength <= maxtreeThreshold;
    mincounter = bintree_get_size(mintree);
    maxcounter = bintree_get_size(maxtree);
    if (maxtreeThreshold < maxcounter && popFromMaxAllowed) {
      bintree_insert_node_at_bottom(mintree, bintree_pop_top_node(maxtree));
//      g_print("bintree_insert_node_at_bottom\n");
      continue;
    }
    if (mintreeThreshold < mincounter && popFromMinAllowed) {
      bintree_insert_node_at_top(maxtree, bintree_pop_bottom_node(mintree));
//      g_print("bintree_insert_node_at_top\n");
      continue;
    }
    break;
  }while(1);
}

static void _swpercentile2_calculate(swpercentile2_t* this) {
    gpointer selected = NULL;
    Bintree* mintree = this->mintree;
    Bintree* maxtree = this->maxtree;
    gint32 mincounter = bintree_get_size(mintree);
    gint32 maxcounter = bintree_get_size(maxtree);
    gint32 total = mincounter + maxcounter;
    BintreeNode* left;
    BintreeNode* right;
    gdouble position;
    gboolean useOneIndex;

    if (total < 1) {
      return;
    }
    if (total < this->required || mincounter < 1 || maxcounter < 1) {
      if (!this->estimator || (mincounter < 1 && maxcounter < 1)) {
        return;
      }
      if (mincounter < 1) {
        left = bintree_get_bottom_node(maxtree);
        right = bintree_get_top_node(maxtree);
      } else if (maxcounter < 1){
        left = bintree_get_bottom_node(mintree);
        right = bintree_get_top_node(mintree);
      } else {
        left = bintree_get_top_node(maxtree);
        right = bintree_get_bottom_node(mintree);
      }
      selected = this->estimator(left->values->data, right->values->data);
//      swplugin_notify(this->base, this->extractor(selected));
      return;
    }

    left = bintree_get_top_node(maxtree);
    right = bintree_get_bottom_node(mintree);
    if(!left && !right) {
      return;
    } else if(!left) {
      swplugin_notify(this->base, this->extractor(right));
      return;
    } else if(!right) {
      swplugin_notify(this->base, this->extractor(left));
      return;
    }

    position = (gdouble)total * (this->percentile / 100.0);
    useOneIndex = floor(position) != position;
    if (this->ratio == 1.0) {
      useOneIndex = total %2 == 1;
    }

    if (useOneIndex) {
      gdouble ratio = (gdouble)maxcounter / (gdouble)mincounter;
      if (this->ratio < ratio) {
        selected = left->values->data;
      } else if (ratio < this->ratio) {
        selected = right->values->data;
      } else if (this->ratio < 1) {
        selected = right->values->data;
      } else {
        selected = left->values->data;
      }
    } else {
      // let position = total * (this.percentile / 100.0);
      gint32 index1 = floor(position);
      gint32 index2 = ceil(position + 1.0);
      if (index2 <= maxcounter) {
        selected = left->values->data;
      } else if (index1 <= maxcounter) {
        if(this->mean_calcer) {
          selected = this->mean_calcer(left->values->data, right->values->data);
        } else {
          selected = left->values->data;
        }
      } else {
        selected = right->values->data;
      }
    }

    {
      gpointer result = this->extractor(selected);
//      g_print("result: %u\n", *(guint32*)result);
      swplugin_notify(this->base, result);
    }
  }

static void _swpercentile2_on_add(gpointer dataptr, gpointer value)
{
  swpercentile2_t* this;
  this = dataptr;
  if (bintree_get_node_counter(this->maxtree) < 1 && bintree_get_node_counter(this->mintree) < 1) {
    bintree_insert_value(this->maxtree, value);
    return;
  }

  if (bintree_get_node_counter(this->maxtree) < 1) {
    if (this->cmp(value, bintree_get_bottom_value(this->mintree)) < 0) {
      bintree_insert_value(this->maxtree, value);
    } else {
      bintree_insert_value(this->mintree, value);
    }
    return;
  }

  if (bintree_get_node_counter(this->mintree) < 1) {
    if (this->cmp(bintree_get_top_value(this->maxtree), value) < 0) {
      bintree_insert_value(this->mintree, value);
    } else {
      bintree_insert_value(this->maxtree, value);
    }
    return;
  }

  if (this->cmp(value, bintree_get_top_value(this->maxtree)) <= 0) {
    bintree_insert_value(this->maxtree, value);
  } else {
    bintree_insert_value(this->mintree, value);
  }
  _swpercentile2_balancing(this);
  _swpercentile2_calculate(this);
//  bintree_print(this->maxtree);
//  g_print("-----------------\n");
//  bintree_print(this->mintree);
//  g_print("==================\n");
}

static void _swpercentile2_on_rem(gpointer dataptr, gpointer value)
{
  swpercentile2_t* this;
  this = dataptr;
  if (bintree_get_size(this->maxtree) < 1) {
    bintree_delete_value(this->mintree, value);
    goto done;
  }

  if (this->cmp(value, bintree_get_top_value(this->maxtree)) <= 0) {
    bintree_delete_value(this->maxtree, value);
  } else {
    bintree_delete_value(this->mintree, value);
  }
done:
  _swpercentile2_calculate(this);
}

gpointer swpercentile2_prefer_left_selector(gpointer left, gpointer right) {
  return left ? left : right;
}

gpointer swpercentile2_prefer_right_selector(gpointer left, gpointer right) {
  return right ? right : left;
}

gpointer swpercentile2_self_extractor(gpointer value) {
  return value;
}

SlidingWindowPlugin* make_swpercentile2(
                              gint32     percentile,
                              GCompareFunc  cmp,
                              ListenerFunc on_calculated_cb,
                              gpointer     udata,
                              SWExtractorFunc extractor,
                              SWMeanCalcer mean_calcer,
                              SWEstimator estimator
                              )
{

  SlidingWindowPlugin* this;
  this = make_swplugin(on_calculated_cb, udata);
  this->priv = _swpercentile2priv_ctor(this, percentile, cmp, extractor, mean_calcer, estimator);

  this->add_pipe          = _swpercentile2_on_add;
  this->add_data          = this->priv;
  this->rem_pipe          = _swpercentile2_on_rem;
  this->rem_data          = this->priv;
  this->disposer          = _swpercentile2_disposer;
  return this;

}



//-----------------------------------------------------------------------------------

typedef struct _swlinpercentile{
  SlidingWindowPlugin* base;
  gint32 percentile;
  GCompareFunc cmp;
  SWExtractorFunc extractor;
  gdouble ratio;
  gint32 required;
  GQueue* items;
  gpointer* array;
  gint32 array_length;
  SWMeanCalcer mean_calcer;
}swlinpercentile_t;

static swlinpercentile_t* _swlinpercentilepriv_ctor(SlidingWindowPlugin* base, gint32 percentile,
    GCompareFunc cmp, SWExtractorFunc extractor, SWMeanCalcer mean_calcer)
{
  swlinpercentile_t* this;
  this = malloc(sizeof(swlinpercentile_t));
  memset(this, 0, sizeof(swlinpercentile_t));
  this->base = base;
  this->cmp = cmp;
  this->percentile = percentile;
  this->extractor = extractor;
  this->mean_calcer = mean_calcer;
  this->ratio = (gdouble)percentile / (gdouble)(100 - percentile);
  if (this->ratio < 1.) {
    this->required = 1./this->ratio + 1;
  } else if (1. < this->ratio) {
    this->required = this->ratio + 1;
  } else {
    this->required = 2;
  }
  this->items = g_queue_new();
  return this;
}

static void _swlinpercentilepriv_disposer(gpointer target)
{
  swlinpercentile_t* this = target;
  if(!target){
    return;
  }

  free(this);
}

static void _swlinpercentile_disposer(gpointer target)
{
  SlidingWindowPlugin* this = target;
  if(!target){
    return;
  }

  _swlinpercentilepriv_disposer(this->priv);
  this->priv = NULL;
  free(this);
}

static void _swlinpercentile_queue_foreach_helper(gpointer item, swlinpercentile_t* this) {
  this->array[this->array_length++] = item;
}

static void _swlinpercentile_calculate(swlinpercentile_t* this) {
  gpointer selected;
  gdouble position;
  this->array = g_malloc0(sizeof(gpointer) * g_queue_get_length(this->items));
  this->array_length = 0;
  g_queue_foreach(this->items, (GFunc) _swlinpercentile_queue_foreach_helper, this);
  qsort(this->array, this->array_length, sizeof(gpointer), this->cmp);
  position = (gdouble) this->array_length * ((gdouble) this->percentile / 100.);
  if(floor(position) == position) {
    gint index = position;
    selected = this->mean_calcer(this->array[index-1], this->array[index]);
  } else {
    gint index = floor(position);
    selected = this->array[index];
  }
  swplugin_notify(this->base, this->extractor(selected));
  g_free(this->array);
}

static void _swlinpercentile_on_add(gpointer dataptr, gpointer value)
{
  swlinpercentile_t* this;
  this = dataptr;
  g_queue_push_tail(this->items, value);
  _swlinpercentile_calculate(this);
}

static void _swlinpercentile_on_rem(gpointer dataptr, gpointer value)
{
  swlinpercentile_t* this = dataptr;
  g_queue_pop_head(this->items);
  _swlinpercentile_calculate(this);
}

SlidingWindowPlugin* make_swlinpercentile(
                              gint32     percentile,
                              GCompareFunc  cmp,
                              ListenerFunc on_calculated_cb,
                              gpointer     udata,
                              SWExtractorFunc extractor,
                              SWMeanCalcer mean_calcer
                              )
{

  SlidingWindowPlugin* this;
  this = make_swplugin(on_calculated_cb, udata);
  this->priv = _swlinpercentilepriv_ctor(this, percentile, cmp, extractor, mean_calcer);

  this->add_pipe          = _swlinpercentile_on_add;
  this->add_data          = this->priv;
  this->rem_pipe          = _swlinpercentile_on_rem;
  this->rem_data          = this->priv;
  this->disposer          = _swlinpercentile_disposer;
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





