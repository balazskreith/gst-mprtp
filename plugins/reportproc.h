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

struct _GstMPRTCPReportSummary{
  GstClockTime        created;
  guint32             ssrc;
  guint8              subflow_id;
  struct{
    gboolean          processed;
    GstClockTime      RTT;
    guint32           jitter;
    gdouble           lost_rate;
    guint16           HSSN;
    guint16           cycle_num;
    guint32           cum_packet_lost;
  }RR;

  struct{
    gboolean          processed;
    guint64           ntptime;
    guint32           rtptime;
    guint32           packet_count;
    guint32           octet_count;
  }SR;

  struct{
    gboolean          processed;
    GstClockTime      values[100];
    guint16           length;
    GstClockTime      offset;
  }XR_OWD_RLE;

  struct{
    gboolean          processed;
    guint8            interval_metric;
    GstClockTime      min_delay;
    GstClockTime      max_delay;
    GstClockTime      median_delay;
  }XR_OWD;

  struct{
    gboolean          processed;
    guint16           values[100];
    guint16           length;
  }XR_RFC3611;

  struct{
    gboolean          processed;
    guint8            interval_metric;
    gboolean          early_bit;
    guint32           discarded_bytes;
  }XR_RFC7243;

  struct{
    gboolean          processed;
    guint16           values[100];
    guint16           length;
    gint32            total;
  }XR_RFC7097;
};



struct _ReportProcessor
{
  GObject                  object;
  GRWLock                  rwmutex;
  GstClock*                sysclock;
  guint32                  ssrc;
  gsize                    length;
  gchar                    logfile[255];
};

struct _ReportProcessorClass{
  GObjectClass parent_class;
};

void report_processor_set_ssrc(ReportProcessor *this, guint32 ssrc);
GstMPRTCPReportSummary* report_processor_process_mprtcp(ReportProcessor * this, GstBuffer* buffer);
void report_processor_set_logfile(ReportProcessor *this, const gchar *logfile);
GType report_processor_get_type (void);
#endif /* REPPROCER_H_ */
