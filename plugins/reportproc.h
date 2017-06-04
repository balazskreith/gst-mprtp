/*
 * refctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef REPPROCER_H_
#define REPPROCER_H_

#include <gst/gst.h>
#include "streamjoiner.h"
#include "ricalcer.h"


typedef struct _ReportProcessor ReportProcessor;
typedef struct _ReportProcessorClass ReportProcessorClass;
typedef struct _GstMPRTCPReportSummary GstMPRTCPReportSummary;


#define REPORTPROCESSOR_TYPE             (report_processor_get_type())
#define REPORTPROCESSOR(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),REPORTPROCESSOR_TYPE,ReportProcessor))
#define REPORTPROCESSOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),REPORTPROCESSOR_TYPE,ReportProcessorClass))
#define REPORTPROCESSOR_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),REPORTPROCESSOR_TYPE))
#define REPORTPROCESSOR_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),REPORTPROCESSOR_TYPE))
#define REPORTPROCESSOR_CAST(src)        ((ReportProcessor *)(src))


typedef struct _GstMPRTCPXRReportSummary{
  gboolean            processed;
  struct{
    gboolean          processed;
    GstClockTime      min_delay;
    GstClockTime      max_delay;
    GstClockTime      median_delay;
    guint8            interval_metric;
  }OWD;

  struct{
    gboolean          processed;
    gboolean          early_bit;
    guint8            thinning;
    guint16           begin_seq;
    guint16           end_seq;
    gboolean          vector[1024];
    guint             vector_length;
  }LostRLE;

  struct{
    gboolean          processed;
    guint8            interval_metric;
    gboolean          early_bit;
    guint32           discarded_bytes;
  }DiscardedBytes;

  struct{
    gboolean          processed;
    guint8            interval_metric;
    gboolean          early_bit;
    guint32           discarded_packets;
  }DiscardedPackets;

  struct{
    gboolean          processed;
    guint8            report_count;
    guint32           report_timestamp;
    guint16           begin_seq;
    guint16           end_seq;
    struct {
     gboolean lost;
     gboolean ecn;
     guint16  ato;
    }vector[1024];
    guint vector_length;
  }CongestionControlFeedback;
}GstMPRTCPXRReportSummary;

struct _GstMPRTCPReportSummary{
  GstClockTime        created;
  GstClockTime        updated;
  guint32             ssrc;
  guint8              subflow_id;
  struct{
    gboolean          processed;
    GstClockTime      RTT;
    guint32           jitter;
    gdouble           lost_rate;
    guint16           HSSN;
    guint16           cycle_num;
    guint32           total_packet_lost;
  }RR;

  struct{
    gboolean          processed;
    guint64           ntptime;
    guint32           rtptime;
    guint32           packet_count;
    guint32           octet_count;
  }SR;

  GstMPRTCPXRReportSummary XR;

  struct{
    gboolean          processed;
    guint32           media_source_ssrc;
    guint32           fci_id;
    gchar             fci_data[1400];
    guint             fci_length;
  }AFB;
};



struct _ReportProcessor
{
  GObject                  object;
  GRWLock                  rwmutex;
  GstClock*                sysclock;
  GstClockTime             made;
  guint32                  ssrc;
  gsize                    length;
  gchar                    logfile[255];
};

struct _ReportProcessorClass{
  GObjectClass parent_class;
};

void report_processor_set_ssrc(ReportProcessor *this, guint32 ssrc);
void report_processor_process_mprtcp(ReportProcessor * this, GstBuffer* buffer, GstMPRTCPReportSummary* result);
void report_processor_set_logfile(ReportProcessor *this, const gchar *logfile);
GType report_processor_get_type (void);
#endif /* REPPROCER_H_ */
