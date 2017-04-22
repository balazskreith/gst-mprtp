#ifndef INCGUARD_NTRT_LIBRARY_SWPLUGINS_H_
#define INCGUARD_NTRT_LIBRARY_SWPLUGINS_H_

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include "slidingwindow.h"
#include "lib_bintree.h"


#define StructCmpFnc(funcName, Type, field) \
static gint funcName(gpointer pa, gpointer pb) \
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
  Type *left, *right, *min, *max;                  \
  left  = candidates->left;                        \
  right = candidates->right;                       \
  min   = candidates->min;                         \
  max   = candidates->max;                         \
  if(!left){                                       \
      PercResult = ((Type*)right)->field;          \
  }else if(!right){                                \
    PercResult = ((Type*)left)->field;             \
  }else{                                           \
    PercResult =  ((Type*)left)->field;            \
    PercResult += ((Type*)right)->field;           \
    PercResult/=2;                                 \
  }                                                \
  minResult = ((Type*)min)->field;                 \
  maxResult = ((Type*)max)->field;                 \
}


#define PercentileResultPipeFnc(name, TypeThis, ResultField, MinField, MaxField, Type, field, defaultResult) \
static void name(gpointer udata, swpercentilecandidates_t* candidates) \
{ \
  TypeThis* this = udata; \
  PercentileResult(Type, field, candidates, this->ResultField, this->MinField, this->MaxField, defaultResult); \
} \


#define PercentileRawResult(Type, candidates, PercResult, minResult, maxResult, defaultResult) \
if(!candidates->processed){                        \
  PercResult = defaultResult;                      \
}else{                                             \
  Type *left, *right, *min, *max;                  \
  left  = candidates->left;                        \
  right = candidates->right;                       \
  min   = candidates->min;                         \
  max   = candidates->max;                         \
  if(!left){                                       \
      PercResult = *right;                         \
  }else if(!right){                                \
    PercResult = *left;                            \
  }else{                                           \
    PercResult =  *left;                           \
    PercResult += *right;                          \
    PercResult /=2;                                \
  }                                                \
  minResult = *min;                                \
  maxResult = *max;                                \
}

#define PercentileRawResultPipeFnc(name, TypeThis, ResultType, ResultField, MinField, MaxField, defaultResult) \
static void name(gpointer udata, swpercentilecandidates_t* candidates) \
{ \
  TypeThis* this = udata; \
  PercentileRawResult(ResultType, candidates, this->ResultField, this->MinField, this->MaxField, defaultResult); \
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
  gboolean   processed;
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
                                  ListenerFunc on_calculated_cb,
                                  gpointer       udata
                          );

typedef gdouble (*SWDataExtractor)(gpointer item);


#define swplugin_define_on_calculated_double(this_type, on_calculated_fnc, on_calculated_field) \
    static void on_calculated_fnc(gpointer udata, gpointer result){ \
      ((this_type*)udata)->on_calculated_field = *(gdouble*)result; \
    }

#define swplugin_define_swdataextractor(extractor_fnc, item_type, extracted_field) \
    static gdouble extractor_fnc(gpointer itemptr){ \
      return ((item_type*)itemptr)->extracted_field; \
    }


SlidingWindowPlugin* make_swavg(ListenerFunc on_calculated_cb,
                                  gpointer       udata,
                                  SWDataExtractor   extractor
                          );


SlidingWindowPlugin* make_swstd(ListenerFunc on_calculated_cb,
                                  gpointer        udata,
                                  SWDataExtractor extractor,
                                  gint32          window_size
                          );


SlidingWindowPlugin* make_swcorr(ListenerFunc on_calculated_cb,
    gpointer udata,
    SWDataExtractor extractor1,
    SWDataExtractor extractor2,
    GstClockTime tau,
    gint         max_length);

void swcorr_set_tau(SlidingWindowPlugin* plugin, GstClockTime tau);

void swpercentile_set_percentile(
    SlidingWindowPlugin* plugin,
    gint32 percentile
    );

SlidingWindowPlugin* make_swpercentile(
                              gint32     percentile,
                              bintree3cmp  cmp,
                              ListenerFunc on_calculated_cb,
                              gpointer       udata
                              );

SlidingWindowPlugin* make_swint32_stater(ListenerFunc on_calculated_cb, gpointer on_calculated_udata);

#endif /* INCGUARD_NTRT_LIBRARY_SWPLUGINS_H_ */
