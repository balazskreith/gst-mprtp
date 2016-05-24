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
static guint16 _setup_rtp2mprtp (MPRTPSPath * this, GstRTPBuffer *rtp);
static void _refresh_stat(MPRTPSPath * this, GstRTPBuffer *rtp, guint16 sn);
//static void _send_mprtp_packet(MPRTPSPath * this,
//                               GstBuffer *buffer);
//static GstBuffer* _create_monitor_packet(MPRTPSPath * this);

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
make_mprtps_path (guint8 id)
{
  MPRTPSPath *result;

  result = g_object_new (MPRTPS_PATH_TYPE, NULL);
  THIS_WRITELOCK (result);
  result->id = id;
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
  this->seq = 0;
  this->cycle_num = 0;
  this->flags = MPRTPS_PATH_FLAG_ACTIVE |
      MPRTPS_PATH_FLAG_NON_CONGESTED | MPRTPS_PATH_FLAG_NON_LOSSY;

  this->monitoring_interval = 0;

}


void
mprtps_path_init (MPRTPSPath * this)
{
  g_rw_lock_init (&this->rwmutex);
  this->sysclock = gst_system_clock_obtain ();
  this->packetstracker = NULL;
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


void mprtps_path_set_monitoring_interval(MPRTPSPath *this, guint monitoring_interval)
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

void mprtps_path_set_target_bitrate(MPRTPSPath * this, gint32 target_bitrate)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->target_bitrate = target_bitrate;
  THIS_WRITEUNLOCK (this);
}

gint32 mprtps_path_get_target_bitrate(MPRTPSPath * this)
{
  gint32 result;
  THIS_READLOCK (this);
  result = this->target_bitrate;
  THIS_READUNLOCK (this);
  return result;
}


void mprtps_path_set_state(MPRTPSPath * this, MPRTPSPathState new_state)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->actual_state = new_state;
  THIS_WRITEUNLOCK (this);
}

MPRTPSPathState mprtps_path_get_state(MPRTPSPath * this)
{
  MPRTPSPathState result;
  THIS_READLOCK (this);
  result = this->actual_state;
  THIS_READUNLOCK (this);
  return result;
}

void mprtps_path_set_monitored_bitrate(MPRTPSPath * this, gint32 monitored_bitrate, gint32 monitored_packets)
{
  g_return_if_fail (this);
  THIS_WRITELOCK (this);
  this->monitored_bitrate = monitored_bitrate;
  this->monitored_packets = monitored_packets;
  THIS_WRITEUNLOCK (this);
}

gint32 mprtps_path_get_monitored_bitrate(MPRTPSPath * this, guint32 *packets_num)
{
  gint32 result;
  THIS_READLOCK (this);
  result = this->monitored_bitrate;
  if(packets_num) *packets_num = this->monitored_packets;
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

void
mprtps_path_set_approval_process(MPRTPSPath *this, gpointer data, gboolean(*approval)(gpointer, GstRTPBuffer *))
{
  THIS_WRITELOCK (this);
  this->approval = approval;
  this->approval_data = data;
  THIS_WRITEUNLOCK (this);
}

gboolean mprtps_path_approve_request(MPRTPSPath *this, GstRTPBuffer *rtp)
{
  gboolean result;
  THIS_READLOCK(this);
  result = !this->approval ? TRUE : this->approval(this->approval_data, rtp);
  THIS_READUNLOCK(this);
  return result;
}


void mprtps_path_set_packetstracker(MPRTPSPath *this, void(*packetstracker)(gpointer,  guint, guint16), gpointer data)
{
  THIS_WRITELOCK(this);
  this->packetstracker = packetstracker;
  this->packetstracker_data = data;
  THIS_WRITEUNLOCK(this);
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
  result = this->total_sent_packets_num;
  THIS_READUNLOCK (this);
  return result;
}


guint32
mprtps_path_get_total_sent_payload_bytes (MPRTPSPath * this)
{
  guint32 result;
  THIS_READLOCK (this);
  result = this->total_sent_payload_bytes;
  THIS_READUNLOCK (this);
  return result;
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
  _refresh_stat(this, &rtp, _setup_rtp2mprtp (this, &rtp));
  gst_rtp_buffer_unmap(&rtp);

  this->last_sent = _now(this);

  if(!monitoring_request || !this->monitoring_interval) goto done;
  *monitoring_request = this->total_sent_packets_num % this->monitoring_interval == 0;
done:
  THIS_WRITEUNLOCK (this);
}


guint16
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

  return data.seq;
}

void
_refresh_stat(MPRTPSPath * this,
              GstRTPBuffer *rtp,
              guint16 sn)
{
  guint payload_bytes;

  payload_bytes = gst_rtp_buffer_get_payload_len (rtp);

  ++this->total_sent_packets_num;
  this->total_sent_payload_bytes += payload_bytes;

  if(this->packetstracker){
    this->packetstracker(this->packetstracker_data, payload_bytes, sn);
  }

}



#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK


