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
	MPRTP_SENDER_SUBFLOW_EVENT_DEAD       = 1,
	MPRTP_SENDER_SUBFLOW_EVENT_DISTORTION = 2,
	MPRTP_SENDER_SUBFLOW_EVENT_BID        = 3,
	MPRTP_SENDER_SUBFLOW_EVENT_SETTLED    = 4,
	MPRTP_SENDER_SUBFLOW_EVENT_CONGESTION = 5,
	MPRTP_SENDER_SUBFLOW_EVENT_KEEP       = 6,
	MPRTP_SENDER_SUBFLOW_EVENT_LATE       = 7,
	MPRTP_SENDER_SUBFLOW_EVENT_CHARGE     = 8,
	MPRTP_SENDER_SUBFLOW_EVENT_DISCHARGE  = 9,
	MPRTP_SENDER_SUBFLOW_EVENT_fi         = 10,
	MPRTP_SENDER_SUBFLOW_EVENT_JOINED     = 11,
	MPRTP_SENDER_SUBFLOW_EVENT_DETACHED   = 12,
} MPRTPSubflowEvent;


struct _MPRTPSenderSubflow{
  GObject              object;
  GMutex               mutex;
  GstClock*            sysclock;
  guint16              id;
  GstPad*              outpad;

  //gboolean             linked;
  MPRTPSubflowStates   state;

  void               (*fire)(MPRTPSSubflow*,MPRTPSubflowEvent,void*);
  void               (*process_rtpbuf_out)(MPRTPSSubflow*, guint, GstRTPBuffer*);
  void               (*process_mprtcp_block)(MPRTPSSubflow*,GstMPRTCPSubflowBlock*);
  void               (*setup_sr_riport)(MPRTPSSubflow*, GstMPRTCPSubflowRiport*);
  guint16            (*get_id)(MPRTPSSubflow*);
  GstClockTime       (*get_sr_riport_time)(MPRTPSSubflow*);
  void               (*set_sr_riport_time)(MPRTPSSubflow*, GstClockTime);
  void               (*set_charge_value)(MPRTPSSubflow*, gfloat);
  void               (*set_alpha_value)(MPRTPSSubflow*, gfloat);
  void               (*set_beta_value)(MPRTPSSubflow*, gfloat);
  void               (*set_gamma_value)(MPRTPSSubflow*, gfloat);
  guint32            (*get_sending_rate)(MPRTPSSubflow*);
  gboolean           (*is_active)(MPRTPSSubflow*);
  GstPad*            (*get_outpad)(MPRTPSSubflow*);
  guint32            (*get_sent_packet_num)(MPRTPSSubflow*);


  //influence calculation and states
  GstClockTime         last_riport_received;
  gfloat               alpha_value;
  gfloat               beta_value;
  gfloat               gamma_value;
  gfloat               charge_value;
  gboolean             active;
  gfloat               SR; //Sending Rate
  guint32              UB; //Utilized bytes
  guint32              DB; //Discarded bytes
  guint8               distortions;  //History of lost and discarded packet riports by using a continously shifted 8 byte value
  guint16              consequent_RR_missing;
  guint16              consequent_settlements;
  gboolean             increasement_request;
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
  gboolean             RR_arrived;        //Indicate that a receiver report is arrived, used by the scheduler
  guint16              HSN_r;
  guint16              cycle_num_r;
  guint8               fraction_lost;
  guint32              cum_packet_losts; //
  guint16              inter_packet_losts;
  GstClockTimeDiff     RR_time_dif;      //the time difference between two consequtive RR
  guint32              jitter;
  guint64              LSR;
  guint64              DLSR;
  guint64              RRT;
  guint64              RTT;             //round trip time



  //refreshed by receiving Discarded packet report
  guint32              DP;
  guint32              early_discarded_bytes;
  guint32              late_discarded_bytes;

  //refreshed by sender after sending all reports out
  guint32              sent_report_size;
  guint32              received_report_size;

  guint32              sent_packet_num;
};

struct _MPRTPSenderSubflowClass {
  GObjectClass   parent_class;

};

GType mprtps_subflow_get_type (void);
MPRTPSSubflow* make_mprtps_subflow(guint16 id, GstPad* srcpad);

G_END_DECLS

#endif /* MPRTPSSUBFLOW_H_ */
