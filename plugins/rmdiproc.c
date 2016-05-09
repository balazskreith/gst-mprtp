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
#include "rmdiproc.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

//#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
//#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
//#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
//#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define THIS_READLOCK(this)
#define THIS_READUNLOCK(this)
#define THIS_WRITELOCK(this)
#define THIS_WRITEUNLOCK(this)


GST_DEBUG_CATEGORY_STATIC (rmdi_processor_debug_category);
#define GST_CAT_DEFAULT cormdi_processor_debug_category

G_DEFINE_TYPE (RMDIProcessor, rmdi_processor, G_TYPE_OBJECT);

typedef struct _CorrBlock CorrBlock;

struct _CorrBlock{
  guint           id,N;
  gint64          Iu0,Iu1,Id1,Id2,Id3,G01,M0,M1,G_[64],M_[64];
  gint            index;
  gdouble         g;
  CorrBlock*     next;
};

typedef struct _RMDIProcessorPrivate{
  GstClockTime        delay50th;
  GstClockTime        min_delay;
}RMDIProcessorPrivate;


#define _priv(this) ((RMDIProcessorPrivate*) this->priv)
#define _now(this) (gst_clock_get_time(this->sysclock))
//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------
static void rmdi_processor_finalize (GObject * object);

static void _readable_result(RMDIProcessor *this, RMDIProcessorResult *result);
void
rmdi_processor_class_init (RMDIProcessorClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = rmdi_processor_finalize;

  GST_DEBUG_CATEGORY_INIT (rmdi_processor_debug_category, "rmdi_processor", 0,
      "RMDIProcessor");

}

void
rmdi_processor_finalize (GObject * object)
{
  RMDIProcessor *this;
  this = RMDIPROCESSOR(object);

  mprtp_free(this->priv);
  g_object_unref(this->sysclock);
  this->packetstracker = mprtps_path_unref_packetstracker(this->path);
  g_object_unref(this->path);
}

void
rmdi_processor_init (RMDIProcessor * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sysclock = gst_system_clock_obtain();
  this->priv = g_malloc0(sizeof(RMDIProcessorPrivate));
}

static void _delay50th_pipe(gpointer data, PercentileTrackerPipeData *stats)
{
  RMDIProcessor *this = data;
  this->result.qdelay_median = _priv(this)->delay50th = stats->percentile;
  _priv(this)->min_delay = stats->min;

}

static void _max_bytes_in_flight(gpointer data, gint64 value)
{
  RMDIProcessor *this = data;
  this->result.max_bytes_in_flight = value;
}

RMDIProcessor *make_rmdi_processor(MPRTPSPath *path)
{
  RMDIProcessor *this;

  this = g_object_new (RMDIPROCESSOR_TYPE, NULL);
  THIS_WRITELOCK (this);

  this->id                     = mprtps_path_get_id(path);
  this->path                   = g_object_ref(path);
  this->made                   = _now(this);
  this->delays                 = make_percentiletracker(600, 50);
  this->packetstracker         = mprtps_path_ref_packetstracker(path);
  this->bytes_in_flight        = make_numstracker(50, 5 * GST_SECOND);

  numstracker_add_plugin(this->bytes_in_flight,
                         (NumsTrackerPlugin*)make_numstracker_minmax_plugin(_max_bytes_in_flight, this, NULL, NULL));

  percentiletracker_set_treshold(this->delays, 60 * GST_SECOND);
  percentiletracker_set_stats_pipe(this->delays, _delay50th_pipe, this);

  THIS_WRITEUNLOCK (this);
  return this;
}


void rmdi_processor_reset(RMDIProcessor *this)
{
  THIS_WRITELOCK (this);
  THIS_WRITEUNLOCK (this);
}

