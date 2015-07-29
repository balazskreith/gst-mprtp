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
static void mprtps_subflow_set_alpha_value(MPRTPSSubflow* this, gfloat value);
static void mprtps_subflow_set_beta_value(MPRTPSSubflow* this, gfloat value);
static void mprtps_subflow_set_gamma_value(MPRTPSSubflow* this, gfloat value);
static void mprtps_subflow_set_charge_value(MPRTPSSubflow* this, gfloat value);
static guint32 mprtps_subflow_get_sending_rate(MPRTPSSubflow* this);
static void _fire(MPRTPSSubflow *this, MPRTPSubflowEvent event, void *data);

static void
mprtps_subflow_setup_sr_riport(MPRTPSSubflow *this, GstMPRTCPSubflowRiport *header);

static void _proc_rtcprr(MPRTPSSubflow* this, GstRTCPRR* rr);

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
  subflow->active = FALSE;
  subflow->early_discarded_bytes = 0;
  subflow->late_discarded_bytes = 0;
  subflow->state = MPRTP_SENDER_SUBFLOW_STATE_NON_CONGESTED;
  subflow->sent_packet_num = 0;
  subflow->ssrc = g_random_int ();
  subflow->HSN_r = 0;
}


void
mprtps_subflow_init (MPRTPSSubflow * subflow)
{
  subflow->fire = mprtps_subflow_FSM_fire;
  subflow->process_rtpbuf_out = mprtps_subflow_process_rtpbuffer_out;
  subflow->setup_sr_riport = mprtps_subflow_setup_sr_riport;
  subflow->get_id = mprtps_subflow_get_id;
  subflow->get_sr_riport_time = mprtps_subflow_get_sr_riport_time;
  subflow->set_sr_riport_time = mprtps_subflow_set_sr_riport_time;
  subflow->get_sent_packet_num = mprtps_subflow_get_sent_packet_num;
  subflow->is_active = mprtps_subflow_is_active;
  subflow->get_outpad = mprtps_subflow_get_outpad;
  subflow->process_mprtcp_block = mprtps_subflow_process_mprtcp_block;
  subflow->set_alpha_value = mprtps_subflow_set_alpha_value;
  subflow->set_beta_value = mprtps_subflow_set_beta_value;
  subflow->set_gamma_value = mprtps_subflow_set_gamma_value;
  subflow->set_charge_value = mprtps_subflow_set_charge_value;
  subflow->get_sending_rate = mprtps_subflow_get_sending_rate;

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

	subflow->octet_count += gst_rtp_buffer_get_payload_len(rtp);
	gst_rtp_buffer_add_extension_onebyte_header(rtp, ext_header_id, (gpointer) &data, sizeof(data));

	g_mutex_unlock(&subflow->mutex);
	//_print_rtp_packet_info(rtp);
}

void mprtps_subflow_process_mprtcp_block(MPRTPSSubflow* this,
	GstMPRTCPSubflowBlock *block)
{
  guint8 type;
  g_mutex_lock(&this->mutex);

  gst_rtcp_header_getdown(&block->block_header, NULL,
    NULL, NULL, &type, NULL, NULL);

  if(type == GST_RTCP_TYPE_SR){

  }else if(type == GST_RTCP_TYPE_RR){
	  _proc_rtcprr(this, &block->receiver_riport);
  }else if(type == GST_RTCP_XR_RFC2743_BLOCK_TYPE_IDENTIFIER){

  }

  g_mutex_unlock(&this->mutex);
}

