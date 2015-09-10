/* GStreamer Mprtp sender subflow
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
#include "mprtpssubflow.h"
#include "gstmprtcpbuffer.h"

#define MPRTPS_SUBFLOW_RTT_BOUNDARY_TO_LATE_EVENT (1500 * GST_MSECOND)
#define MPRTPS_SUBFLOW_RIPORT_TIMEOUT_TO_LATE_EVENT (30 * GST_SECOND)
#define MPRTPS_SUBFLOW_RTT_BOUNDARY_FROM_PASSIVE_TO_ACTIVE_STATE (300 * GST_MSECOND)
#define MPRTPS_SUBFLOW_TIME_BOUNDARY_TO_DETACHED_EVENT (5 * 60 * GST_SECOND)

GST_DEBUG_CATEGORY_STATIC (gst_mprtpssubflow_debug_category);
#define GST_CAT_DEFAULT gst_mprtpssubflow_debug_category

G_DEFINE_TYPE (MPRTPSSubflow, mprtps_subflow, G_TYPE_OBJECT);


static void mprtps_subflow_finalize (GObject * object);
static void mprtps_subflow_process_rtpbuffer_out(MPRTPSSubflow* subflow,
	guint ext_header_id, GstRTPBuffer* rtp);
static void mprtps_subflow_process_mprtcp_block(MPRTPSSubflow* subflow,
	GstMPRTCPSubflowBlock *block);

static void _print_rtp_packet_info(GstRTPBuffer *rtp);

static guint16 mprtps_subflow_get_id(MPRTPSSubflow* this);
static guint32 mprtps_subflow_get_sent_packet_num(MPRTPSSubflow* this);
static gboolean mprtps_subflow_is_active(MPRTPSSubflow* this);
static gboolean mprtps_subflow_is_non_congested(MPRTPSSubflow* this);
static gboolean mprtps_subflow_is_non_lossy(MPRTPSSubflow* this);
//static GstPad* mprtps_subflow_get_outpad(MPRTPSSubflow* this);
static gfloat mprtps_subflow_get_sending_bid(MPRTPSSubflow* this);
static guint8 mprtps_subflow_get_state(MPRTPSSubflow* this);
static gfloat mprtps_subflow_load_sending_rate(MPRTPSSubflow *this);

static MPRTPSubflowEvent mprtps_subflow_check_latency(MPRTPSSubflow* this);
static MPRTPSubflowEvent mprtps_subflow_check_congestion(MPRTPSSubflow* this);
static MPRTPSubflowEvent mprtps_subflow_check_lossy(MPRTPSSubflow* this);
static MPRTPSubflowEvent mprtps_subflow_check_monotocity(MPRTPSSubflow* this);
static gboolean mprtps_subflow_is_new(MPRTPSSubflow* this);
static gboolean mprtps_subflow_is_detached(MPRTPSSubflow* this);

static void mprtps_subflow_save_goodput(MPRTPSSubflow* this);
static void mprtps_subflow_set_joined(MPRTPSSubflow* this);
static void mprtps_subflow_set_detached(MPRTPSSubflow* this);
static void mprtps_subflow_set_active(MPRTPSSubflow* this);
static void mprtps_subflow_set_passive(MPRTPSSubflow* this);
static void mprtps_subflow_set_lossy(MPRTPSSubflow* this);
static void mprtps_subflow_set_non_lossy(MPRTPSSubflow* this);
static void mprtps_subflow_set_non_congested(MPRTPSSubflow* this);
static void mprtps_subflow_set_congested(MPRTPSSubflow* this);
static void
mprtps_subflow_setup_sr_riport(MPRTPSSubflow *this, GstMPRTCPSubflowRiport *header);

static gboolean mprtps_subflow_try_reload_from_passive(MPRTPSSubflow* this, gfloat *sending_bid);
static gboolean mprtps_subflow_try_reload_from_congested(MPRTPSSubflow* this, gfloat *sending_bid);
static gboolean mprtps_subflow_try_reload_from_lossy(MPRTPSSubflow* this, gfloat *sending_bid);

static gboolean mprtps_subflow_do_riport_now(MPRTPSSubflow* this, GstClockTime *next_time);
static void mprtps_subflow_set_avg_rtcp_size(MPRTPSSubflow *this, gsize packet_size);

static void _proc_rtcprr(MPRTPSSubflow* this, GstRTCPRR* rr);
static void _proc_rtcpxr_rfc7243(MPRTPSSubflow* this, GstRTCPXR_RFC7243* xr);
static gboolean _is_subflow_non_lossy(MPRTPSSubflow* this);
static gboolean _is_subflow_active(MPRTPSSubflow* this);
static gboolean _is_subflow_non_congested(MPRTPSSubflow* this);
static void _save_sending_bid(MPRTPSSubflow* this);



void
mprtps_subflow_class_init (MPRTPSSubflowClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = mprtps_subflow_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_mprtpssubflow_debug_category, "mprtpssubflow", 0, "MPRTP Sender Subflow");
}

/**
 * mprtp_source_new:
 *
 * Create a #MPRTPSource with @ssrc.
 *
 * Returns: a new #MPRTPSource. Use g_object_unref() after usage.
 */
