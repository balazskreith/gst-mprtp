/*
 * signalreport.h
 *
 *  Created on: Apr 1, 2016
 *      Author: balazs
 */

#ifndef PLUGINS_SIGNALREPORT_H_
#define PLUGINS_SIGNALREPORT_H_

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include "mprtpdefs.h"

typedef struct _MPRTPSubflowUtilizationSignalData{
  guint                      controlling_mode;
  gint8                      path_state;
  gint8                      path_flags_value;
  gint32                     target_bitrate;

  gdouble                    RTT;
  guint32                    jitter;
  gdouble                    lost_rate;
  guint16                    HSSN;
  guint16                    cycle_num;
  guint32                    cum_packet_lost;
  GstClockTime               owd_median;
  GstClockTime               owd_min;
  GstClockTime               owd_max;

}MPRTPSubflowUtilizationSignalData;

typedef struct _MPRTPPluginSignalData{
  MPRTPSubflowUtilizationSignalData subflow[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
  gint32                            target_media_rate;
}MPRTPPluginSignalData;



#endif /* PLUGINS_SIGNALREPORT_H_ */