guint32 mprtps_subflow_get_sending_rate(MPRTPSSubflow* this)
{
  guint32 result;
  g_mutex_lock(&this->mutex);
  result = (guint32) this->SR;
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
  guint32 ext_hsn;
  guint32 cum_packet_lost,LSR,DLSR;
  GstClockTime now;
  guint16 last_HSN_r = this->HSN_r;
  guint16 last_cycle_num_r = this->cycle_num_r;
  guint16 last_inter_packet_losts = this->inter_packet_losts;
  gfloat sending_rate = this->SR;
  guint16 sent_packet_num;

  now = gst_clock_get_time (this->sysclock);

  gst_rtcp_rrb_getdown(&rr->blocks, NULL,
      &this->fraction_lost, &cum_packet_lost, &ext_hsn, &this->jitter,
	  &LSR, &DLSR);

  this->inter_packet_losts = cum_packet_lost - this->cum_packet_losts;
  this->cum_packet_losts = cum_packet_lost;
  this->cycle_num_r = ext_hsn>>16;
  this->HSN_r = ext_hsn & 0xFFFF;
  this->LSR =  (now & 0xFFFF000000000000ULL) | ((guint64)LSR)<<16;
  this->DLSR = (guint64)DLSR;
  this->RTT = now - this->LSR - this->DLSR;
  this->last_riport_received = now;

  /*
   g_print("inter packet_lost: %d, cum_packet_lost %d, fraction lost: %d, "
		  "jitter: %d, HSN_r: %d, LSR: %d, "
		  "DLSR: %d, RTT: %d\n",
		  this->inter_packet_losts, this->cum_packet_losts, this->fraction_lost,
		  this->jitter, this->HSN_r, this->LSR, this->DLSR, this->RTT);
  */

  //calculate sending rate;
  sent_packet_num = uint16_diff(last_HSN_r, this->HSN_r);
  if(this->inter_packet_losts == 0 && sent_packet_num == 0){
    return;
  }
  MPRTPSubflowEvent event = MPRTP_SENDER_SUBFLOW_EVENT_KEEP;
  sending_rate = sent_packet_num - this->inter_packet_losts;

  this->SR = (this->SR + 15.0 * sending_rate) / 16.0;
  if(last_inter_packet_losts > 0 && this->late_discarded_bytes > 0){
    _fire(this, MPRTP_SENDER_SUBFLOW_EVENT_DISTORTION, NULL);
  }

  g_print("subflow %d SR: %f\n", this->id, this->SR);
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
	  this->early_discarded_bytes = discarded_bytes;
    }else{
	  this->late_discarded_bytes = discarded_bytes;
    }
  }else if(interval_metric == RTCP_XR_RFC7243_I_FLAG_INTERVAL_DURATION){
    if(early_bit){
      this->early_discarded_bytes += discarded_bytes;
    }else{
      this->late_discarded_bytes += discarded_bytes;
    }
  }else if(interval_metric == RTCP_XR_RFC7243_I_FLAG_SAMPLED_METRIC){

  }
  this->last_riport_received = now;
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


void mprtps_subflow_set_sr_riport_time(MPRTPSSubflow* this, GstClockTime time)
{
	g_mutex_lock(&this->mutex);
	this->sr_riport_time = time;
	g_mutex_unlock(&this->mutex);
}

void mprtps_subflow_set_alpha_value(MPRTPSSubflow* this, gfloat value)
{
	g_mutex_lock(&this->mutex);
	this->alpha_value = value;
	g_mutex_unlock(&this->mutex);
}

void mprtps_subflow_set_beta_value(MPRTPSSubflow* this, gfloat value)
{
	g_mutex_lock(&this->mutex);
	this->beta_value = value;
	g_mutex_unlock(&this->mutex);
}

void mprtps_subflow_set_gamma_value(MPRTPSSubflow* this, gfloat value)
{
	g_mutex_lock(&this->mutex);
	this->gamma_value = value;
	g_mutex_unlock(&this->mutex);
}

void mprtps_subflow_set_charge_value(MPRTPSSubflow* this, gfloat value)
{
	g_mutex_lock(&this->mutex);
	this->charge_value = value;
	g_mutex_unlock(&this->mutex);
}

