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

#include "mprtpspath.h"
#include "sndratedistor.h"
#include "streamsplitter.h"
#include "reportprod.h"
#include "reportproc.h"
#include "fecenc.h"
#include "signalreport.h"

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

  GstTask*                   thread;
  GstClockTime               made;
  GRecMutex                  thread_mutex;
  GHashTable*                subflows;
  GRWLock                    rwmutex;
  ReportProcessor*           report_processor;
  ReportProducer*            report_producer;
  GstClock*                  sysclock;
  GstClockTime               expected_lost_detected;
  guint64                    ticknum;
  guint                      subflow_num;

  gboolean                   report_is_flowable;
  GstBufferReceiverFunc      send_mprtcp_packet_func;
  gpointer                   send_mprtcp_packet_data;
  GstSchedulerSignaling      utilization_signal_func;
  gpointer                   utilization_signal_data;
  MPRTPPluginSignalData*     mprtp_signal_data;

  FECEncoder*                fecencoder;
  guint32                    fec_sum_bitrate;
  guint32                    fec_sum_packetsrate;

  gint32                     target_bitrate_t1;
  gint32                     target_bitrate;

  SendingRateDistributor*    sndratedistor;

  GstMPRTCPReportSummary     reports_summary;
};

struct _SndControllerClass{
  GObjectClass parent_class;
};




//Class functions
void sndctrler_setup(SndController* this,
                     StreamSplitter* splitter,
                     SendingRateDistributor *pacer,
                     FECEncoder* fecencoder);

void
sndctrler_setup_callbacks(SndController *this,
                          gpointer mprtcp_send_data,
                          GstBufferReceiverFunc mprtcp_send_func,
                          gpointer utilization_signal_data,
                          GstSchedulerSignaling utilization_signal_func);

void
sndctrler_change_interval_type(
    SndController * this,
    guint8 subflow_id,
    guint type);

void
sndctrler_change_controlling_mode(
    SndController * this,
    guint8 subflow_id,
    guint mode,
    gboolean *fec_enable);

void sndctrler_setup_report_timeout(
    SndController * this,
    guint8 subflow_id,
    GstClockTime report_timeout);

void
sndctrler_rem_path (SndController *controller_ptr, guint8 subflow_id);
void
sndctrler_add_path (SndController *controller_ptr, guint8 subflow_id, MPRTPSPath * path);
void
sndctrler_report_can_flow (SndController *this);
void sndctrler_receive_mprtcp (SndController *this,GstBuffer * buf);

void
sndctrler_setup_siganling(gpointer ptr,
                                void(*scheduler_signaling)(gpointer, gpointer),
                                gpointer scheduler);

GType sndctrler_get_type (void);
#endif /* SEFCTRLER_H_ */