MPRTPSSubflow*
make_mprtps_subflow(guint16 id)
{
  MPRTPSSubflow *result;

  result = g_object_new (MPRTPS_SUBFLOW_TYPE, NULL);
  result->id = id;
  //result->outpad = srcpad;
  return result;


}

/**
 * mprtps_subflow_reset:
 * @src: an #MPRTPSSubflow
 *
 * Reset the subflow of @src.
 */
void
mprtps_subflow_reset (MPRTPSSubflow * subflow)
{
  subflow->seq = 0;
  subflow->cycle_num = 0;
  subflow->octet_count = 0;
  subflow->packet_count = 0;
  subflow->early_discarded_bytes = 0;
  subflow->late_discarded_bytes = 0;
  subflow->early_discarded_bytes_sum = 0;
  subflow->late_discarded_bytes_sum = 0;
  subflow->state = MPRTP_SENDER_SUBFLOW_STATE_NON_LOSSY;
  subflow->late_riported = FALSE;
  subflow->last_riport_received = 0;
  subflow->RTT = 0;
  subflow->never_checked = TRUE;
  subflow->manual_event = MPRTP_SENDER_SUBFLOW_EVENT_KEEP;
  subflow->consecutive_lost = 0;
  subflow->consecutive_non_lost = 0;
  subflow->consecutive_discarded = 0;
  subflow->consecutive_keep = 0;
  subflow->consecutive_non_discarded = 0;
  subflow->sent_packet_num = 0;
  subflow->rr_blocks_read_index = 0;
  subflow->rr_blocks_write_index = 0;
  subflow->rr_blocks_arrived = FALSE;
  subflow->ssrc = g_random_int ();
  subflow->HSSN = 0;
  subflow->last_goodput = 0.0;
  subflow->goodput = 0.0;
  subflow->goodput_monotocity = 0;
  subflow->sending_bid = 1.0;
  subflow->saved_sending_bid = 0;
  subflow->saved_sending_bid_time = 0;
  subflow->sent_packet_num_since_last_rr = 0;
  subflow->sent_payload_bytes_sum = 0;
  subflow->initialized = FALSE;
  subflow->joined_time = 0;
  subflow->packet_limit_to_riport = 10;
  subflow->sent_packet_num_since_last_sr = 0;
  subflow->sent_octet_since_last_sr = 0;
  subflow->sr_started = FALSE;
  subflow->sr_riport_normal_period_time = 5 * GST_SECOND;
  subflow->media_bw_avg = 0.;
  subflow->avg_rtcp_size = 128.;
}


void
mprtps_subflow_init (MPRTPSSubflow * subflow)
{
  //subflow->fire = mprtps_subflow_FSM_fire;
  subflow->process_rtpbuf_out = mprtps_subflow_process_rtpbuffer_out;
  subflow->setup_sr_riport = mprtps_subflow_setup_sr_riport;
  subflow->get_id = mprtps_subflow_get_id;
  subflow->get_sent_packet_num = mprtps_subflow_get_sent_packet_num;
  subflow->is_active = mprtps_subflow_is_active;
  subflow->is_non_congested = mprtps_subflow_is_non_congested;
  subflow->is_non_lossy = mprtps_subflow_is_non_lossy;
  //subflow->get_outpad = mprtps_subflow_get_outpad;
  subflow->process_mprtcp_block = mprtps_subflow_process_mprtcp_block;
  subflow->get_sending_bid = mprtps_subflow_get_sending_bid;
  subflow->get_state = mprtps_subflow_get_state;
  subflow->check_congestion = mprtps_subflow_check_congestion;
  subflow->check_lossy = mprtps_subflow_check_lossy;
  subflow->check_latency = mprtps_subflow_check_latency;
  subflow->check_monotocity = mprtps_subflow_check_monotocity;
  subflow->is_new = mprtps_subflow_is_new;
  subflow->is_detached = mprtps_subflow_is_detached;
  subflow->set_joined = mprtps_subflow_set_joined;
  subflow->set_detached = mprtps_subflow_set_detached;
  subflow->set_active = mprtps_subflow_set_active;
  subflow->set_passive = mprtps_subflow_set_passive;
  subflow->set_non_congested = mprtps_subflow_set_non_congested;
  subflow->set_congested = mprtps_subflow_set_congested;
  subflow->set_lossy = mprtps_subflow_set_lossy;
  subflow->set_non_lossy = mprtps_subflow_set_non_lossy;
  subflow->try_reload_from_congested = mprtps_subflow_try_reload_from_congested;
  subflow->try_reload_from_lossy = mprtps_subflow_try_reload_from_lossy;
  subflow->try_reload_from_passive = mprtps_subflow_try_reload_from_passive;
  //subflow->push_buffer = mprtps_subflow_push_buffer;

  subflow->set_avg_rtcp_size = mprtps_subflow_set_avg_rtcp_size;
  subflow->do_riport_now = mprtps_subflow_do_riport_now;

  mprtps_subflow_reset (subflow);
  g_mutex_init(&subflow->mutex);
  subflow->sysclock = gst_system_clock_obtain();
}


void
mprtps_subflow_finalize (GObject * object)
{
  MPRTPSSubflow *subflow = MPRTPS_SUBFLOW(object);
  g_object_unref (subflow->sysclock);
  subflow = MPRTPS_SUBFLOW_CAST (object);

}

gboolean mprtps_subflow_is_new(MPRTPSSubflow* this)
{
	gboolean result;
	g_mutex_lock(&this->mutex);
	result = !this->initialized;
	g_mutex_unlock(&this->mutex);
	return result;
}


gboolean mprtps_subflow_is_detached(MPRTPSSubflow* this)
{
	gboolean result;
	g_mutex_lock(&this->mutex);
	result = this->detached;
	g_mutex_unlock(&this->mutex);
	return result;
}

void mprtps_subflow_set_joined(MPRTPSSubflow* this)
{
	g_mutex_lock(&this->mutex);
	this->initialized = TRUE;
	this->joined_time = gst_clock_get_time (this->sysclock);
	g_mutex_unlock(&this->mutex);
}

void mprtps_subflow_set_detached(MPRTPSSubflow* this)
{
	g_mutex_lock(&this->mutex);
	this->detached = TRUE;
	g_mutex_unlock(&this->mutex);
}

void mprtps_subflow_set_active(MPRTPSSubflow* this)
{
	g_mutex_lock(&this->mutex);
	this->state |= (guint8) MPRTP_SENDER_SUBFLOW_STATE_ACTIVE;

	this->consecutive_non_discarded = 0;
	this->consecutive_discarded = 0;
	this->consecutive_non_lost = 0;
	this->consecutive_lost = 0;
	g_mutex_unlock(&this->mutex);
}

void mprtps_subflow_set_passive(MPRTPSSubflow* this)
{
	g_mutex_lock(&this->mutex);
	this->state &= (guint8) 255 ^ (guint8)MPRTP_SENDER_SUBFLOW_STATE_ACTIVE;
	this->consecutive_non_discarded = 0;
	this->consecutive_discarded = 0;
	this->consecutive_non_lost = 0;
	this->consecutive_lost = 0;
	g_mutex_unlock(&this->mutex);
}

void mprtps_subflow_set_lossy(MPRTPSSubflow* this)
{
	g_mutex_lock(&this->mutex);
	this->state &= (guint8) 255 ^ (guint8)MPRTP_SENDER_SUBFLOW_STATE_NON_LOSSY;
	this->consecutive_lost = 0;
	this->consecutive_non_lost = 0;
	g_mutex_unlock(&this->mutex);
}

void mprtps_subflow_set_non_lossy(MPRTPSSubflow* this)
{
	g_mutex_lock(&this->mutex);
	this->state |= (guint8) MPRTP_SENDER_SUBFLOW_STATE_NON_LOSSY;
	this->consecutive_lost = 0;
	this->consecutive_non_lost = 0;
	g_mutex_unlock(&this->mutex);
}

void mprtps_subflow_set_congested(MPRTPSSubflow* this)
{
	g_mutex_lock(&this->mutex);
	this->state &= (guint8) 255 ^ (guint8)MPRTP_SENDER_SUBFLOW_STATE_NON_CONGESTED;
	this->consecutive_non_discarded = 0;
	this->consecutive_discarded = 0;
	this->consecutive_keep = 0;
	g_mutex_unlock(&this->mutex);
}

void mprtps_subflow_set_non_congested(MPRTPSSubflow* this)
{
	g_mutex_lock(&this->mutex);
	this->state |= (guint8) MPRTP_SENDER_SUBFLOW_STATE_NON_CONGESTED;
	this->consecutive_non_discarded = 0;
	this->consecutive_discarded = 0;
	this->consecutive_keep = 1;
	g_mutex_unlock(&this->mutex);
}

void
mprtps_subflow_process_rtpbuffer_out(MPRTPSSubflow* subflow, guint ext_header_id, GstRTPBuffer* rtp)
{
	MPRTPSubflowHeaderExtension data;
	guint payload_bytes;
	g_mutex_lock(&subflow->mutex);

	data.id = subflow->id;
	if(++subflow->seq == 0){
		++subflow->cycle_num;
	}
	data.seq = subflow->seq;
	++subflow->sent_packet_num_since_last_sr;
	++subflow->packet_count;
	++subflow->sent_packet_num;
	++subflow->sent_packet_num_since_last_rr;
	payload_bytes = gst_rtp_buffer_get_payload_len(rtp);
	subflow->sent_payload_bytes_sum += payload_bytes;
	subflow->octet_count += payload_bytes>>3;
	subflow->sent_octet_since_last_sr += (payload_bytes>>3) + 28 /*UDP header*/;
	gst_rtp_buffer_add_extension_onebyte_header(rtp, ext_header_id, (gpointer) &data, sizeof(data));

	g_mutex_unlock(&subflow->mutex);
	//_print_rtp_packet_info(rtp);
}

void mprtps_subflow_process_mprtcp_block(MPRTPSSubflow* this,
	GstMPRTCPSubflowBlock *block)
{
  guint8 type;
  GstClockTime now;

  g_mutex_lock(&this->mutex);
  now = gst_clock_get_time(this->sysclock);
  if(this->last_riport_received < now - (500 * GST_MSECOND)){
    this->last_riport_received = now;
  }

  gst_rtcp_header_getdown(&block->block_header, NULL,
    NULL, NULL, &type, NULL, NULL);
  if(type == GST_RTCP_TYPE_SR){

  }else if(type == GST_RTCP_TYPE_RR){
	  _proc_rtcprr(this, &block->receiver_riport);
  }else if(type == GST_RTCP_TYPE_XR){
     _proc_rtcpxr_rfc7243(this, &block->xr_rfc7243_riport);
  }


  g_mutex_unlock(&this->mutex);
}

gfloat mprtps_subflow_get_sending_bid(MPRTPSSubflow* this)
{
  gfloat result;
  gfloat rr_change;
  guint x;
  g_mutex_lock(&this->mutex);
  result = this->sending_bid;
  if(this->last_goodput == this->goodput){
	goto mprtps_subflow_get_sending_rate_done;
  }
  if(this->consecutive_keep < 1){
    result = this->goodput;
    //g_print("sending_bid (subflow %d) = goodput = %f\n", this->id, this->goodput);
  }else{
	if(this->consecutive_keep > 10){
	  this->consecutive_keep = 10;
	}
	x = this->consecutive_keep;
	this->sending_bid = (this->goodput + this->sending_bid * (gfloat) (x-1)) /
			            (gfloat) x;
//	g_print("sending_bid (subflow %d) = (%f + %f * %d) / %d = %f\n",
//			this->id, this->goodput, this->sending_bid, x-1, x, this->sending_bid);
  }
  result = this->sending_bid;
mprtps_subflow_get_sending_rate_done:
  g_mutex_unlock(&this->mutex);
  return result;
}



gboolean mprtps_subflow_try_reload_from_passive(MPRTPSSubflow* this, gfloat *sending_bid)
{
  gboolean result = FALSE;
  g_mutex_lock(&this->mutex);
  if(this->saved_sending_bid_time > 0){
    *sending_bid = this->saved_sending_bid;
    result = TRUE;
  }
  g_mutex_unlock(&this->mutex);
  return result;
}

gboolean mprtps_subflow_try_reload_from_congested(MPRTPSSubflow* this, gfloat *sending_bid)
{
  gboolean result = FALSE;
  GstClockTime now;
  g_mutex_lock(&this->mutex);
  now = gst_clock_get_time(this->sysclock);
  if(now - 5 * 60 * GST_SECOND < this->saved_sending_bid_time){
    *sending_bid = this->saved_sending_bid;
    result = TRUE;
  }
  g_mutex_unlock(&this->mutex);
  return result;
}


gboolean mprtps_subflow_try_reload_from_lossy(MPRTPSSubflow* this, gfloat *sending_bid)
{
  gboolean result = FALSE;
  GstClockTime now;
  g_mutex_lock(&this->mutex);
  now = gst_clock_get_time(this->sysclock);
  if(now - 60 * GST_SECOND < this->saved_sending_bid_time){
    *sending_bid = this->saved_sending_bid;
    result = TRUE;
  }
  g_mutex_unlock(&this->mutex);
  return result;
}

guint8 mprtps_subflow_get_state(MPRTPSSubflow* this)
{
  MPRTPSubflowFlags result;
  g_mutex_lock(&this->mutex);
  result = this->state;
  g_mutex_unlock(&this->mutex);
  return result;
}


static guint16 uint16_diff(guint16 a, guint16 b)
{
  if(a <= b){
  	return b-a;
  }
  return ~((guint16)(a-b));
}

void
_proc_rtcprr(MPRTPSSubflow* this, GstRTCPRR* rr)
{
  guint64 LSR,DLSR;
  GstClockTime now;
  GstRTCPRRBlock *rrb;
  guint16 HSN_diff;
  guint16 HSSN;
  gfloat lost_rate;
  GstClockTimeDiff interval;
  guint64 mseconds;
  guint32 payload_bytes_sum, discarded_bytes;

  now = gst_clock_get_time (this->sysclock);
  //--------------------------
  //validate
  //--------------------------

  gst_rtcp_rrb_getdown(&rr->blocks, NULL, NULL, NULL, &HSSN, NULL, &LSR, &DLSR);
  HSSN &= 0x0000FFFF;
  HSN_diff = uint16_diff(this->HSSN, HSSN);
  LSR =  (now & 0xFFFF000000000000ULL) | (((guint64)LSR)<<16);
  DLSR = (guint64)DLSR;

  if(HSN_diff > 32767){
    return;
  }
  if(this->rr_blocks_arrived && (LSR == 0 || DLSR == 0)){
    return;
  }

  //--------------------------
  //processing
  //--------------------------

  if(!this->rr_blocks_arrived ||
	 ++this->rr_blocks_write_index == MPRTPS_SUBFLOW_RRBLOCK_MAX)
  {
	  this->rr_blocks_write_index = 0;
  }

  if(this->rr_blocks_write_index == this->rr_blocks_read_index){
    if(++this->rr_blocks_read_index == MPRTPS_SUBFLOW_RRBLOCK_MAX){
    	this->rr_blocks_read_index = 0;
    }
  }

  rrb =  this->rr_blocks + this->rr_blocks_write_index;
  gst_rtcp_copy_rrb_ntoh(&rr->blocks, rrb);
  this->rr_blocks_arrivetime[this->rr_blocks_write_index] = now;

  if(LSR > 0){
	guint64 diff = now - LSR;
    //RTT = now - LSR - DLSR;
//    g_print("RTT (%llu)= now (%llu) - LSR (%llu) - DLSR (%llu)\n",
//    		  this->RTT, now, LSR, DLSR);
	DLSR = GST_MSECOND * DLSR;
    if(DLSR < diff){
//    	g_print("DLSR: %llu <? diff: %llu\n", DLSR, diff);
    	this->RTT = diff - DLSR;
//    	    g_print("RTT - subflow %d (%llu)= now (%llu) - LSR (%llu) - DLSR (%llu)\n",
//    	    		  this->id, this->RTT, now, LSR, DLSR);
    }
  }

  if(rrb->fraction_lost > 0){
    ++this->consecutive_lost;
    this->consecutive_non_lost = 0;
  }else{
    this->consecutive_lost = 0;
    ++this->consecutive_non_lost;
  }
  lost_rate = ((gfloat) rrb->fraction_lost) / 256.0;
  this->lost_rate = lost_rate;

  if(!this->rr_blocks_arrived){
    goto _proc_rtcprr_done;
  }

  interval = GST_CLOCK_DIFF(this->RRT, now);
  mseconds = GST_TIME_AS_MSECONDS((GstClockTime) interval);

  this->last_goodput = this->goodput;
  if(this->sent_packet_num_since_last_rr < HSN_diff){
    this->sent_packet_num_since_last_rr = HSN_diff;
  }
  payload_bytes_sum = (gfloat) this->sent_payload_bytes_sum /
		              (gfloat) this->sent_packet_num_since_last_rr *
					  (gfloat) HSN_diff;
  this->HSSN = HSSN;
  this->sent_packet_num_since_last_rr -= HSN_diff;
  this->sent_payload_bytes_sum -= payload_bytes_sum;

  discarded_bytes = this->late_discarded_bytes + this->early_discarded_bytes;
/*
  this->goodput = ((gfloat) payload_bytes_sum *
		                 (1.0-lost_rate) -
						 (gfloat)discarded_bytes) /
						((gfloat)(interval>>16));
*/
  this->goodput = ((gfloat) payload_bytes_sum *
  		                 (1.0-lost_rate) -
  						 (gfloat)discarded_bytes) /
  						((gfloat) mseconds);

  /*
  g_print("subflow %d %f * %f - %f / %f = %f\n", this->id,
		  (gfloat)payload_bytes_sum,
		  (1.0-lost_rate),
		  (gfloat)discarded_bytes,
		  ((gfloat) mseconds),
		  this->goodput);
  /**/
_proc_rtcprr_done:
  this->rr_blocks_arrived = TRUE;
  this->RRT = now;
}

void
_proc_rtcpxr_rfc7243(MPRTPSSubflow* this, GstRTCPXR_RFC7243* xr)
{
  GstClockTime now;
  guint8 interval_metric;
  guint32 discarded_bytes;
  gboolean early_bit;

  now = gst_clock_get_time (this->sysclock);

  gst_rtcp_xr_rfc7243_getdown(xr, &interval_metric,
		&early_bit, NULL, &discarded_bytes);

  if(interval_metric == RTCP_XR_RFC7243_I_FLAG_CUMULATIVE_DURATION){
    if(early_bit){
      this->early_discarded_bytes = discarded_bytes - this->early_discarded_bytes_sum;
	  this->early_discarded_bytes_sum = discarded_bytes;
    }else{
      this->late_discarded_bytes = discarded_bytes - this->late_discarded_bytes_sum;
	  this->late_discarded_bytes_sum = discarded_bytes;
    }
  }else if(interval_metric == RTCP_XR_RFC7243_I_FLAG_INTERVAL_DURATION){
    if(early_bit){
      this->early_discarded_bytes = discarded_bytes;
      this->early_discarded_bytes_sum += this->early_discarded_bytes;
    }else{
      this->late_discarded_bytes = discarded_bytes;
      this->late_discarded_bytes_sum += this->late_discarded_bytes;
    }
  }else if(interval_metric == RTCP_XR_RFC7243_I_FLAG_SAMPLED_METRIC){

  }

  if(discarded_bytes > 0){
    ++this->consecutive_discarded;
    this->consecutive_non_discarded = 0;
  }else{
	this->consecutive_discarded = 0;
	++this->consecutive_non_discarded;
  }

  this->last_xr7243_riport_received = now;
}


