/*
 * screamctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef SCREAMSUBCTRLER_H_
#define SCREAMSUBCTRLER_H_

#include <gst/gst.h>
#include "mprtpspath.h"
#include "sndratedistor.h"
#include "reportproc.h"
#include "signalreport.h"


typedef struct _SCREAMSubController SCREAMSubController;
typedef struct _SCREAMSubControllerClass SCREAMSubControllerClass;

#define SCREAMSUBCTRLER_TYPE             (screamsubctrler_get_type())
#define SCREAMSUBCTRLER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),SCREAMSUBCTRLER_TYPE,SCREAMSubController))
#define SCREAMSUBCTRLER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),SCREAMSUBCTRLER_TYPE,SCREAMSubControllerClass))
#define SCREAMSUBCTRLER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),SCREAMSUBCTRLER_TYPE))
#define SCREAMSUBCTRLER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),SCREAMSUBCTRLER_TYPE))
#define SCREAMSUBCTRLER_CAST(src)        ((SCREAMSubController *)(src))

typedef void (*SubRateCtrlerFnc)(SCREAMSubController*);
typedef void (*SubRateAction)(SCREAMSubController*);
typedef void (*SubTargetRateCtrler)(SCREAMSubController*, gint32);


typedef struct {
    guint stream_id;
    guint size;
    guint seq;
    guint64 transmit_time_us;
    gboolean is_used;
} TransmittedRtpPacket;

typedef void (*GstScreamQueueBitrateRequestedCb) (guint bitrate, guint stream_id, gpointer user_data);
typedef guint (*GstScreamQueueNextPacketSizeCb) (guint stream_id, gpointer user_data);
typedef void (*GstScreamQueueApproveTransmitCb) (guint stream_id, gpointer user_data);
typedef void (*GstScreamQueueClearQueueCb) (guint stream_id, gpointer user_data);

#define MAX_TX_PACKETS 1000
#define BASE_OWD_HIST_SIZE 50
#define OWD_FRACTION_HIST_SIZE 20
#define OWD_NORM_HIST_SIZE 100
#define BYTES_IN_FLIGHT_HIST_SIZE 10

struct _SCREAMSubController
{
  GObject                   object;
  guint8                    id;
  GRWLock                   rwmutex;
  GstClock*                 sysclock;
  MPRTPSPath*               path;
  GstClockTime              made;

  GHashTable *streams;

  gint maxTxPackets;
  gboolean approve_timer_running;
  /*guint transmitted_packets_size;*/
  TransmittedRtpPacket transmitted_packets[MAX_TX_PACKETS];

  guint64 srtt_sh_us;
  guint64 srtt_us;
  guint acked_owd; // OWD of last acked packet
  guint base_owd;
  guint32 base_owd_hist[BASE_OWD_HIST_SIZE];
  gint base_owd_hist_ptr;
  gfloat owd;
  gfloat owd_fraction_avg;
  gfloat owd_fraction_hist[OWD_FRACTION_HIST_SIZE];
  gint owd_fraction_hist_ptr;
  gfloat owd_trend;
  gfloat owd_target;
  gfloat owd_norm_hist[OWD_NORM_HIST_SIZE];
  gint owd_norm_hist_ptr;
  gfloat owd_sbd_skew;
  gfloat owd_sbd_var;
  gfloat owd_sbd_mean;
  gfloat owd_sbd_mean_sh;

  // CWND management
  guint bytes_newly_acked;
  guint mss; // Maximum Segment Size
  guint cwnd; // congestion window
  guint cwnd_min;
  guint cwnd_i;
  gboolean was_cwnd_increase;
  guint bytes_in_flight_lo_hist[BYTES_IN_FLIGHT_HIST_SIZE];
  guint bytes_in_flight_hi_hist[BYTES_IN_FLIGHT_HIST_SIZE];
  guint bytes_in_flight_hist_ptr;
  guint bytes_in_flight_hi_max;
  guint acc_bytes_in_flight_max;
  guint n_acc_bytes_in_flight_max;
  gfloat rate_transmitted;
  gfloat owd_trend_mem;
  guint64 delta_t;

  // Loss event
  gboolean loss_event;


  // Fast start
  gboolean in_fast_start;
  guint n_fast_start;

  // Transmission scheduling*/
  gfloat pacing_bitrate;

      gboolean is_initialized;
      // These need to be initialized when time_us is known
      guint64 last_srtt_update_t_us;
  guint64 last_base_owd_add_t_us;
  guint64 base_owd_reset_t_us;
  guint64 last_add_to_owd_fraction_hist_t_us;
  guint64 last_bytes_in_flight_t_us;
  guint64 last_loss_event_t_us;
  guint64 last_congestion_detected_t_us;
  guint64 last_transmit_t_us;
  guint64 next_transmit_t_us;
  guint64 last_rate_update_t_us;

  // TODO Debug variables , remove
  guint64 lastfb;

  gpointer                  priv;

};

struct _SCREAMSubControllerClass{
  GObjectClass parent_class;

};
GType screamsubctrler_get_type (void);
SCREAMSubController *make_screamsubctrler(MPRTPSPath *path);

gboolean screamsubctrler_path_approver(gpointer data,    GstBuffer *buffer);

void screamsubctrler_enable(SCREAMSubController *this);
void screamsubctrler_disable(SCREAMSubController *this);

void screamsubctrler_report_update(SCREAMSubController *this, GstMPRTCPReportSummary *summary);
void screamsubctrler_time_update(SCREAMSubController *this);

void screamsubctrler_signal_update(SCREAMSubController *this, MPRTPSubflowFECBasedRateAdaption *params);
void screamsubctrler_signal_request(SCREAMSubController *this, MPRTPSubflowFECBasedRateAdaption *result);

#endif /* SCREAMSUBCTRLER_H_ */
