/*
 * smanctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef SMANCTRLER_H_
#define SMANCTRLER_H_

#include <gst/gst.h>

#include "mprtpspath.h"

typedef struct _SndManualController SndManualController;
typedef struct _SndManualControllerClass SndManualControllerClass;
typedef void(*GstBufferReceiverFunc)(gpointer,GstBuffer*);

#define SMANCTRLER_TYPE             (smanctrler_get_type())
#define SMANCTRLER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SMANCTRLER_TYPE,SndManualController))
#define SMANCTRLER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SMANCTRLER_TYPE,SndManualControllerClass))
#define SMANCTRLER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SMANCTRLER_TYPE))
#define SMANCTRLER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SMANCTRLER_TYPE))
#define SMANCTRLER_CAST(src)        ((SndManualController *)(src))


struct _SndManualController
{
  GObject          object;
};

struct _SndManualControllerClass{
  GObjectClass parent_class;
};


void smanctrler_set_callbacks(void(**riport_can_flow_indicator)(gpointer),
                              void(**controller_add_path)(gpointer,guint8,MPRTPSPath*),
                              void(**controller_rem_path)(gpointer,guint8),
                              void(**controller_pacing)(gpointer, gboolean));

GstBufferReceiverFunc
smanctrler_setup_mprtcp_exchange(SndManualController *this,
                                gpointer mprtcp_send_func_data,
                                GstBufferReceiverFunc mprtcp_send_func );


GType smanctrler_get_type (void);

#endif /* SMANCTRLER_H_ */
