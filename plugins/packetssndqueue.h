/*
 * packetssndqueue.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef PACKETSSNDQUEUE_H_
#define PACKETSSNDQUEUE_H_

#include <gst/gst.h>
#include "bintree.h"

typedef struct _PacketsSndQueue PacketsSndQueue;
typedef struct _PacketsSndQueueClass PacketsSndQueueClass;

#define PACKETSSNDQUEUE_TYPE             (packetssndqueue_get_type())
#define PACKETSSNDQUEUE(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),PACKETSSNDQUEUE_TYPE,PacketsSndQueue))
#define PACKETSSNDQUEUE_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),PACKETSSNDQUEUE_TYPE,PacketsSndQueueClass))
#define PACKETSSNDQUEUE_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),PACKETSSNDQUEUE_TYPE))
#define PACKETSSNDQUEUE_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),PACKETSSNDQUEUE_TYPE))
#define PACKETSSNDQUEUE_CAST(src)        ((PacketsSndQueue *)(src))
//
//// Min OWD target. Default value: 0.1
//#define OWD_TARGET_LO 0.1
////Max OWD target. Default value: 0.4s
//#define OWD_TARGET_HI 0.4
////Headroom for limitation of CWND. Default value: 1.1
//#define MAX_BYTES_IN_FLIGHT_HEAD_ROOM 1.1
////Gain factor for congestion window adjustment. Default value:  1.0
//#define GAIN 1.0
////CWND scale factor due to loss event. Default value: 0.6
//#define BETA 0.6
//// Target rate scale factor due to loss event. Default value: 0.8
//#define BETA_R 0.8
////Additional slack [%]  to the congestion window. Default value: 10%
//#define BYTES_IN_FLIGHT_SLACK .1
////Interval between video bitrate adjustments. Default value: 0.1s ->100ms
//#define RATE_ADJUST_INTERVAL 100
////Video coder frame period [s]
//#define FRAME_PERIOD 0
////Min target_bitrate [bps]
//#define TARGET_BITRATE_MIN 0
////Max target_bitrate [bps]
//#define TARGET_BITRATE_MAX 0
////Timespan [s] from lowest to highest bitrate. Default value: 10s->10000ms
//#define RAMP_UP_TIME 10000
////Guard factor against early congestion onset.
////A higher value gives less jitter possibly at the
////expense of a lower video bitrate. Default value: 0.0..0.2
//#define PRE_CONGESTION_GUARD 0.
////Guard factor against RTP queue buildup. Default value: 0.0..2.0
//#define TX_QUEUE_SIZE_FACTOR 1.0
//
////OWD target. Initial value: OWD_TARGET_LO
//gint owd_target;
////EWMA filtered owd fraction.Initial value:  0.0
//gint owd_fraction_avg;
////Vector of the last 20 owd_fraction
//gint owd_fraction_hist;
////OWD trend indicates incipient congestion. Initial value: 0.0
//gint owd_trend;
////Vector of the last 100 owd
//gint owd_norm_hist;
////Maximum segment size
//gint mss;
////Minimum congestion window [byte]. Initial value: 2*MSS
//gint min_cwnd;
////True if in fast start state. Initial value: TRUE
//gint in_fast_start;
////COngestion window
//gint cwnd;
////Congestion window inflection point. Initial value: 1
//gint cwnd_i;
////The number of bytes that was acknowledged with the
////last received acknowledgement. i.e.: Bytes acknowledged
////since the last CWND update [byte].
////Reset after a CWND update. Initial value: 0
//gint bytes_newly_acked;
////Upper limit of how many bytes that can be transmitted [byte].
////Updated when CWND is updated and when RTP packet is transmitted.
////Initial value: 0
//gint send_wnd;
////Approximate estimate of interpacket transmission
////itnerval [ms], updated when RTP packet transmitted. Initial value: 1
//gint t_pace;
////A vector of the  last 20 RTP packet queue delay samples.Array
//gint age_vec;
////Indicates the intensity of the frame skips. Initial value: 0.0
//gint frame_skip_intensity;
////Number of video  frames since the last skip.Initial value:  0
//gint since_last_frame_skip;
////Number of consecutive frame skips.Initial value:  0
//gint consecutive_frame_skips;
////Video target bitrate [bps]
//gint target_bitrate;
////Video target bitrate inflection point i.e. the last known highest
////target_bitrate during fast start. Used to limit bitrate increase
////close to the last know congestion point. Initial value: 1
//gint target_bitrate_i;
////Measured transmit bitrate [bps]. Initial value: 0.0
//gint rate_transmit;
////Measured throughput based on received acknowledgements [bps].
////Initial value: 0.0
//gint rate_acked;
////Smoothed RTT [s], computed similar to method depicted in [RFC6298].
////Initial value: 0.0
//gint s_rtt;
////Size of RTP packets in queue [bits].
//gint rtp_queue_size;
////Size of the last transmitted RTP packets [byte]. Initial value: 0
//gint rtp_size;
////Skip encoding of video frame if true. gint false
//gint frame_skip;

typedef struct _PacketsSndQueueNode PacketsSndQueueNode;

struct _PacketsSndQueue
{
  GObject                  object;
  PacketsSndQueueNode*     head;
  PacketsSndQueueNode*     tail;
  guint32                  counter;
  GRWLock                  rwmutex;
  PointerPool*             node_pool;
  GstClock*                sysclock;


};

struct _PacketsSndQueueNode
{
  PacketsSndQueueNode* next;
  GstClockTime         added;
  GstBuffer*           buffer;
  guint32              payload_bytes;
};

struct _PacketsSndQueueClass{
  GObjectClass parent_class;

};
GType packetssndqueue_get_type (void);
PacketsSndQueue *make_packetssndqueue(void);
void packetssndqueue_reset(PacketsSndQueue *this);
guint32 packetssndqueue_get_num(PacketsSndQueue *this);
void packetssndqueue_push(PacketsSndQueue *this,
                          GstBuffer* buffer,
                          guint32 payload_bytes);
GstBuffer* packetssndqueue_pop(PacketsSndQueue *this);
gboolean packetssndqueue_has_buffer(PacketsSndQueue *this, guint32 *payload_bytes);
#endif /* PACKETSSNDQUEUE_H_ */
