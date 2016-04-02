/*
 * refctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef REFCTRLER_H_
#define REFCTRLER_H_

#include <gst/gst.h>

#include "mprtprpath.h"
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
  GObject          object;

  GstTask*          thread;
  GRecMutex         thread_mutex;

  GHashTable*       subflows;
  GRWLock           rwmutex;
  GstClockTime      made;
  GstClock*         sysclock;
  StreamJoiner*     joiner;
  guint32           ssrc;
  void            (*send_mprtcp_packet_func)(gpointer,GstBuffer*);
  gpointer          send_mprtcp_packet_data;
  gboolean          report_is_flowable;

  ReportProducer*   report_producer;
  ReportProcessor*  report_processor;

  FECDecoder*       fecdecoder;
  gpointer          fec_early_repaired_bytes;
  gpointer          fec_total_repaired_bytes;
  gdouble           FFRE;
  guint             orp_tick;

  GstMPRTCPReportSummary reports_summary;
};

struct _RcvControllerClass{
  GObjectClass parent_class;
};



//Class functions
void rcvctrler_setup(RcvController *this,
                     StreamJoiner* splitter,
                     FECDecoder*   fecdecoder);

void
rcvctrler_change_interval_type(
    RcvController * this,
    guint8 subflow_id,
    guint type);

void
rcvctrler_change_reporting_mode(
    RcvController *this,
    guint8 subflow_id,
    guint reports,
    guint cngctrler);

void
rcvctrler_add_path (
    RcvController *this,
    guint8 subflow_id,
    MpRTPRPath * path);


void
rcvctrler_rem_path (
    RcvController *this,
    guint8 subflow_id);

void
rcvctrler_receive_mprtcp (
    RcvController *this,
    GstBuffer * buf);

void rcvctrler_enable_auto_rate_and_cc(RcvController *this);
void rcvctrler_disable_auto_rate_and_congestion_control(RcvController *this);


void
rcvctrler_setup_callbacks(RcvController * this,
                          gpointer mprtcp_send_data,
                          GstBufferReceiverFunc mprtcp_send_func);



GType rcvctrler_get_type (void);
#endif /* REFCTRLER_H_ */
