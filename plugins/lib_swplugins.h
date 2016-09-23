#ifndef INCGUARD_NTRT_LIBRARY_SWPLUGINS_H_
#define INCGUARD_NTRT_LIBRARY_SWPLUGINS_H_

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include "slidingwindow.h"
#include "lib_bintree.h"


#define StructCmpFnc(Type, field) \
static gint _statitem_##field##_cmp(gpointer pa, gpointer pb) \
{ \
  Type *a,*b; \
  a = pa; b = pb; \
  if(a->field == b->field) return 0; \
  return a->field < b->field ? -1 : 1; \
} \


#define PercentileResult(Type, field, candidates, PercResult, minResult, maxResult, defaultResult) \
if(!candidates->processed){                        \
  PercResult = defaultResult;                      \
}else{                                             \
  FBRAFBStatItem *left, *right, *min, *max;        \
  left  = candidates->left;                        \
  right = candidates->right;                       \
  min   = candidates->min;                         \
  max   = candidates->max;                         \
  if(!left){                                       \
      PercResult = ((Type*)right)->field;          \
  }else if(!right){                                \
	  PercResult = ((Type*)left)->field;           \
  }else{                                           \
	  PercResult =  ((Type*)left)->field;          \
	  PercResult += ((Type*)right)->field;         \
      PercResult>>=1;                              \
  }                                                \
  minResult = ((Type*)min)->field;                 \
  maxResult = ((Type*)max)->field;                 \
}                                                  \


#define PercentileResultPipeFnc(name, TypeThis, ResultField, MinField, MaxField, Type, field, defaultResult) \
static void name(gpointer udata, swpercentilecandidates_t* candidates) \
{ \
  TypeThis* this = udata;	\
  PercentileResult(Type, field, candidates, this->ResultField, this->MinField, this->MaxField, defaultResult); \
} \

void swperctest(void);
void swprinter_int32(gpointer data, gchar* string);
void swprinter_uint32(gpointer data, gchar* string);
void swprinter_int64(gpointer data, gchar* string);
void swprinter_uint64(gpointer data, gchar* string);
SlidingWindowPlugin* make_swprinter(void (*sprint)(gpointer,gchar*));

typedef struct swminmaxdat_struct_t{
  gpointer min;
  gpointer max;
}swminmaxstat_t;

typedef struct swpercentilecandidates_struct_t{
  gboolean  processed;
  gpointer   left;
  gpointer   right;
  gpointer   min;
  gpointer   max;
}swpercentilecandidates_t;




typedef struct swint32stat_struct_t{
  gint32  counter;
  gint32  sum;
  gint32  sum_2;
  gdouble avg;
  gdouble var;
  gdouble dev;
}swint32stat_t;

SlidingWindowPlugin* make_swminmax(bintree3cmp cmp,
                          void (*minmax_pipe)(gpointer,swminmaxstat_t*),
                          gpointer minmax_data
                          );


SlidingWindowPlugin* make_swpercentile_with_sprint(
                              gint32     percentile,
                              bintree3cmp  cmp,
                              void      (*percentile_pipe)(gpointer,swpercentilecandidates_t*),
                              gpointer       percentile_data,
                              void (*sprint)(gpointer,gchar*)
                              );

void swpercentile_fileprint_data(SlidingWindowPlugin *plugin, const gchar *filename);
SlidingWindowPlugin* make_swpercentile(
                              gint32     percentile,
                              bintree3cmp  cmp,
                              void      (*percentile_pipe)(gpointer,swpercentilecandidates_t*),
                              gpointer       percentile_data
                              );

SlidingWindowPlugin* make_swint32_stater(void (*pipe)(gpointer,swint32stat_t*),gpointer pipe_data);

#endif /* INCGUARD_NTRT_LIBRARY_SWPLUGINS_H_ */
