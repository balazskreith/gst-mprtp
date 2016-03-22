/*
 * conetqueue_analyser.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef NETQANALYSER_H_
#define NETQANALYSER_H_

#include <gst/gst.h>
#include <stdio.h>

typedef struct _NetQueueAnalyser NetQueueAnalyser;
typedef struct _NetQueueAnalyserClass NetQueueAnalyserClass;


#include "bintree2.h"
#include "numstracker.h"
#include "percentiletracker.h"
#include "mprtpspath.h"
#include "floatsbuffer.h"
#include "reportproc.h"

#define NETQANALYSER_TYPE             (netqueue_analyser_get_type())
#define NETQANALYSER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),NETQANALYSER_TYPE,NetQueueAnalyser))
#define NETQANALYSER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),NETQANALYSER_TYPE,NetQueueAnalyserClass))
#define NETQANALYSER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),NETQANALYSER_TYPE))
#define NETQANALYSER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),NETQANALYSER_TYPE))
#define NETQANALYSER_CAST(src)        ((NetQueueAnalyser *)(src))

typedef struct _NetQueueAnalyserResult NetQueueAnalyserResult;

struct _NetQueueAnalyser
{
  GObject                  object;
  guint8                   id;
  GstClockTime             made;
  GstClockTime             last_stable;
  GstClock*                sysclock;
  GRWLock                  rwmutex;
  gpointer                 priv;
  GstClockTime             last_congestion;
  PercentileTracker*       delays;
};

struct _NetQueueAnalyserClass{
  GObjectClass parent_class;

};

struct _NetQueueAnalyserResult{
  gdouble        corrH;
  gboolean       discards;
  gboolean       rdiscards;
  gdouble        trend;
  gdouble        g_125,g_250,g_500,g_1000;
  gboolean       distortion;
  gboolean       congestion;
  GstClockTime   stability_time;
};

GType netqueue_analyser_get_type (void);
void netqueue_analyser_reset(NetQueueAnalyser *this);
NetQueueAnalyser *make_netqueue_analyser(guint8 id);
void netqueue_analyser_time_update(NetQueueAnalyser *this, gint32 sending_bitrate);
void netqueue_analyser_reset_stability(NetQueueAnalyser *this);
void netqueue_analyser_set_acfs_history(NetQueueAnalyser *this,
                                        gint32 g125_length,
                                        gint32 g250_length,
                                        gint32 g500_length,
                                        gint32 g1000_length);
void netqueue_analyser_get_acfs_history(NetQueueAnalyser *this,
                                        gint32 *g125_length,
                                        gint32 *g250_length,
                                        gint32 *g500_length,
                                        gint32 *g1000_length);
void netqueue_analyser_do(NetQueueAnalyser *this,
                          GstMPRTCPReportSummary *summary,
                          NetQueueAnalyserResult *result);
void netqueue_analyser_append_logfile(NetQueueAnalyser *this, FILE *file);
#endif /* NETQANALYSER_H_ */
