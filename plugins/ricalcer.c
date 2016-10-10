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
#include "mprtpdefs.h"


GST_DEBUG_CATEGORY_STATIC (ricalcer_debug_category);
#define GST_CAT_DEFAULT ricalcer_debug_category

#define _now(this) (gst_clock_get_time (this->sysclock))

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

G_DEFINE_TYPE (ReportIntervalCalculator, ricalcer, G_TYPE_OBJECT);

//static gdouble const RTCP_MIN_TIME = 5.;
static const gdouble RTCP_MIN_TIME = 1.0;
//static const gdouble RTCP_MAX_TIME = 7.5;

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void ricalcer_finalize (GObject * object);

static gdouble
_get_rtcp_interval (
    gint senders,
    gint members,
    gdouble rtcp_bw,
    gint we_sent,
    gdouble avg_rtcp_size,
    gint initial);


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
ricalcer_init (ReportIntervalCalculator * this)
{
  this->sysclock = gst_system_clock_obtain();
}

void
ricalcer_finalize (GObject * object)
{
  ReportIntervalCalculator * this;
  this = RICALCER(object);
  g_object_unref(this->sysclock);
}


gboolean ricalcer_rtcp_fb_allowed(ReportIntervalCalculator * this, SndSubflow *subflow)
{
  return subflow->rtcp_interval_type == RTCP_INTERVAL_IMMEDIATE_FEEDBACK_MODE;
}

gboolean ricalcer_rtcp_regular_allowed_sndsubflow(ReportIntervalCalculator * this, SndSubflow *subflow)
{
  gdouble interval_in_sec;
  if(_now(this) < subflow->next_regular_rtcp){
    return FALSE;
  }
  interval_in_sec =
      _get_rtcp_interval (
                          1,                                       //senders
                          2,                                       //members
                          subflow->target_bitrate * .05,           //rtcp_bw
                          this->sender_side?1:0,                   //we_sent
                          128,                                     //avg_rtcp_size
                          subflow->next_regular_rtcp == 0?0:1);    //initialized

  subflow->next_regular_rtcp = _now(this) + interval_in_sec * GST_SECOND;
  return TRUE;
}

gboolean ricalcer_rtcp_regular_allowed_rcvsubflow(ReportIntervalCalculator * this, RcvSubflow *subflow)
{
  gdouble interval_in_sec;
  if(_now(this) < subflow->next_regular_rtcp){
    return FALSE;
  }
  interval_in_sec =
      _get_rtcp_interval (
                          1,                                       //senders
                          2,                                       //members
                          500000 * .05,                            //rtcp_bw
                          this->sender_side?1:0,                   //we_sent
                          128,                                     //avg_rtcp_size
                          subflow->next_regular_rtcp == 0?0:1);    //initialized

  subflow->next_regular_rtcp = _now(this) + interval_in_sec * GST_SECOND;
  return TRUE;
}

ReportIntervalCalculator *make_ricalcer(gboolean sender_side)
{
  ReportIntervalCalculator *result;
  result = g_object_new (RICALCER_TYPE, NULL);
  result->sender_side = sender_side;
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
      rtcp_min_time = 1.;
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

