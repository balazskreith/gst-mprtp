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

typedef struct _MPRTPSubflowFBRACngCtrlerParams{
  //parameters can be changed
  gint32              min_monitoring_interval;
  gint32              max_monitoring_interval;
  gdouble             bottleneck_epsilon;
  gdouble             normal_monitoring_interval;
  gdouble             bottleneck_monitoring_interval;
  gint32              max_distortion_keep_time;
  gdouble             bottleneck_increasement_factor;
  gint32              min_ramp_up_bitrate;
  gint32              max_ramp_up_bitrate;
  gint32              min_target_bitrate;
  gint32              max_target_bitrate;
  gdouble             reduce_target_factor;

  GstClockTime        qdelay_congestion_treshold;
  gdouble             discad_cong_treshold;

  gdouble             distorted_trend_th;
  gdouble             keep_trend_th;
  gboolean            pacing_allowed;

}MPRTPSubflowFBRA2CngCtrlerParams;

typedef struct _MPRTPSubflowMARCRateController{
  guint32 target_bitrate;
  guint32 goodput_bitrate;
  guint32 sending_bitrate;
  MPRTPSubflowFBRA2CngCtrlerParams cngctrler;
}MPRTPSubflowFECBasedRateAdaption;

typedef struct _MPRTPSubflowManualRateController{

}MPRTPSubflowManualRateController;

typedef union _MPRTPSubflowRateController{
  MPRTPSubflowFECBasedRateAdaption fbra;
  MPRTPSubflowManualRateController manual;
}MPRTPSubflowRateController;

typedef struct _MPRTPSubflowExtendedReport{
  guint32      total_discarded_bytes;
  GstClockTime owd_median;
  GstClockTime owd_min;
  GstClockTime owd_max;
}MPRTPSubflowExtendedReport;

typedef struct _MPRTPSubflowReceiverReport{
  GstClockTime RTT;
  guint32      jitter;
  gdouble      lost_rate;
  guint16      HSSN;
  guint16      cycle_num;
  guint32      cum_packet_lost;
}MPRTPSubflowReceiverReport;

typedef struct _MPRTPSubflowUtilizationSignalData{
  MPRTPSubflowReceiverReport receiver_report;
  MPRTPSubflowExtendedReport extended_report;
  guint                      controlling_mode;
  gint8                      path_state;
  gint32                     target_bitrate;
  MPRTPSubflowRateController ratectrler;
}MPRTPSubflowUtilizationSignalData;

typedef struct _MPRTPPluginSignalData{
  MPRTPSubflowUtilizationSignalData subflow[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
  gint32                            target_media_rate;
}MPRTPPluginSignalData;



#endif /* PLUGINS_SIGNALREPORT_H_ */
