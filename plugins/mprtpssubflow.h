/*
 * mprtpssubflow.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef MPRTPSSUBFLOW_H_
#define MPRTPSSUBFLOW_H_

#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include "gstmprtcpbuffer.h"

G_BEGIN_DECLS

typedef struct _MPRTPSenderSubflow MPRTPSSubflow;
typedef struct _MPRTPSenderSubflowClass MPRTPSSubflowClass;
typedef struct _MPRTPSubflowHeaderExtension MPRTPSubflowHeaderExtension;

#define MPRTPS_SUBFLOW_TYPE             (mprtps_subflow_get_type())
#define MPRTPS_SUBFLOW(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),MPRTPS_SUBFLOW_TYPE,MPRTPSSubflow))
#define MPRTPS_SUBFLOW_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),MPRTPS_SUBFLOW_TYPE,MPRTPSSubflowClass))
#define MPRTPS_SUBFLOW_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),MPRTPS_SUBFLOW_TYPE))
#define MPRTPS_SUBFLOW_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),MPRTPS_SUBFLOW_TYPE))
#define MPRTPS_SUBFLOW_CAST(src)        ((MPRTPSSubflow *)(src))


#define MPRTPS_SUBFLOW_RRBLOCK_MAX 10
#define MPRTPS_SUBFLOW_XR7243BLOCK_MAX 5
#define MPRTPS_SUBFLOW_PAYLOAD_BYTES_ARRAY_LENGTH 32768

typedef struct _MPRTPSubflowHeaderExtension{
  guint16 id;
  guint16 seq;
};

typedef enum{
	MPRTP_SENDER_SUBFLOW_STATE_NON_CONGESTED = 1,
	MPRTP_SENDER_SUBFLOW_STATE_CONGESTED     = 2,
	MPRTP_SENDER_SUBFLOW_STATE_PASSIVE       = 3,
} MPRTPSubflowStates;

typedef enum{
	//MPRTP_SENDER_SUBFLOW_EVENT_DEAD       = 1,
	MPRTP_SENDER_SUBFLOW_EVENT_DISTORTION = 2,
	//MPRTP_SENDER_SUBFLOW_EVENT_BID        = 3,
	MPRTP_SENDER_SUBFLOW_EVENT_SETTLED    = 4,
	MPRTP_SENDER_SUBFLOW_EVENT_CONGESTION = 5,
	MPRTP_SENDER_SUBFLOW_EVENT_KEEP       = 6,
	MPRTP_SENDER_SUBFLOW_EVENT_LATE       = 7,
	//MPRTP_SENDER_SUBFLOW_EVENT_DISCHARGE  = 9,
	//MPRTP_SENDER_SUBFLOW_EVENT_fi         = 10,
	MPRTP_SENDER_SUBFLOW_EVENT_JOINED     = 11,
	MPRTP_SENDER_SUBFLOW_EVENT_DETACHED   = 12,
	MPRTP_SENDER_SUBFLOW_EVENT_REFRESH    = 13,
} MPRTPSubflowEvent;


struct _MPRTPSenderSubflow{
  GObject              object;
  GMutex               mutex;
  GstClock*            sysclock;
  guint16              id;
  GstPad*              outpad;

  MPRTPSubflowStates   state;
  gboolean             never_checked;
  gboolean             segment_sent;
  gboolean             cap_sent;
  gboolean             flowable;

  void               (*process_rtpbuf_out)(MPRTPSSubflow*, guint, GstRTPBuffer*);
  void               (*process_mprtcp_block)(MPRTPSSubflow*,GstMPRTCPSubflowBlock*);
  void               (*setup_sr_riport)(MPRTPSSubflow*, GstMPRTCPSubflowRiport*);
  guint16            (*get_id)(MPRTPSSubflow*);
  GstClockTime       (*get_sr_riport_time)(MPRTPSSubflow*);
  void               (*set_sr_riport_time)(MPRTPSSubflow*, GstClockTime);
  gfloat             (*get_sending_rate)(MPRTPSSubflow*);
  gboolean           (*is_active)(MPRTPSSubflow*);
  GstPad*            (*get_outpad)(MPRTPSSubflow*);
  guint32            (*get_sent_packet_num)(MPRTPSSubflow*);
  void               (*set_event)(MPRTPSSubflow*,MPRTPSubflowEvent);
  MPRTPSubflowEvent  (*check)(MPRTPSSubflow*);
  void               (*set_state)(MPRTPSSubflow*, MPRTPSubflowStates);
  MPRTPSubflowEvent  (*get_state)(MPRTPSSubflow*);
  guint              (*get_consecutive_keeps_num)(MPRTPSSubflow*);
  void               (*save_sending_rate)(MPRTPSSubflow*);
  gfloat             (*load_sending_rate)(MPRTPSSubflow*);
  gboolean           (*push_event)(MPRTPSSubflow*,GstEvent*);
  GstFlowReturn      (*push_buffer)(MPRTPSSubflow*,GstBuffer*);
  gboolean           (*is_flowable)(MPRTPSSubflow*);


  //influence calculation and states
  GstClockTime         last_riport_received;
  GstClockTime         last_checked_riport_time;
  GstClockTime         last_xr7243_riport_received;
  gboolean             late_riported;
  //gboolean             active;
  gfloat               SR; //Sending Rate
  guint32              UB; //Utilized bytes
  guint32              DB; //Discarded bytes
  guint16              consequent_distortions;

  //maintained by sending packets
  guint16              seq;               //The actual subflow specific sequence number
  guint16              cycle_num;         // the number of cycle the sequence number has
  guint32              ssrc;

  //refreshed by sending an SR
  guint16              HSN_s;             //HSN at the sender report time
  guint32              octet_count;       //
  gint32               packet_count;      //
  GstClockTime         sr_riport_time;

  //refreshed by receiving a receiver report
  GstRTCPRRBlock       rr_blocks[MPRTPS_SUBFLOW_RRBLOCK_MAX];
  GstClockTime         rr_blocks_arrivetime[MPRTPS_SUBFLOW_RRBLOCK_MAX];
  guint16              rr_blocks_write_index;
  guint16              rr_blocks_read_index;
  gboolean             rr_blocks_arrived;

  gfloat              last_receiver_rate;
  gfloat              receiver_rate;
  gint                rr_monotocity;
  gfloat              last_rr_change;
  GstClockTime        last_refresh_event_time;
  gfloat              saved_sending_rate;
  GstClockTime        saved_sending_rate_time;

  //gboolean             RR_arrived;        //Indicate that a receiver report is arrived, used by the scheduler
  guint16              HSSN; //highest seen sequence number
  guint8               consecutive_lost;
  guint8               consecutive_discarded;
  guint8               consecutive_settlements;
  guint8               consecutive_distortions;
  guint                consecutive_keep_events;
  guint32              cum_packet_losts; //
  guint64              RRT;
  guint64              RTT;             //round trip time
  guint32              sent_packet_num_since_last_rr;
  guint32              sent_payload_bytes_sum;

  MPRTPSubflowEvent    manual_event;

  //refreshed by receiving Discarded packet report
  guint32              DP;
  guint32              early_discarded_bytes;
  guint32              early_discarded_bytes_sum;
  guint32              late_discarded_bytes;
  guint32              late_discarded_bytes_sum;

  //refreshed by sender after sending all reports out
  guint32              sent_report_size;
  guint32              received_report_size;

  guint32              sent_packet_num;
};

struct _MPRTPSenderSubflowClass {
  GObjectClass   parent_class;
  void           (*push_packet)(MPRTPSSubflow*,GstBuffer*);
};

GType mprtps_subflow_get_type (void);
MPRTPSSubflow* make_mprtps_subflow(guint16 id, GstPad* srcpad);

G_END_DECLS


#endif /* MPRTPSSUBFLOW_H_ */