void
mprtps_subflow_setup_sr_riport(MPRTPSSubflow *this,
		GstMPRTCPSubflowRiport *riport)
{
  GstMPRTCPSubflowBlock *block;
  GstRTCPSR *sr;
  guint64 ntptime;
  guint32 rtptime;

  g_mutex_lock(&this->mutex);

  ntptime = gst_clock_get_time(this->sysclock);

  rtptime = (guint32)(gst_rtcp_ntp_to_unix (ntptime)>>32), //rtptime

  block = gst_mprtcp_riport_add_block_begin(riport, this->id);
  sr = gst_mprtcp_riport_block_add_sr(block);
  gst_rtcp_header_change(&sr->header, NULL, NULL, NULL, NULL, NULL, &this->ssrc);

  gst_rtcp_srb_setup(&sr->sender_block, ntptime, rtptime,
	this->packet_count, this->octet_count);

  gst_mprtcp_riport_add_block_end(riport, block);
  if(this->last_sr_riport_sent_time > 0){
	 this->media_bw_avg += (((gdouble) (this->sent_octet_since_last_sr<<3) /
			                 ((gdouble)GST_TIME_AS_MSECONDS(
			                   ntptime -this->last_sr_riport_sent_time
							  ) * 1./1000.
							 )) - this->media_bw_avg) * 1./16.
							;
  }

  this->last_sr_riport_sent_time = ntptime;
  this->sent_packet_num_since_last_sr = 0;
  this->sent_octet_since_last_sr = 0;
  g_mutex_unlock(&this->mutex);
}






guint16 mprtps_subflow_get_id(MPRTPSSubflow* this)
{
	guint16 result;
	g_mutex_lock(&this->mutex);
	result = this->id;
	g_mutex_unlock(&this->mutex);
	return result;
}

guint32 mprtps_subflow_get_sent_packet_num(MPRTPSSubflow* this)
{
	guint32 result;
	g_mutex_lock(&this->mutex);
	result = this->sent_packet_num;
	this->sent_packet_num = 0;
	g_mutex_unlock(&this->mutex);
	return result;
}


void mprtps_subflow_set_avg_rtcp_size(MPRTPSSubflow *this, gsize packet_size)
{
	g_mutex_lock(&this->mutex);
	this->avg_rtcp_size += ((gdouble)packet_size - this->avg_rtcp_size) * 1./16.;
	g_mutex_unlock(&this->mutex);
}

gboolean mprtps_subflow_do_riport_now(MPRTPSSubflow* this, GstClockTime *next_time)
{
	GstClockTime now;
	gboolean result = FALSE;
	GstClockTime t_normal;
	GstClockTime t_bw_min;

	gdouble randv = g_random_double_range(0.5, 1.5);
	g_mutex_lock(&this->mutex);
	now = gst_clock_get_time(this->sysclock);
    if(!this->sr_started){
      this->sr_riport_interval = 1 * GST_SECOND;
      this->sr_riport_time = now + this->sr_riport_interval;
      this->sr_started = TRUE;
      goto mprtpr_subflow_is_riporting_time_done;
    }

	if(now < this->sr_riport_time){
      goto mprtpr_subflow_is_riporting_time_done;
	}

    if(this->media_bw_avg > 0.0){
      this->sr_riport_bw = this->media_bw_avg * 0.01;
      t_bw_min = (GstClockTime)(this->avg_rtcp_size*2./this->sr_riport_bw * (gdouble)GST_SECOND) ;
      t_normal = MIN(this->sr_riport_normal_period_time, t_bw_min);
      if(t_normal < GST_SECOND){
    	 t_normal = GST_SECOND;
      }
    }else{
      t_normal = this->sr_riport_normal_period_time;
    }

    result = TRUE;
    this->sr_riport_interval = (gdouble)t_normal * randv;


	this->sr_riport_time = now + this->sr_riport_interval;
//	 g_print("subflow %d (%llu) this->sr_riport_time = %llu + %llu\n",
//			 this->id, this->ssrc, now,
//				GST_TIME_AS_MSECONDS(this->sr_riport_interval));
mprtpr_subflow_is_riporting_time_done:

    *next_time = this->sr_riport_time;
	g_mutex_unlock(&this->mutex);
	return result;
}




gfloat mprtps_subflow_load_sending_rate(MPRTPSSubflow *this)
{
	gfloat result = 0.0;
	g_mutex_lock(&this->mutex);
    if(gst_clock_get_time(this->sysclock) - 1 * GST_SECOND
    		< this->saved_sending_bid_time){
      result = this->saved_sending_bid;
      this->sending_bid = this->saved_sending_bid;
    }
	g_mutex_unlock(&this->mutex);
	return result;
}



