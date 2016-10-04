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
#include <math.h>
#include <string.h>
#include "fecenc.h"
#include "gstmprtcpbuffer.h"

#define _now(this) gst_clock_get_time (this->sysclock)

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
  FECENCODER_MESSAGE_TYPE_FEC_REQUEST            = 1,
  FECENCODER_MESSAGE_TYPE_PAYLOAD_CHANGE         = 2,
  FECENCODER_REQUEST_MPRTP_EXT_HEADER_ID_CHANGE  = 3,
}MessageTypes;

typedef struct{
  MessageTypes type;
}Message;

typedef struct{
  Message    base;
  guint8     payload_type;
}PayloadChangeMessage;

typedef struct{
  Message    base;
  guint8     mprtp_ext_header_id;
}MPRTPExtHeaderIDChangeMessage;

typedef struct{
  Message    base;
  guint8     subflow_id;
}FECRequestMessage;

static void fecencoder_finalize (GObject * object);
static BitString* _make_bitstring(GstBuffer* buf);
static void _fecenc_process(gpointer udata);
static FECEncoderResponse* _fec_response_ctor(void);
static void _fecencoder_add_rtpbuffer(FECEncoder *this, GstBuffer *buf);
static GstBuffer* _fecencoder_get_fec_packet(FECEncoder *this, guint8 subflow_id, gint32* packet_length);
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

  g_object_unref(this->response_handler);

  g_free(this->seqtracks);
  g_queue_clear(this->bitstrings);
  g_object_unref(this->bitstrings);
  g_object_unref(this->sysclock);

}

FECEncoder *make_fecencoder(Mediator* response_handler)
{
  FECEncoder *this;
  this = g_object_new (FECENCODER_TYPE, NULL);
  this->made = _now(this);

  this->messages     = g_async_queue_new();
  this->buffers      = g_async_queue_new();

  this->response_handler = g_object_ref(response_handler);

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
  this->mprtp_ext_header_id = MPRTP_DEFAULT_EXTENSION_HEADER_ID;
}


void fecencoder_reset(FECEncoder *this)
{

}


void fecencoder_add_rtpbuffer(FECEncoder *this, GstBuffer* buffer)
{
  g_async_queue_push(this->buffers, gst_buffer_ref(buffer));
}

void fecencoder_request_fec(FECEncoder *this, guint8 subflow_id)
{
  FECRequestMessage* message = g_slice_new0(FECRequestMessage);
  message->base.type = FECENCODER_MESSAGE_TYPE_FEC_REQUEST;
  message->subflow_id = subflow_id;
  g_async_queue_push(this->messages, message);
}


void fecencoder_set_payload_type(FECEncoder* this, guint8 fec_payload_type)
{
  PayloadChangeMessage* request = g_slice_new0(PayloadChangeMessage);
  request->base.type = FECENCODER_MESSAGE_TYPE_PAYLOAD_CHANGE;
  request->payload_type = fec_payload_type;
  g_async_queue_push(this->messages, request);
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
  gst_buffer_unref(buf);

  //Too many bitstring
  while(this->max_protection_num <= g_queue_get_length(this->bitstrings)){
    g_slice_free(BitString, (BitString*) g_queue_pop_head(this->bitstrings));
  }

}



GstBuffer*
_fecencoder_get_fec_packet(FECEncoder *this, guint8 subflow_id, gint32* packet_length)
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
  if(0 < subflow_id)
  {
    guint16 subflow_seq = subflowseqtracker_increase(this->seqtracks + subflow_id);
    gst_rtp_buffer_set_mprtp_extension(&rtp, this->mprtp_ext_header_id, subflow_id, subflow_seq);
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


static void _process_message(FECEncoder* this, Message* message)
{
  switch(message->type){
    case FECENCODER_MESSAGE_TYPE_PAYLOAD_CHANGE:
    {
      PayloadChangeMessage* fec_payload_message = (PayloadChangeMessage*)message;
      this->payload_type = fec_payload_message->payload_type;
      g_slice_free(PayloadChangeMessage, fec_payload_message);
    }
    break;
    case FECENCODER_REQUEST_MPRTP_EXT_HEADER_ID_CHANGE:
    {
      MPRTPExtHeaderIDChangeMessage* fec_payload_message = (MPRTPExtHeaderIDChangeMessage*)message;
      this->mprtp_ext_header_id = fec_payload_message->mprtp_ext_header_id;
      g_slice_free(MPRTPExtHeaderIDChangeMessage, fec_payload_message);
    }
    break;
    case FECENCODER_MESSAGE_TYPE_FEC_REQUEST:
    {
      FECRequestMessage* fec_request = (FECRequestMessage*)message;
      FECEncoderResponse* fec_response = _fec_response_ctor();
      fec_response->fecbuffer = _fecencoder_get_fec_packet(this, fec_request->subflow_id, &fec_response->payload_size);
      mediator_set_response(this->response_handler, fec_response);

      g_slice_free(FECRequestMessage, fec_request);
    }
    break;
    default:
      g_warning("Unhandled message with type %d", message->type);
    break;
  }
}

void _fecenc_process(gpointer udata)
{
  FECEncoder* this = udata;
  Message* message;
  GstBuffer* buffer;
  buffer = g_async_queue_try_pop(this->buffers);
  if(buffer){
    _fecencoder_add_rtpbuffer(this, buffer);
  }

  message = (Message*) g_async_queue_timeout_pop(this->messages, 1000);
  if(message){
    _process_message(this, message);
  }
  return;
}

FECEncoderResponse* _fec_response_ctor(void)
{
  FECEncoderResponse* result;
  result = g_slice_new0(FECEncoderResponse);
  result->ref = 1;
  return result;
}

