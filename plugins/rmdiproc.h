/*
 * cormdi_processor.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef RMDIPROCESSOR_H_
#define RMDIPROCESSOR_H_

#include <gst/gst.h>
#include <stdio.h>

#include "packetssndtracker.h"

typedef struct _RMDIProcessor RMDIProcessor;
typedef struct _RMDIProcessorClass RMDIProcessorClass;


#include "bintree2.h"
#include "numstracker.h"
#include "percentiletracker.h"
#include "mprtpspath.h"
#include "reportproc.h"

#define RMDIPROCESSOR_TYPE             (rmdi_processor_get_type())
#define RMDIPROCESSOR(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),RMDIPROCESSOR_TYPE,RMDIProcessor))
#define RMDIPROCESSOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),RMDIPROCESSOR_TYPE,RMDIProcessorClass))
#define RMDIPROCESSOR_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),RMDIPROCESSOR_TYPE))
#define RMDIPROCESSOR_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),RMDIPROCESSOR_TYPE))
#define RMDIPROCESSOR_CAST(src)        ((RMDIProcessor *)(src))

typedef struct _RMDIProcessorResult RMDIProcessorResult;

struct _RMDIProcessorResult{
  guint32        max_bytes_in_flight;
  guint32        bytes_in_flight;
  guint32        sent_packets;
  guint32        sender_bitrate;
  guint32        goodput_bitrate;
  gdouble        utilized_fraction;
  GstClockTime   qdelay_actual;
  GstClockTime   qdelay_median;
  gboolean       owd_processed;
  gdouble        owd_corr;
};

struct _RMDIProcessor
{
  GObject                  object;
  guint8                   id;
  MPRTPSPath*              path;
  GstClockTime             made;
  GstClockTime             last_stable;
  GstClock*                sysclock;
  GRWLock                  rwmutex;
  gpointer                 priv;
  PercentileTracker*       delays;
  PacketsSndTracker*       packetstracker;
  NumsTracker*             bytes_in_flight;
  guint16                  last_HSSN;
  guint32                  last_disc_packets_num;
  GstClockTime             last_delay;
  GstClockTime             last_delay_t1;
  GstClockTime             last_delay_t2;

  RMDIProcessorResult      result;
};

struct _RMDIProcessorClass{
  GObjectClass parent_class;

};




GType rmdi_processor_get_type (void);
void rmdi_processor_reset(RMDIProcessor *this);
RMDIProcessor *make_rmdi_processor(MPRTPSPath *path);


void rmdi_processor_do(RMDIProcessor       *this,
                         GstMPRTCPReportSummary *summary,
                         RMDIProcessorResult *result);

void rmdi_processor_approve_owd(RMDIProcessor *this);

void rmdi_processor_set_acfs_history(RMDIProcessor *this,
                                        gint32 g125_length,
                                        gint32 g250_length,
                                        gint32 g500_length,
                                        gint32 g1000_length);

void rmdi_processor_get_acfs_history(RMDIProcessor *this,
                                        gint32 *g125_length,
                                        gint32 *g250_length,
                                        gint32 *g500_length,
                                        gint32 *g1000_length);


#endif /* RMDIPROCESSOR_H_ */
