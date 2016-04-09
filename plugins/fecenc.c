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
#include "bintree.h"
#include "mprtpspath.h"

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

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
  gint16   length;
  guint16   seq_num;
  guint32   ssrc;
}BitString;

static void fecencoder_finalize (GObject * object);
static BitString* _make_bitstring(GstBuffer* buf);


//------------------------- Utility functions --------------------------------
typedef struct _Subflow{
  guint8                     id;
  MPRTPSPath*                path;
  guint32                    total_payload_len;
  guint32                    total_packets_sent;
  guint16                    sequence_num;
  guint16                    cycle_num;
}Subflow;

static Subflow *_subflow_ctor (void);
static void _subflow_dtor (Subflow * this);
static void _ruin_subflow (gpointer subflow);
static Subflow *_make_subflow (MPRTPSPath * path);
static void _reset_subflow (Subflow * this);
static Subflow *_get_subflow(FECEncoder * this, guint8 subflow_id);

void
fecencoder_class_init (FECEncoderClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = fecencoder_finalize;

  GST_DEBUG_CATEGORY_INIT (fecencoder_debug_category, "fecencoder", 0,
      "AAAAAAAAA");

}

void
fecencoder_finalize (GObject * object)
{
  FECEncoder *this;
  this = FECENCODER(object);
  g_object_unref(this->sysclock);
}

void
fecencoder_init (FECEncoder * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->subflows = g_hash_table_new_full (NULL, NULL,
      NULL, (GDestroyNotify) _ruin_subflow);

  this->sysclock = gst_system_clock_obtain();
  this->max_protection_num = GST_RTPFEC_MAX_PROTECTION_NUM;
  this->bitstrings = g_queue_new();

}


void fecencoder_reset(FECEncoder *this)
{
  THIS_WRITELOCK(this);

  THIS_WRITEUNLOCK(this);
}

FECEncoder *make_fecencoder(void)
{
  FECEncoder *this;
  this = g_object_new (FECENCODER_TYPE, NULL);
  this->made = _now(this);
  return this;
}


void fecencoder_set_payload_type(FECEncoder *this, guint8 payload_type)
{
  THIS_WRITELOCK(this);
  this->payload_type = payload_type;
  THIS_WRITEUNLOCK(this);
}

void fecencoder_get_stats(FECEncoder *this, guint8 subflow_id, guint32 *packets, guint32 *payloads)
{
  Subflow *subflow;
  THIS_READLOCK(this);
  subflow = _get_subflow(this, subflow_id);
  if(!subflow){
    goto done;
  }
  if(packets){
      *packets = subflow->total_packets_sent;
    }
  if(payloads){
      *payloads = subflow->total_payload_len;
    }
done:
  THIS_READUNLOCK(this);
}

void fecencoder_add_rtpbuffer(FECEncoder *this, GstBuffer *buf)
{
  THIS_WRITELOCK(this);
  g_queue_push_tail(this->bitstrings, _make_bitstring(buf));
  while(this->max_protection_num <= g_queue_get_length(this->bitstrings)){
    mprtp_free(g_queue_pop_head(this->bitstrings));
  }
  THIS_WRITEUNLOCK(this);
}



GstBuffer*
fecencoder_get_fec_packet(FECEncoder *this)
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
  THIS_WRITELOCK(this);
  fecbitstring = mprtp_malloc(sizeof(BitString));
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
  mprtp_free(actual);
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
  mprtp_free(fecbitstring);

  THIS_WRITEUNLOCK(this);
  return result;
}


void
fecencoder_add_path (FECEncoder * this, MPRTPSPath *path)
{
  Subflow *subflow;
  subflow = _make_subflow(path);
  g_hash_table_insert (this->subflows, GINT_TO_POINTER (subflow->id), subflow);
}

void
fecencoder_rem_path (FECEncoder * this, guint8 subflow_id)
{
  g_hash_table_remove (this->subflows, GINT_TO_POINTER (subflow_id));
}

void
fecencoder_assign_to_subflow (FECEncoder * this,
                  GstBuffer *buf,
                  guint8 mprtp_ext_header_id,
                  guint8 subflow_id)
{
  MPRTPSubflowHeaderExtension data;
  GstRTPBuffer                rtp = GST_RTP_BUFFER_INIT;
  Subflow *subflow;
  THIS_WRITELOCK(this);
  subflow = _get_subflow(this, subflow_id);
  gst_rtp_buffer_map(buf, GST_MAP_READWRITE, &rtp);
  data.id = subflow_id;
  data.seq = ++subflow->sequence_num;
  gst_rtp_buffer_add_extension_onebyte_header (&rtp, mprtp_ext_header_id, (gpointer) &data, sizeof (data));
  subflow->total_payload_len += gst_rtp_buffer_get_payload_len(&rtp);
  ++subflow->total_packets_sent;
  gst_rtp_buffer_unmap(&rtp);
  THIS_WRITEUNLOCK(this);
}

BitString* _make_bitstring(GstBuffer* buf)
{
  BitString *result;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  result = mprtp_malloc(sizeof(BitString));
  rtpfecbuffer_setup_bitstring(buf, result->bytes, &result->length);
  gst_rtp_buffer_map(buf, GST_MAP_READ, &rtp);
  result->seq_num = gst_rtp_buffer_get_seq(&rtp);
  result->ssrc    = gst_rtp_buffer_get_ssrc(&rtp);
  gst_rtp_buffer_unmap(&rtp);
  return result;
}



//---------------------- Utility functions ----------------------------------
Subflow *
_subflow_ctor (void)
{
  Subflow *result;
  result = mprtp_malloc (sizeof (Subflow));
  return result;
}

void
_subflow_dtor (Subflow * this)
{
  g_return_if_fail (this);
  mprtp_free (this);
}

void
_ruin_subflow (gpointer subflow)
{
  Subflow *this;
  g_return_if_fail (subflow);
  this = (Subflow *) subflow;
  g_object_unref (this->path);
  _subflow_dtor (this);
}

Subflow *
_make_subflow (MPRTPSPath * path)
{
  Subflow *result;

  result                  = _subflow_ctor ();
  result->path            = g_object_ref (path);
  result->id              = mprtps_path_get_id(path);

  _reset_subflow (result);
  return result;
}

void
_reset_subflow (Subflow * this)
{

}

//static void _subflow_iterator(
//    FECEncoder * this,
//    void(*process)(Subflow*,gpointer),
//    gpointer data)
//{
//  GHashTableIter iter;
//  gpointer       key, val;
//  Subflow*       subflow;
//
//  g_hash_table_iter_init (&iter, this->subflows);
//  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val))
//  {
//    subflow = (Subflow *) val;
//    process(subflow, data);
//  }
//}

Subflow *_get_subflow(FECEncoder * this, guint8 subflow_id)
{
  return g_hash_table_lookup (this->subflows, GINT_TO_POINTER (subflow_id));
}

#undef DEBUG_PRINT_TOOLS
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
#undef THIS_READLOCK
#undef THIS_READUNLOCK
