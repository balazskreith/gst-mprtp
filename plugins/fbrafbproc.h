/*
 * fbrafbprocessor.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FBRAFBPROCESSOR_H_
#define FBRAFBPROCESSOR_H_

#include <gst/gst.h>
#include "reportproc.h"
#include "lib_swplugins.h"

typedef struct _FBRAFBProcessor FBRAFBProcessor;
typedef struct _FBRAFBProcessorClass FBRAFBProcessorClass;

#define FBRAFBPROCESSOR_TYPE             (fbrafbprocessor_get_type())
#define FBRAFBPROCESSOR(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FBRAFBPROCESSOR_TYPE,FBRAFBProcessor))
#define FBRAFBPROCESSOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FBRAFBPROCESSOR_TYPE,FBRAFBProcessorClass))
#define FBRAFBPROCESSOR_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FBRAFBPROCESSOR_TYPE))
#define FBRAFBPROCESSOR_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FBRAFBPROCESSOR_TYPE))
#define FBRAFBPROCESSOR_CAST(src)        ((FBRAFBProcessor *)(src))


typedef struct _FBRAFBProcessorStat
{
  gint32                   bytes_in_flight;
  gint32                   packets_in_flight;
  gint32                   sent_bytes_in_1s;
  gint32                   sent_packets_in_1s;
  gint32                   goodput_bytes;
  GstClockTime             owd_ltt_median;
  GstClockTime             owd_stt_median;
  gdouble                  owd_corr;
  gboolean                 recent_discarded;
  gdouble                  stability;
  GstClockTime             RTT;
  gdouble                  srtt;
  gint32                   discarded_packets_in_1s;
  gint32                   received_packets_in_1s;
//  gint64                   max_bytes_in_flight;
}FBRAFBProcessorStat;

struct _FBRAFBProcessor
{
  GObject                  object;
  GRWLock                  rwmutex;
  GstClock*                sysclock;
  GQueue*                  sent;
  GQueue*                  sent_in_1s;
  GQueue*                  acked;
  guint8                   subflow_id;

  struct{
    GstClockTime median,min,max;
  }owd_stat;

  gint32                   measurements_num;

  SlidingWindow           *owd_sw;
  FBRAFBProcessorStat      stat;

  GstClockTime             congestion_detected;
  GstClockTime             last_discard;

};

struct _FBRAFBProcessorClass{
  GObjectClass parent_class;

};

GType fbrafbprocessor_get_type (void);
FBRAFBProcessor *make_fbrafbprocessor(guint8 subflow_id);

void fbrafbprocessor_reset(FBRAFBProcessor *this);
void fbrafbprocessor_track(gpointer data, guint payload_len, guint16 sn);
void fbrafbprocessor_get_stats (FBRAFBProcessor * this, FBRAFBProcessorStat* result);
gint32 fbrafbprocessor_get_sent_bytes_in_1s(FBRAFBProcessor *this);
GstClockTime fbrafbprocessor_get_fbinterval(FBRAFBProcessor *this);
void fbrafbprocessor_record_congestion(FBRAFBProcessor *this);
void fbrafbprocessor_refresh_owd_ltt(FBRAFBProcessor *this);
void fbrafbprocessor_update(FBRAFBProcessor *this, GstMPRTCPReportSummary *summary);

#endif /* FBRAFBPROCESSOR_H_ */
