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

static void fbrafbprocessor_finalize (GObject * object);
static void _process_rle_discvector(FBRAFBProcessor *this, GstMPRTCPXRReportSummary *xr);
static void _process_afb(FBRAFBProcessor *this, guint32 id, GstRTCPAFB_REPS *remb);
static void _process_owd(FBRAFBProcessor *this, GstMPRTCPXRReportSummary *xrsummary);
static void _unref_item(FBRAFBProcessor * this, FBRAFBProcessorItem* item);
//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------


static gint
_cmp_uint16 (guint16 x, guint16 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 32768) return -1;
  if(x > y && x - y > 32768) return -1;
  if(x < y && y - x > 32768) return 1;
  if(x > y && x - y < 32768) return 1;
  return 0;
}

static void owd_logger(gpointer data, gchar* string)
{
  FBRAFBProcessor *this = data;
  THIS_READLOCK(this);

  sprintf(string, "%lu,%lu,%lu,%lu\n",
               GST_TIME_AS_USECONDS(this->stat.owd_stt_median),
               GST_TIME_AS_USECONDS(this->stat.owd_ltt80),
               GST_TIME_AS_USECONDS(this->stat.RTT),
               (GstClockTime)this->stat.srtt / 1000
               );

  THIS_READUNLOCK(this);

}


static void _owd_ltt80_percentile_pipe(gpointer udata, swpercentilecandidates_t *candidates)
{
  FBRAFBProcessor *this = udata;
  if(!candidates->processed){
      this->owd_stat.ltt80th = this->owd_stat.min = this->owd_stat.max = this->stat.owd_stt_median;
      g_warning("Not enough owd_stt to calculate the ltt80th");
    return;
  }

  if(!candidates->left){
    this->owd_stat.ltt80th = *(GstClockTime*)candidates->right;
  }else if(!candidates->right){
    this->owd_stat.ltt80th = *(GstClockTime*)candidates->left;
  }else{
    this->owd_stat.ltt80th = *(GstClockTime*)candidates->left;
    this->owd_stat.ltt80th += *(GstClockTime*)candidates->right;
    this->owd_stat.ltt80th>>=1;
  }
  this->owd_stat.min = *(GstClockTime*)candidates->min;
  this->owd_stat.max = *(GstClockTime*)candidates->max;

  this->stat.owd_th1  = CONSTRAIN(5 * GST_MSECOND,
                                  50 * GST_MSECOND,
                                  this->owd_stat.min * .1);

  this->stat.owd_th2  = CONSTRAIN(5 * GST_MSECOND,
                                  50 * GST_MSECOND,
                                  this->owd_stat.ltt80th * .2 );
}

