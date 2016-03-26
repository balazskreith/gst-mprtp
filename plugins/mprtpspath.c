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
#include <string.h>
#include "mprtpspath.h"
#include "gstmprtcpbuffer.h"


#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define _now(this) (gst_clock_get_time(this->sysclock))

GST_DEBUG_CATEGORY_STATIC (gst_mprtps_path_category);
#define GST_CAT_DEFAULT gst_mprtps_path_category

G_DEFINE_TYPE (MPRTPSPath, mprtps_path, G_TYPE_OBJECT);

static void mprtps_path_finalize (GObject * object);
static void mprtps_path_reset (MPRTPSPath * this);
static void _setup_rtp2mprtp (MPRTPSPath * this, GstRTPBuffer *rtp);
static void _refresh_stat(MPRTPSPath * this, GstRTPBuffer *rtp);
//static void _send_mprtp_packet(MPRTPSPath * this,
//                               GstBuffer *buffer);
//static GstBuffer* _create_monitor_packet(MPRTPSPath * this);

#define MIN_PACE_INTERVAL 1
#define MINIMUM_PACE_BANDWIDTH 50000

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

void
mprtps_path_class_init (MPRTPSPathClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = mprtps_path_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_mprtps_path_category, "mprtpspath", 0,
      "MPRTP Sender Path");
}

MPRTPSPath *
make_mprtps_path (guint8 id)
{
  MPRTPSPath *result;

  result = g_object_new (MPRTPS_PATH_TYPE, NULL);
  THIS_WRITELOCK (result);
  result->id = id;
  //  result->send_mprtp_func_data = func_this;
//  result->send_mprtp_packet_func = send_func;
  THIS_WRITEUNLOCK (result);
  return result;
}

/**
 * mprtps_path_reset:
 * @src: an #MPRTPSPath
 *
 * Reset the subflow of @src.
 */
void
mprtps_path_reset (MPRTPSPath * this)
{
  this->is_new = TRUE;
  this->seq = 0;
  this->cycle_num = 0;
  this->flags = MPRTPS_PATH_FLAG_ACTIVE |
      MPRTPS_PATH_FLAG_NON_CONGESTED | MPRTPS_PATH_FLAG_NON_LOSSY;

  memset(&this->packets, 0, sizeof(MPRTPSPathPackets));

  this->monitoring_interval = 0;

}


void
mprtps_path_init (MPRTPSPath * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sysclock = gst_system_clock_obtain ();
  mprtps_path_reset (this);
}


void
mprtps_path_finalize (GObject * object)
{
  MPRTPSPath *this = MPRTPS_PATH_CAST (object);
  g_object_unref (this->sysclock);
}


guint8
mprtps_path_get_id (MPRTPSPath * this)
{
  guint16 result;
  THIS_READLOCK (this);
  result = this->id;
  THIS_READUNLOCK (this);
  return result;
}

gboolean
mprtps_path_is_new (MPRTPSPath * this)
{
  gboolean result;
  THIS_READLOCK (this);
  result = this->is_new;
  THIS_READUNLOCK (this);
  return result;
}

void
mprtps_path_set_not_new (MPRTPSPath * this)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->is_new = FALSE;
  THIS_WRITEUNLOCK (this);
}

guint8
mprtps_path_get_flags (MPRTPSPath * this)
{
  guint8 result;
  THIS_READLOCK (this);
  result = this->flags;
  THIS_READUNLOCK (this);
  return result;
}


gboolean
mprtps_path_is_active (MPRTPSPath * this)
{
  gboolean result;
  THIS_READLOCK (this);
  result = (this->flags & (guint8) MPRTPS_PATH_FLAG_ACTIVE) ? TRUE : FALSE;
  THIS_READUNLOCK (this);
  return result;
}


void
mprtps_path_set_active (MPRTPSPath * this)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->flags |= (guint8) MPRTPS_PATH_FLAG_ACTIVE;
  this->sent_active = gst_clock_get_time (this->sysclock);
  this->sent_passive = 0;
  THIS_WRITEUNLOCK (this);
}


void
mprtps_path_set_passive (MPRTPSPath * this)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->flags &= (guint8) 255 ^ (guint8) MPRTPS_PATH_FLAG_ACTIVE;
  this->sent_passive = gst_clock_get_time (this->sysclock);
  this->sent_active = 0;
  THIS_WRITEUNLOCK (this);
}

guint16 mprtps_path_get_actual_seq(MPRTPSPath * this)
{
  guint16 result;
  THIS_READLOCK (this);
  result = this->seq;
  THIS_READUNLOCK (this);
  return result;
}

