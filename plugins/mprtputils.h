/*
 * mprtpdefs.h
 *
 *  Created on: Mar 14, 2016
 *      Author: balazs
 */

#ifndef PLUGINS_MPRTPDEFS_H_
#define PLUGINS_MPRTPDEFS_H_

#include <gst/gst.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include <gst/rtp/gstrtpbuffer.h>
#include "gstmprtpdefs.h"

#define CONSTRAIN(min,max,value) MAX(min, MIN(max, value))

#define DISABLE_LINE if(0)

#define current_unix_time_in_us g_get_real_time ()
#define current_unix_time_in_ms (current_unix_time_in_us / 1000L)
#define current_unix_time_in_s  (current_unix_time_in_ms / 1000L)
#define epoch_now_in_ns ((guint64)((current_unix_time_in_us + 2208988800000000LL) * 1000))
#define get_ntp_from_epoch_ns(epoch_in_ns) gst_util_uint64_scale (epoch_in_ns, (1LL << 32), GST_SECOND)
#define get_epoch_time_from_ntp_in_ns(ntp_time) gst_util_uint64_scale (ntp_time, GST_SECOND, (1LL << 32))
#define NTP_NOW get_ntp_from_epoch_ns(epoch_now_in_ns)


#define PROFILING_GT(msg, func, gt) \
{  \
  GstClockTime start, elapsed; \
  start = _now(this); \
  func; \
  elapsed = GST_TIME_AS_MSECONDS(_now(this) - start); \
  if(gt < elapsed) {g_print(msg" elapsed time in ms: %lu\n", elapsed); }\
} \


#define PROFILING(msg, func) PROFILING_GT(msg, func, 0)


typedef enum{
  RTCP_INTERVAL_REGULAR_INTERVAL_MODE      = 0,
  RTCP_INTERVAL_EARLY_RTCP_MODE            = 1,
  RTCP_INTERVAL_IMMEDIATE_FEEDBACK_MODE    = 2,
}RTCPIntervalType;

typedef enum{
  CONGESTION_CONTROLLING_TYPE_NONE         = 0,
  CONGESTION_CONTROLLING_TYPE_FRACTAL      = 2,
}CongestionControllingType;


typedef struct _MPRTPSubflowHeaderExtension MPRTPSubflowHeaderExtension;
typedef struct _RTPAbsTimeExtension RTPAbsTimeExtension;

struct _MPRTPSubflowHeaderExtension
{
  guint8 id;
  guint16 seq;
};

struct _RTPAbsTimeExtension
{
  guint8 time[3];
};

typedef struct{
  guint8   subflow_id;
  guint16  subflow_seq;
  gboolean repaired;
}LostPacket;

typedef struct{
  guint16   seqence_num;
  guint16   cycle_num;
}SubflowSeqTrack;

void do_bitxor(guint8* to, guint8* from, gint32 length);

void gst_rtp_buffer_set_mprtp_extension(GstRTPBuffer* rtp, guint8 ext_header_id, guint8 subflow_id, guint16 subflow_seq);
void gst_rtp_buffer_get_mprtp_extension(GstRTPBuffer* rtp, guint8 ext_header_id, guint8 *subflow_id, guint16 *subflow_seq);

void gst_rtp_buffer_set_abs_time_extension(GstRTPBuffer* rtp, guint8 abs_time_ext_header_id);
guint64 gst_rtp_buffer_get_abs_time_extension(GstRTPBuffer* rtp, guint8 abs_time_ext_header_id);
guint64 gst_rtp_buffer_get_abs_time_extension_new(GstRTPBuffer* rtp, guint8 abs_time_ext_header_id);

gboolean gst_buffer_is_mprtp(GstBuffer* buffer, guint8 mprtp_ext_header_id);
gboolean gst_rtp_buffer_is_mprtp(GstRTPBuffer* rtp, guint8 mprtp_ext_header_id);
gboolean gst_rtp_buffer_is_fectype(GstRTPBuffer* rtp, guint8 fec_payload_type);

guint16 subflowseqtracker_increase(SubflowSeqTrack *subseqtracker);

#endif /* PLUGINS_MPRTPDEFS_H_ */
