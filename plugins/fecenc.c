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
#include "fecenc.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>
#include "mprtpspath.h"

#define _now(this) gst_clock_get_time (this->sysclock)

//#define THIS_READLOCK(this)
//#define THIS_READUNLOCK(this)
//#define THIS_WRITELOCK(this)
//#define THIS_WRITEUNLOCK(this)
//#define _cmp_uint16(x,y) ((x==y)?0:((gint16) (x - y)) < 0 ? -1 : 1)


GST_DEBUG_CATEGORY_STATIC (fecencoder_debug_category);
#define GST_CAT_DEFAULT fecencoder_debug_category

G_DEFINE_TYPE (FECEncoder, fecencoder, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------
typedef struct _BitString{
  guint8    bytes[GST_RTPFEC_PARITY_BYTES_MAX_LENGTH];
  gint16    length;
  guint16   seq_num;
  guint32   ssrc;
}BitString;

typedef enum{
  FECENCODER_REQUEST_TYPE_RTP_BUFFER             = 1,
  FECENCODER_REQUEST_TYPE_FEC_REQUEST            = 2,
  FECENCODER_REQUEST_TYPE_PAYLOAD_CHANGE         = 3,
}RequestTypes;

typedef struct{
  RequestTypes type;
}Request;

typedef struct{
  Request    base;
  guint8     payload_type;
}PayloadChangeRequest;

typedef struct{
  Request    base;
  GstBuffer* buffer;
}RTPBufferRequest;

typedef struct{
  Request            base;
  SndSubflow*        subflow;
}FECRequest;


static void fecencoder_finalize (GObject * object);
static BitString* _make_bitstring(GstBuffer* buf);
static void _fecenc_process(gpointer udata);
static void _fecencoder_add_rtpbuffer(FECEncoder *this, GstBuffer *buf);
static GstBuffer* _fecencoder_get_fec_packet(FECEncoder *this, SndSubflow *subflow, gint32* packet_length);
//------------------------- Utility functions --------------------------------

void
fecencoder_class_init (FECEncoderClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fecencoder_finalize;

  GST_DEBUG_CATEGORY_INIT (fecencoder_debug_category, "fecencoder", 0,
      "FEC Encoder component");

}

void
fecencoder_finalize (GObject * object)
{
  FECEncoder *this;
  this = FECENCODER(object);
  gst_task_stop(this->thread);
  gst_task_join (this->thread);

  g_async_queue_unref(this->messages_out);

  g_free(this->seqtracks);
  g_queue_clear(this->bitstrings);
  g_object_unref(this->bitstrings);
  g_object_unref(this->sysclock);

  gst_object_unref (this->thread);
}

FECEncoder *make_fecencoder(GAsyncQueue *responses)
{
  FECEncoder *this;
  this = g_object_new (FECENCODER_TYPE, NULL);
  this->made = _now(this);
  this->messages_out = g_async_queue_ref(responses);

  gst_task_set_lock (this->thread, &this->thread_mutex);
  gst_task_start (this->thread);
  return this;
}

void
fecencoder_init (FECEncoder * this)
{
  this->thread   = gst_task_new (_fecenc_process, this, NULL);
  this->sysclock = gst_system_clock_obtain();
  this->max_protection_num = GST_RTPFEC_MAX_PROTECTION_NUM;
  this->bitstrings = g_queue_new();
  this->seqtracks  = g_malloc0(sizeof(SubflowSeqTrack) * 256);
}


void fecencoder_reset(FECEncoder *this)
{

}


void fecencoder_add_rtpbuffer(FECEncoder *this, GstBuffer* buffer)
{
  RTPBufferRequest* request = g_slice_new0(RTPBufferRequest);
  request->base.type = FECENCODER_REQUEST_TYPE_RTP_BUFFER;
  request->buffer = gst_buffer_ref(buffer);
  g_async_queue_push(this->requests, request);
}

void fecencoder_request_fec(FECEncoder *this, SndSubflow* subflow)
{
  FECRequest* request = g_slice_new0(FECRequest);
  request->base.type = FECENCODER_REQUEST_TYPE_FEC_REQUEST;
  request->subflow = subflow;
  g_async_queue_push(this->requests, request);
}


void fecencoder_set_payload_type(FECEncoder* this, guint8 fec_payload_type)
{
  PayloadChangeRequest* request = g_slice_new0(PayloadChangeRequest);
  request->base.type = FECENCODER_REQUEST_TYPE_PAYLOAD_CHANGE;
  request->payload_type = fec_payload_type;
  g_async_queue_push(this->requests, request);
}

void fecencoder_ref_response(FECEncoderResponse* response)
{
  ++response->ref;
}

void fecencoder_unref_response(FECEncoderResponse* response)
{
  if(0 < response->ref){
    return;
  }
  g_slice_free(FECEncoderResponse, response);
}


void _fecencoder_add_rtpbuffer(FECEncoder *this, GstBuffer *buf)
{
  g_queue_push_tail(this->bitstrings, _make_bitstring(buf));
  while(this->max_protection_num <= g_queue_get_length(this->bitstrings)){
    mprtp_free(g_queue_pop_head(this->bitstrings));
  }
}



GstBuffer*
_fecencoder_get_fec_packet(FECEncoder *this, SndSubflow *subflow, gint32* packet_length)
{
  GstBuffer* result = NULL;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint8* payload;
  gint i;
  BitString *actual = NULL;
  BitString *fecbitstring = NULL;
  GstRTPFECHeader *fecheader;
  gboolean init = FALSE;
  gint n_mask = 0;
//  fecbitstring = mprtp_malloc(sizeof(BitString));
  fecbitstring = g_slice_new0(BitString);
again:
  if(g_queue_get_length(this->bitstrings) < 1){
    goto create;
  }
  ++n_mask;
  actual = g_queue_pop_head(this->bitstrings);
  fecbitstring->length = MAX(fecbitstring->length, actual->length);
  for(i=0; i < fecbitstring->length; ++i){
    fecbitstring->bytes[i] ^= actual->bytes[i];
  }
  if(!init){
    init = TRUE;
    fecbitstring->seq_num = actual->seq_num;
    fecbitstring->ssrc    = actual->ssrc;
  }
  g_slice_free(BitString, actual);
  goto again;
create:
  result = gst_rtp_buffer_new_allocate (
      fecbitstring->length + 10, /*fecheader is 20 byte, we use 10 byte from febitstring for creating its header */
      0,
      0
      );

  gst_rtp_buffer_map(result, GST_MAP_READWRITE, &rtp);
  gst_rtp_buffer_set_payload_type(&rtp, this->payload_type);
  gst_rtp_buffer_set_seq(&rtp, ++this->seq_num);
  gst_rtp_buffer_set_ssrc(&rtp, fecbitstring->ssrc);

  //MPRTP setup begin
  if(subflow)
  {
    guint8  mprtp_ext_header_id = sndsubflow_get_mprtp_ext_header_id(subflow);
    guint8  subflow_id  = subflow->id;
    guint16 subflow_seq = subflowseqtracker_increase(this->seqtracks + subflow_id);
    gst_rtp_buffer_set_mprtp_extension(&rtp, mprtp_ext_header_id, subflow_id, subflow_seq);
  }
  //mprtp setup end;

  payload = gst_rtp_buffer_get_payload(&rtp);
  fecheader = (GstRTPFECHeader*) payload;
  memcpy(fecheader, fecbitstring->bytes, 8);
  memcpy(&fecheader->length_recovery, fecbitstring->bytes + 8, 2);
  fecheader->F          = 1;
  fecheader->R          = 0;
  fecheader->sn_base    = g_htons(fecbitstring->seq_num);
  fecheader->ssrc       = g_htonl(fecbitstring->ssrc);
  fecheader->SSRC_Count = 1;
  fecheader->N_MASK     = n_mask;
  fecheader->M_MASK     = 0;
  fecheader->reserved   = 0;
  memcpy(payload + sizeof(GstRTPFECHeader), fecbitstring->bytes + 10, fecbitstring->length - 10);
  gst_rtp_buffer_unmap(&rtp);
//  mprtp_free(fecbitstring);
  if(packet_length){
    *packet_length = fecbitstring->length + 10;
  }
  g_slice_free(BitString, fecbitstring);

  return result;
}


BitString* _make_bitstring(GstBuffer* buf)
{
  BitString *result;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
//  result = mprtp_malloc(sizeof(BitString));
  result = g_slice_new0(BitString);
  rtpfecbuffer_setup_bitstring(buf, result->bytes, &result->length);
  gst_rtp_buffer_map(buf, GST_MAP_READ, &rtp);
  result->seq_num = gst_rtp_buffer_get_seq(&rtp);
  result->ssrc    = gst_rtp_buffer_get_ssrc(&rtp);
  gst_rtp_buffer_unmap(&rtp);
  return result;
}


static void _fecenc_process(gpointer udata)
{
  FECEncoder* this = udata;
  Request* request;
again:
  request = (Request*) g_async_queue_timeout_pop(this->requests, 1000);
  if(!request){
    goto done;
  }
  switch(request->type){
    case FECENCODER_REQUEST_TYPE_PAYLOAD_CHANGE:
    {
      PayloadChangeRequest* fec_payload_request = (PayloadChangeRequest*)request;
      this->payload_type = fec_payload_request->payload_type;
      g_slice_free(PayloadChangeRequest, fec_payload_request);
    }
    break;
    case FECENCODER_REQUEST_TYPE_FEC_REQUEST:
    {
      FECRequest* fec_request = (FECRequest*)request;
      FECEncoderResponse* fec_response = _fec_response_ctor();
      fec_response->fecbuffer = _fecencoder_get_fec_packet(this, fec_request->subflow, &fec_response->payload_size);
      g_slice_free(FECRequest, fec_request);
      g_async_queue_push(this->messages_out, fec_response);
    }
    break;
    case FECENCODER_REQUEST_TYPE_RTP_BUFFER:
    {
      RTPBufferRequest* rtp_buffer_request = (RTPBufferRequest*)request;
      _fecencoder_add_rtpbuffer(this, rtp_buffer_request->buffer);
      gst_buffer_unref(rtp_buffer_request->buffer);
      g_slice_free(RTPBufferRequest, rtp_buffer_request);
    }
    break;
    default:
      g_warning("Unhandled message with type %d", request->type);
    break;
  }
  goto again;
done:
  return;
}

FECEncoderResponse* _fec_response_ctor(void)
{
  FECEncoderResponse* result;
  result = g_slice_new0(FECEncoderResponse);
  result->ref = 1;
  return result;
}