static void _process_owd(RMDIProcessor *this, GstMPRTCPXRReportSummary *xrsummary)
{
  this->result.owd_processed = xrsummary->OWD.median_delay;

  if(!xrsummary->OWD.median_delay){
    goto done;
  }

  this->result.qdelay_actual = xrsummary->OWD.median_delay;

  this->last_delay_t2         = this->last_delay_t1;
  this->last_delay_t1         = this->last_delay;
  this->last_delay            = xrsummary->OWD.median_delay;

  if(this->result.qdelay_median){
    this->result.owd_corr =  1.0 * this->last_delay + 0. * this->last_delay_t1 + 0. * this->last_delay_t2;
    this->result.owd_corr /=  this->result.qdelay_median;
  }else{
    this->result.owd_corr = 1.;
  }

  if(this->last_delay && this->last_delay_t1){
    GstClockTime ddelay;
    ddelay = this->last_delay < this->last_delay_t1 ? this->last_delay_t1 - this->last_delay : this->last_delay - this->last_delay_t1;
    this->result.jitter += (ddelay - this->result.jitter) / 16.;
  }
  this->result.delay_avg = this->result.delay_avg * .75 + this->last_delay * .25;

done:
  return;
}

static void _process_afb(RMDIProcessor *this, guint32 id, GstRTCPAFB_REMB *remb)
{
  PacketsSndTrackerStat trackerstat;
  gfloat                estimation;
  guint16               hssn;

  if(id != RTCP_AFB_REMB_ID){
    return;
  }
  gst_rtcp_afb_remb_getdown(remb, NULL, &estimation, NULL, &hssn);
  this->result.rcv_est_max_bitrate = estimation;


  packetssndtracker_update_hssn(this->packetstracker, hssn);
  packetssndtracker_get_stats(this->packetstracker, &trackerstat);

  this->result.sender_bitrate = trackerstat.sent_bytes_in_1s << 3;
  this->result.sent_packets   = trackerstat.sent_packets_in_1s;
  numstracker_add(this->bytes_in_flight, trackerstat.bytes_in_flight);
  this->result.bytes_in_flight = trackerstat.bytes_in_flight;
}

void rmdi_processor_set_rtt(RMDIProcessor       *this,
                            GstClockTime         rtt)
{
  packetssndtracker_set_rtt(this->packetstracker, rtt);
}

void rmdi_processor_do(RMDIProcessor       *this,
                       GstMPRTCPReportSummary *summary,
                       RMDIProcessorResult *result)
{

  if(!summary->AFB.processed && !summary->XR.OWD.processed){
    goto done;
  }
  this->result.congestion_notification = FALSE;
  if(summary->XR.DiscardedPackets.processed){
    this->result.congestion_notification = TRUE;
  }

  if(summary->XR.OWD.processed){
    _process_owd(this, &summary->XR);
  }
  if(summary->AFB.processed){
    _process_afb(this, summary->AFB.fci_id, (GstRTCPAFB_REMB *)summary->AFB.fci_data);
  }

  this->result.sender_bitrate = packetssndtracker_get_sent_bytes_in_1s(this->packetstracker) * 8;

  memcpy(result, &this->result, sizeof(RMDIProcessorResult));

  _readable_result(this, result);
done:
  return;

}

void rmdi_processor_approve_owd(RMDIProcessor *this)
{
  percentiletracker_add(this->delays, this->last_delay);

}

void _readable_result(RMDIProcessor *this, RMDIProcessorResult *result)
{
  GstClockTime secs, msecs;
  gchar filename[255];
  memset(filename, 0, 255);
  sprintf(filename, "rmdiproc_%d.log", this->id);
  secs  = GST_TIME_AS_SECONDS(_now(this) - this->made);
  msecs = GST_TIME_AS_MSECONDS(_now(this) - this->made);
  mprtp_logger(filename,
               "############ Receiver Measured Delay Impact Processor Result #################\n"
               "time since started:  %ds (%dms)\n"
               "max_bytes_in_flight: %d\n"
               "sender_bitrate:      %d\n"
               "goodput_bitrate:     %d\n"
               "utilized_fraction:   %f\n"
               ,

               secs,
               msecs,

               result->max_bytes_in_flight,
               result->sender_bitrate,
               result->goodput_bitrate,
               result->utilized_fraction
  );
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
