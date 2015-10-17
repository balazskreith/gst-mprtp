/*
 * rmanctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef RMANCTRLER_H_
#define RMANCTRLER_H_

#include <gst/gst.h>

#include "mprtprpath.h"
#include "smanctrler.h"
#include "streamjoiner.h"

typedef struct _RcvManualController RcvManualController;
typedef struct _RcvManualControllerClass RcvManualControllerClass;

#define RMANCTRLER_TYPE             (rmanctrler_get_type())
#define RMANCTRLER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),RMANCTRLER_TYPE,RcvManualController))
#define RMANCTRLER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),RMANCTRLER_TYPE,RcvManualControllerClass))
#define RMANCTRLER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),RMANCTRLER_TYPE))
#define RMANCTRLER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),RMANCTRLER_TYPE))
#define RMANCTRLER_CAST(src)        ((RcvManualController *)(src))


struct _RcvManualController
{
  GObject          object;
  StreamJoiner*    joiner;
};

struct _RcvManualControllerClass{
  GObjectClass parent_class;
};


void rmanctrler_setup(gpointer this,
                     StreamJoiner* splitter);
//Class functions
void rmanctrler_set_callbacks(void(**riport_can_flow_indicator)(gpointer),
                             void(**controller_add_path)(gpointer,guint8,MpRTPRPath*),
                             void(**controller_rem_path)(gpointer,guint8));
GstBufferReceiverFunc
rmanctrler_setup_mprtcp_exchange(RcvManualController *this,
                                gpointer mprtcp_send_func_data,
                                GstBufferReceiverFunc mprtcp_send_func );

GType rmanctrler_get_type (void);

#endif /* RMANCTRLER_H_ */
