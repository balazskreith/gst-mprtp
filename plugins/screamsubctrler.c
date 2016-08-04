/* GStreamer
 * Copyright (C) 2015 Balázs Kreith (contact: balazs.kreith@gmail.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/rtp/gstrtcpbuffer.h>
#include "screamsubctrler.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

//#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
//#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
//#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
//#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define THIS_READLOCK(this)
#define THIS_READUNLOCK(this)
#define THIS_WRITELOCK(this)
#define THIS_WRITEUNLOCK(this)

GST_DEBUG_CATEGORY_STATIC (screamsubctrler_debug_category);
#define GST_CAT_DEFAULT screamsubctrler_debug_category

G_DEFINE_TYPE (SCREAMSubController, screamsubctrler, G_TYPE_OBJECT);

/* Timestamp sampling rate for SCReAM feedback*/
#define TIMESTAMP_RATE 1000.0f

/*
 * A few switches to make debugging easier
 * Open a full congestion window
 */
#define OPEN_CWND FALSE

/*
 * Some good to have features, SCReAM works also with these disabled
 * Enable shared bottleneck detection and OWD target adjustement
 * good if SCReAM needs to compete with e.g FTP but
 * Can in some cases cause self-inflicted congestion
 */
#define ENABLE_SBD TRUE
/* Fast start can resume if little or no congestion detected */
#define ENABLE_CONSECUTIVE_FAST_START TRUE
/* Packet pacing reduces jitter */
#define ENABLE_PACKET_PACING TRUE

/*
 * ==== Main tuning parameters (if tuning necessary) ====
 * Most important parameters first
 * Typical frame period
 */
#define FRAME_PERIOD 0.040f
/* Max video rampup speed in bps/s (bits per second increase per second) */
#define RAMP_UP_SPEED 200000.0f // bps/s
/* CWND scale factor upon loss event */
#define LOSS_BETA 0.6f
/*
 * Compensation factor for RTP queue size
 * A higher value such as 0.2 gives less jitter esp. in wireless (LTE)
 * but potentially also lower link utilization
 */
#define TX_QUEUE_SIZE_FACTOR 1.0f
/*
 * Compensation factor for detected congestion in rate computation
 * A higher value such as 0.2 gives less jitter esp. in wireless (LTE)
 * but potentially also lower link utilization
 */
#define OWD_GUARD 0.2f

/* Video rate scaling due to loss events */
#define LOSS_EVENT_RATE_SCALE 0.9f
/*
 * Additional send window slack (if no or little congestion detected)
 * An increased value such as 0.5 can improve transmission of Key frames
 * however with a higher risk of unstable behavior in
 * sudden congestion situations
 */
#define BYTES_IN_FLIGHT_SLACK 0.0f
/* Rate adjust interval */
#define RATE_ADJUST_INTERVAL 200000 /* us */

/* ==== Less important tuning parameters ==== */
/* Min pacing interval and min pacing rate*/
#define MIN_PACE_INTERVAL 0.00f    /* s */
#define MINIMUM_PACE_BANDWIDTH 50000.0f /* bps */
/* Initial MSS */
#define INIT_MSS 100
/* Initial CWND */
#define INIT_CWND 5000
/* CWND up and down gain factors */
#define CWND_GAIN_UP 1.0f
#define CWND_GAIN_DOWN 1.0f
/* Min and max OWD target */
#define OWD_TARGET_MIN 0.1f /* ms */
#define OWD_TARGET_MAX 0.4f /* ms */
/* Congestion window validation */
#define BYTES_IN_FLIGHT_HIST_INTERVAL 1000000 /* Time (us) between stores */
#define MAX_BYTES_IN_FLIGHT_HEADROOM 1.0f
/* OWD trend and shared bottleneck detection */
#define OWD_FRACTION_HIST_INTERVAL 50000 /* us */
/* Max video rate estimation update period */
#define RATE_UPDATE_INTERVAL 50000  /* us */

/*
 * When the queued time is > than MAX_RTP_QUEUE_TIME the queue time is emptied. This allow for faster
 * "catching up" when the throughput drops from a very high to a very low value
 */
#define MAX_RTP_QUEUE_TIME 0.5f

/* Just a high timer.. */
#define DONT_APPROVE_TRANSMIT_TIME 10000000


typedef struct _Private{

}Private;

#define _priv(this) ((Private*)this->priv)

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

 void screamsubctrler_finalize (GObject * object);


// const gdouble ST_ = 1.1; //Stable treshold
// const gdouble OT_ = 2.;  //Overused treshold
// const gdouble DT_ = 1.5; //Down Treshold



#define _now(this) (gst_clock_get_time(this->sysclock))

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
screamsubctrler_class_init (SCREAMSubControllerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = screamsubctrler_finalize;

  GST_DEBUG_CATEGORY_INIT (screamsubctrler_debug_category, "screamsubctrler", 0,
      "SCREAM+MARC Subflow Rate Controller");

}

void
screamsubctrler_finalize (GObject * object)
{
  SCREAMSubController *this;
  this = SCREAMSUBCTRLER(object);
  mprtp_free(this->priv);
  g_object_unref(this->sysclock);
  g_object_unref(this->path);
}

void
screamsubctrler_init (SCREAMSubController * this)
{
  this->priv = mprtp_malloc(sizeof(Private));
  this->sysclock = gst_system_clock_obtain();
  g_rw_lock_init (&this->rwmutex);

}


gboolean screamsubctrler_path_approver(gpointer data, GstBuffer *buffer)
{
//  SCREAMSubController *this = data;
  return TRUE;
}

SCREAMSubController *make_screamsubctrler(MPRTPSPath *path)
{
  SCREAMSubController *result;
  result                      = g_object_new (SCREAMSUBCTRLER_TYPE, NULL);
  result->path                = g_object_ref(path);
  result->id                  = mprtps_path_get_id(result->path);
  result->made                = _now(result);
  mprtps_path_set_state(result->path, MPRTPS_PATH_STATE_STABLE);

  return result;
}

void screamsubctrler_enable(SCREAMSubController *this)
{

}

void screamsubctrler_disable(SCREAMSubController *this)
{

}

void screamsubctrler_time_update(SCREAMSubController *this)
{

}

void screamsubctrler_signal_update(SCREAMSubController *this, MPRTPSubflowFECBasedRateAdaption *params)
{

}

void screamsubctrler_signal_request(SCREAMSubController *this, MPRTPSubflowFECBasedRateAdaption *result)
{

}

void screamsubctrler_report_update(
                         SCREAMSubController *this,
                         GstMPRTCPReportSummary *summary)
{

}