void mprtps_path_set_skip_duration(MPRTPSPath * this, GstClockTime duration)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->skip_until = _now(this) + duration;
  THIS_WRITEUNLOCK (this);
}


void mprtps_path_set_monitor_packet_interval(MPRTPSPath *this, guint monitoring_interval)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->monitoring_interval = monitoring_interval;
  THIS_WRITEUNLOCK (this);
}

void mprtps_path_set_mprtp_ext_header_id(MPRTPSPath *this, guint ext_header_id)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->mprtp_ext_header_id = ext_header_id;
  THIS_WRITEUNLOCK (this);
}

gboolean
mprtps_path_is_non_lossy (MPRTPSPath * this)
{
  gboolean result;
  THIS_READLOCK (this);
  result = (this->flags & (guint8) MPRTPS_PATH_FLAG_NON_LOSSY) ? TRUE : FALSE;
  THIS_READUNLOCK (this);
  return result;

}

void
mprtps_path_set_lossy (MPRTPSPath * this)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->flags &= (guint8) 255 ^ (guint8) MPRTPS_PATH_FLAG_NON_LOSSY;
  this->sent_lossy = gst_clock_get_time (this->sysclock);
  THIS_WRITEUNLOCK (this);
}



void
mprtps_path_set_non_lossy (MPRTPSPath * this)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->flags |= (guint8) MPRTPS_PATH_FLAG_NON_LOSSY;
  THIS_WRITEUNLOCK (this);
}


gboolean
mprtps_path_is_non_congested (MPRTPSPath * this)
{
  gboolean result;
  THIS_READLOCK (this);
  result =
      (this->flags & (guint8) MPRTPS_PATH_FLAG_NON_CONGESTED) ? TRUE : FALSE;
  THIS_READUNLOCK (this);
  return result;

}


void
mprtps_path_set_congested (MPRTPSPath * this)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->flags &= (guint8) 255 ^ (guint8) MPRTPS_PATH_FLAG_NON_CONGESTED;
  this->sent_congested = gst_clock_get_time (this->sysclock);
  THIS_WRITEUNLOCK (this);
}


void
mprtps_path_set_non_congested (MPRTPSPath * this)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->flags |= (guint8) MPRTPS_PATH_FLAG_NON_CONGESTED;
  this->sent_non_congested = gst_clock_get_time (this->sysclock);
  THIS_WRITEUNLOCK (this);
}


gboolean
mprtps_path_is_monitoring (MPRTPSPath * this)
{
  gboolean result;
  THIS_READLOCK (this);
  result = this->monitoring_interval > 0;
  THIS_READUNLOCK (this);
  return result;
}

gboolean mprtps_path_has_expected_lost(MPRTPSPath * this)
{
  gboolean result;
  THIS_WRITELOCK (this);
  result = this->expected_lost;
  this->expected_lost = FALSE;
  THIS_WRITEUNLOCK (this);
  return result;
}

void
mprtps_path_set_keep_alive_period(MPRTPSPath *this, GstClockTime period)
{
  THIS_WRITELOCK (this);
  this->keep_alive_period = period;
  THIS_WRITEUNLOCK (this);
}


gboolean
mprtps_path_request_keep_alive(MPRTPSPath *this)
{
  gboolean result = FALSE;
  THIS_WRITELOCK (this);
  if(!this->keep_alive_period){
    goto done;
  }
  if(this->last_sent < _now(this) - this->keep_alive_period){
    this->last_sent = _now(this);
    result = TRUE;
  }
done:
  THIS_WRITEUNLOCK (this);
  return result;
}

guint32
mprtps_path_get_total_sent_packets_num (MPRTPSPath * this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = this->packets.total_sent_packets_num;
  THIS_READUNLOCK (this);
  return result;
}




guint32
mprtps_path_get_total_sent_payload_bytes (MPRTPSPath * this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = this->packets.total_sent_payload_bytes;
  THIS_READUNLOCK (this);
  return result;
}


guint32 mprtps_path_get_sent_bytes_in1s(MPRTPSPath *this)
{
  gint64 result = 0;
  THIS_READLOCK(this);
  if(!this->packets.activated){
    goto done;
  }
  result = this->packets.sent_bytes_in_1s;
done:
  THIS_READUNLOCK(this);
  return result;
}

guint32 mprtps_path_get_received_bytes_in1s(MPRTPSPath *this)
{
  gint64 result = 0;
  THIS_READLOCK(this);
  if(!this->packets.activated){
    goto done;
  }
  result = this->packets.received_bytes_in_1s;
done:
  THIS_READUNLOCK(this);
  return result;
}

guint32 mprtps_path_get_goodput_bytes_in1s(MPRTPSPath *this)
{
  gint64 result = 0;
  THIS_READLOCK(this);
  if(!this->packets.activated){
    goto done;
  }
  result = this->packets.goodput_bytes_in_1s;
done:
  THIS_READUNLOCK(this);
  return result;
}

