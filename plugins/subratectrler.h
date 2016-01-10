/*
 * subratectrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef SUBRATECTRLER_H_
#define SUBRATECTRLER_H_

#include <gst/gst.h>
#include "bintree.h"

typedef struct _SubflowRateController SubflowRateController;
typedef struct _SubflowRateControllerClass SubflowRateControllerClass;

#define SUBRATECTRLER_TYPE             (subratectrler_get_type())
#define SUBRATECTRLER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SUBRATECTRLER_TYPE,SubflowRateController))
#define SUBRATECTRLER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SUBRATECTRLER_TYPE,SubflowRateControllerClass))
#define SUBRATECTRLER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SUBRATECTRLER_TYPE))
#define SUBRATECTRLER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SUBRATECTRLER_TYPE))
#define SUBRATECTRLER_CAST(src)        ((SubflowRateController *)(src))

typedef void (*SubRateProc)(SendingRateDistributor*);

struct _SubflowRateController
{
  GObject                  object;
  GRWLock                  rwmutex;
  gboolean                 initialized;
  GstClock*                sysclock;
  guint8                  id;
  MPRTPSPath*             path;

  gint32                 requested_bytes;
  gint32                 supplied_bytes;
  gint32                 movable_bytes;


  FloatNumsTracker*      owd_norm_hist_20;
  FloatNumsTracker*      owd_norm_hist_100;
//further development
// gdouble                target_weight;
// gboolean               bw_is_shared;
//  gdouble               weight;

  gdouble                bounce_point;

  gint32                 target_rate;
  gint32                 next_target;
  gint32                 max_rate;
  gint32                 min_rate;

  //Need for monitoring
  guint                  monitoring_interval;
  guint                  monitoring_time;

  guint                  disable_controlling;
  SubRateProc            checker;
  guint                  turning_point;
  guint                  monitoring_disabled;



  guint8*                moments;
  gint                   moments_index;
  PercentileTracker*     ltt_delays_th;
  PercentileTracker*     ltt_delays_target;

  //OWD target. Initial value: OWD_TARGET_LO
  guint64 owd_target;
  //EWMA filtered owd fraction.Initial value:  0.0
  gdouble owd_fraction_avg;
  //Vector of the last 20 owd_fraction
  FloatNumsTracker *owd_fraction_hist;
  //OWD trend indicates incipient congestion. Initial value: 0.0
  gdouble owd_trend;
  //Vector of the last 100 owd
  gint owd_norm_hist;
  //Maximum segment size
  gint mss;
  //Minimum congestion window [byte]. Initial value: 2*MSS
  gint min_cwnd;
  //True if in fast start state. Initial value: TRUE
  gint in_fast_start;
  //COngestion window
  gint cwnd;
  //Congestion window inflection point. Initial value: 1
  gint cwnd_i;
  //The number of bytes that was acknowledged with the
  //last received acknowledgement. i.e.: Bytes acknowledged
  //since the last CWND update [byte].
  //Reset after a CWND update. Initial value: 0
  gint bytes_newly_acked;
  //Upper limit of how many bytes that can be transmitted [byte].
  //Updated when CWND is updated and when RTP packet is transmitted.
  //Initial value: 0
  gdouble send_wnd;
  //Approximate estimate of interpacket transmission
  //itnerval [ms], updated when RTP packet transmitted. Initial value: 1
  gint t_pace;
  //A vector of the  last 20 RTP packet queue delay samples.Array
  gint age_vec;
  //Indicates the intensity of the frame skips. Initial value: 0.0
  gdouble frame_skip_intensity;
  //Number of video  frames since the last skip.Initial value:  0
  gint since_last_frame_skip;
  //Number of consecutive frame skips.Initial value:  0
  gint consecutive_frame_skips;
  //Video target bitrate [bps]
  gint target_bitrate;
  //Video target bitrate inflection point i.e. the last known highest
  //target_bitrate during fast start. Used to limit bitrate increase
  //close to the last know congestion point. Initial value: 1
  gint target_bitrate_i;
  //Measured transmit bitrate [bps]. Initial value: 0.0
  gdouble rate_transmit;
  //Measured throughput based on received acknowledgements [bps].
  //Initial value: 0.0
  gdouble rate_acked;
  //Smoothed RTT [s], computed similar to method depicted in [RFC6298].
  //Initial value: 0.0
  gdouble s_rtt;
  //Size of RTP packets in queue [bits].
  gint rtp_queue_size;
  //Size of the last transmitted RTP packets [byte]. Initial value: 0
  gint rtp_size;
  //Skip encoding of video frame if true. gint false
  gboolean frame_skip;

};


struct _SubflowRateControllerClass{
  GObjectClass parent_class;

};
GType subratectrler_get_type (void);
SubflowRateController *make_subratectrler(MPRTPSPath *path);
void subratectrler_set(SubflowRateController *this,
                              MPRTPSPath *path,
                              guint32 sending_target);
void subratectrler_unset(SubflowRateController *this);
guint64 subratectrler_measurement_update(
                         SubflowRateController *this,
                         RRMeasurement * measurement);

#endif /* SUBRATECTRLER_H_ */
