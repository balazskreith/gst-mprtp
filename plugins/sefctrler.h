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
#include "bintree.h"

typedef struct _SndEventBasedController SndEventBasedController;
typedef struct _SndEventBasedControllerClass SndEventBasedControllerClass;
typedef struct _SplitCtrlerMoment SplitCtrlerMoment;

#define SEFCTRLER_TYPE             (sefctrler_get_type())
#define SEFCTRLER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SEFCTRLER_TYPE,SndEventBasedController))
#define SEFCTRLER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SEFCTRLER_TYPE,SndEventBasedControllerClass))
#define SEFCTRLER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SEFCTRLER_TYPE))
#define SEFCTRLER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SEFCTRLER_TYPE))
#define SEFCTRLER_CAST(src)        ((SndEventBasedController *)(src))



typedef enum
{
  SPLITCTRLER_EVENT_DISTORTION    = -2,
  SPLITCTRLER_EVENT_CONGESTION    = -1,
  SPLITCTRLER_EVENT_FI            =  0,
  SPLITCTRLER_EVENT_CLEARANCE     =  1,
  SPLITCTRLER_EVENT_SETTLEMENT    =  2,
} SplitCtrlerEvent;

typedef enum
{
  SPLITCTRLER_STATE_OVERUSED    = -1,
  SPLITCTRLER_STATE_STABLE      =  0,
  SPLITCTRLER_STATE_UNDERUSED   =  1,
} SplitCtrlerState;

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
  BinTree*          subflow_delays_tree;
  guint8            subflow_delays_index;
  guint64           subflow_delays[256];
  SplitCtrlerMoment*splitctrler_moments;
  gint              splitctrler_index;
  guint64           changed_num;
  gboolean          pacing;
  GstClockTime      last_recalc_time;
  GstClockTime      RTT_max;
//  gboolean          new_report_arrived;
  gboolean          bids_recalc_requested;
  gboolean          bids_commit_requested;
  guint32           ssrc;
  void            (*send_mprtcp_packet_func)(gpointer,GstBuffer*);
  gpointer          send_mprtcp_packet_data;
  gboolean          report_is_flowable;
  gboolean          suspicious;
  GstClockTime      suspicious_time;

  GstClockTime      stability_time;
  gboolean          stability_started;
  SplitCtrlerState  state;
//
//  //for stat and plot
//  GstTask*          stat_thread;
//  GRecMutex         stat_thread_mutex;
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
