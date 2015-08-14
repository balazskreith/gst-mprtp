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

#define MPRTPS_SUBFLOW_RTT_BOUNDARY_TO_LATE_EVENT (300 * GST_MSECOND)
#define MPRTPS_SUBFLOW_RIPORT_TIMEOUT_TO_LATE_EVENT (30 * GST_SECOND)
#define MPRTPS_SUBFLOW_RTT_BOUNDARY_FROM_PASSIVE_STATE (200 * GST_MSECOND)
#define MPRTPS_SUBFLOW_TIME_BOUNDARY_TO_DETACHED_EVENT (5 * 60 * GST_SECOND)

GST_DEBUG_CATEGORY_STATIC (gst_mprtpssubflow_debug_category);
#define GST_CAT_DEFAULT gst_mprtpssubflow_debug_category

G_DEFINE_TYPE (MPRTPSSubflow, mprtps_subflow, G_TYPE_OBJECT);


static void mprtps_subflow_finalize (GObject * object);
static void mprtps_subflow_process_rtpbuffer_out(MPRTPSSubflow* subflow,
	guint ext_header_id, GstRTPBuffer* rtp);
static void mprtps_subflow_process_mprtcp_block(MPRTPSSubflow* subflow,
	GstMPRTCPSubflowBlock *block);

static void mprtps_subflow_FSM_fire(MPRTPSSubflow *subflow,
	MPRTPSubflowEvent event, void* data);
static void _print_rtp_packet_info(GstRTPBuffer *rtp);

static guint16 mprtps_subflow_get_id(MPRTPSSubflow* this);
static GstClockTime mprtps_subflow_get_sr_riport_time(MPRTPSSubflow* this);
static void mprtps_subflow_set_sr_riport_time(MPRTPSSubflow*,GstClockTime);
static guint32 mprtps_subflow_get_sent_packet_num(MPRTPSSubflow* this);
static gboolean mprtps_subflow_is_active(MPRTPSSubflow* this);
static GstPad* mprtps_subflow_get_outpad(MPRTPSSubflow* this);
static gfloat mprtps_subflow_get_sending_rate(MPRTPSSubflow* this);
static void mprtps_subflow_set_event(MPRTPSSubflow *this, MPRTPSubflowEvent event);
static MPRTPSubflowEvent mprtps_subflow_check(MPRTPSSubflow* this);
static void mprtps_subflow_set_state(MPRTPSSubflow* this, MPRTPSubflowStates target);
static MPRTPSubflowStates mprtps_subflow_get_state(MPRTPSSubflow* this);
static MPRTPSubflowEvent _get_event_for_non_congested_state(MPRTPSSubflow* this, GstClockTime now);
static MPRTPSubflowEvent _get_event_for_passive_state(MPRTPSSubflow* this, GstClockTime now);
static MPRTPSubflowEvent _get_event_for_congested_state(MPRTPSSubflow* this, GstClockTime now);
static void mprtps_subflow_save_sending_rate(MPRTPSSubflow *this);
static gfloat mprtps_subflow_load_sending_rate(MPRTPSSubflow *this);
static guint mprtps_subflow_get_consecutive_keeps_num(MPRTPSSubflow *this);

static void
mprtps_subflow_setup_sr_riport(MPRTPSSubflow *this, GstMPRTCPSubflowRiport *header);

static void _proc_rtcprr(MPRTPSSubflow* this, GstRTCPRR* rr);
static void _proc_rtcpxr_rfc7243(MPRTPSSubflow* this, GstRTCPXR_RFC7243* xr);

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
make_mprtps_subflow(guint16 id, GstPad* srcpad)
{
  MPRTPSSubflow *result;

  result = g_object_new (MPRTPS_SUBFLOW_TYPE, NULL);
  result->id = id;
  result->outpad = srcpad;
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
  subflow->state = MPRTP_SENDER_SUBFLOW_STATE_NON_CONGESTED;
  subflow->last_riport_received = 0;
  subflow->RTT = 0;
  subflow->never_checked = TRUE;
  subflow->manual_event = MPRTP_SENDER_SUBFLOW_EVENT_KEEP;
  subflow->consecutive_lost = 0;
  subflow->consecutive_distortions = 0;
  subflow->consecutive_keep_events = 0;
  subflow->consecutive_discarded = 0;
  subflow->consecutive_settlements = 0;
  subflow->sent_packet_num = 0;
  subflow->rr_blocks_read_index = 0;
  subflow->rr_blocks_write_index = 0;
  subflow->rr_blocks_arrived = FALSE;
  //subflow->last_increased_failed = FALSE;
  //subflow->try_increased = FALSE;
  subflow->ssrc = g_random_int ();
  subflow->HSSN = 0;
  subflow->last_receiver_rate = 0.0;
  subflow->receiver_rate = 0.0;
  subflow->rr_monotocity = 0;
  subflow->last_rr_change = 0;
  subflow->SR = 1.0;
  subflow->saved_sending_rate = 0;
  subflow->saved_sending_rate_time = 0;
  subflow->sent_packet_num_since_last_rr = 0;
  subflow->sent_payload_bytes_sum = 0;
}