gboolean mprtps_subflow_is_active(MPRTPSSubflow* this)
{
	gboolean result;
	g_mutex_lock(&this->mutex);
	result = _is_subflow_active(this);
	g_mutex_unlock(&this->mutex);
	return result;
}

gboolean mprtps_subflow_is_non_lossy(MPRTPSSubflow* this)
{
	gboolean result;
	g_mutex_lock(&this->mutex);
	result = _is_subflow_non_lossy(this);
	g_mutex_unlock(&this->mutex);
	return result;
}

gboolean mprtps_subflow_is_non_congested(MPRTPSSubflow* this)
{
	gboolean result;
	g_mutex_lock(&this->mutex);
	result = _is_subflow_non_congested(this);
	g_mutex_unlock(&this->mutex);
	return result;
}



MPRTPSubflowEvent mprtps_subflow_check_latency(MPRTPSSubflow* this)
{
	GstClockTime now;
	MPRTPSubflowEvent result = MPRTP_SENDER_SUBFLOW_EVENT_KEEP;
	g_mutex_lock(&this->mutex);
	now = gst_clock_get_time (this->sysclock);
	if(!_is_subflow_active(this)){
	  if(this->last_checked_riport_time < this->last_riport_received &&
		 this->RTT < MPRTPS_SUBFLOW_RTT_BOUNDARY_FROM_PASSIVE_TO_ACTIVE_STATE)
	  {
		 result = MPRTP_SENDER_SUBFLOW_EVENT_SETTLED;
	  }
	  goto mprtps_subflow_check_latency_done;
	}
	if(!this->rr_blocks_arrived){
	  if(this->joined_time > 0 &&
		 this->joined_time < now - MPRTPS_SUBFLOW_RIPORT_TIMEOUT_TO_LATE_EVENT){
		result = MPRTP_SENDER_SUBFLOW_EVENT_LATE;
	  }
	  goto mprtps_subflow_check_latency_done;
	}

	if(this->last_checked_riport_time == this->last_riport_received)
	{
	  if(this->last_riport_received <
		 now - MPRTPS_SUBFLOW_RIPORT_TIMEOUT_TO_LATE_EVENT)
	  {
	    result = MPRTP_SENDER_SUBFLOW_EVENT_LATE;
	  }
	  goto mprtps_subflow_check_latency_done;
   }

   if(0 < this->RTT && MPRTPS_SUBFLOW_RTT_BOUNDARY_TO_LATE_EVENT < this->RTT){
	   result = MPRTP_SENDER_SUBFLOW_EVENT_LATE;
	   goto mprtps_subflow_check_latency_done;
   }

mprtps_subflow_check_latency_done:
	g_mutex_unlock(&this->mutex);
	return result;
}

MPRTPSubflowEvent mprtps_subflow_check_congestion(MPRTPSSubflow* this)
{
	MPRTPSubflowEvent result = MPRTP_SENDER_SUBFLOW_EVENT_KEEP;
	g_mutex_lock(&this->mutex);
	if(this->last_checked_riport_time == this->last_riport_received){
	  goto mprtps_subflow_check_congestion_done;
	}

    if(!_is_subflow_non_congested(this)){
      if(this->consecutive_non_discarded > 2){
    	this->consecutive_non_discarded = 0;
        result = MPRTP_SENDER_SUBFLOW_EVENT_SETTLED;
      }
      goto mprtps_subflow_check_congestion_done;
    }

    if(this->consecutive_discarded == 1){
		result = MPRTP_SENDER_SUBFLOW_EVENT_DISTORTION;
		goto mprtps_subflow_check_congestion_done;
	}

    if(this->consecutive_discarded > 2){
    	this->consecutive_discarded = 0;
    	result = MPRTP_SENDER_SUBFLOW_EVENT_CONGESTION;
    	goto mprtps_subflow_check_congestion_done;
    }

    ++this->consecutive_keep;
    if(this->consecutive_non_discarded > 2){
      _save_sending_bid(this);
      goto mprtps_subflow_check_congestion_done;
    }

mprtps_subflow_check_congestion_done:
    if(result != MPRTP_SENDER_SUBFLOW_EVENT_KEEP){
      this->consecutive_keep = 0;
    }
	g_mutex_unlock(&this->mutex);
	return result;
}

MPRTPSubflowEvent mprtps_subflow_check_lossy(MPRTPSSubflow* this)
{
	MPRTPSubflowEvent result = MPRTP_SENDER_SUBFLOW_EVENT_KEEP;
	g_mutex_lock(&this->mutex);
	if(this->last_checked_riport_time == this->last_riport_received){
	  goto mprtps_subflow_check_lossy_done;
	}
	if(!_is_subflow_non_lossy(this)){
	  if(this->consecutive_non_lost > 2){
		this->consecutive_non_lost = 0;
	    result = MPRTP_SENDER_SUBFLOW_EVENT_SETTLED;
	  }
	  goto mprtps_subflow_check_lossy_done;
	}

	if(this->consecutive_lost > 2){
	  this->consecutive_lost = 0;
	  result = MPRTP_SENDER_SUBFLOW_EVENT_LOSTS;
	  goto mprtps_subflow_check_lossy_done;
	}

mprtps_subflow_check_lossy_done:
	g_mutex_unlock(&this->mutex);
	return result;
}

