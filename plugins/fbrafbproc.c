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
#include "fbrafbproc.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>     /* qsort */
#include <stdio.h>
#include "mprtplogger.h"

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

//#define THIS_READLOCK(this)
//#define THIS_READUNLOCK(this)
//#define THIS_WRITELOCK(this)
//#define THIS_WRITEUNLOCK(this)

#define _now(this) gst_clock_get_time (this->sysclock)

GST_DEBUG_CATEGORY_STATIC (fbrafbprocessor_debug_category);
#define GST_CAT_DEFAULT fbrafbprocessor_debug_category

G_DEFINE_TYPE (FBRAFBProcessor, fbrafbprocessor, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------
typedef struct{
  gint         ref;
  gdouble      owd_corr;
  GstClockTime owd;
  gint64       owd_in_ms;
  gdouble      FD;
  gint32       BiF;
  gint32       overused;
}FBRAFBStatItem;

static void fbrafbprocessor_finalize (GObject * object);
static void _process_rle_discvector(FBRAFBProcessor *this, GstMPRTCPXRReportSummary *xr);
static void _process_statitem(FBRAFBProcessor *this);
static void _process_afb(FBRAFBProcessor *this, guint32 id, GstRTCPAFB_REPS *remb);
static void _process_owd(FBRAFBProcessor *this, GstMPRTCPXRReportSummary *xrsummary);
static void _ref_rtpitem(FBRAFBProcessor * this, FBRAFBProcessorItem* item);
static void _unref_rtpitem(FBRAFBProcessor * this, FBRAFBProcessorItem* item);
static FBRAFBProcessorItem* _retrieve_item(FBRAFBProcessor * this, guint16 seq);
static void _ref_statitem(FBRAFBProcessor * this, FBRAFBStatItem* item);
static void _unref_statitem(FBRAFBProcessor * this, FBRAFBStatItem* item);
//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------


static void owd_logger(gpointer data, gchar* string)
{
  FBRAFBProcessor *this = data;
  THIS_READLOCK(this);

  sprintf(string, "%lu,%lu,%lu,%lu\n",
               GST_TIME_AS_USECONDS(this->stat.owd_stt),
               GST_TIME_AS_USECONDS(this->stat.owd_ltt80),
               GST_TIME_AS_USECONDS(this->stat.RTT),
               (GstClockTime)this->stat.srtt / 1000
               );

  THIS_READUNLOCK(this);

}


#define _statitem_cmpfnc(field) \
static gint _statitem_##field##_cmp(gpointer pa, gpointer pb) \
{ \
  FBRAFBStatItem *a,*b; \
  a = pa; b = pb; \
  if(a->field == b->field) return 0; \
  return a->field < b->field ? -1 : 1; \
} \

_statitem_cmpfnc(owd);
//_statitem_cmpfnc(BiF);



static void _owd_ltt80_percentile_pipe(gpointer udata, swpercentilecandidates_t *candidates)
{
  FBRAFBProcessor *this = udata;
  FBRAFBStatItem *left, *right, *min, *max;

  if(!candidates->processed){
      this->owd_ltt_ewma = this->owd_ltt_ewma == 0. ? this->stat.owd_stt : (this->owd_ltt_ewma * .8 + this->stat.owd_stt * .2);
      this->stat.owd_th_dist  = this->owd_ltt_ewma * .2;
      this->stat.owd_th_cng  = this->owd_ltt_ewma * .8;
      this->owd_stat.ltt80th = this->owd_stat.min = this->owd_stat.max = this->owd_ltt_ewma;
      g_warning("Not enough owd_stt to calculate the ltt80th");
    return;
  }
  this->owd_ltt_ewma = 0.;
  left  = candidates->left;
  right = candidates->right;

  if(!left){
    this->owd_stat.ltt80th = right->owd;
  }else if(!right){
    this->owd_stat.ltt80th = left->owd;
  }else{
    this->owd_stat.ltt80th = left->owd;
    this->owd_stat.ltt80th += right->owd;
    this->owd_stat.ltt80th>>=1;
  }
  min = candidates->min;
  max = candidates->max;
  this->owd_stat.min = min->owd;
  this->owd_stat.max = max->owd;

//  this->stat.owd_th1  = CONSTRAIN(10 * GST_MSECOND,
//                                  100 * GST_MSECOND,
//                                  this->owd_stat.min * .33);
//
//  this->stat.owd_th2  = CONSTRAIN(100 * GST_MSECOND,
//                                  300 * GST_MSECOND,
//                                  this->owd_stat.ltt80th * .33);

  this->stat.owd_th_dist  = this->stat.owd_std * 2.;
  this->stat.owd_th_cng  = this->stat.owd_std * 4.;


//
//  this->stat.owd_th2  = CONSTRAIN(100 * GST_MSECOND,
//                                  300 * GST_MSECOND,
//                                  this->owd_stat.ltt80th * .33);
}



static void _sent_add_pipe(gpointer udata, gpointer itemptr)
{
  FBRAFBProcessor *this = udata;
  FBRAFBProcessorItem *item = itemptr;

  _ref_rtpitem(this, item);

  this->stat.sent_bytes_in_1s += item->payload_bytes;
  ++this->stat.sent_packets_in_1s;
}

static void _sent_rem_pipe(gpointer udata, gpointer itemptr)
{
  FBRAFBProcessor *this = udata;
  FBRAFBProcessorItem *item = itemptr;
  this->stat.sent_bytes_in_1s -= item->payload_bytes;
  --this->stat.sent_packets_in_1s;

  _unref_rtpitem(this, item);
}


static void _BiF_add_pipe(gpointer udata, gpointer itemptr)
{
  FBRAFBProcessor *this = udata;
  FBRAFBProcessorItem *item = itemptr;

  _ref_rtpitem(this, item);

  this->stat.bytes_in_flight += item->payload_bytes;
  ++this->stat.packets_in_flight;

}

static void _BiF_rem_pipe(gpointer udata, gpointer itemptr)
{
  FBRAFBProcessor *this = udata;
  FBRAFBProcessorItem *item = itemptr;

  if(!item->acknowledged){
    g_warning("sequence number %hu not acknowledged in 3RTT or 3s. Hm????", item->seq_num);
    this->stat.bytes_in_flight -= item->payload_bytes;
    --this->stat.packets_in_flight;
  }else if(this->stat.higest_acked_seq_num == item->seq_num){
	  GstClockTime now = _now(this);
	  gdouble rtt = now - item->sent;
	  this->stat.RTT = (this->stat.RTT == 0) ? rtt : (rtt * .125 + this->stat.RTT * .875);
	  if(this->stat.RTT < now - this->stat.srtt_updated){
		  this->stat.srtt = (this->stat.srtt == 0.) ? this->stat.RTT : this->stat.RTT * .125 + this->stat.srtt * .875;
		  this->stat.srtt_updated = now;
	  }
  }

  _unref_rtpitem(this, item);
}


static void _acked_1s_add_pipe(gpointer udata, gpointer itemptr)
{
  FBRAFBProcessor *this = udata;
  FBRAFBProcessorItem *item = itemptr;

  _ref_rtpitem(this, item);

  item->acknowledged = TRUE;

  this->stat.bytes_in_flight -= item->payload_bytes;
  --this->stat.packets_in_flight;

  ++this->stat.acked_packets_in_1s;

  if(item->discarded){
    ++this->stat.discarded_packets_in_1s;
  }else{
    this->stat.goodput_bytes += item->payload_bytes;
  }

  return;
}

static void _acked_1s_rem_pipe(gpointer udata, gpointer itemptr)
{
  FBRAFBProcessor *this = udata;
  FBRAFBProcessorItem *item = itemptr;

  if(item->discarded){
    --this->stat.discarded_packets_in_1s;
  }else{
    this->stat.goodput_bytes -= item->payload_bytes;
  }

  --this->stat.acked_packets_in_1s;

  _unref_rtpitem(this, item);

}




void
fbrafbprocessor_class_init (FBRAFBProcessorClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fbrafbprocessor_finalize;

  GST_DEBUG_CATEGORY_INIT (fbrafbprocessor_debug_category, "fbrafbprocessor", 0,
      "FBRAFBProcessor");

}

void
fbrafbprocessor_finalize (GObject * object)
{
  FBRAFBProcessor *this;
  this = FBRAFBPROCESSOR(object);
  g_object_unref(this->sysclock);
  g_free(this->items);
}

#include <string.h>
static void _sent_sprint(gpointer data, gchar *result)
{
  FBRAFBProcessorItem *item = data;
  sprintf(result,"Sent window seq: %hu payload: %d", item->seq_num, item->payload_bytes);
}

static void _acked_sprint(gpointer data, gchar *result)
{
  FBRAFBProcessorItem *item = data;
  sprintf(result,"Acked window seq: %hu payload: %d", item->seq_num, item->payload_bytes);
}

static void _stt_statitem_rem_pipe(gpointer udata, gpointer itemptr)
{
  FBRAFBProcessor *this = udata;
  FBRAFBStatItem *item = itemptr;

  this->stat.overused_sum -= item->overused;
  --this->stat.overused_num;
  this->stat.overused_avg = (gdouble)this->stat.overused_sum / (gdouble)this->stat.overused_num;

  this->swstat.fdsum    -= item->FD;
  this->swstat.fdsqsum  -= item->FD * item->FD;
  this->stat.FD_avg      = this->swstat.fdsum / (gdouble)this->stat.overused_num;

  _unref_statitem(this, item);

}

static void _stt_statitem_add_pipe(gpointer udata, gpointer itemptr)
{
  FBRAFBProcessor *this = udata;
  FBRAFBStatItem *item = itemptr;

  item->overused = item->owd < this->stat.owd_ltt80 ? -1 : 1;
  this->stat.overused_sum += item->overused;
  ++this->stat.overused_num;
  this->stat.overused_avg = (gdouble) this->stat.overused_sum / (gdouble) this->stat.overused_num;

  this->swstat.fdsum    += item->FD;
  this->swstat.fdsqsum  += item->FD * item->FD;
  this->stat.FD_avg      = this->swstat.fdsum / (gdouble)this->stat.overused_num;

  _ref_statitem(this, item);

}

static void _sw_refresh(FBRAFBProcessor *this)
{
  //Calculate Moving Variance:
  //V = (N * SX2 - (SX1 * SX1)) / (N * (N - 1))
  if(1 < this->swstat.num){
    this->stat.owd_var = (this->swstat.num * this->swstat.owdsqsum) - (this->swstat.owdsum * this->swstat.owdsum);
    this->stat.owd_var /= this->swstat.num * (this->swstat.num - 1);
    this->stat.owd_std = sqrt(this->stat.owd_var);
  }else{
    this->stat.owd_var = this->stat.owd_std = 0.;
  }

//  g_print("FD_avg: %1.2f| owd_var: %1.2f| owd_std: %1.2f\n", this->stat.FD_avg, this->stat.owd_var, this->stat.owd_std);
}

static void _sw_ltt_statitem_rem_pipe(gpointer udata, gpointer itemptr)
{
  FBRAFBProcessor *this = udata;
  FBRAFBStatItem *item = itemptr;

  --this->swstat.num;

  this->swstat.owdsum   -= item->owd_in_ms;
  this->swstat.owdsqsum -= item->owd_in_ms * item->owd_in_ms;
  _sw_refresh(this);

  _unref_statitem(this, item);
}

static void _sw_ltt_statitem_add_pipe(gpointer udata, gpointer itemptr)
{
  FBRAFBProcessor *this = udata;
  FBRAFBStatItem *item = itemptr;

  ++this->swstat.num;
  this->swstat.fdsum    += item->FD;
  this->swstat.fdsqsum  += item->FD * item->FD;

  this->swstat.owdsum   += item->owd_in_ms;
  this->swstat.owdsqsum += item->owd_in_ms * item->owd_in_ms;
  _sw_refresh(this);
}

void
fbrafbprocessor_init (FBRAFBProcessor * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sent             = g_queue_new();
  this->sent_in_1s       = g_queue_new();
  this->acked            = g_queue_new();
  this->sysclock         = gst_system_clock_obtain();
  this->measurements_num = 0;
  this->stat.RTT         = GST_SECOND;
  this->stat.srtt        = 0;

  this->items            = g_malloc0(sizeof(FBRAFBProcessorItem) * 65536);
  this->stt_sw           = make_slidingwindow(100,  GST_SECOND * 2);
  this->ltt_sw           = make_slidingwindow(600,  GST_SECOND * 30);
  this->acked_1s_sw      = make_slidingwindow(2000, GST_SECOND);
  this->sent_sw          = make_slidingwindow(2000, GST_SECOND);

  this->BiF_sw          = make_slidingwindow(2000, GST_SECOND);


//  this->owd_offs = make_slidingwindow_double(10, GST_SECOND);
//  slidingwindow_add_pipes(this->owd_offs, _owd_off_rem_pipe, this, _owd_off_add_pipe, this);

  slidingwindow_add_pipes(this->sent_sw, _sent_rem_pipe, this, _sent_add_pipe, this);
  slidingwindow_add_pipes(this->BiF_sw, _BiF_rem_pipe, this, _BiF_add_pipe, this);
  slidingwindow_add_pipes(this->acked_1s_sw, _acked_1s_rem_pipe, this, _acked_1s_add_pipe, this);

  slidingwindow_add_pipes(this->ltt_sw, _sw_ltt_statitem_rem_pipe, this, _sw_ltt_statitem_add_pipe, this);

  slidingwindow_add_pipes(this->stt_sw, _stt_statitem_rem_pipe, this, _stt_statitem_add_pipe, this);

  DISABLE_LINE slidingwindow_add_plugin(this->sent_sw, make_swprinter(_sent_sprint));
  DISABLE_LINE slidingwindow_add_plugin(this->acked_1s_sw, make_swprinter(_acked_sprint));

  slidingwindow_add_plugins(this->ltt_sw,
                            make_swpercentile(80, _statitem_owd_cmp, _owd_ltt80_percentile_pipe, this),
                            //make_swpercentile(60, _statitem_BiF_cmp, _owd_BiF_percentile_pipe, this),
                           NULL);


  //slidingwindow_add_plugin(this->owd_sw, make_swpercentile(50, bintree3cmp_uint64, _owd_percentile_pipe, this));
}


FBRAFBProcessor *make_fbrafbprocessor(guint8 subflow_id)
{
    FBRAFBProcessor *this;
    this = g_object_new (FBRAFBPROCESSOR_TYPE, NULL);
    this->subflow_id = subflow_id;

    {
      gchar filename[255];
      sprintf(filename, "owd_%d.csv", this->subflow_id);
      mprtp_logger_add_logging_fnc(owd_logger, this, filename);
    }

    return this;
}


void fbrafbprocessor_reset(FBRAFBProcessor *this)
{
  THIS_WRITELOCK (this);
  this->measurements_num = 0;
  THIS_WRITEUNLOCK (this);
}

void fbrafbprocessor_track(gpointer data, guint payload_len, guint16 sn)
{
  FBRAFBProcessor *this;
  FBRAFBProcessorItem* item;
  this = data;
  THIS_WRITELOCK (this);

  item = _retrieve_item(this, sn);

  item->payload_bytes = payload_len;
  item->seq_num       = sn;
  item->sent          = _now(this);

//  ++this->stat.packets_in_flight;
//  this->stat.bytes_in_flight += payload_len;

  slidingwindow_add_data(this->sent_sw, item);
  slidingwindow_add_data(this->BiF_sw, item);
  slidingwindow_refresh(this->acked_1s_sw);

//  slidingwindow_set_treshold(this->stt_sw, CONSTRAIN(100 * GST_MSECOND, GST_SECOND, 3 * this->stat.RTT));
  THIS_WRITEUNLOCK (this);
}

static void _update_fraction_discarded(FBRAFBProcessor *this)
{
  if(this->stat.acked_packets_in_1s == 0 || this->stat.discarded_packets_in_1s == 0){
    this->stat.discarded_rate = 0.;
    return;
  }

  this->stat.discarded_rate = (gdouble)this->stat.discarded_packets_in_1s;
  this->stat.discarded_rate /= (gdouble)this->stat.acked_packets_in_1s;

//  slidingwindow_add_data(this->stt_sw, &this->stat.discarded_rate);

}

void fbrafbprocessor_update(FBRAFBProcessor *this, GstMPRTCPReportSummary *summary)
{

  //TODO: This is the function where we have Profiling problem! Still? I have changed the access for items

  PROFILING("THIS_WRITELOCK", THIS_WRITELOCK (this));

  if(summary->RR.processed){
	//this->stat.RTT  = summary->RR.RTT;
	//this->stat.srtt = (this->stat.srtt == 0.) ? summary->RR.RTT : (summary->RR.RTT * .1 + this->stat.srtt * .9);
    slidingwindow_set_treshold(this->BiF_sw, 3 * MIN(GST_SECOND, this->stat.srtt));
  }
  if(summary->XR.DiscardedRLE.processed){
    _process_rle_discvector(this, &summary->XR);
    this->stat.recent_discarded = 0 < this->last_discard && _now(this) < this->last_discard + this->stat.RTT;
    _update_fraction_discarded(this);
  }
  if(summary->XR.OWD.processed){
      PROFILING("_process_owd",_process_owd(this, &summary->XR));
  }
  if(summary->AFB.processed){
      PROFILING("_process_afb",_process_afb(this, summary->AFB.fci_id, (GstRTCPAFB_REPS *)summary->AFB.fci_data));
  }
  _process_statitem(this);
  ++this->measurements_num;
  THIS_WRITEUNLOCK (this);
}

void
fbrafbprocessor_get_stats (FBRAFBProcessor * this, FBRAFBProcessorStat* result)
{
  THIS_READLOCK (this);
  memcpy(result, &this->stat, sizeof(FBRAFBProcessorStat));
  THIS_READUNLOCK (this);
}


static void _refresh_owd_ltt(FBRAFBProcessor *this)
{
//  slidingwindow_add_data(this->ltt_sw, &this->stat.owd_stt);
  slidingwindow_add_data(this->ltt_sw, this->last_statitem);
  this->stat.owd_ltt80 = this->owd_stat.ltt80th;
}

void fbrafbprocessor_approve_owd_ltt(FBRAFBProcessor *this)
{
  THIS_WRITELOCK (this);

  _refresh_owd_ltt(this);

  THIS_WRITEUNLOCK (this);
}

void _process_statitem(FBRAFBProcessor *this)
{
  FBRAFBStatItem *item;
  item = g_slice_new0(FBRAFBStatItem);
  item->FD        = this->stat.discarded_rate;
  item->owd       = this->stat.owd_stt;
  item->owd_in_ms = GST_TIME_AS_MSECONDS(this->stat.owd_stt);
  item->owd_corr  = this->stat.owd_corr;
  item->BiF       = this->stat.bytes_in_flight;
  item->ref       = 1;

  slidingwindow_add_data(this->stt_sw, item);
  this->last_statitem = item;
}

void _process_afb(FBRAFBProcessor *this, guint32 id, GstRTCPAFB_REPS *reps)
{
  gfloat                tendency;
  guint8                sampling_num;

  if(id != RTCP_AFB_REPS_ID){
    return;
  }

  gst_rtcp_afb_reps_getdown(reps, &sampling_num, &tendency);
  this->stat.tendency = 0 < sampling_num ? tendency : 1.;

}

void _process_owd(FBRAFBProcessor *this, GstMPRTCPXRReportSummary *xrsummary)
{
  if(!xrsummary->OWD.median_delay){
    goto done;
  }

  this->stat.owd_stt = xrsummary->OWD.median_delay;

  if(this->stat.owd_ltt80){
    this->stat.owd_corr =  (gdouble) this->stat.owd_stt;
    this->stat.owd_corr /=  (gdouble)this->stat.owd_ltt80;
  }else{
    this->stat.owd_corr = /*this->stat.owdl_corr = */ 1.;
  }


done:
  return;
}

void _process_rle_discvector(FBRAFBProcessor *this, GstMPRTCPXRReportSummary *xr)
{
  FBRAFBProcessorItem *item;
  guint16 act_seq, end_seq;
  gint i;

  act_seq = xr->DiscardedRLE.begin_seq;
  end_seq = xr->DiscardedRLE.end_seq;
  if(act_seq == end_seq){
    goto done;
  }
//  g_print("rle vector received from %d to %d\n", act_seq, end_seq);
  for(i=0; act_seq <= end_seq; ++act_seq, ++i){
    item = this->items + act_seq;
    if(item->acknowledged){
        continue;
      }
    item->discarded = !xr->DiscardedRLE.vector[i];
    slidingwindow_add_data(this->acked_1s_sw, item);
  }
  this->stat.higest_acked_seq_num = end_seq;
done:
  slidingwindow_refresh(this->sent_sw);
  slidingwindow_refresh(this->BiF_sw);
}


void _ref_rtpitem(FBRAFBProcessor * this, FBRAFBProcessorItem* item)
{
  ++item->ref;
}

void _unref_rtpitem(FBRAFBProcessor * this, FBRAFBProcessorItem* item)
{
  if(0 < item->ref){
    --item->ref;
  }
}

FBRAFBProcessorItem* _retrieve_item(FBRAFBProcessor * this, guint16 seq)
{
  FBRAFBProcessorItem* item;
  item = this->items + seq;
  if(!item->ref){
    goto done;
  }

  --this->stat.packets_in_flight;
  this->stat.bytes_in_flight-=item->payload_bytes;

done:
  memset(item, 0, sizeof(FBRAFBProcessorItem));
  return item;
}


void _ref_statitem(FBRAFBProcessor * this, FBRAFBStatItem* item)
{
  ++item->ref;
}

void _unref_statitem(FBRAFBProcessor * this, FBRAFBStatItem* item)
{
  if(0 < --item->ref){
    return;
  }
  g_slice_free(FBRAFBStatItem, item);
}




#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
