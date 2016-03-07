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
static void _setup_rtp2mprtp (MPRTPSPath * this, GstBuffer * buffer);
static void _refresh_stat(MPRTPSPath * this, GstBuffer *buffer);
static void _send_mprtp_packet(MPRTPSPath * this,
                               GstBuffer *buffer);
static GstBuffer* _create_monitor_packet(MPRTPSPath * this);

#define MIN_PACE_INTERVAL 1
#define MINIMUM_PACE_BANDWIDTH 50000

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
make_mprtps_path (guint8 id, void (*send_func)(gpointer, GstBuffer*), gpointer func_this)
{
  MPRTPSPath *result;

  result = g_object_new (MPRTPS_PATH_TYPE, NULL);
  THIS_WRITELOCK (result);
  result->id = id;
  result->send_mprtp_func_data = func_this;
  result->send_mprtp_packet_func = send_func;
  THIS_WRITEUNLOCK (result);
  return result;
}
//
//void g_print_rrmeasurement(RRMeasurement *measurement)
//{
//  g_print("+=+=+=+=+=+=+=+=+=+=+ RRMeasurement dump =+=+=+=+=+=+=+=+=+=+=+=+=+\n"
//  "|Time: %29lu|\n"
//  "|RTT: %29lu|\n"
//  "|jitter: %29u|\n"
//  "|cum_packet_lost: %29u|\n"
//  "|lost: %29u|\n"
//  "|median_delay: %29lu|\n"
//  "|min_delay: %29lu|\n"
//  "|max_delay: %29lu|\n"
//  "|early_discarded_bytes: %29u|\n"
//  "|late_discarded_bytes: %29u|\n"
//  "|early_discarded_bytes_sum: %29u|\n"
//  "|late_discarded_bytes_sum: %29u|\n"
//  "|HSSN: %29hu|\n"
//  "|cycle_num: %29hu|\n"
//  "|expected_packets: %29hu|\n"
//  "|PiT: %29hu|\n"
//  "|expected_payload_bytes: %29u|\n"
//  "|sent_payload_bytes_sum: %29u|\n"
//  "|lost_rate: %29f|\n"
//  "|goodput: %29f|\n"
//  "|sender_rate: %29f|\n"
//  "|receiver_rate: %29f|\n"
//  "|checked: %29d|\n"
//  "|bytes_in_flight: %29u|\n"
//  "|bytes_in_queue: %29u|\n"
//  "+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+\n",
//  measurement->time,
//  measurement->RTT,
//  measurement->jitter,
//  measurement->cum_packet_lost,
//  measurement->lost,
//  measurement->recent_delay,
//  measurement->min_delay,
//  measurement->max_delay,
//  measurement->early_discarded_bytes,
//  measurement->late_discarded_bytes,
//  measurement->early_discarded_bytes_sum,
//  measurement->late_discarded_bytes_sum,
//  measurement->HSSN,
//  measurement->cycle_num,
//  measurement->expected_packets,
//  measurement->PiT,
//  measurement->expected_payload_bytes,
//  measurement->sent_payload_bytes_sum,
//  measurement->lost_rate,
//  measurement->goodput,
//  measurement->sender_rate,
//  measurement->receiver_rate,
//  measurement->checked,
//  measurement->bytes_in_flight_acked,
//  measurement->bytes_in_queue );
//
//}

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

  this->total_sent_packet_num = 0;
  this->total_sent_normal_packet_num = 0;
  this->total_sent_payload_bytes_sum = 0;
  this->total_sent_frames_num = 0;
  this->last_sent_frame_timestamp = 0;
  this->sent_octets_read = 0;
  this->sent_octets_write = 0;
  this->monitor_payload_type = FALSE;
  this->monitoring_interval = 0;
}


void
mprtps_path_init (MPRTPSPath * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sysclock = gst_system_clock_obtain ();
  this->sent_bytes = make_numstracker(512, GST_SECOND);
  mprtps_path_reset (this);
}


void
mprtps_path_finalize (GObject * object)
{
  MPRTPSPath *this = MPRTPS_PATH_CAST (object);
  g_object_unref (this->sysclock);
  g_object_unref(this->sent_bytes);
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

guint16 mprtps_path_get_HSN(MPRTPSPath * this)
{
  guint16 result;
  THIS_READLOCK (this);
  result = this->seq;
  THIS_READUNLOCK (this);
  return result;
}

void
mprtps_path_set_delay(MPRTPSPath * this, GstClockTime delay)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->path_delay = delay;
  THIS_WRITEUNLOCK (this);
}

GstClockTime
mprtps_path_get_delay(MPRTPSPath * this)
{
  GstClockTime result;
  THIS_READLOCK (this);
  result = this->path_delay;
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


void mprtps_path_set_monitor_payload_id(MPRTPSPath *this, guint8 payload_type)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->monitor_payload_type = payload_type;
  THIS_WRITEUNLOCK (this);
}

void mprtps_path_set_monitor_packet_provider(MPRTPSPath *this, MonitorPackets *monitorpackets)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->monitorpackets = monitorpackets;
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
  this->sent_middly_congested = gst_clock_get_time (this->sysclock);
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
mprtps_path_is_monitoring (MPRTPSPath * this)
{
  gboolean result;
  THIS_READLOCK (this);
  result = this->monitoring_interval > 0;
  THIS_READUNLOCK (this);
  return result;
}

