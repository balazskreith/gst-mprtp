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
#include "mprtputils.h"

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
  FECENCODER_MESSAGE_TYPE_RTP_BUFFER             = 4,
}MessageTypes;

typedef struct{
  MessageTypes type;
  gchar        content[1024];//TODO increase it if any of the message type exceeds this limit!!!!
}Message;

typedef struct{
  MessageTypes type;
  guint8        payload_type;
}PayloadChangeMessage;

typedef struct{
  MessageTypes type;
  guint8       mprtp_ext_header_id;
}MPRTPExtHeaderIDChangeMessage;

typedef struct{
  MessageTypes type;
  guint8       subflow_id;
  guint8       protected_num;
}FECRequestMessage;

typedef struct{
  MessageTypes type;
  GstBuffer*   buffer;
}RTPBufferMessage;

static void fecencoder_finalize (GObject * object);
static BitString* _make_bitstring(FECEncoder* this, GstBuffer* buf);
static void _fecenc_process(FECEncoder * this);
static FECEncoderResponse* _fec_response_ctor(void);
static void _fecencoder_add_rtpbuffer(FECEncoder *this, GstBuffer *buf);
static GstBuffer* _fecencoder_get_fec_packet(FECEncoder *this, guint8 subflow_id, guint8 protected_num, gint32* packet_length);

DEFINE_RECYCLE_TYPE(static, bitstring, BitString);

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
  g_queue_free(this->bitstrings);
  g_queue_clear(this->pending_responses);
  g_queue_free(this->pending_responses);
  g_object_unref(this->sysclock);
  g_object_unref(this->bitstring_recycle);
  g_object_unref(this->messenger);
}

static void _bitstring_shaper(BitString* result, gpointer udata){
  memset(result, 0, sizeof(BitString));
}

FECEncoder *make_fecencoder(Mediator* response_handler)
{
  FECEncoder *this;
  this = g_object_new (FECENCODER_TYPE, NULL);
  this->made = _now(this);

  this->messenger    = make_messenger(sizeof(Message));
  this->bitstring_recycle = make_recycle_bitstring(32, (RecycleItemShaper)_bitstring_shaper);

  this->response_handler = g_object_ref(response_handler);

  gst_task_set_lock (this->thread, &this->thread_mutex);
  gst_task_start (this->thread);
  return this;
}

void
fecencoder_init (FECEncoder * this)
{
  this->thread   = gst_task_new ((GstTaskFunction)_fecenc_process, this, NULL);
  this->sysclock = gst_system_clock_obtain();
  this->max_protection_num = GST_RTPFEC_MAX_PROTECTION_NUM;
  this->bitstrings = g_queue_new();
  this->pending_responses = g_queue_new();
  this->seqtracks  = g_malloc0(sizeof(SubflowSeqTrack) * 256);
  this->mprtp_ext_header_id = MPRTP_DEFAULT_EXTENSION_HEADER_ID;
}


void fecencoder_reset(FECEncoder *this)
{

}


void fecencoder_add_rtpbuffer(FECEncoder *this, GstBuffer *buffer)
{
  RTPBufferMessage* msg;

  messenger_lock(this->messenger);

  msg = messenger_retrieve_block_unlocked(this->messenger);
  msg->type = FECENCODER_MESSAGE_TYPE_RTP_BUFFER;
  msg->buffer = buffer;

  messenger_push_block_unlocked(this->messenger, msg);
  messenger_unlock(this->messenger);
}

void fecencoder_request_fec(FECEncoder *this, guint8 subflow_id, guint8 protected_num)
{
  FECRequestMessage* msg;
  messenger_lock(this->messenger);

  msg = messenger_retrieve_block_unlocked(this->messenger);
  msg->type = FECENCODER_MESSAGE_TYPE_FEC_REQUEST;
  msg->subflow_id = subflow_id;
  msg->protected_num = protected_num;
  messenger_push_block_unlocked(this->messenger, msg);
  messenger_unlock(this->messenger);
}


void fecencoder_set_payload_type(FECEncoder* this, guint8 fec_payload_type)
{
  PayloadChangeMessage* msg;
  messenger_lock(this->messenger);

  msg = messenger_retrieve_block_unlocked(this->messenger);
  msg->type = FECENCODER_MESSAGE_TYPE_PAYLOAD_CHANGE;
  msg->payload_type = fec_payload_type;
  messenger_push_block_unlocked(this->messenger, msg);
  messenger_unlock(this->messenger);
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
  g_queue_push_tail(this->bitstrings, _make_bitstring(this, buf));

  //Too many bitstring
  while(this->max_protection_num <= g_queue_get_length(this->bitstrings)){
    recycle_add(this->bitstring_recycle, g_queue_pop_head(this->bitstrings));
  }

}

