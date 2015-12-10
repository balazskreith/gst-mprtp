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
#include "streamsplitter.h"
#include "smanctrler.h"
#include "streamjoiner.h"
#include "ricalcer.h"

typedef struct _RcvEventBasedController RcvEventBasedController;
typedef struct _RcvEventBasedControllerClass RcvEventBasedControllerClass;

#define REFCTRLER_TYPE             (refctrler_get_type())
#define REFCTRLER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),REFCTRLER_TYPE,RcvEventBasedController))
#define REFCTRLER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),REFCTRLER_TYPE,RcvEventBasedControllerClass))
#define REFCTRLER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),REFCTRLER_TYPE))
#define REFCTRLER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),REFCTRLER_TYPE))
#define REFCTRLER_CAST(src)        ((RcvEventBasedController *)(src))


struct _RcvEventBasedController
{
  GObject          object;

  GstTask*          thread;
  GRecMutex         thread_mutex;

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
  ReportIntervalCalculator *ricalcer;

  BinTree*          subflow_skew_tree;
  guint8            subflow_skew_index;
  guint64           subflow_skews[256];


};

struct _RcvEventBasedControllerClass{
  GObjectClass parent_class;
};



//Class functions
void refctrler_setup(gpointer this,
                     StreamJoiner* splitter);

void refctrler_set_callbacks(void(**riport_can_flow_indicator)(gpointer),
                             void(**controller_add_path)(gpointer,guint8,MpRTPRPath*),
                             void(**controller_rem_path)(gpointer,guint8));

GstBufferReceiverFunc
refctrler_setup_mprtcp_exchange(RcvEventBasedController * this,
                                gpointer mprtcp_send_func_data,
                                GstBufferReceiverFunc mprtcp_send_func );

GType refctrler_get_type (void);
#endif /* REFCTRLER_H_ */
