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

  gboolean          stat_enabled;
  GstTask*          stat_thread;
  GRecMutex         stat_thread_mutex;

  GHashTable*       subflows;
  GRWLock           rwmutex;
  GstClock*         sysclock;
  StreamJoiner*     joiner;
  guint32           ssrc;
  void            (*send_mprtcp_packet_func)(gpointer,GstBuffer*);
  gpointer          send_mprtcp_packet_data;
  gboolean          report_is_flowable;
  gboolean          rfc7097_enabled;
  gboolean          rfc7243_enabled;
  gboolean          rfc3611_losts_enabled;
//  ReportIntervalCalculator *ricalcer;

  gboolean          enabled;


};

struct _RcvControllerClass{
  GObjectClass parent_class;
};



//Class functions
void rcvctrler_setup(RcvController *this,
                     StreamJoiner* splitter);

void
rcvctrler_add_path (
    RcvController *this,
    guint8 subflow_id,
    MpRTPRPath * path);


void
rcvctrler_rem_path (
    RcvController *this,
    guint8 subflow_id);

void rcvctrler_set_logging_flag(RcvController *this, gboolean enable);

void
rcvctrler_report_can_flow (RcvController *this);

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

void
rcvctrler_setup_discarding_reports(RcvController * this,
                          gboolean rle_reports,
                          gboolean sum_reports);

void
rcvctrler_setup_rle_lost_reports(RcvController * this,
                          gboolean enabling);

GType rcvctrler_get_type (void);
#endif /* REFCTRLER_H_ */
