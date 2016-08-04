/*
 * fbractrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef FBRATARGETCTRLER_H_
#define FBRATARGETCTRLER_H_

#include <gst/gst.h>
#include "mprtpspath.h"
#include "fbrafbproc.h"
#include "signalreport.h"

typedef struct _FBRATargetCtrler FBRATargetCtrler;
typedef struct _FBRATargetCtrlerClass FBRATargetCtrlerClass;
//typedef struct _SubflowMeasurement SubflowMeasurement;

#define FBRATARGETCTRLER_TYPE             (fbratargetctrler_get_type())
#define FBRATARGETCTRLER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),FBRATARGETCTRLER_TYPE,FBRATargetCtrler))
#define FBRATARGETCTRLER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),FBRATARGETCTRLER_TYPE,FBRATargetCtrlerClass))
#define FBRATARGETCTRLER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),FBRATARGETCTRLER_TYPE))
#define FBRATARGETCTRLER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),FBRATARGETCTRLER_TYPE))
#define FBRATARGETCTRLER_CAST(src)        ((FBRATargetCtrler *)(src))

struct _FBRATargetCtrler
{
  GObject                   object;
  guint8                    id;
  GRWLock                   rwmutex;
  GstClock*                 sysclock;
  MPRTPSPath*               path;
  SlidingWindow*            items;

  gint32                    monitoring_interval;
  GstClockTime              monitoring_started;

  GstClockTime              changed;
  GstClockTime              reached;
  GstClockTime              interval;
  GstClockTime              increased;
  GstClockTime              broke;
  GstClockTime              halted;
  GstClockTime              reverted;
  GstClockTime              probed;
  GstClockTime              refreshed;
  GstClockTime              stabilize_started;

  GstClockTime              monitoring_reached;

  gint32                    rcved_fb;
  gint32                    required_fb;
  gint32                    target_bitrate;
  gint32                    bottleneck_point;
  gint32                    stable_point;

  gboolean                  target_approvement;
  gboolean                  probe_approvement;
  gdouble                   stabilized_tendency;
  gdouble                   owd_corr;
  gpointer                  priv;
};

struct _FBRATargetCtrlerClass{
  GObjectClass parent_class;

};
GType fbratargetctrler_get_type (void);
FBRATargetCtrler *make_fbratargetctrler(MPRTPSPath *path);
void fbratargetctrler_probe(FBRATargetCtrler *this);
void _stop_probing(FBRATargetCtrler *this);
void fbratargetctrler_update_rtpavg(FBRATargetCtrler* this, gint32 payload_length);
void fbratargetctrler_set_bottleneck_point(FBRATargetCtrler* this);
void fbratargetctrler_update(FBRATargetCtrler* this, FBRAFBProcessorStat* stat);
void fbratargetctrler_halt(FBRATargetCtrler* this);
void fbratargetctrler_revert(FBRATargetCtrler* this);
void fbratargetctrler_break(FBRATargetCtrler* this);
void fbratargetctrler_accelerate(FBRATargetCtrler* this);
gboolean fbratargetctrler_get_approvement(FBRATargetCtrler* this);
gboolean fbratargetctrler_get_target_approvement(FBRATargetCtrler* this);
gboolean fbratargetctrler_get_probe_approvement(FBRATargetCtrler* this);
void fbratargetctrler_refresh_target(FBRATargetCtrler* this);

void fbratargetctrler_set_initial(FBRATargetCtrler *this, gint32 target_bitrate);

void fbratargetctrler_signal_update(FBRATargetCtrler *this, MPRTPSubflowFECBasedRateAdaption *params);
void fbratargetctrler_signal_request(FBRATargetCtrler *this, MPRTPSubflowFECBasedRateAdaption *result);

#endif /* FBRATARGETCTRLER_H_ */