void
mprtps_subflow_init (MPRTPSSubflow * subflow)
{
  //subflow->fire = mprtps_subflow_FSM_fire;
  subflow->process_rtpbuf_out = mprtps_subflow_process_rtpbuffer_out;
  subflow->setup_sr_riport = mprtps_subflow_setup_sr_riport;
  subflow->get_id = mprtps_subflow_get_id;
  subflow->get_sr_riport_time = mprtps_subflow_get_sr_riport_time;
  subflow->set_sr_riport_time = mprtps_subflow_set_sr_riport_time;
  subflow->get_sent_packet_num = mprtps_subflow_get_sent_packet_num;
  subflow->is_active = mprtps_subflow_is_active;
  subflow->get_outpad = mprtps_subflow_get_outpad;
  subflow->process_mprtcp_block = mprtps_subflow_process_mprtcp_block;
  subflow->get_sending_rate = mprtps_subflow_get_sending_rate;
  subflow->set_event = mprtps_subflow_set_event;
  subflow->check = mprtps_subflow_check;
  subflow->set_state = mprtps_subflow_set_state;
  subflow->get_state = mprtps_subflow_get_state;
  subflow->save_sending_rate = mprtps_subflow_save_sending_rate;
  subflow->load_sending_rate = mprtps_subflow_load_sending_rate;
  subflow->get_consecutive_keeps_num = mprtps_subflow_get_consecutive_keeps_num;

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

void
mprtps_subflow_process_rtpbuffer_out(MPRTPSSubflow* subflow, guint ext_header_id, GstRTPBuffer* rtp)
{
	MPRTPSubflowHeaderExtension data;
	g_mutex_lock(&subflow->mutex);

	data.id = subflow->id;
	if(++subflow->seq == 0){
		++subflow->cycle_num;
	}
	data.seq = subflow->seq;
	++subflow->packet_count;
	++subflow->sent_packet_num;
	++subflow->sent_packet_num_since_last_rr;
	subflow->sent_payload_bytes_sum += gst_rtp_buffer_get_payload_len(rtp);
	subflow->octet_count += gst_rtp_buffer_get_payload_len(rtp);
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

void mprtps_subflow_set_event(MPRTPSSubflow *this, MPRTPSubflowEvent event)
{
	g_mutex_lock(&this->mutex);
	this->manual_event = event;
	g_mutex_unlock(&this->mutex);
}

gfloat mprtps_subflow_get_sending_rate(MPRTPSSubflow* this)
{
  gfloat result;
  gfloat rr_change;
  g_mutex_lock(&this->mutex);
  result = this->SR;
  if(this->last_receiver_rate == this->receiver_rate){
	goto mprtps_subflow_get_sending_rate_done;
  }

  rr_change = this->receiver_rate / this->last_receiver_rate;
  if(rr_change < 0.9){
    --this->rr_monotocity;
  }else if(rr_change > 1.1){
    ++this->rr_monotocity;
  }else{
	if(this->last_rr_change < 0.51){
      this->rr_monotocity = -3;
	}else if(this->last_rr_change > 1.49){
	  this->rr_monotocity = 3;
	}else{
      this->rr_monotocity = 0;
	}
  }

  if(this->rr_monotocity < -2 || this->rr_monotocity > 2 ){
	GstClockTime now = gst_clock_get_time(this->sysclock);
    if(this->last_refresh_event_time < now - 10 * GST_SECOND){
      this->manual_event = MPRTP_SENDER_SUBFLOW_EVENT_REFRESH;
      this->last_refresh_event_time = now;
    }
    this->rr_monotocity = 0;
  }

  this->last_rr_change = rr_change;
  result = (this->receiver_rate + 3.0 * this->SR) / 4.0;
  this->SR = result;

mprtps_subflow_get_sending_rate_done:
  g_mutex_unlock(&this->mutex);
  return result;
}

void mprtps_subflow_set_state(MPRTPSSubflow* this, MPRTPSubflowStates target)
{
  g_mutex_lock(&this->mutex);
  this->state = target;
  g_mutex_unlock(&this->mutex);
}

MPRTPSubflowStates mprtps_subflow_get_state(MPRTPSSubflow* this)
{
  MPRTPSubflowStates result;
  g_mutex_lock(&this->mutex);
  result = this->state;
  g_mutex_unlock(&this->mutex);
  return result;
}

MPRTPSubflowEvent mprtps_subflow_check(MPRTPSSubflow* this)
{
  MPRTPSubflowEvent result = MPRTP_SENDER_SUBFLOW_EVENT_KEEP;
  GstClockTime now;

  g_mutex_lock(&this->mutex);
  now = gst_clock_get_time(this->sysclock);

  if(this->never_checked){
	this->never_checked = FALSE;
	result = MPRTP_SENDER_SUBFLOW_EVENT_JOINED;
	goto mprtps_subflow_check_done;
  }

  switch(this->state){
    case MPRTP_SENDER_SUBFLOW_STATE_NON_CONGESTED:
      result =_get_event_for_non_congested_state(this, now);
    break;
    case MPRTP_SENDER_SUBFLOW_STATE_CONGESTED:
      result =_get_event_for_congested_state(this, now);
    break;
    case MPRTP_SENDER_SUBFLOW_STATE_PASSIVE:
      result =_get_event_for_passive_state(this, now);
    break;
    default:
    break;
  }

mprtps_subflow_check_done:
  this->last_checked_riport_time = this->last_riport_received;
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
  gint i;
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
    this->RTT = now - LSR - DLSR;
    //g_print("RTT (%llu)= now (%llu) - LSR (%llu) - DLSR (%llu)\n",
    //		  this->RTT, now, LSR, DLSR);
  }

  if(rrb->fraction_lost > 0){
    ++this->consecutive_lost;
  }else{
    this->consecutive_lost = 0;
  }
  lost_rate = ((gfloat) rrb->fraction_lost) / 256.0;

  if(!this->rr_blocks_arrived){
    goto _proc_rtcprr_done;
  }

  interval = GST_CLOCK_DIFF(this->RRT, now);
  this->last_receiver_rate = this->receiver_rate;
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

  this->receiver_rate = ((gfloat) payload_bytes_sum *
						 (1.0-lost_rate) -
						 (gfloat)discarded_bytes) /
						((gfloat)(interval>>16));

  //*
  g_print("subflow %d %f * %f - %f / %f = %f\n", this->id,
		  (gfloat)payload_bytes_sum,
		  (1.0-lost_rate),
		  (gfloat)discarded_bytes,
		  ((gfloat)(interval>>16)),
		  this->receiver_rate);
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
  }else{
	this->consecutive_discarded = 0;
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

  g_mutex_unlock(&this->mutex);
}


void
mprtps_subflow_FSM_fire(MPRTPSSubflow *this, MPRTPSubflowEvent event, void *data)
{
  gfloat *rate;
  g_mutex_lock(&this->mutex);
  _fire(this, event, data);
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

GstClockTime mprtps_subflow_get_sr_riport_time(MPRTPSSubflow* this)
{
	GstClockTime result;
	g_mutex_lock(&this->mutex);
	result = this->sr_riport_time;
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

void mprtps_subflow_save_sending_rate(MPRTPSSubflow *this)
{
	g_mutex_lock(&this->mutex);
	this->saved_sending_rate = this->SR;
	this->saved_sending_rate_time = gst_clock_get_time(this->sysclock);
	g_mutex_unlock(&this->mutex);
}

gfloat mprtps_subflow_load_sending_rate(MPRTPSSubflow *this)
{
	gfloat result = 0.0;
	g_mutex_lock(&this->mutex);
    if(gst_clock_get_time(this->sysclock) - 1 * GST_SECOND
    		< this->saved_sending_rate_time){
      result = this->saved_sending_rate;
      this->SR = this->saved_sending_rate;
    }
	g_mutex_unlock(&this->mutex);
	return result;
}

guint mprtps_subflow_get_consecutive_keeps_num(MPRTPSSubflow *this)
{
    guint result;
	g_mutex_lock(&this->mutex);
	result = this->consecutive_keep_events;
	g_mutex_unlock(&this->mutex);
	return result;
}


void mprtps_subflow_set_sr_riport_time(MPRTPSSubflow* this, GstClockTime time)
{
	g_mutex_lock(&this->mutex);
	this->sr_riport_time = time;
	g_mutex_unlock(&this->mutex);
}


gboolean mprtps_subflow_is_active(MPRTPSSubflow* this)
{
	gboolean result;
	g_mutex_lock(&this->mutex);
	result = this->state != MPRTP_SENDER_SUBFLOW_STATE_PASSIVE;
	g_mutex_unlock(&this->mutex);
	return result;
}


GstPad* mprtps_subflow_get_outpad(MPRTPSSubflow* this)
{
	GstPad* result;
	g_mutex_lock(&this->mutex);
	result = this->outpad;
	g_mutex_unlock(&this->mutex);
	return result;
}


//one thing is certain:
//the state is non_congested
MPRTPSubflowEvent _get_event_for_non_congested_state(MPRTPSSubflow* this, GstClockTime now)
{
  MPRTPSubflowEvent result = MPRTP_SENDER_SUBFLOW_EVENT_KEEP;
  if(this->manual_event != MPRTP_SENDER_SUBFLOW_EVENT_KEEP){
    result = this->manual_event;
    this->manual_event = MPRTP_SENDER_SUBFLOW_EVENT_KEEP;
    this->consecutive_keep_events = 0;
  	goto _get_event_for_non_congested_state_done;
  }

  if(this->last_checked_riport_time == this->last_riport_received){
    if(this->rr_blocks_arrived &&
       this->last_riport_received <
	   now - MPRTPS_SUBFLOW_RIPORT_TIMEOUT_TO_LATE_EVENT){
      return MPRTP_SENDER_SUBFLOW_EVENT_LATE;
    }
	return result;
  }

  if(this->consecutive_lost > 0 && this->consecutive_discarded > 0){
	if(++this->consecutive_distortions > 2){
	  this->consecutive_distortions = 0;
	  result = MPRTP_SENDER_SUBFLOW_EVENT_CONGESTION;
	}else{
	  result = MPRTP_SENDER_SUBFLOW_EVENT_DISTORTION;
	}
	goto _get_event_for_non_congested_state_done;
  }
  this->consecutive_distortions = 0;

  if(this->RTT > MPRTPS_SUBFLOW_RTT_BOUNDARY_TO_LATE_EVENT){
    result = MPRTP_SENDER_SUBFLOW_EVENT_LATE;
    goto _get_event_for_non_congested_state_done;
  }

_get_event_for_non_congested_state_done:
  if(result == MPRTP_SENDER_SUBFLOW_EVENT_KEEP){
    ++this->consecutive_keep_events;
  }else{
	this->consecutive_keep_events = 0;
  }
  return result;
}

MPRTPSubflowEvent _get_event_for_congested_state(MPRTPSSubflow* this, GstClockTime now)
{
  MPRTPSubflowEvent result = MPRTP_SENDER_SUBFLOW_EVENT_KEEP;
  if(this->manual_event != MPRTP_SENDER_SUBFLOW_EVENT_KEEP){
    result = this->manual_event;
    this->manual_event = MPRTP_SENDER_SUBFLOW_EVENT_KEEP;
   	goto _get_event_for_congested_state_done;
  }

  if(this->last_checked_riport_time == this->last_riport_received){
    if(this->last_riport_received < now - MPRTPS_SUBFLOW_RIPORT_TIMEOUT_TO_LATE_EVENT){
      result = MPRTP_SENDER_SUBFLOW_EVENT_LATE;
    }
    goto _get_event_for_congested_state_done;
  }

  if(this->RTT > MPRTPS_SUBFLOW_RTT_BOUNDARY_TO_LATE_EVENT){
    result = MPRTP_SENDER_SUBFLOW_EVENT_LATE;
    goto _get_event_for_congested_state_done;
  }

  if(this->consecutive_lost > 0 && this->consecutive_discarded > 0){
	result = MPRTP_SENDER_SUBFLOW_EVENT_DISTORTION;
	this->consecutive_settlements = 0;
  	goto _get_event_for_congested_state_done;
  }

  if(++this->consecutive_settlements > 3){
	result = MPRTP_SENDER_SUBFLOW_EVENT_SETTLED;
	goto _get_event_for_congested_state_done;
  }

_get_event_for_congested_state_done:
  return result;
}

MPRTPSubflowEvent _get_event_for_passive_state(MPRTPSSubflow* this, GstClockTime now)
{
  if(this->last_riport_received < now - MPRTPS_SUBFLOW_TIME_BOUNDARY_TO_DETACHED_EVENT)
  {
	 return MPRTP_SENDER_SUBFLOW_EVENT_DETACHED;
  }

  if(this->last_checked_riport_time == this->last_riport_received){
	if(this->rr_blocks_arrived &&
	   this->last_riport_received <
	   now - MPRTPS_SUBFLOW_RIPORT_TIMEOUT_TO_LATE_EVENT){
	  return MPRTP_SENDER_SUBFLOW_EVENT_LATE;
	}
	return MPRTP_SENDER_SUBFLOW_EVENT_KEEP;
  }

  if(this->RTT < MPRTPS_SUBFLOW_RTT_BOUNDARY_FROM_PASSIVE_STATE){
	return MPRTP_SENDER_SUBFLOW_EVENT_SETTLED;
  }

  return MPRTP_SENDER_SUBFLOW_EVENT_KEEP;
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
