/* GStreamer Scheduling tree
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
#include "ricalcer.h"
//#include "mprtpspath.h"
#include <math.h>
#include <gst/gst.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mprtplogger.h"


GST_DEBUG_CATEGORY_STATIC (ricalcer_debug_category);
#define GST_CAT_DEFAULT ricalcer_debug_category

#define _now(this) (gst_clock_get_time (this->sysclock))

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

G_DEFINE_TYPE (ReportIntervalCalculator, ricalcer, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void ricalcer_finalize (GObject * object);
static gdouble _calc_report_interval(ReportIntervalCalculator * this);

static gboolean _do_report_now (ReportIntervalCalculator * this);

static gdouble
_get_rtcp_interval (
    gint senders,
    gint members,
    gdouble rtcp_bw,
    gint we_sent,
    gdouble avg_rtcp_size,
    gint initial);

static void _logging(gpointer data);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

void
ricalcer_class_init (ReportIntervalCalculatorClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = ricalcer_finalize;

  GST_DEBUG_CATEGORY_INIT (ricalcer_debug_category, "ricalcer", 0,
      "MpRTP Manual Sending Controller");
}

void
ricalcer_finalize (GObject * object)
{
  ReportIntervalCalculator * this;
  this = RICALCER(object);
  g_object_unref(this->sysclock);
}

void ricalcer_set_mode(ReportIntervalCalculator *this, RTCPIntervalMode mode)
{
  THIS_WRITELOCK(this);
  this->mode = mode;
  THIS_WRITEUNLOCK(this);
}

gboolean ricalcer_rtcp_regular_allowed(ReportIntervalCalculator * this)
{
  gboolean result;
  THIS_WRITELOCK(this);
  result = _do_report_now(this);
  THIS_WRITEUNLOCK(this);
  return result;
}

gboolean ricalcer_rtcp_fb_allowed(ReportIntervalCalculator * this)
{
  gboolean result;
  THIS_WRITELOCK(this);
  if(this->mode == RTCP_INTERVAL_IMMEDIATE_FEEDBACK_MODE){
    result = TRUE;
    goto done;
  }
  result = _do_report_now(this);
done:
  THIS_WRITEUNLOCK(this);
  return result;
}


void
ricalcer_init (ReportIntervalCalculator * this)
{
  this->media_rate = 64000.;
  this->initialized = FALSE;
  this->avg_rtcp_size = 128.;
  this->allow_early = TRUE;
  this->urgent = FALSE;
  this->max_interval = 1.5;
  this->base_interval = 1.5;
  this->min_interval = .5;
  this->interval_spread = 1.;
  this->sysclock = gst_system_clock_obtain();
  g_rw_lock_init (&this->rwmutex);
  mprtp_logger_add_logging_fnc(_logging, this, 10, &this->rwmutex);
}

ReportIntervalCalculator *make_ricalcer(gboolean sender_side)
{
  ReportIntervalCalculator *result;
  result = g_object_new (RICALCER_TYPE, NULL);
  result->sender_side = sender_side;
  return result;
}

gboolean _do_report_now (ReportIntervalCalculator * this)
{
  gboolean result = FALSE;

  if (!this->initialized) {
    this->actual_interval = (GstClockTime)(_calc_report_interval(this) * (gdouble)GST_SECOND);
    this->next_time = _now(this) + this->actual_interval;
    this->initialized = TRUE;
    goto done;
  }
  if(this->mode == RTCP_INTERVAL_REGULAR_INTERVAL_MODE){
    result = this->next_time <= _now(this);
    if(result){
      this->last_time = _now(this);
      this->actual_interval = (GstClockTime)(_calc_report_interval(this) * (gdouble)GST_SECOND);
      this->next_time = _now(this) + this->actual_interval;
    }
    goto done;
  }
  //early or immediate, do the same
  if(this->urgent){
    this->urgent = FALSE;
    if(this->allow_early){
      result = TRUE;
      this->allow_early = FALSE;
      this->actual_interval = (GstClockTime)(_calc_report_interval(this) * (gdouble)GST_SECOND);
      this->next_time = _now(this) + 2 * this->actual_interval;
      this->interval_spread = MAX(10., this->interval_spread * 0.5);
      goto done;
    }
  }
  result = this->next_time <= _now(this);
  if(result){
    gdouble actual_interval;
    actual_interval = _calc_report_interval(this);
    this->allow_early = TRUE;
    this->last_time = _now(this);
    this->actual_interval = (GstClockTime)((MIN(g_random_double_range(5.0, 7.5),
                                                actual_interval * this->interval_spread) * (gdouble)GST_SECOND));
    this->interval_spread = MIN(100., this->interval_spread * g_random_double_range(1., 2.));
//    g_print("Actual Interval: %f (%f) -> %lu\n", actual_interval, this->interval_spread, this->actual_interval / GST_MSECOND);
    this->next_time = _now(this) + this->actual_interval;
  }
done:
  return result;
}



void ricalcer_refresh_parameters(ReportIntervalCalculator * this,
                                 gdouble media_rate,
                                 gdouble avg_rtcp_size)
{
  THIS_WRITELOCK(this);
  this->media_rate = media_rate;
  this->avg_rtcp_size = avg_rtcp_size;
  THIS_WRITEUNLOCK(this);
}

void ricalcer_urgent_report_request(ReportIntervalCalculator * this)
{
  THIS_WRITELOCK(this);
  this->urgent = TRUE;
  THIS_WRITEUNLOCK(this);
}

gdouble _calc_report_interval(ReportIntervalCalculator * this)
{
  gdouble result;
  result =
      _get_rtcp_interval (
        1,                        //senders
        2,                        //members
        this->media_rate * .05,         //rtcp_bw
        this->sender_side?1:0,    //we_sent
        this->avg_rtcp_size,      //avg_rtcp_size
        this->initialized?0:1);       //initial



  return result;
}


//Copied from RFC3550
gdouble
_get_rtcp_interval (gint senders,
    gint members,
    gdouble rtcp_bw, gint we_sent, gdouble avg_rtcp_size, gint initial)
{
  /*
   * Minimum average time between RTCP packets from this site (in
   * seconds).  This time prevents the reports from `clumping' when
   * sessions are small and the law of large numbers isn't helping
   * to smooth out the traffic.  It also keeps the report interval
   * from becoming ridiculously small during transient outages like
   * a network partition.
   */
//  gdouble const RTCP_MIN_TIME = 5.;
  gdouble const RTCP_MIN_TIME = 1.;
  /*
   * Fraction of the RTCP bandwidth to be shared among active
   * senders.  (This fraction was chosen so that in a typical
   * session with one or two active senders, the computed report
   * time would be roughly equal to the minimum report time so that
   * we don't unnecessarily slow down receiver reports.)  The
   * receiver fraction must be 1 - the sender fraction.
   */
  gdouble const RTCP_SENDER_BW_FRACTION = 0.25;
  gdouble const RTCP_RCVR_BW_FRACTION = (1 - RTCP_SENDER_BW_FRACTION);
  /*
   * To compensate for "timer reconsideration" converging to a
   * value below the intended average.
   */
  gdouble const COMPENSATION = 2.71828 - 1.5;

  gdouble t;                    /* interval */
  gdouble rtcp_min_time = RTCP_MIN_TIME;

  gint n;                       /* no. of members for computation */

  /*
   * Very first call at application start-up uses half the min
   * delay for quicker notification while still allowing some time
   * before reporting for randomization and to learn about other
   * sources so the report interval will converge to the correct
   * interval more quickly.

   */
  if (initial) {
//    rtcp_min_time /= 2;
      rtcp_min_time = RTCP_MIN_TIME;
  }else{
      rtcp_min_time = 0.;
  }
  /*
   * Dedicate a fraction of the RTCP bandwidth to senders unless
   * the number of senders is large enough that their share is
   * more than that fraction.
   */
  n = members;
  if (senders <= members * RTCP_SENDER_BW_FRACTION) {
    if (we_sent) {
      rtcp_bw *= RTCP_SENDER_BW_FRACTION;
      n = senders;
    } else {
      rtcp_bw *= RTCP_RCVR_BW_FRACTION;
      n -= senders;
    }
  }

  /*
   * The effective number of sites times the average packet size is
   * the total number of octets sent when each site sends a report.
   * Dividing this by the effective bandwidth gives the time
   * interval over which those packets must be sent in order to
   * meet the bandwidth target, with a minimum enforced.  In that
   * time interval we send one report so this time is also our
   * average time between reports.
   */
  t = avg_rtcp_size * n / rtcp_bw;
  if (t < rtcp_min_time)
    t = rtcp_min_time;

  /*
   * To avoid traffic bursts from unintended synchronization with
   * other sites, we then pick our actual next report interval as a
   * random number uniformly distributed between 0.5*t and 1.5*t.
   */
  t = t * (drand48 () + 0.5);
  t = t / COMPENSATION;
  return t;
}


void _logging(gpointer data)
{
  gchar logfile[255];
  ReportIntervalCalculator *this;
  this = data;

  sprintf(logfile, "%s_ricalcer", this->sender_side ? "snd" : "rcv");

  mprtp_logger(logfile,
               "actual_interval: %lu\n"
               "allow_early:     %d\n"
               "avg_rtcp_size:   %f\n"
               "base_interval:   %f\n"
               "initialized:     %d\n"
               "last_time:       %lu\n"
               "media_rate:      %f\n"
               "min_interval:    %f\n"
               "mode:            %d\n"
               "next_time:       %lu mseconds remain\n"
               "urgent:          %d\n"
               "#########################\n"
               ,
               this->actual_interval,
               this->allow_early,
               this->avg_rtcp_size,
               this->base_interval,
               this->initialized,
               this->last_time,
               this->media_rate,
               this->min_interval,
               this->mode,
               GST_TIME_AS_MSECONDS(this->next_time - _now(this)),
               this->urgent
               );


}
