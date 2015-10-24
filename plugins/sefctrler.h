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
  gboolean          pacing;

  gboolean          new_report_arrived;
  gboolean          bids_recalc_requested;
  gboolean          bids_commit_requested;
  guint32           ssrc;
  void            (*notify_system_func)(gpointer);
  gpointer          notify_system_data;
  void            (*send_mprtcp_packet_func)(gpointer,GstBuffer*);
  gpointer          send_mprtcp_packet_data;
  gboolean          riport_is_flowable;
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
                              gboolean (**controller_is_pacing)(gpointer));

GstBufferReceiverFunc
sefctrler_setup_mprtcp_exchange(SndEventBasedController * this,
                                gpointer mprtcp_send_func_data,
                                GstBufferReceiverFunc mprtcp_send_func );
GstBufferReceiverFunc
sefctrler_setup_notifications (SndEventBasedController * this,
                               gpointer data,
                               GstMpRTPSubflowsNotification func);

GType sefctrler_get_type (void);
#endif /* SEFCTRLER_H_ */
