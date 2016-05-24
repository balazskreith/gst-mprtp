#ifndef SUBRATECTRLER_H_
#define SUBRATECTRLER_H_

#include <gst/gst.h>
#include "mprtpspath.h"
#include "bintree.h"
#include "sndratedistor.h"
#include "reportproc.h"
#include "signalreport.h"

typedef struct _SubflowRateController SubflowRateController;
typedef struct _SubflowRateControllerClass SubflowRateControllerClass;

#define SUBRATECTRLER_TYPE             (subratectrler_get_type())
#define SUBRATECTRLER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SUBRATECTRLER_TYPE,SubflowRateController))
#define SUBRATECTRLER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SUBRATECTRLER_TYPE,SubflowRateControllerClass))
#define SUBRATECTRLER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SUBRATECTRLER_TYPE))
#define SUBRATECTRLER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SUBRATECTRLER_TYPE))
#define SUBRATECTRLER_CAST(src)        ((SubflowRateController *)(src))

typedef enum{
  SUBRATECTRLER_NO_CTRL     = 0,
  SUBRATECTRLER_FBRA_MARC   = 2,
}SubRateControllerType;


struct _SubflowRateController
{
  GObject                   object;
  GRWLock                   rwmutex;
  GstClock*                 sysclock;
  gpointer                  controller;
  MPRTPSPath*               path;
  guint8                    id;
  gboolean                  enabled;
  SubRateControllerType     type;
};

struct _SubflowRateControllerClass{
  GObjectClass parent_class;

};
GType subratectrler_get_type (void);
SubflowRateController *make_subratectrler(MPRTPSPath *path);

void subratectrler_change(SubflowRateController *this, SubRateControllerType type);
void subratectrler_report_update(SubflowRateController *this, GstMPRTCPReportSummary *summary);
void subratectrler_time_update(SubflowRateController *this);
void subratectrler_signal_update(SubflowRateController *this, MPRTPSubflowRateController *ratectrler_params);
void subratectrler_signal_request(SubflowRateController *this, MPRTPSubflowRateController *ratectrler_params);
gboolean subratectrler_packet_approver(gpointer data,GstRTPBuffer *buf);
#endif /* SUBRATECTRLER_H_ */
