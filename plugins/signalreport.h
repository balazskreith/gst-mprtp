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
  GstClockTime        min_approve_interval;
  gdouble             approve_min_factor;
  gdouble             approve_max_factor;
  gint32              min_ramp_up_bitrate;
  gint32              max_ramp_up_bitrate;
  gint32              min_target_bitrate;
  gint32              max_target_bitrate;
  gdouble             approvement_epsilon;

  gdouble             discard_dist_treshold;
  gdouble             discard_cong_treshold;
  gdouble             owd_corr_cng_th;

  gdouble             owd_corr_dist_th;
  gboolean            reactive_cc_allowed;

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
