/*
 * sefctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef SEFCTRLER_H_
#define SEFCTRLER_H_

#include <gst/gst.h>
#include <gio/gio.h>
#include <stdio.h>

#include "messenger.h"
#include "reportprod.h"
#include "reportproc.h"
#include "gstmprtpdefs.h"
#include "fecenc.h"
#include "sndtracker.h"

typedef struct _SndController SndController;
typedef struct _SndControllerClass SndControllerClass;
typedef void(*GstBufferReceiverFunc)(gpointer,GstBuffer*);
typedef void(*GstSchedulerSignaling)(gpointer, gpointer);

#define SNDCTRLER_TYPE             (sndctrler_get_type())
#define SNDCTRLER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SNDCTRLER_TYPE,SndController))
#define SNDCTRLER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SNDCTRLER_TYPE,SndControllerClass))
#define SNDCTRLER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SNDCTRLER_TYPE))
#define SNDCTRLER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SNDCTRLER_TYPE))
#define SNDCTRLER_CAST(src)        ((SndController *)(src))


struct _SndController
{
  GObject                    object;

  GstClockTime               made;
  SndSubflows*               subflows;
  ReportProcessor*           report_processor;
  ReportProducer*            report_producer;
  GstClock*                  sysclock;
  SndTracker*                sndtracker;
  Notifier*                  on_rtcp_ready;

  gboolean                   report_is_flowable;

  MPRTPPluginSignalData*     mprtp_signal_data;
  GstClockTime               time_update_period;
  gint32                     overused_subflows;

  GSList*                    controllers;
  ReportIntervalCalculator*  ricalcer;
  Messenger*                 emit_msger;

  GstClockTime               last_regular_emit;

  GstMPRTCPReportSummary     reports_summary;
};

struct _SndControllerClass{
  GObjectClass parent_class;
};




SndController* make_sndctrler(
    SndTracker*  sndtracker,
        SndSubflows* subflows,
        Notifier*    on_rtcp_ready,
        Messenger*   emit_msger);


void
sndctrler_report_can_flow (SndController *this);

void
sndctrler_time_update (SndController *this);

void
sndctrler_receive_mprtcp (SndController *this,GstBuffer * buf);

void
sndctrler_setup_siganling(gpointer ptr,
                                void(*scheduler_signaling)(gpointer, gpointer),
                                gpointer scheduler);

GType sndctrler_get_type (void);
#endif /* SEFCTRLER_H_ */