static gboolean _BiF_obsolation(gpointer udata, SlidingWindowItem *switem)
{
  FBRAFBProcessor *this = udata;
  FBRAFBProcessorItem *item = switem->data;

  if(_cmp_uint16(item->seq_num, this->lowest_acked_seq) < 0){
    return TRUE;
  }

  if(0 < _cmp_uint16(item->seq_num, this->highest_acked_seq)){
    return FALSE;
  }

  item->received = TRUE;
  return TRUE;
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
  this->owd_sw           = make_slidingwindow_uint64(600, 60 * GST_SECOND);
  this->recv_sw          = make_slidingwindow(2000, GST_SECOND);
  this->sent_sw          = make_slidingwindow(2000, GST_SECOND);
  this->BiF_sw           = make_slidingwindow(2000, 0);

  slidingwindow_setup_custom_obsolation(this->BiF_sw, _BiF_obsolation, this);
  slidingwindow_add_pipes(this->BiF_sw, _BiF_rem_pipe, this, _BiF_add_pipe, this);
  slidingwindow_add_pipes(this->sent_sw, _sent_rem_pipe, this, sent_add_pipe, this);
  slidingwindow_add_pipes(this->recv_sw, _recv_rem_pipe, this, _recv_add_pipe, this);

  slidingwindow_add_plugins(this->owd_sw,
                            make_swpercentile(80, bintree3cmp_uint64, _owd_ltt80_percentile_pipe, this),
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


static void _obsolate_sent_packet(FBRAFBProcessor *this)
{
  FBRAFBProcessorItem* item;
  GstClockTime treshold;
  treshold = _now(this) - GST_SECOND;

  while(!g_queue_is_empty(this->sent_in_1s)){
    item = g_queue_peek_head(this->sent_in_1s);
    if(treshold <= item->sent){
      break;
    }
    item = g_queue_pop_head(this->sent_in_1s);
    this->stat.sent_bytes_in_1s -= item->payload_bytes;
    --this->stat.sent_packets_in_1s;
    _unref_item(this, item);
  }
}

static void _obsolate_acked_packet(FBRAFBProcessor *this)
{
  FBRAFBProcessorItem* item;
  GstClockTime treshold;
  if(g_queue_is_empty(this->acked)){
    goto done;
  }

  item = g_queue_peek_tail(this->acked);
  treshold = item->sent - GST_SECOND;
//  g_print("reference seq: %hu, sent: %lu, treshold: %lu\n", item->seq_num, GST_TIME_AS_MSECONDS(item->sent), GST_TIME_AS_MSECONDS(treshold));

  while(!g_queue_is_empty(this->acked)){
    item = g_queue_peek_head(this->acked);
    if(treshold < item->sent){
      break;
    }
    item = g_queue_pop_head(this->acked);
    if(!item->discarded){
      this->stat.goodput_bytes-=item->payload_bytes;
//      g_print("-gp:%u(%hu) ",item->payload_bytes, item->seq_num);
    }else{
      --this->stat.discarded_packets_in_1s;
    }
    --this->stat.received_packets_in_1s;
    _unref_item(this, item);
  }

done:
  return;
}

void fbrafbprocessor_track(gpointer data, guint payload_len, guint16 sn)
{
  FBRAFBProcessor *this;
  FBRAFBProcessorItem* item;
  GstClockTime now;
  this = data;
  THIS_WRITELOCK (this);
  now = _now(this);
  //make item
  item = g_slice_new0(FBRAFBProcessorItem);
  item->payload_bytes = payload_len;
  item->sent          = now;
  item->seq_num       = sn;
  item->ref           = 2;

  this->stat.bytes_in_flight+= item->payload_bytes;
  ++this->stat.packets_in_flight;
  g_queue_push_tail(this->sent, item);

  this->stat.sent_bytes_in_1s += item->payload_bytes;
  ++this->stat.sent_packets_in_1s;
  g_queue_push_tail(this->sent_in_1s, item);
  _obsolate_sent_packet(this);

  THIS_WRITEUNLOCK (this);
}

void fbrafbprocessor_update(FBRAFBProcessor *this, GstMPRTCPReportSummary *summary)
{

  //TODO: This is the function where we have Profiling problem!

  PROFILING("THIS_WRITELOCK", THIS_WRITELOCK (this));

  if(summary->RR.processed){
    this->stat.RTT  = summary->RR.RTT;
    this->stat.srtt = (this->stat.srtt == 0.) ? summary->RR.RTT : (summary->RR.RTT * .1 + this->stat.srtt * .9);
  }
  if(summary->XR.DiscardedRLE.processed){
    PROFILING("_process_rle_discvector", _process_rle_discvector(this, &summary->XR));
    this->stat.recent_discarded = 0 < this->last_discard && _now(this) < this->last_discard + this->stat.RTT;

  }
  if(summary->XR.OWD.processed){
      PROFILING("_process_owd",_process_owd(this, &summary->XR));
  }
  if(summary->AFB.processed){
      PROFILING("_process_afb",_process_afb(this, summary->AFB.fci_id, (GstRTCPAFB_REPS *)summary->AFB.fci_data));
  }
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
  slidingwindow_add_data(this->owd_sw, &this->stat.owd_stt_median);
  this->stat.owd_ltt80 = this->owd_stat.ltt80th;
}

void fbrafbprocessor_approve_owd_ltt(FBRAFBProcessor *this)
{
  THIS_WRITELOCK (this);

  _refresh_owd_ltt(this);

  THIS_WRITEUNLOCK (this);
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

  this->stat.owd_stt_median = xrsummary->OWD.median_delay;

//  this->last_delay_t2         = this->last_delay_t1;
//  this->last_delay_t1         = this->last_delay;
//  this->last_delay            = xrsummary->OWD.median_delay;

  if(this->stat.owd_ltt80){
    this->stat.owdh_corr =  (gdouble) this->stat.owd_stt_median;
    this->stat.owdh_corr /=  (gdouble)this->stat.owd_ltt80;
  }else{
    this->stat.owdh_corr = /*this->stat.owdl_corr = */ 1.;
  }

done:
  return;
}

void _process_rle_discvector(FBRAFBProcessor *this, GstMPRTCPXRReportSummary *xr)
{
  FBRAFBProcessorItem *item;
  guint16 act_seq, end_seq;
  gint i;
  if(this->rle_initialized && _cmp_uint16(xr->DiscardedRLE.begin_seq, this->highest_acked_seq) < 0){
    g_warning("The received feedback rle vector begin_seq is lower than the actual highest_acked_seq");
    return;
  }

  this->rle_initialized = TRUE;
  this->lowest_acked_seq  = act_seq = xr->DiscardedRLE.begin_seq;
  this->highest_acked_seq = end_seq = xr->DiscardedRLE.end_seq;
  for(i=0; act_seq <= end_seq; ++act_seq, ++i){
    item = this->items + act_seq;
    item->discarded = !xr->DiscardedRLE.vector[i];
  }
}


#define _done_if_sent_queue_is_empty(this)                            \
    if(g_queue_is_empty(this->sent)){                                 \
      g_warning("sent queue is empty. What do you want to track?");   \
      goto done;                                                      \
    }                                                                 \

void _process_rle_discvector(FBRAFBProcessor *this, GstMPRTCPXRReportSummary *xr)
{
  FBRAFBProcessorItem* item;
  guint i;
  GList *it = NULL;

  _done_if_sent_queue_is_empty(this);

  //g_print("RLE processed\n");

  item = g_queue_peek_head(this->sent);
  while(_cmp_uint16(item->seq_num, xr->DiscardedRLE.end_seq) <= 0){
    this->stat.bytes_in_flight -= item->payload_bytes;
    --this->stat.packets_in_flight;
    this->stat.goodput_bytes += item->payload_bytes;
    ++this->stat.received_packets_in_1s;
    g_queue_push_tail(this->acked, g_queue_pop_head(this->sent));
    if(_cmp_uint16(item->seq_num, xr->DiscardedRLE.begin_seq) == 0){
      it = g_queue_peek_tail_link(this->acked);
    }
    if(g_queue_is_empty(this->sent)){
      break;
    }
    item = g_queue_peek_head(this->sent);
  }

  //TODO: Bytes in flight tracking comes here

  if(g_queue_is_empty(this->acked) || !it){
    goto done;
  }

  item = it->data;
  for(i = 0; _cmp_uint16(item->seq_num, xr->DiscardedRLE.end_seq) <= 0; ++i){
    if(!xr->DiscardedRLE.vector[i]){
      this->stat.goodput_bytes -= item->payload_bytes;
      item->discarded = TRUE;
      this->last_discard = _now(this);
      ++this->stat.discarded_packets_in_1s;
    }
    if(!it->next){
      break;
    }
    it = it->next;
    item = it->data;
  }

  _obsolate_acked_packet(this);

done:
  return;
}

#undef _done_if_sent_queue_is_empty

void _unref_item(FBRAFBProcessor * this, FBRAFBProcessorItem* item)
{
  if(0 < --item->ref){
    return;
  }
//  g_print("Drop an item seq is %hu, %s discarded, gp: %d\n",
//          item->seq_num,
//          !item->discarded?"doesn't":" ",
//          this->stat.goodput_bytes * 8);
  g_slice_free(FBRAFBProcessorItem, item);
}

#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