guint32
mprtps_path_get_total_sent_packets_num (MPRTPSPath * this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = this->total_sent_packet_num;
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


guint32
mprtps_path_get_total_sent_payload_bytes (MPRTPSPath * this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = this->total_sent_payload_bytes_sum;
  THIS_READUNLOCK (this);
  return result;
}

guint32
mprtps_path_get_total_sent_frames_num (MPRTPSPath * this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = this->total_sent_frames_num;
  THIS_READUNLOCK (this);
  return result;
}

guint32
mprtps_path_get_sent_octet_sum_for (MPRTPSPath * this, guint32 amount)
{
  guint32 result = 0, read;
  guint16 i;

  THIS_WRITELOCK (this);
  if (amount < 1 || this->sent_octets_read == this->sent_octets_write) {
    goto done;
  }
  for ( i = 0;
      this->sent_octets_read != this->sent_octets_write &&  i < amount;
      ++ i) {
    read =  this->sent_octets[this->sent_octets_read];
    result += read;
    this->sent_octets_read += 1;
    this->sent_octets_read &= MAX_INT32_POSPART;
  }
  this->octets_in_flight_acked-= result;
done:
  THIS_WRITEUNLOCK (this);
  return result;
}

void mprtps_path_get_bytes_in_flight(MPRTPSPath *this, guint32 *acked)
{
  THIS_READLOCK(this);
  if(acked) *acked = this->octets_in_flight_acked<<3;
  THIS_READUNLOCK(this);
}

guint32 mprtps_path_get_sent_bytes_in1s(MPRTPSPath *this)
{
  gint64 result;
  THIS_READLOCK(this);
  numstracker_get_stats(this->sent_bytes, &result);
  THIS_READUNLOCK(this);
  return result;
}

void
mprtps_path_process_rtp_packet(MPRTPSPath * this,
                               GstBuffer * buffer)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  THIS_WRITELOCK (this);
  gst_rtp_buffer_map(buffer, GST_MAP_READWRITE, &rtp);
  gst_rtp_buffer_unmap(&rtp);

  if(0 < this->skip_until){
    if(_now(this) < this->skip_until){
      this->expected_lost|=TRUE;
      gst_buffer_unref(buffer);
      goto done;
    }
    this->skip_until = 0;
  }

  _setup_rtp2mprtp (this, buffer);
  _send_mprtp_packet(this, buffer);
//  goto done;
  if(!this->monitoring_interval) goto done;
  if(this->total_sent_normal_packet_num % this->monitoring_interval != 0) goto done;
  {
    GstBuffer *buffer;
    buffer = _create_monitor_packet(this);
    _setup_rtp2mprtp(this, buffer);
    _send_mprtp_packet(this, buffer);
  }
done:
  THIS_WRITEUNLOCK (this);

}

void
_setup_rtp2mprtp (MPRTPSPath * this,
                  GstBuffer * buffer)
{
  MPRTPSubflowHeaderExtension data;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  gst_rtp_buffer_map(buffer, GST_MAP_READWRITE, &rtp);
  data.id = this->id;
  if (++(this->seq) == 0) {
    ++(this->cycle_num);
  }
  data.seq = this->seq;

  gst_rtp_buffer_add_extension_onebyte_header (&rtp, this->mprtp_ext_header_id,
      (gpointer) & data, sizeof (data));
  if(this->ssrc_allowed == 0)
    this->ssrc_allowed = gst_rtp_buffer_get_ssrc(&rtp);
  gst_rtp_buffer_unmap(&rtp);
}

void
_refresh_stat(MPRTPSPath * this,
              GstBuffer *buffer)
{
  guint payload_bytes;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp);
  payload_bytes = gst_rtp_buffer_get_payload_len (&rtp);
  ++this->total_sent_packet_num;
  this->last_sent_payload_bytes = payload_bytes;
  this->last_packet_sent_time = gst_clock_get_time (this->sysclock);
  if(gst_rtp_buffer_get_timestamp(&rtp) != this->last_sent_frame_timestamp){
      ++this->total_sent_frames_num;
      this->last_sent_frame_timestamp = gst_rtp_buffer_get_timestamp(&rtp);
  }
  if(gst_rtp_buffer_get_payload_type(&rtp) != this->monitor_payload_type){
    this->total_sent_payload_bytes_sum += payload_bytes;
    this->sent_octets[this->sent_octets_write] = payload_bytes >> 3;
    this->octets_in_flight_acked += payload_bytes>>3;
    numstracker_add(this->sent_bytes, payload_bytes);
    ++this->total_sent_normal_packet_num;
  } else {
    this->sent_octets[this->sent_octets_write] = 0;
  }

  this->sent_octets_write += 1;
  this->sent_octets_write &= MAX_INT32_POSPART;
  gst_rtp_buffer_unmap(&rtp);
}

void
_send_mprtp_packet(MPRTPSPath * this,
                      GstBuffer *buffer)
{
  _refresh_stat(this, buffer);
  this->send_mprtp_packet_func(this->send_mprtp_func_data, buffer);
}


GstBuffer* _create_monitor_packet(MPRTPSPath * this)
{
  GstBuffer *result;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  if(this->monitorpackets){
    result = monitorpackets_provide_rtp_packet(this->monitorpackets);
  }else{
    result = gst_rtp_buffer_new_allocate (1400, 0, 0);
  }
  gst_rtp_buffer_map(result, GST_MAP_READWRITE, &rtp);
  gst_rtp_buffer_set_payload_type(&rtp, this->monitor_payload_type);

  gst_rtp_buffer_unmap(&rtp);
  return result;
}

#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK


