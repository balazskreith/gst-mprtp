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

typedef struct _FBRAFBProcessorItem{
  guint        ref;
  guint16      seq_num;
  guint32      payload_bytes;
  GstClockTime sent;
  gboolean     discarded;
//  gboolean     seen;
}FBRAFBProcessorItem;

G_DEFINE_TYPE (FBRAFBProcessor, fbrafbprocessor, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void fbrafbprocessor_finalize (GObject * object);
static void _process_rle_discvector(FBRAFBProcessor *this, GstMPRTCPXRReportSummary *xr);
static gdouble _get_fbinterval_in_sec(FBRAFBProcessor *this);
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


//  {
//    gdouble bw;
//    bw = this->stat.max_bytes_in_flight * 8;
//    bw /= (gdouble)(stats->percentile * 2.) / (gdouble)GST_SECOND;
//    g_print("bw est: %f\n", bw);
//  }

//static gdouble _off_congestion(FBRASubController *this)
//{
//  GstClockTime elapsed,th_l,th_h;
//  if(!this->congestion_detected){
//    return 1.;
//  }
//  elapsed = GST_TIME_AS_MSECONDS(_now(this) - this->congestion_detected);
//  th_l = GST_TIME_AS_MSECONDS(this->srtt);
//  th_h = th_l * 3;
//  if(th_h < elapsed){
//    return 1.;
//  }
//  if(elapsed < th_l){
//    return 0.;
//  }
//  elapsed -= th_l;
//  return (gdouble)elapsed / (gdouble)th_h;
//}


//
//static gdouble _owd_ltt_alpha(FBRASubController *this)
//{
//  gdouble scl;
//  gdouble result;
//  scl    = 1000 / GST_TIME_AS_MSECONDS(fbrafbprocessor_get_fbinterval(this->fbprocessor));
//  result = _off_congestion(this) * _fbstat(this).stability;
//  result *= result;
//  result /= scl;
//  return result;
//}


static void owd_logger(gpointer data, gchar* string)
{
  FBRAFBProcessor *this = data;
  THIS_READLOCK(this);

  sprintf(string, "%lu,%lu,%lu,%lu\n",
               GST_TIME_AS_USECONDS(this->stat.owd_stt_median),
               GST_TIME_AS_USECONDS(this->stat.owd_ltt_median),
               GST_TIME_AS_USECONDS(this->stat.RTT),
               (GstClockTime)this->stat.srtt / 1000
               );

  THIS_READUNLOCK(this);

}

static void _owd_percentile_pipe(gpointer udata, swpercentilecandidates_t *candidates)
{
  FBRAFBProcessor *this = udata;
  if(!candidates->processed){
      this->owd_stat.median = this->owd_stat.min = this->owd_stat.max = this->stat.owd_stt_median;
      g_warning("Not enough owd_stt to calculate the median");
    return;
  }

  if(!candidates->left){
    this->owd_stat.median = *(GstClockTime*)candidates->right;
  }else if(!candidates->right){
    this->owd_stat.median = *(GstClockTime*)candidates->left;
  }else{
    this->owd_stat.median = *(GstClockTime*)candidates->left;
    this->owd_stat.median += *(GstClockTime*)candidates->right;
    this->owd_stat.median>>=1;
  }
  this->owd_stat.min = *(GstClockTime*)candidates->min;
  this->owd_stat.max = *(GstClockTime*)candidates->max;
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
  this->owd_sw           = make_slidingwindow_uint64(600, 60 * GST_SECOND);
  slidingwindow_add_plugin(this->owd_sw, make_swpercentile_with_sprint(50, bintree3cmp_uint64, _owd_percentile_pipe, this, swprinter_uint64));
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
  THIS_WRITELOCK (this);
  if(summary->RR.processed){
    this->stat.RTT  = summary->RR.RTT;
    this->stat.srtt = (this->stat.srtt == 0.) ? summary->RR.RTT : (summary->RR.RTT * .1 + this->stat.srtt * .9);
  }
  if(summary->XR.DiscardedRLE.processed){
    _process_rle_discvector(this, &summary->XR);
    this->stat.recent_discarded = 0 < this->last_discard && _now(this) < this->last_discard + this->stat.RTT;

  }
  if(summary->XR.OWD.processed){
    _process_owd(this, &summary->XR);
  }
  if(summary->AFB.processed){
    _process_afb(this, summary->AFB.fci_id, (GstRTCPAFB_REPS *)summary->AFB.fci_data);
  }
  ++this->measurements_num;
  THIS_WRITEUNLOCK (this);
}

void
fbrafbprocessor_get_stats (FBRAFBProcessor * this, FBRAFBProcessorStat* result)
{
  THIS_READLOCK (this);
  memcpy(result, &this->stat, sizeof(FBRAFBProcessorStat));
//  g_print("BiF: %d |GP:%d |owd_corr: %3.2f |owd_ltt: %lu |owd_stt: %lu |PiF: %d |RD: %d |SR: %d| SP: %d| REPS: %f\n",
//          this->stat.bytes_in_flight,
//          this->stat.goodput_bytes,
//          this->stat.owd_corr,
//          GST_TIME_AS_MSECONDS(this->stat.owd_ltt_median),
//          GST_TIME_AS_MSECONDS(this->stat.owd_stt_median),
//          this->stat.packets_in_flight,
//          this->stat.recent_discarded,
//          this->stat.sent_bytes_in_1s  * 8,
//          this->stat.sent_packets_in_1s,
//          this->stat.stability
//      );
  if(this->stat.goodput_bytes * 8 < 0) g_print("#%d|%d#\n", this->stat.goodput_bytes, this->stat.goodput_bytes * 8);
  THIS_READUNLOCK (this);
}

gint32 fbrafbprocessor_get_sent_bytes_in_1s(FBRAFBProcessor *this)
{
  gint32 result;
  THIS_READLOCK (this);
  _obsolate_sent_packet(this);
  result = this->stat.sent_bytes_in_1s;
  THIS_READUNLOCK(this);
  return result;
}

GstClockTime fbrafbprocessor_get_fbinterval(FBRAFBProcessor *this)
{
  GstClockTime result;
  THIS_READLOCK (this);
  result = _get_fbinterval_in_sec(this) * GST_SECOND;
  THIS_READUNLOCK(this);
  return result;
}

void fbrafbprocessor_record_congestion(FBRAFBProcessor *this)
{
  THIS_WRITELOCK (this);
  this->congestion_detected = _now(this);
  THIS_WRITEUNLOCK (this);
}

static gboolean _off_congestion(FBRAFBProcessor *this)
{
  GstClockTime now, elapsed;
  if(!this->congestion_detected){
      return 1.;
  }
  now = _now(this);
  if(now - this->stat.srtt < this->congestion_detected){
    return 0.;
  }
  if(this->congestion_detected < now - 3 * this->stat.srtt){
    return 1.;
  }
  elapsed = now - this->congestion_detected;
  return (gdouble)elapsed / this->stat.srtt;
}


static void _refresh_owd_ltt_ewma(FBRAFBProcessor *this)
{
  gdouble alpha;
  alpha = this->stat.stability * _off_congestion(this);
  alpha *= alpha;
  alpha *= _get_fbinterval_in_sec(this);
  this->stat.owd_ltt_median *= 1.-alpha;
  this->stat.owd_ltt_median += alpha * this->stat.owd_stt_median;
}

static void _refresh_owd_ltt_median(FBRAFBProcessor *this)
{
  slidingwindow_add_data(this->owd_sw, &this->stat.owd_stt_median);
  this->stat.owd_ltt_median = this->owd_stat.median;
}

void fbrafbprocessor_refresh_owd_ltt(FBRAFBProcessor *this)
{
  THIS_WRITELOCK (this);

  DISABLE_LINE _refresh_owd_ltt_ewma(this);
  _refresh_owd_ltt_median(this);

  THIS_WRITEUNLOCK (this);
}


gdouble _get_fbinterval_in_sec(FBRAFBProcessor *this)
{
  return (1.0/MIN(50,MAX(10,(this->stat.goodput_bytes * 8)/20000)));
}

void _process_afb(FBRAFBProcessor *this, guint32 id, GstRTCPAFB_REPS *reps)
{
  gfloat                stability;
  guint8                sampling_num;

  if(id != RTCP_AFB_REPS_ID){
    return;
  }
  gst_rtcp_afb_reps_getdown(reps, &sampling_num, &stability);
  this->stat.stability = 0 < sampling_num ? stability : 1.;

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

  if(this->stat.owd_ltt_median){
//    this->stat.owd_corr =  1.0 * this->last_delay + 0. * this->last_delay_t1 + 0. * this->last_delay_t2;
    this->stat.owd_corr =  (gdouble) this->stat.owd_stt_median;
    this->stat.owd_corr /=  (gdouble)this->stat.owd_ltt_median;
  }else{
    this->stat.owd_corr = 1.;
  }

done:
  return;
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
