/*
 * sefctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef SEFCTRLER_H_
#define SEFCTRLER_H_

#include <gst/gst.h>

#include "mprtpspath.h"
#include "streamsplitter.h"
#include "smanctrler.h"

typedef struct _SndEventBasedController SndEventBasedController;
typedef struct _SndEventBasedControllerClass SndEventBasedControllerClass;
typedef struct _ControllerRecord ControllerRecord;

#define SEFCTRLER_TYPE             (sefctrler_get_type())
#define SEFCTRLER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SEFCTRLER_TYPE,SndEventBasedController))
#define SEFCTRLER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SEFCTRLER_TYPE,SndEventBasedControllerClass))
#define SEFCTRLER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SEFCTRLER_TYPE))
#define SEFCTRLER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SEFCTRLER_TYPE))
#define SEFCTRLER_CAST(src)        ((SndEventBasedController *)(src))

typedef enum{
  MPRTP_CC_STATE_UNDERUSED    = -1,
  MPRTP_CC_STATE_STABLE       =  0,
  MPRTP_CC_STATE_OVERUSED     =  1,
}MpRTPCCState;

struct _SndEventBasedController
{
  GObject          object;

  GstTask*          thread;
  GRecMutex         thread_mutex;
  GHashTable*       subflows;
  GRWLock           rwmutex;
  StreamSplitter*   splitter;
  GstClock*         sysclock;
  guint             subflow_num;
  ControllerRecord* records;
  gint              records_max;
  gint              records_index;
  guint64           changed_num;
  guint32           min_delay;
  gboolean          pacing;
  GstClockTime      last_recalc_time;
  GstClockTime      RTT_max;
//  gboolean          new_report_arrived;
  gboolean          bids_recalc_requested;
  gboolean          bids_commit_requested;
  guint32           ssrc;
  void            (*send_mprtcp_packet_func)(gpointer,GstBuffer*);
  gpointer          send_mprtcp_packet_data;
  gboolean          riport_is_flowable;
  gboolean          suspicious;
  GstClockTime      suspicious_time;

  MpRTPCCState      cc_state;
  MpRTPCCState      cc_prev_state;
  GstClockTime      stability_time;
  gboolean          stability_started;


  //for stat and plot
  GstTask*          stat_thread;
  GRecMutex         stat_thread_mutex;
};

struct _SndEventBasedControllerClass{
  GObjectClass parent_class;
};



//Class functions
void sefctrler_setup(SndEventBasedController* this,
                     StreamSplitter* splitter);

void sefctrler_set_callbacks(void(**riport_can_flow_indicator)(gpointer),
                              void(**controller_add_path)(gpointer,guint8,MPRTPSPath*),
                              void(**controller_rem_path)(gpointer,guint8),
                              void(**controller_pacing)(gpointer, gboolean),
                              gboolean (**controller_is_pacing)(gpointer),
                              GstStructure* (**controller_state)(gpointer));

GstBufferReceiverFunc
sefctrler_setup_mprtcp_exchange(SndEventBasedController * this,
                                gpointer mprtcp_send_func_data,
                                GstBufferReceiverFunc mprtcp_send_func );

GType sefctrler_get_type (void);
#endif /* SEFCTRLER_H_ */
