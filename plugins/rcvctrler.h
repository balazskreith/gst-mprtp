/*
 * refctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef REFCTRLER_H_
#define REFCTRLER_H_

#include <gst/gst.h>

#include "streamjoiner.h"
#include "ricalcer.h"
#include "sndctrler.h"
#include "streamsplitter.h"
#include "reportprod.h"
#include "reportproc.h"
#include "fecdec.h"

typedef struct _RcvController RcvController;
typedef struct _RcvControllerClass RcvControllerClass;

#define RCVCTRLER_TYPE             (rcvctrler_get_type())
#define RCVCTRLER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),RCVCTRLER_TYPE,RcvController))
#define RCVCTRLER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),RCVCTRLER_TYPE,RcvControllerClass))
#define RCVCTRLER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),RCVCTRLER_TYPE))
#define RCVCTRLER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),RCVCTRLER_TYPE))
#define RCVCTRLER_CAST(src)        ((RcvController *)(src))


struct _RcvController
{
  GObject                   object;

  RcvSubflows*              subflows;
  GstClockTime              made;
  GstClock*                 sysclock;
  guint32                   ssrc;
  gboolean                  report_is_flowable;

  Notifier*                 on_rtcp_ready;
  ReportProducer*           report_producer;
  ReportProcessor*          report_processor;

  GSList*                   fbproducers;
  RcvTracker*               rcvtracker;

  GstMPRTCPReportSummary    reports_summary;
  GstClockTime              last_time_update;
  ReportIntervalCalculator* ricalcer;

  guint16 HSN;
  gint32 last_rcvd_ts;
  gint32 rtcp_fb_frame_interval_rcvd;
  gint32 rtcp_fb_frame_interval_th;
  GstClockTime last_fb_report;
  GstClockTime fb_timeout;
};


struct _RcvControllerClass{
  GObjectClass parent_class;
};


RcvController* make_rcvctrler(
    RcvTracker* rcvtracker,
    RcvSubflows* subflows,
    Notifier* on_rtcp_ready);


void
rcvctrler_time_update (
    RcvController *this);


void
rcvctrler_receive_mprtcp (
    RcvController *this,
    GstBuffer * buf);


GType rcvctrler_get_type (void);
#endif /* REFCTRLER_H_ */
