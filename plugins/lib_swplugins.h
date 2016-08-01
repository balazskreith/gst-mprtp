#ifndef INCGUARD_NTRT_LIBRARY_SWPLUGINS_H_
#define INCGUARD_NTRT_LIBRARY_SWPLUGINS_H_

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include "slidingwindow.h"
#include "lib_bintree.h"

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