static BitString* _assemble_fec_bitstring(FECEncoder *this, guint8 subflow_id, guint8 protected_num, gint* n_mask) {
  BitString* result = recycle_retrieve_and_shape(this->bitstring_recycle, NULL);
  BitString *actual = NULL;
  GList* it;
  gboolean init = FALSE;
  *n_mask = 0;
  for (it = this->bitstrings->tail; it && *n_mask < protected_num; it = it->prev) {
    ++(*n_mask);
    actual = it->data;
    result->length = MAX(result->length, actual->length);
    do_bitxor(result->bytes, actual->bytes, result->length);
    if(!init){
      init = TRUE;
      result->seq_num = actual->seq_num;
      result->ssrc    = actual->ssrc;
    }
  }
  return result;
}

GstBuffer*
_fecencoder_get_fec_packet(FECEncoder *this, guint8 subflow_id, guint8 protected_num, gint32* packet_length)
{
  GstBuffer* result = NULL;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint8* payload;
  BitString *fecbitstring = NULL;
  GstRTPFECHeader *fecheader;
  gint n_mask;
  if(g_queue_is_empty(this->bitstrings)){
    goto done;
  }

  fecbitstring = _assemble_fec_bitstring(this, subflow_id, protected_num, &n_mask);
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

//  gst_print_rtpfec_buffer(result);
//  mprtp_free(fecbitstring);
  if(packet_length){
    *packet_length = fecbitstring->length + 10;
  }
  recycle_add(this->bitstring_recycle, fecbitstring);
done:
  return result;
}


BitString* _make_bitstring(FECEncoder* this, GstBuffer* buf)
{
  BitString *result;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  result = recycle_retrieve_and_shape(this->bitstring_recycle, NULL);
  rtpfecbuffer_setup_bitstring(buf, result->bytes, &result->length);
  gst_rtp_buffer_map(buf, GST_MAP_READ, &rtp);
  result->seq_num = gst_rtp_buffer_get_seq(&rtp);
  result->ssrc    = gst_rtp_buffer_get_ssrc(&rtp);
  gst_rtp_buffer_unmap(&rtp);
  return result;
}

static void _send_fec_response(FECEncoder* this, FECEncoderResponse *response)
{
  response->fecbuffer  = _fecencoder_get_fec_packet(this, response->subflow_id, response->protected_num, &response->payload_size);
  if(!response->fecbuffer){
    g_queue_push_tail(this->pending_responses, response);
  }else{
    mediator_set_response(this->response_handler, response);
  }
}

static void _process_message(FECEncoder* this, Message* message)
{
  switch(message->type){
    case FECENCODER_MESSAGE_TYPE_RTP_BUFFER:
    {
      RTPBufferMessage *rtp_buffer_msg = (RTPBufferMessage*) message;
      _fecencoder_add_rtpbuffer(this, rtp_buffer_msg->buffer);
//      gst_buffer_unref(rtp_buffer_msg->buffer);
      if(!g_queue_is_empty(this->pending_responses)){
        _send_fec_response(this, g_queue_pop_head(this->pending_responses));
      }
    }
    break;
    case FECENCODER_MESSAGE_TYPE_PAYLOAD_CHANGE:
    {
      PayloadChangeMessage* fec_payload_message = (PayloadChangeMessage*)message;
      this->payload_type = fec_payload_message->payload_type;
    }
    break;
    case FECENCODER_REQUEST_MPRTP_EXT_HEADER_ID_CHANGE:
    {
      MPRTPExtHeaderIDChangeMessage* fec_payload_message = (MPRTPExtHeaderIDChangeMessage*)message;
      this->mprtp_ext_header_id = fec_payload_message->mprtp_ext_header_id;
    }
    break;
    case FECENCODER_MESSAGE_TYPE_FEC_REQUEST:
    {
      FECRequestMessage* fec_request = (FECRequestMessage*)message;
      FECEncoderResponse* fec_response = _fec_response_ctor();
      fec_response->subflow_id = fec_request->subflow_id;
      fec_response->protected_num = fec_request->protected_num;
      _send_fec_response(this, fec_response);
    }
    break;
    default:
      g_warning("Unhandled message at FecEncoder with type %d", message->type);
    break;
  }
}

static void _fecenc_process(FECEncoder* this)
{
  Message* message;

  message = (Message*) messenger_pop_block_with_timeout(this->messenger, 10000);

  if(!message){
    goto done;
  }

  _process_message(this, message);
  messenger_throw_block(this->messenger, message);
done:
  return;
}

//void _fecenc_process(FECEncoder* this)
//{
//
//  PROFILING(
//      "fecenc_process",
//      _fecenc_process_(this)
//  );
//}

FECEncoderResponse* _fec_response_ctor(void)
{
  FECEncoderResponse* result;
  result = g_slice_new0(FECEncoderResponse);
  result->ref = 1;
  return result;
}

