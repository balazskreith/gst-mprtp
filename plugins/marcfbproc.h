/*
 * comarcfb_processor.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef MARCFBPROCESSOR_H_
#define MARCFBPROCESSOR_H_

#include <gst/gst.h>
#include <stdio.h>
#include "packetstracker.h"

typedef struct _MARCFBProcessor MARCFBProcessor;
typedef struct _MARCFBProcessorClass MARCFBProcessorClass;


#include "bintree2.h"
#include "numstracker.h"
#include "percentiletracker.h"
#include "mprtpspath.h"
#include "reportproc.h"

#define MARCFBPROCESSOR_TYPE             (marcfb_processor_get_type())
#define MARCFBPROCESSOR(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),MARCFBPROCESSOR_TYPE,MARCFBProcessor))
#define MARCFBPROCESSOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),MARCFBPROCESSOR_TYPE,MARCFBProcessorClass))
#define MARCFBPROCESSOR_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),MARCFBPROCESSOR_TYPE))
#define MARCFBPROCESSOR_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),MARCFBPROCESSOR_TYPE))
#define MARCFBPROCESSOR_CAST(src)        ((MARCFBProcessor *)(src))

typedef struct _MARCFBProcessorResult MARCFBProcessorResult;

struct _MARCFBProcessor
{
  GObject                  object;
  guint8                   id;
  MPRTPSPath*              path;
  GstClockTime             made;
  GstClockTime             last_stable;
  GstClock*                sysclock;
  GRWLock                  rwmutex;
  gpointer                 priv;
  GstClockTime             last_congestion;
  PercentileTracker*       delays;
  PacketsTracker*          packetstracker;
};

struct _MARCFBProcessorClass{
  GObjectClass parent_class;

};
struct _MARCFBProcessorResult{
  guint32        sender_bitrate;
  guint32        receiver_bitrate;
  guint32        goodput_bitrate;
  gdouble        utilized_rate;
  gdouble        lost_rate;
  gdouble        corrH;
  gdouble        trend;
  gdouble        g_125,g_250,g_500,g_1000;
};



GType marcfb_processor_get_type (void);
void marcfb_processor_reset(MARCFBProcessor *this);
MARCFBProcessor *make_marcfb_processor(MPRTPSPath *path);


void marcfb_processor_do(MARCFBProcessor       *this,
                         GstMPRTCPReportSummary *summary,
                         MARCFBProcessorResult *result);

void marcfb_processor_set_acfs_history(MARCFBProcessor *this,
                                        gint32 g125_length,
                                        gint32 g250_length,
                                        gint32 g500_length,
                                        gint32 g1000_length);

void marcfb_processor_get_acfs_history(MARCFBProcessor *this,
                                        gint32 *g125_length,
                                        gint32 *g250_length,
                                        gint32 *g500_length,
                                        gint32 *g1000_length);


#endif /* MARCFBPROCESSOR_H_ */
