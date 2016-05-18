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

static void _owd_ltt_pipe(gpointer data, PercentileTrackerPipeData *stats)
{
  FBRAFBProcessor *this = data;
  this->stat.owd_ltt_median = stats->percentile;
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
  this->sent        = g_queue_new();
  this->sent_in_1s  = g_queue_new();
  this->acked       = g_queue_new();
  this->sysclock    = gst_system_clock_obtain();
  this->stat.RTT    = GST_SECOND;
  this->owd_ltt     = make_percentiletracker(600, 50);
  percentiletracker_set_treshold(this->owd_ltt, 30 * GST_SECOND);
  percentiletracker_set_stats_pipe(this->owd_ltt, _owd_ltt_pipe, this);
}

FBRAFBProcessor *make_fbrafbprocessor(void)
{
    FBRAFBProcessor *this;
    this = g_object_new (FBRAFBPROCESSOR_TYPE, NULL);
    return this;
}


void fbrafbprocessor_reset(FBRAFBProcessor *this)
{
  THIS_WRITELOCK (this);

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
    }
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
    this->stat.RTT = summary->RR.RTT;
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

void fbrafbprocessor_approve_owd(FBRAFBProcessor *this)
{
  THIS_WRITELOCK (this);
  percentiletracker_add(this->owd_ltt, this->last_delay);
  THIS_WRITEUNLOCK (this);
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

  this->last_delay_t2         = this->last_delay_t1;
  this->last_delay_t1         = this->last_delay;
  this->last_delay            = xrsummary->OWD.median_delay;

  if(this->stat.owd_ltt_median){
    this->stat.owd_corr =  1.0 * this->last_delay + 0. * this->last_delay_t1 + 0. * this->last_delay_t2;
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

  item = g_queue_peek_head(this->sent);
  while(_cmp_uint16(item->seq_num, xr->DiscardedRLE.end_seq) <= 0){
    this->stat.bytes_in_flight -= item->payload_bytes;
    --this->stat.packets_in_flight;
    this->stat.goodput_bytes += item->payload_bytes;
    g_queue_push_tail(this->acked, g_queue_pop_head(this->sent));
    if(_cmp_uint16(item->seq_num, xr->DiscardedRLE.begin_seq) == 0){
      it = g_queue_peek_tail_link(this->acked);
    }
    if(g_queue_is_empty(this->sent)){
      break;
    }
    item = g_queue_peek_head(this->sent);
  }
  if(g_queue_is_empty(this->acked) || !it){
    goto done;
  }

  item = it->data;
  for(i = 0; _cmp_uint16(item->seq_num, xr->DiscardedRLE.end_seq) <= 0; ++i){
    if(!xr->DiscardedRLE.vector[i]){
      this->stat.goodput_bytes -= item->payload_bytes;
      item->discarded = TRUE;
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
