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
  gint32     max_rate;
  gint32     min_rate;

}MPRTPSubflowMARCCngCtrlerParams;

typedef struct _MPRTPSubflowMARCRateController{
  guint32 target_bitrate;
  gint8   state;
  guint32 discarded_bitrate;
  guint32 receiver_bitrate;
  guint32 sending_bitrate;

  MPRTPSubflowMARCCngCtrlerParams cngctrler;
}MPRTPSubflowFECBasedRateAdaption;

typedef union _MPRTPSubflowRateController{
  MPRTPSubflowFECBasedRateAdaption fbra;
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
  MPRTPSubflowRateController ratectrler;
}MPRTPSubflowUtilizationSignalData;

typedef struct _MPRTPPluginSignalData{
  MPRTPSubflowUtilizationSignalData subflow[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
}MPRTPPluginSignalData;



#endif /* PLUGINS_SIGNALREPORT_H_ */
