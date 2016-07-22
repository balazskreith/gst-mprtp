#include "lib_swplugins.h"
#include "gstmprtpbuffer.h"
#include <math.h>



//----------------- SWPrinter plugin --------------------------------------

void swprinter_int32(gpointer data, gchar* string)
{
  sprintf(string, "%d", *(gint32*)data);
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
  bintree3_t     *tree;
  void         (*minmax_pipe)(gpointer,swminmaxstat_t*);
  gpointer          minmax_data;
  swminmaxstat_t stat;
}swminmax_t;


static swminmax_t* _swminmaxpriv_ctor(bintree3cmp cmp, void (*minmax_pipe)(gpointer,swminmaxstat_t*), gpointer minmax_data)
{
  swminmax_t* this;
  this = malloc(sizeof(swminmax_t));
  memset(this, 0, sizeof(swminmax_t));
  this->tree = make_bintree3(cmp);
  this->minmax_pipe = minmax_pipe;
  this->minmax_data = minmax_data;
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
  free(this);
}

static void _swminmax_add_pipe(gpointer dataptr, gpointer itemptr)
{
  swminmax_t* this;
  this = dataptr;
  bintree3_insert_data(this->tree, itemptr);
  this->stat.min = bintree3_get_bottom_data(this->tree);
  this->stat.max = bintree3_get_top_data(this->tree);
  if(this->minmax_pipe){
    this->minmax_pipe(this->minmax_data, &this->stat);
  }
}

static void _swminmax_rem_pipe(gpointer dataptr, gpointer itemptr)
{
  swminmax_t* this;
  this = dataptr;
  bintree3_delete_value(this->tree, itemptr);
  this->stat.min = bintree3_get_bottom_data(this->tree);
  this->stat.max = bintree3_get_top_data(this->tree);
  if(this->minmax_pipe){
    this->minmax_pipe(this->minmax_data, &this->stat);
  }
}

SlidingWindowPlugin* make_swminmax(bintree3cmp cmp, void (*minmax_pipe)(gpointer,swminmaxstat_t*), gpointer minmax_data)
{
  SlidingWindowPlugin* this;
  this = swplugin_ctor();
  this->priv = _swminmaxpriv_ctor(cmp, minmax_pipe, minmax_data);
  this->add_pipe = _swminmax_add_pipe;
  this->add_data = this->priv;
  this->rem_pipe = _swminmax_rem_pipe;
  this->rem_data = this->priv;
  this->disposer = _swminmax_disposer;
  return this;
}




//-----------------------------------------------------------------------------------

typedef struct _swpercentile{
  bintree3_t         *mintree;
  bintree3_t         *maxtree;
  void             (*percentile_pipe)(gpointer,swpercentilecandidates_t*);
  gpointer              percentile_data;
  gint32            percentile;
  double             ratio;
  gint32            required;
  bintree3cmp         cmp;
  swpercentilecandidates_t  candidates;
  gint32            Mxc,Mnc;
  gboolean          sprinted;
}swpercentile_t;


static swpercentile_t* _swpercentilepriv_ctor(gint32    percentile,
                                              bintree3cmp cmp,
                                              void     (*percentile_pipe)(gpointer,swpercentilecandidates_t*),
                                              gpointer      percentile_data)
{
  swpercentile_t* this;
  this = malloc(sizeof(swpercentile_t));
  memset(this, 0, sizeof(swpercentile_t));

  this->percentile      = CONSTRAIN(10,90,percentile);
  this->ratio           = (double)this->percentile / (double)(100-this->percentile);
  this->cmp             = cmp;
  this->mintree = make_bintree3(cmp);
  this->maxtree = make_bintree3(cmp);
  this->percentile_pipe = percentile_pipe;
  this->percentile_data = percentile_data;
  this->Mxc = this->Mnc = 0;

  if(this->ratio < 1.){
    this->required = (1./this->ratio) + 1;
  }else if(1. < this->ratio){
    this->required = this->ratio + 1;
  }else{
    this->required = 2;
  }

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


static void _swpercentile_pipe(swpercentile_t *this)
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

  this->candidates.min = bintree3_get_bottom_data(this->maxtree);
  this->candidates.max = bintree3_get_top_data(this->mintree);

done:
  this->percentile_pipe(this->percentile_data, &this->candidates);
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

static void _swpercentile_add_pipe(gpointer dataptr, gpointer itemptr)
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
  _swpercentile_pipe(this);
}

