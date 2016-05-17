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
}FBRAFBProcessorStat;

struct _FBRAFBProcessor
{
  GObject                  object;
  GRWLock                  rwmutex;
  GstClock*                sysclock;
  GQueue*                  sent;
  GQueue*                  sent_in_1s;
  GQueue*                  acked;

  FBRAFBProcessorStat      stat;
  GstClockTime             rtt;
  PercentileTracker*       owd_ltt;

  GstClockTime             last_discard;
  GstClockTime             last_delay;
  GstClockTime             last_delay_t1;
  GstClockTime             last_delay_t2;
};

struct _FBRAFBProcessorClass{
  GObjectClass parent_class;

};

GType fbrafbprocessor_get_type (void);
FBRAFBProcessor *make_fbrafbprocessor(void);

void fbrafbprocessor_reset(FBRAFBProcessor *this);
void fbrafbprocessor_track(gpointer data, guint payload_len, guint16 sn);
void fbrafbprocessor_get_stats (FBRAFBProcessor * this, FBRAFBProcessorStat* result);
void fbrafbprocessor_approve_owd(FBRAFBProcessor *this);
void fbrafbprocessor_update(FBRAFBProcessor *this, GstMPRTCPReportSummary *summary);

#endif /* FBRAFBPROCESSOR_H_ */
