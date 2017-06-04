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
#include "fecdec.h"


#define _now(this) gst_clock_get_time (this->sysclock)

typedef struct {
  guint16 base_sn;
  guint16 protected_num;
  GstBuffer* buffer;
}FECPacket;

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


GST_DEBUG_CATEGORY_STATIC (fecdecoder_debug_category);
#define GST_CAT_DEFAULT fecdecoder_debug_category

G_DEFINE_TYPE (FECDecoder, fecdecoder, G_TYPE_OBJECT);

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void fecdecoder_finalize (GObject * object);
static void _obsolete_packets(FECDecoder *this);
static gint _cmp_rcvpacket_abs_seq_custom(RcvPacket *a, RcvPacket* b, gpointer udata);
static gint _cmp_fec_packet_by_missing_seq(FECPacket *fec_packet, guint16* missing_seq);
static gint _cmp_rcv_packet_by_missing_seq(RcvPacket *rcv_packet, FECPacket* fec_packet);
static GstBuffer* _get_repaired_rtpbuffer(FECDecoder *this, GList* rcv_packets_list, FECPacket* fec_packet, guint16 missing_seq);

void
fecdecoder_class_init (FECDecoderClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fecdecoder_finalize;

  GST_DEBUG_CATEGORY_INIT (fecdecoder_debug_category, "fecdecoder", 0,
      "FEC Decoder Component");

}

void
fecdecoder_finalize (GObject * object)
{
  FECDecoder *this;
  this = FECDECODER(object);

  g_object_unref(this->sysclock);

  g_queue_free(this->rcv_packets);
  g_queue_free(this->fec_packets);
  g_queue_free(this->fec_recycle);
}

void
fecdecoder_init (FECDecoder * this)
{
  this->sysclock    = gst_system_clock_obtain();

  this->rcv_packets = g_queue_new();
  this->fec_packets = g_queue_new();
  this->fec_recycle = g_queue_new();
  g_mutex_init(&this->mutex);
}


void fecdecoder_reset(FECDecoder *this)
{

}

FECDecoder *make_fecdecoder(void)
{
  FECDecoder *this;
  this = g_object_new (FECDECODER_TYPE, NULL);
  this->made = _now(this);
  return this;
}

gint _cmp_fec_packet_by_missing_seq(FECPacket *fec_packet, guint16* missing_seq)
{
  if (_cmp_uint16(*missing_seq, fec_packet->base_sn) < 0) {
    return -1;
  }
  if (_cmp_uint16(fec_packet->base_sn + fec_packet->protected_num - 1, *missing_seq) < 0) {
    return -1;
  }
  return 0;
}


gint _cmp_rcv_packet_by_missing_seq(RcvPacket *rcv_packet, FECPacket* fec_packet)
{
  if (_cmp_uint16(rcv_packet->abs_seq, fec_packet->base_sn) < 0) {
    return -1;
  }
  if (_cmp_uint16(fec_packet->base_sn + fec_packet->protected_num - 1, rcv_packet->abs_seq) < 0) {
    return 1;
  }
  return 0;
}

static gboolean _is_repair_possible(GList *rcv_packets_list, FECPacket* fec_packet) {
  if(!rcv_packets_list) {
    return FALSE;
  }
  // TODO: if we start using parity format the possibilty of the repair must be changed
  return g_list_length(rcv_packets_list) == fec_packet->protected_num - 1;
}

GstBuffer* fecdecoder_pop_rtp_packet(FECDecoder *this, guint16 seq_num)
{
  GstBuffer* result = NULL;
  GList* fec_packets_list;
  GList* rcv_packets_list = NULL, *it;
  FECPacket* fec_packet;
  g_mutex_lock (&this->mutex);
  fec_packets_list = g_queue_find_custom(this->fec_packets, &seq_num, (GCompareFunc)_cmp_fec_packet_by_missing_seq);
  if(!fec_packets_list) {
    goto done;
  }
  fec_packet = fec_packets_list->data;
  for (it = this->rcv_packets->head; it; it = it->next) {
    if (_cmp_rcv_packet_by_missing_seq(it->data, fec_packet) != 0) {
      continue;
    }
    rcv_packets_list = g_list_prepend(rcv_packets_list, it->data);
  }
  if(!_is_repair_possible(rcv_packets_list, fec_packet)) {
    goto done;
  }
  result = _get_repaired_rtpbuffer(this, rcv_packets_list, fec_packet, seq_num);
done:
  g_mutex_unlock (&this->mutex);
  return result;
}

void fecdecoder_push_rcv_packet(FECDecoder *this, RcvPacket *packet)
{
  g_mutex_lock (&this->mutex);
  g_queue_insert_sorted(this->rcv_packets, packet, (GCompareDataFunc) _cmp_rcvpacket_abs_seq_custom, NULL);
  _obsolete_packets(this);
  g_mutex_unlock (&this->mutex);
}

void fecdecoder_push_fec_buffer(FECDecoder *this, GstBuffer *buffer)
{
  FECPacket* packet;
  g_mutex_lock (&this->mutex);
  packet = g_queue_is_empty(this->fec_recycle) ? g_slice_new0(FECPacket) : g_queue_pop_head(this->fec_recycle);
  packet->buffer = buffer;
  {
    GstRTPFECHeader* fec_header;
    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
    gst_rtp_buffer_map(packet->buffer, GST_MAP_READ, &rtp);
    fec_header = (GstRTPFECHeader*) gst_rtp_buffer_get_payload(&rtp);
    packet->base_sn = g_ntohs(fec_header->sn_base);
    packet->protected_num = fec_header->N_MASK;
    gst_rtp_buffer_unmap(&rtp);
  }
  g_queue_push_tail(this->fec_packets, packet);
  g_mutex_unlock (&this->mutex);
}

void _obsolete_packets(FECDecoder *this) {
  RcvPacket *rcv_packets_head,*rtp_tail;
  FECPacket* fec_buffer;
again:
  if(g_queue_is_empty(this->rcv_packets)) {
    return;
  }
  if(g_queue_is_empty(this->fec_packets)) {
    if(20 < g_queue_get_length(this->rcv_packets)) {
      rcvpacket_unref(g_queue_pop_head(this->rcv_packets));
    }
    return;
  }
  rcv_packets_head = g_queue_peek_head(this->rcv_packets);
  rtp_tail = g_queue_peek_tail(this->rcv_packets);
  if (rtp_tail->received - rcv_packets_head->received < 300 * GST_MSECOND) {
    return;
  }
  rcv_packets_head = g_queue_pop_head(this->rcv_packets);
  fec_buffer = g_queue_peek_head(this->fec_packets);
  if (_cmp_uint16(rcv_packets_head->abs_seq, fec_buffer->base_sn) < 0) {
    rcvpacket_unref(rcv_packets_head);
    goto again;
  }
  fec_buffer = g_queue_pop_head(this->fec_packets);
  memset(fec_buffer, 0, sizeof(FECPacket));
  g_queue_push_head(this->fec_recycle, fec_buffer);
  rcvpacket_unref(rcv_packets_head);
  goto again;
}

gint _cmp_rcvpacket_abs_seq_custom(RcvPacket *a, RcvPacket* b, gpointer udata)
{
  return _cmp_uint16(a->abs_seq, b->abs_seq);
}


GstBuffer* _get_repaired_rtpbuffer(FECDecoder *this, GList* rcv_packets_list, FECPacket* fec_packet, guint16 missing_seq) {
  GList* it;
  guint8 bitstring[1600];
  gint16 bitstring_length;
  guint8 fecbitstring[1600];
  guint16 length = 0;
  GstBasicRTPHeader* rtpheader;
  GstBuffer*         result = NULL;
  guint8* databed;
  guint32 fec_header_ssrc;

  memset(fecbitstring, 0, 1600);
  memset(bitstring, 0, 1600);

  {
    GstRTPFECHeader* fec_header;
    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
    guint16 payload_length;
    gpointer payload;
    gst_rtp_buffer_map(fec_packet->buffer, GST_MAP_READ, &rtp);
    payload = gst_rtp_buffer_get_payload(&rtp);
    fec_header = (GstRTPFECHeader*) payload;
    payload_length = gst_rtp_buffer_get_payload_len(&rtp);
    memcpy(fecbitstring, payload, 8);
    memcpy(fecbitstring + 8, &fec_header->length_recovery, 2);
    memcpy(fecbitstring + 10, (guint8*)payload + sizeof(GstRTPFECHeader), payload_length - sizeof(GstRTPFECHeader));
    fec_header_ssrc = fec_header->ssrc;
    gst_rtp_buffer_unmap(&rtp);
  }

  for(it = rcv_packets_list; it; it = it->next){
    RcvPacket *rcv_packet = it->data;
    rtpfecbuffer_setup_bitstring(rcv_packet->buffer, bitstring, &bitstring_length);
    do_bitxor(fecbitstring, bitstring, bitstring_length);
  }
  memcpy(&length, fecbitstring + 8, 2);
  length = g_ntohs(length);

  databed = mprtp_malloc(length + 12);

  memcpy(databed,     fecbitstring,     2);
  memcpy(databed + 4, fecbitstring + 4, 4);

  rtpheader = (GstBasicRTPHeader*) databed;
  rtpheader->version = 2;
  rtpheader->seq_num = g_htons(missing_seq);
  rtpheader->ssrc    = g_htonl(fec_header_ssrc);

  memcpy(databed + 12, fecbitstring + 10, length);

  result = gst_buffer_new_wrapped(databed, length + 12);
  return result;

}