MPRTPSubflowEvent mprtps_subflow_check_monotocity(MPRTPSSubflow* this)
{
	MPRTPSubflowEvent result = MPRTP_SENDER_SUBFLOW_EVENT_KEEP;
	gfloat actual,overall;
	GstClockTime now;
	g_mutex_lock(&this->mutex);
	now = gst_clock_get_time(this->sysclock);
	if(this->last_checked_riport_time == this->last_riport_received){
	  goto mprtps_subflow_check_monotocity_done;
	}
    if(!_is_subflow_active(this) || !_is_subflow_non_congested(this)){
      this->goodput_monotocity = 0;
      goto mprtps_subflow_check_monotocity_done;
    }

    if(now - 45 * GST_SECOND < this->saved_sending_bid_time){
      overall = this->goodput / this->saved_sending_bid;
      if(overall < 0.51){
        this->goodput_monotocity = -3;
        goto mprtps_subflow_check_monotocity_done;
      }else if(1.49 < overall){
    	this->goodput_monotocity = 3;
    	goto mprtps_subflow_check_monotocity_done;
      }
    }

	actual = this->goodput / this->last_goodput;
	if(actual < 0.51){
	  this->goodput_monotocity = -3;
    }else if(actual < 0.9){
	  --this->goodput_monotocity;
    }else if(actual > 1.49){
	  this->goodput_monotocity = 3;
    }else if(actual > 1.1){
	  ++this->goodput_monotocity;
    }else{
	  this->goodput_monotocity = 0;
    }

mprtps_subflow_check_monotocity_done:
	if(this->goodput_monotocity > 2 || this->goodput_monotocity < -2){
	  this->goodput_monotocity = 0;
	  result = MPRTP_SENDER_SUBFLOW_EVENT_REFRESH;
	}
	g_mutex_unlock(&this->mutex);
	return result;
}


void mprtps_subflow_save_goodput(MPRTPSSubflow* this)
{
  g_mutex_lock(&this->mutex);
  _save_sending_bid(this);
  g_mutex_unlock(&this->mutex);
}


gboolean _is_subflow_active(MPRTPSSubflow* this)
{
  return (this->state & (guint8) MPRTP_SENDER_SUBFLOW_STATE_ACTIVE) ? TRUE : FALSE;
}

gboolean _is_subflow_non_lossy(MPRTPSSubflow* this)
{
  return (this->state & (guint8) MPRTP_SENDER_SUBFLOW_STATE_NON_LOSSY) ? TRUE : FALSE;
}

gboolean _is_subflow_non_congested(MPRTPSSubflow* this)
{
  return (this->state & (guint8) MPRTP_SENDER_SUBFLOW_STATE_NON_CONGESTED) ? TRUE : FALSE;
}

void _save_sending_bid(MPRTPSSubflow* this)
{
  this->saved_sending_bid = this->sending_bid;
  this->saved_sending_bid_time = gst_clock_get_time(this->sysclock);
}


void _print_rtp_packet_info(GstRTPBuffer *rtp)
{
	gboolean extended;
	g_print(
   "0               1               2               3          \n"
   "0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 \n"
   "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
   "|%3d|%1d|%1d|%7d|%1d|%13d|%31d|\n"
   "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
   "|%63u|\n"
   "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n"
   "|%63u|\n",
			gst_rtp_buffer_get_version(rtp),
			gst_rtp_buffer_get_padding(rtp),
			extended = gst_rtp_buffer_get_extension(rtp),
			gst_rtp_buffer_get_csrc_count(rtp),
			gst_rtp_buffer_get_marker(rtp),
			gst_rtp_buffer_get_payload_type(rtp),
			gst_rtp_buffer_get_seq(rtp),
			gst_rtp_buffer_get_timestamp(rtp),
			gst_rtp_buffer_get_ssrc(rtp)
			);

	if(extended){
		guint16 bits;
		guint8 *pdata;
		guint wordlen;
		gulong index = 0;

		gst_rtp_buffer_get_extension_data (rtp, &bits, (gpointer) & pdata, &wordlen);


		g_print(
	   "+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+\n"
	   "|0x%-29X|%31d|\n"
	   "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n",
	   bits,
	   wordlen);

	   for(index = 0; index < wordlen; ++index){
		 g_print("|0x%-5X = %5d|0x%-5X = %5d|0x%-5X = %5d|0x%-5X = %5d|\n"
				 "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n",
				 *(pdata+index*4), *(pdata+index*4),
				 *(pdata+index*4+1),*(pdata+index*4+1),
				 *(pdata+index*4+2),*(pdata+index*4+2),
				 *(pdata+index*4+3),*(pdata+index*4+3));
	  }
	  g_print("+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+\n");
	}
}