static void _swpercentile_rem_pipe(gpointer dataptr, gpointer itemptr)
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
  _swpercentile_pipe(this);

}

SlidingWindowPlugin* make_swpercentile_with_sprint(
                              gint32     percentile,
                              bintree3cmp  cmp,
                              void      (*percentile_pipe)(gpointer,swpercentilecandidates_t*),
                              gpointer       percentile_data,
                              void (*sprint)(gpointer,gchar*)
                              )
{
  SlidingWindowPlugin *result;
  swpercentile_t* priv;
  result = make_swpercentile(percentile, cmp, percentile_pipe, percentile_data);
  priv = result->priv;
  bintree3_setsprint(priv->maxtree, sprint);
  bintree3_setsprint(priv->mintree, sprint);
  priv->sprinted = TRUE;
  return result;
}

SlidingWindowPlugin* make_swpercentile(
                              gint32     percentile,
                              bintree3cmp  cmp,
                              void      (*percentile_pipe)(gpointer,swpercentilecandidates_t*),
                              gpointer       percentile_data
                              )
{

  SlidingWindowPlugin* this;
  this = swplugin_ctor();
  this->priv = _swpercentilepriv_ctor(percentile,
                                      cmp,
                                      percentile_pipe,
                                      percentile_data);

  this->add_pipe          = _swpercentile_add_pipe;
  this->add_data          = this->priv;
  this->rem_pipe          = _swpercentile_rem_pipe;
  this->rem_data          = this->priv;
  this->disposer          = _swpercentile_disposer;
  return this;

}



typedef struct _swint32stater{
  void          (*pipe)(gpointer,swint32stat_t*);
  gpointer        pipe_data;
  swint32stat_t   stat;
  gint32          sumsqavg;
  gint32          sqsum;
}swint32stater;



static swint32stater* _swint32summer_ctor(void (*pipe)(gpointer,swint32stat_t*),gpointer pipe_data)
{
  swint32stater* this;
  this = malloc(sizeof(swint32stater));
  memset(this, 0, sizeof(swint32stater));
  this->pipe = pipe;
  this->pipe_data = pipe_data;
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
}

static void _swint32stat_add_pipe(gpointer dataptr, gpointer itemptr)
{
  swint32stater* this;
  this = dataptr;
  this->stat.sum    += *(gint32*)itemptr;
  this->stat.sum_2  += *(gint32*)itemptr * *(gint32*)itemptr;
  ++this->stat.counter;

  _swint32stat_calc(this);

  if(this->pipe){
    this->pipe(this->pipe_data, &this->stat);
  }
}

static void _swint32stat_rem_pipe(gpointer dataptr, gpointer itemptr)
{
  swint32stater* this;
  this = dataptr;
  this->stat.sum    -= *(gint32*)itemptr;
  this->stat.sum_2  -= *(gint32*)itemptr * *(gint32*)itemptr;
  --this->stat.counter;

  _swint32stat_calc(this);

  if(this->pipe){
    this->pipe(this->pipe_data, &this->stat);
  }
}

SlidingWindowPlugin* make_swint32_stater(void (*pipe)(gpointer,swint32stat_t*),gpointer pipe_data)
{
  SlidingWindowPlugin* this;
  this = swplugin_ctor();
  this->priv = _swint32summer_ctor(pipe, pipe_data);
  this->add_pipe = _swint32stat_add_pipe;
  this->add_data = this->priv;
  this->rem_pipe = _swint32stat_rem_pipe;
  this->rem_data = this->priv;
  this->disposer = _swint32summer_disposer;
  return this;
}