guint32 mprtps_path_get_bytes_in_flight(MPRTPSPath *this)
{
  gint64 result = 0;
  THIS_READLOCK(this);
  if(!this->packets.activated){
    goto done;
  }
  result = this->packets.bytes_in_flight;
done:
  THIS_READUNLOCK(this);
  return result;
}



void mprtps_path_activate_packets_monitoring(MPRTPSPath * this, gint32 items_length)
{
  MPRTPSPathPackets *packets;
  THIS_WRITELOCK (this);
  packets = &this->packets;
  if(packets->activated){
    GST_WARNING_OBJECT(this, "Packets monitoring on subflow %d has already been activated.", this->id);
    goto done;
  }

  //reset
  packets->read_index            = 0;
  packets->write_index           = 0;
  packets->counter               = 0;
  packets->sent_obsolation_index = 0;
  packets->received_bytes_in_1s  = 0;
  packets->sent_bytes_in_1s      = 0;
  packets->length                = items_length;
  packets->unkown_last_hssn      = TRUE;

  packets->activated = TRUE;
  packets->items = mprtp_malloc(sizeof(MPRTPSPathPacketsItem) * items_length);

done:
  THIS_WRITEUNLOCK (this);
}

void mprtps_path_deactivate_packets_monitoring (MPRTPSPath * this)
{
  MPRTPSPathPackets *packets;
  THIS_WRITELOCK (this);
  packets = &this->packets;
  if(!packets->activated){
    goto done;
  }

  if(packets->items != NULL){
    mprtp_free(packets->items);
    packets->items = NULL;
    packets->length  = 0;
    packets->counter = 0;
  }

  packets->activated = FALSE;
done:
  THIS_WRITEUNLOCK (this);
}

void mprtps_path_packets_refresh(MPRTPSPath *this)
{
//obsolate packets sent older than 2 seconds
  //consider the fact if we reach the last_hssn
  //Todo: if we reach the last_hssn then set it to the next one,
  //but get a notification.
  //If we reach the write index set the last_hssn to 0

  MPRTPSPathPackets*     packets;
  MPRTPSPathPacketsItem* item = NULL;
  GstClockTime           treshold = 0;
  gint32                 next_read_index;

  THIS_WRITELOCK (this);
  packets   = &this->packets;
  treshold  = _now(this);

  if(2 * GST_SECOND < treshold){
    treshold -= 2 * GST_SECOND;
  }

again:
  item = packets->items + packets->read_index;
  if(packets->counter == 0) goto done;
  if(treshold < item->sent) goto done;

  //remove
  if(item->seq_num == packets->last_hssn){
    packets->unkown_last_hssn = TRUE;
  }
  next_read_index = (packets->read_index + 1) % packets->length;

  if(packets->read_index == packets->sent_obsolation_index){
    g_print("Sent bytes subtracted by %d at refresh process\n", item->payload_bytes);
    packets->sent_bytes_in_1s -= item->payload_bytes;
    packets->sent_obsolation_index = next_read_index;
  }

  item->payload_bytes = 0;
  item->sent          = 0;
  item->seq_num       = 0;
  packets->read_index = next_read_index;
  --packets->counter;
  goto again;
done:
  THIS_WRITEUNLOCK(this);
}

