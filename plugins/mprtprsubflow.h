/*
 * mprtpssubflow.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef MPRTPRSUBFLOW_H_
#define MPRTPRSUBFLOW_H_

#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include "gstmprtcpbuffer.h"

G_BEGIN_DECLS

typedef struct _MPRTPReceiverSubflow MPRTPRSubflow;
typedef struct _MPRTPReceiverSubflowClass MPRTPRSubflowClass;


#define MPRTPR_SUBFLOW_TYPE             (mprtpr_subflow_get_type())
#define MPRTPR_SUBFLOW(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),MPRTPR_SUBFLOW_TYPE,MPRTPRSubflow))
#define MPRTPR_SUBFLOW_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),MPRTPR_SUBFLOW_TYPE,MPRTPRSubflowClass))
#define MPRTPR_SUBFLOW_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),MPRTPR_SUBFLOW_TYPE))
#define MPRTPR_SUBFLOW_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),MPRTPR_SUBFLOW_TYPE))
#define MPRTPR_SUBFLOW_CAST(src)        ((MPRTPRSubflow *)(src))



struct _MPRTPReceiverSubflow{
  GObject              object;
  guint16              id;
  GList*               gaps;
  GList*               result;
  guint16              received_since_cycle_is_increased;
  guint16              late_discarded;
  guint32              late_discarded_bytes;
  guint16              early_discarded;
  guint16              duplicated;
  guint16              actual_seq;
  guint8               ext_header_id;
  gboolean             seq_initialized;
  gboolean             skew_initialized;
  guint16              cycle_num;
  guint16              packet_received;
  guint32              jitter;
  guint16              HSN;
  guint16              packet_losts;
  gboolean             distortion;
  GstClockTime         distortion_happened;
  gfloat               packet_lost_rate;
  guint32              cum_packet_losts;
  GRWLock              rwmutex;

  GstClock*            sysclock;
  GstClockTime         LSR;
  guint32              ssrc;
  GMutex               mutex;

  GstClockTime         rr_riport_normal_period_time;
  guint                packet_limit_to_riport;
  gboolean             urgent_riport_is_requested;
  gboolean             allow_early;
  gboolean             rr_started;
  gdouble              avg_rtcp_size;
  gdouble              avg_rtp_size;
  gdouble              rr_riport_bw;
  gdouble              media_bw_avg;
  GstClockTime         rr_riport_time;
  GstClockTime         rr_riport_interval;
  gboolean             rr_paths_congestion_riport_is_started;
  GstClockTime         rr_paths_changing_riport_started;
  GstClockTime         rr_riport_timeout_interval;
  GstClockTime         last_riport_sent_time;

  GstClockTime         last_received_time;
  guint64              ext_rtptime;
  //GstClockTime         last_sent_time;
  guint64              last_packet_skew;
  guint64              skews[100];
  guint64              received_times[100];
  guint8               skews_write_index;
  guint8               skews_read_index;
  gboolean             active;
  gboolean             lost_started_riporting;
  GstClockTime         lost_started_riporting_time;
  gboolean             settled_started_riporting;
  GstClockTime         settled_started_riporting_time;

  void                 (*process_mprtp_packets)(MPRTPRSubflow*,GstBuffer*, guint16 subflow_sequence);
  void                 (*process_mprtcpsr_packets)(MPRTPRSubflow*,GstRTCPBuffer*);
  void                 (*setup_mprtcpsr_packets)(MPRTPRSubflow*,GstRTCPBuffer*);
  void                 (*proc_mprtcpblock)(MPRTPRSubflow*,GstMPRTCPSubflowBlock*);
  gboolean             (*is_active)(MPRTPRSubflow*);
  gboolean             (*is_early_discarded_packets)(MPRTPRSubflow*);
  guint64              (*get_skews_median)(MPRTPRSubflow*);
  void                 (*add_packet_skew)(MPRTPRSubflow*,guint32,guint32);
  GList*               (*get_packets)(MPRTPRSubflow*);
  void                 (*setup_sr_riport)(MPRTPRSubflow*, GstMPRTCPSubflowRiport*);
  guint16              (*get_id)(MPRTPRSubflow*);
  gboolean             (*do_riport_now)(MPRTPRSubflow*, GstClockTime *);
  void                 (*set_avg_rtcp_size)(MPRTPRSubflow*,gsize);
  void                 (*setup_rr_riport)(MPRTPRSubflow*, GstMPRTCPSubflowRiport*);
  void                 (*setup_xr_rfc2743_late_discarded_riport)(MPRTPRSubflow*, GstMPRTCPSubflowRiport*);
  //GstPad*              (*get_outpad)(MPRTPRSubflow*);
};

struct _MPRTPReceiverSubflowClass {
  GObjectClass   parent_class;
};


GType mprtpr_subflow_get_type (void);
MPRTPRSubflow* make_mprtpr_subflow(guint16 id, guint8 header_ext_id);

G_END_DECLS

#endif /* MPRTPRSUBFLOW_H_ */