gboolean mprtps_subflow_is_active(MPRTPSSubflow* this)
{
	gboolean result;
	g_mutex_lock(&this->mutex);
	result = this->active;
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

void _fire(MPRTPSSubflow *this, MPRTPSubflowEvent event, void *data)
{
  switch(this->state){
    case MPRTP_SENDER_SUBFLOW_STATE_NON_CONGESTED:
	  switch(event){
	    case MPRTP_SENDER_SUBFLOW_EVENT_JOINED:
		  this->active = TRUE;
		  this->SR = (gfloat)this->charge_value;
	    break;
	    case MPRTP_SENDER_SUBFLOW_EVENT_DISTORTION:
	      this->SR *= (1.0 - this->alpha_value);
	    break;
	    case MPRTP_SENDER_SUBFLOW_EVENT_DETACHED:
	    case MPRTP_SENDER_SUBFLOW_EVENT_LATE:
	      this->SR = 0.0;
	      this->active = FALSE;
	      this->state = MPRTP_SENDER_SUBFLOW_STATE_PASSIVE;
	    break;
	    case MPRTP_SENDER_SUBFLOW_EVENT_BID:
	      this->SR *= (1.0 + this->gamma_value);
	    break;
	    case MPRTP_SENDER_SUBFLOW_EVENT_CONGESTION:
          this->SR *= (1.0 - this->beta_value);
	      this->state = MPRTP_SENDER_SUBFLOW_STATE_CONGESTED;
	    break;
	    case MPRTP_SENDER_SUBFLOW_EVENT_KEEP:
	    default:
	    break;
	  }
  	  break;
    case MPRTP_SENDER_SUBFLOW_STATE_CONGESTED:
	  switch(event){
		break;
		case MPRTP_SENDER_SUBFLOW_EVENT_DETACHED:
		case MPRTP_SENDER_SUBFLOW_EVENT_LATE:
		  this->active = FALSE;
		  this->SR = 0.0;
		  this->state = MPRTP_SENDER_SUBFLOW_STATE_PASSIVE;
		break;
		case MPRTP_SENDER_SUBFLOW_EVENT_SETTLED:
		  this->state = MPRTP_SENDER_SUBFLOW_STATE_NON_CONGESTED;
		break;
		case MPRTP_SENDER_SUBFLOW_EVENT_KEEP:
		default:
	    break;
	  }
  	  break;
    case MPRTP_SENDER_SUBFLOW_STATE_PASSIVE:
	  switch(event){
	    case MPRTP_SENDER_SUBFLOW_EVENT_DETACHED:
	    case MPRTP_SENDER_SUBFLOW_EVENT_DEAD:
	    break;
	    case MPRTP_SENDER_SUBFLOW_EVENT_SETTLED:
	      this->SR = this->charge_value;
	      this->state = MPRTP_SENDER_SUBFLOW_STATE_NON_CONGESTED;
	    break;
	    default:
	    break;
	  }
  	  break;
	default:
	  break;
  }
}

/*
void _mprtps_subflow_checker_func(void* data)
{
  MPRTPSSubflow *this = data;
  GstClockTime next_time, now;
  GstClockID clock_id;
  GstPad *outpad;
  GstRTCPHeader *header;
  GstMPRTCPSubflowRiport *riport;
  GstRTCPBuffer rtcp = {NULL, };
  GstBuffer *outbuf;
  GstMPRTCPSubflowBlock *block;
  GstRTCPSR *sr;
g_print("subflow checker\n");
  g_mutex_lock(&this->mutex);
  outpad = this->rtcp_outpad != NULL ? this->rtcp_outpad : this->outpad;
  if(!gst_pad_is_linked(outpad)){
	GST_WARNING_OBJECT(this, "The outpad is not linked");
    goto mprtps_subflow_checker_func_end;
  }
  if(!this->active){
	GST_LOG_OBJECT(this, "The subflow (%d) is not active", this->id);
	goto mprtps_subflow_checker_func_end;
  }
  now = gst_clock_get_time(this->sysclock);
  outbuf = gst_rtcp_buffer_new(1400);
  gst_rtcp_buffer_map (outbuf, GST_MAP_READWRITE, &rtcp);

  header = gst_rtcp_add_begin(&rtcp);
  gst_rtcp_header_change(header, NULL, NULL, NULL, NULL, NULL, &this->ssrc);
  riport = gst_mprtcp_add_riport(header);

  block = gst_mprtcp_riport_add_block_begin(riport, this->id);
  sr = gst_mprtcp_riport_block_add_sr(block);
  gst_rtcp_header_change(&sr->header, NULL, NULL, NULL, NULL, NULL, &this->ssrc);

  gst_rtcp_srb_setup(&sr->sender_block,
		    now, //ntptime
		    (guint32)(gst_rtcp_ntp_to_unix (now)>>32), //rtptime
            this->packet_count, //packet count
			this->octet_count //octet count
			);

  gst_mprtcp_riport_add_block_end(riport, block);

  gst_rtcp_add_end(&rtcp, header);
  gst_print_rtcp_buffer(&rtcp);
  gst_rtcp_buffer_unmap (&rtcp);
  //goto mprtps_subflow_checker_func_end;
  if(gst_pad_push(outpad, outbuf) != GST_FLOW_OK){
	GST_WARNING_OBJECT(this, "The outpad does not work");
  }

mprtps_subflow_checker_func_end:
  g_mutex_unlock(&this->mutex);

  next_time = now + 5 * GST_SECOND;
  clock_id = gst_clock_new_single_shot_id (this->sysclock, next_time);

  if(gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED){
    GST_WARNING_OBJECT(this, "The scheduler clock wait is interrupted");
  }
  gst_clock_id_unref (clock_id);
}

*/


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