void mprtps_path_packets_feedback_update(MPRTPSPath *this, GstMPRTCPReportSummary *summary)
{
  gint32                 i;
  GstClockTime           treshold = 0;
  guint16                actual_hssn;
  gint32                 actual_hssn_index = -1;
  gint32                 newly_received_bytes_sum = 0;
  MPRTPSPathPackets*     packets;
  MPRTPSPathPacketsItem* item = NULL;

  THIS_WRITELOCK (this);

  packets                       = &this->packets;
  actual_hssn                   = summary->RR.HSSN;
  packets->bytes_in_flight      = 0;
  packets->received_bytes_in_1s = 0;

  if(packets->counter == 0){
    goto done;
  }

  for(i=packets->write_index; i != packets->read_index;){
    item = packets->items + i;
    if(item->seq_num == actual_hssn){
      actual_hssn_index = i;
      if(GST_SECOND < item->sent){
        treshold = item->sent - GST_SECOND;
      }
      break;
    }
    if(i == 0) i = packets->length - 1;
    else       --i;
  }

  if(actual_hssn_index == -1){
      //didn'T found the hssn reported!
      g_warning("Reported HSSN can not be found amongst sent packets.\n"
          "(counter: %d HSSN: %hu, last_seq: %hu)",
          packets->counter, actual_hssn, item->seq_num);
      goto done;
  }

  for(i = packets->read_index; i != packets->write_index; i = (i+1) % packets->length){
    item = packets->items + i;

    if(item->sent < treshold){
      continue;
    }

    if(!packets->unkown_last_hssn && _cmp_uint16(item->seq_num, packets->last_hssn) <= 0){
      packets->received_bytes_in_1s  += item->payload_bytes;
      continue;
    }

    if( _cmp_uint16(item->seq_num, actual_hssn) <= 0){
      newly_received_bytes_sum += item->payload_bytes;
    }else{
      packets->bytes_in_flight += item->payload_bytes;
    }
  }

  if(summary->RR.processed && 0. < summary->RR.lost_rate){
    newly_received_bytes_sum *= 1.-summary->RR.lost_rate;
  }
  packets->received_bytes_in_1s += newly_received_bytes_sum;
  packets->goodput_bytes_in_1s  = packets->received_bytes_in_1s;

  if(summary->XR_RFC7097.processed){
    packets->goodput_bytes_in_1s -= summary->XR_RFC7097.total;
  }else if(summary->XR_RFC7243.processed){
    packets->goodput_bytes_in_1s -= summary->XR_RFC7243.discarded_bytes;
  }

  packets->unkown_last_hssn = FALSE;
  packets->last_hssn        = actual_hssn;


done:
  THIS_WRITEUNLOCK(this);
}


void
mprtps_path_process_rtp_packet(MPRTPSPath * this,
                               GstBuffer * rtppacket,
                               gboolean *monitoring_request)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  THIS_WRITELOCK (this);

  if(0 < this->skip_until){
    if(_now(this) < this->skip_until){
      this->expected_lost|=TRUE;
      gst_buffer_unref(rtppacket);
      goto done;
    }
    this->skip_until = 0;
  }
  gst_rtp_buffer_map(rtppacket, GST_MAP_READWRITE, &rtp);
  _setup_rtp2mprtp (this, &rtp);
  _refresh_stat(this, &rtp);
  gst_rtp_buffer_unmap(&rtp);

  this->last_sent = _now(this);

  if(!monitoring_request || !this->monitoring_interval) goto done;
  *monitoring_request = this->packets.total_sent_packets_num % this->monitoring_interval == 0;
done:
  THIS_WRITEUNLOCK (this);
}


void
_setup_rtp2mprtp (MPRTPSPath * this,
                  GstRTPBuffer *rtp)
{
  MPRTPSubflowHeaderExtension data;
  data.id = this->id;
  if (++(this->seq) == 0) {
    ++(this->cycle_num);
  }
  data.seq = this->seq;

  gst_rtp_buffer_add_extension_onebyte_header (rtp, this->mprtp_ext_header_id,
      (gpointer) & data, sizeof (data));
}

void
_refresh_stat(MPRTPSPath * this,
              GstRTPBuffer *rtp)
{
  guint                  payload_bytes;
  MPRTPSPathPackets*     packets;
  MPRTPSPathPacketsItem* item;
  gint                   i;
  GstClockTime           treshold;

  packets = &this->packets;

  payload_bytes = gst_rtp_buffer_get_payload_len (rtp);

  ++packets->total_sent_packets_num;
  packets->total_sent_payload_bytes += payload_bytes;

  if(!packets->activated){
    goto done;
  }

  packets->items[packets->write_index].payload_bytes = payload_bytes;
  packets->items[packets->write_index].sent          = treshold = _now(this);
  packets->items[packets->write_index].seq_num       = this->seq;

  packets->sent_bytes_in_1s += payload_bytes;
  treshold                  -= GST_SECOND < _now(this) ? GST_SECOND : 0;

//g_print("Indexes: read - %d, write - %d, obsolation - %d sent_bytes: %d\n",
//        packets->read_index,
//        packets->write_index,
//        packets->sent_obsolation_index,
//        packets->sent_bytes_in_1s);

  for(i = packets->sent_obsolation_index; i != packets->write_index; i = (i+1) % packets->length){
    item = packets->items + i;
    if(treshold < item->sent){
      break;
    }
//    g_print("Sent bytes subtracted by %d at sent process\n", item->payload_bytes);
    packets->sent_bytes_in_1s      -= item->payload_bytes;
    packets->sent_obsolation_index  = (i+1) % packets->length;
  }

  if(++packets->write_index == packets->length){
    packets->write_index = 0;
  }
  if(packets->write_index == packets->read_index){
    //BAD!
    GST_WARNING_OBJECT(this, "Too many packet sent on path or packets monitoring hasn't been refreshed");
  }else{
    ++packets->counter;
  }

done:
  return;
}



#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK


