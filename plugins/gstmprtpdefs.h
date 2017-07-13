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



#define MPRTP_DEFAULT_EXTENSION_HEADER_ID 3
#define ABS_TIME_DEFAULT_EXTENSION_HEADER_ID 8
#define FEC_PAYLOAD_DEFAULT_ID 126
#define SUBFLOW_DEFAULT_SENDING_RATE 300000

#define MPRTP_PLUGIN_MAX_SUBFLOW_NUM 32

#define DEFAULT_CC_TIMESTAMP_GENERATOR_CLOCKRATE 4000
#define DEFAULT_RTP_TIMESTAMP_GENERATOR_CLOCKRATE 90000


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
  guint32                    total_packet_lost;
  GstClockTime               owd_median;
  GstClockTime               owd_min;
  GstClockTime               owd_max;

}MPRTPSubflowUtilizationSignal;

typedef struct _MPRTPPluginSignalData{
  MPRTPSubflowUtilizationSignal     subflow[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
  gint32                            target_media_rate;
}MPRTPPluginSignal;



#endif /* PLUGINS_SIGNALREPORT_H_ */
