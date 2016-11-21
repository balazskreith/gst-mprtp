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
#include "monitor.h"
#include "gstmprtcpbuffer.h"
#include <math.h>
#include <string.h>
#include "rtpfecbuffer.h"
#include "lib_swplugins.h"

#define _now(this) gst_clock_get_time (this->sysclock)

GST_DEBUG_CATEGORY_STATIC (monitor_debug_category);
#define GST_CAT_DEFAULT monitor_debug_category

G_DEFINE_TYPE (Monitor, monitor, G_TYPE_OBJECT);

typedef enum{
  ON_DISCARD  = 1,
  ON_RECEIVED = 2,
  ON_LOST     = 3,
}MonitorPacketEvents;


static gint
_cmp_seq (guint16 x, guint16 y)
{
  if(x == y) return 0;
  if(x < y && y - x < 32768) return -1;
  if(x > y && x - y > 32768) return -1;
  if(x < y && y - x > 32768) return 1;
  if(x > y && x - y < 32768) return 1;
  return 0;
}


static gint _tracked_seq_comparator(gpointer pa, gpointer pb)
{
  MonitorPacket *a;
  guint16* tracked_seq;
  a = pa; tracked_seq = pb;

  if(a->tracked_seq == *tracked_seq) return 0;
  return a->tracked_seq < *tracked_seq ? -1 : 1;
} \

static void _untrack_packet(Monitor* this, MonitorPacket *packet)
{
  TrackingStat *stat = packet->tracker;

  stat->accumulative_bytes -= packet->payload_size;
  --stat->accumulative_packets;
  recycle_add(this->recycle, packet);
}

static void _track_packet(Monitor* this, MonitorPacket* packet)
{
  TrackingStat* stat = packet->tracker;
  stat->total_bytes += packet->payload_size;
  ++stat->total_packets;
  stat->accumulative_bytes += packet->payload_size;
  ++stat->accumulative_packets;
}

//----------------------------------------------------------------------
//-------- Private functions belongs to Scheduler tree object ----------
//----------------------------------------------------------------------

static void monitor_finalize (GObject * object);
static void _setup_monitor_packet(Monitor* this, MonitorPacket* packet, GstRTPBuffer *rtp);
static void _monitor_packet_fire(Monitor* this, MonitorPacket *packet, MonitorPacketEvents event, gpointer arg);
static guint16 _get_tracked_seq(Monitor *this, GstRTPBuffer* rtp);

//----------------------------------------------------------------------
//--------- Private functions implementations to SchTree object --------
//----------------------------------------------------------------------

DEFINE_RECYCLE_TYPE(static, monitor, MonitorPacket);

static void _monitor_shaper(MonitorPacket* result, gpointer udata)
{
  memset(result, 0, sizeof(MonitorPacket));
}

void
monitor_class_init (MonitorClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = monitor_finalize;

  GST_DEBUG_CATEGORY_INIT (monitor_debug_category, "monitor", 0,
      "MpRTP Manual Sending Controller");

}

void
monitor_finalize (GObject * object)
{
  Monitor *this;

  this = MONITOR(object);
  g_queue_clear(this->prepared_packets);
  g_object_unref(this->recycle);
  g_object_unref(this->packets_sw);
  g_object_unref(this->sysclock);
  g_free(this->tracked_packets);
}

Monitor* make_monitor(void)
{
  Monitor* this;
  this = g_object_new (MONITOR_TYPE, NULL);
  this->recycle = make_recycle_monitor(256, (RecycleItemShaper)_monitor_shaper);
  return this;
}


void
monitor_init (Monitor * this)
{
  this->sysclock                 = gst_system_clock_obtain();
  this->tracked_packets           = g_malloc0(sizeof(MonitorPacket*) * 65536);
  this->packets_sw               = make_slidingwindow(1000, GST_SECOND);
  this->mprtp_ext_header_id      = 0;
  this->prepared_packets         = g_queue_new();

  slidingwindow_add_on_change(this->packets_sw,
      (ListenerFunc) _track_packet, (ListenerFunc) _untrack_packet, this);

}

void monitor_set_mprtp_ext_header_id(Monitor* this, guint8 mprtp_ext_header_id)
{
  this->mprtp_ext_header_id = mprtp_ext_header_id;
}

void monitor_set_fec_payload_type(Monitor* this, guint8 fec_payload_type)
{
  this->fec_payload_type = fec_payload_type;
}


void monitor_reset(Monitor* this)
{
  memset(this->tracked_packets, 0, sizeof(MonitorPacket*) * 65536);
  this->initialized = FALSE;
}

static MonitorPacket* _make_new_packet(Monitor* this, guint16 tracked_seq)
{
  MonitorPacket* packet;

  packet = recycle_retrieve_and_shape(this->recycle, NULL);
  packet->state       = MONITORED_PACKET_STATE_UNKNOWN;
  packet->tracked_ntp = NTP_NOW;
  packet->tracked_seq = tracked_seq;
  packet->extended_seq = ((guint32)this->cycle_num << 16) | ((guint32)packet->tracked_seq);

  return packet;
}

static gboolean _is_fec_packet(Monitor* this, MonitorPacket *packet)
{
  if(this->fec_payload_type == 0){
    return FALSE;
  }
  return packet->payload_type == this->fec_payload_type;
}

void monitor_track_rtpbuffer(Monitor* this, GstBuffer* buffer)
{
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  MonitorPacket* packet;
  guint16 tracked_seq;

  gst_rtp_buffer_map(buffer, GST_MAP_READ, &rtp);
  tracked_seq = _get_tracked_seq(this, &rtp);
  packet = this->tracked_packets[tracked_seq];
  this->tracked_packets[tracked_seq] = NULL;

  if(!packet){
    packet = _make_new_packet(this, tracked_seq);
    _setup_monitor_packet(this, packet, &rtp);
  }

  gst_rtp_buffer_unmap(&rtp);

  if(_is_fec_packet(this, packet)){
    packet->tracker = &this->stat.fec;
    slidingwindow_add_data(this->packets_sw, packet);
    //we should not forward fec packets as stat in the chain.
    packet = NULL;
    goto done;
  }

  if(this->initialized == FALSE){
    this->initialized  = TRUE;
    this->tracked_hsn  = packet->extended_seq = packet->tracked_seq;
    this->cycle_num    = 0;
    _monitor_packet_fire(this, packet, ON_RECEIVED, NULL);
    goto done;
  }
  else if(_cmp_seq(packet->tracked_seq, this->tracked_hsn) <= 0)
  {
    _monitor_packet_fire(this, packet, ON_DISCARD, NULL);
    goto done;
  }
  else if(65535 == this->tracked_hsn && 0 == packet->tracked_seq)
  {
    //No loss between the overrun
    ++this->cycle_num;
    packet->extended_seq = ((guint32)this->cycle_num << 16) | ((guint32)packet->tracked_seq);
  }



  while(_cmp_seq(++this->tracked_hsn, packet->tracked_seq) != 0){
    MonitorPacket *missing;
    missing = this->tracked_packets[this->tracked_hsn];
    this->tracked_packets[this->tracked_hsn] = NULL;
    if(!missing){
      missing = _make_new_packet(this, this->tracked_hsn);
    }
    _monitor_packet_fire(this, missing, ON_LOST, NULL);
    g_queue_push_tail(this->prepared_packets, missing);
    if(65535 == this->tracked_hsn && 0 < packet->tracked_seq){
        ++this->cycle_num;//gonna be overrun, but we shouldn't be here if its conseqvent
    }
  }
  _monitor_packet_fire(this, packet, ON_RECEIVED, NULL);
done:
  if(packet){
    g_queue_push_tail(this->prepared_packets, packet);
  }
}

MonitorPacket* monitor_pop_prepared_packet(Monitor* this){
  MonitorPacket* packet;
  if(g_queue_is_empty(this->prepared_packets)){
    return NULL;
  }
  packet = g_queue_pop_head(this->prepared_packets);
  packet->played_out = NTP_NOW;
  return packet;
}


void monitor_track_packetbuffer(Monitor* this, GstBuffer* buffer)
{
  TrackingStat* tracker = NULL;
  MonitorPacket *packet, *already_tracked = NULL;
  GstMapInfo map = GST_MAP_INFO_INIT;

  packet = recycle_retrieve_and_shape(this->recycle, NULL);

  gst_buffer_map(buffer, &map, GST_MAP_READ);
  memcpy(packet, map.data, sizeof(MonitorPacket));
  gst_buffer_unmap(buffer, &map);
//  g_print("Packet %hu tracked as %d\n", packet->tracked_seq, packet->state);

  if(packet->state != MONITORED_PACKET_STATE_DISCARDED){
    this->tracked_packets[packet->tracked_seq] = packet;
    goto done;
  }

  //g_print("Discarded packet arrived %hu, HSN %hu\n", packet->tracked_seq, this->tracked_hsn);

  already_tracked = this->tracked_packets[packet->tracked_seq];
  if(already_tracked){
    //If we have not picked it up yet from the lookup table,
    //then YUPEE, it is not too late to play it out here
    //and it can be marked as received without any side effect
    packet->state = MONITORED_PACKET_STATE_RECEIVED;
    this->tracked_packets[packet->tracked_seq] = packet;
    recycle_add(this->recycle, already_tracked);
    goto done;
  }

  already_tracked = slidingwindow_peek_custom(this->packets_sw, _tracked_seq_comparator, &packet->tracked_seq);
  if(!already_tracked || already_tracked->tracker == NULL){
    //The packet is not tracked any more in anywhere
    //HERE we assumed that the packet is considered to be lost previously,
    //but it is actually discarded.
    --this->stat.lost.total_packets;
    ++this->stat.discarded.total_packets;
    this->stat.discarded.total_bytes+=packet->payload_size;
    recycle_add(this->recycle, packet);
    goto done;
  }

  //So the packet is in the sliding window, and it has been tracked
  //so subtracked it from the tracked stat

  tracker = already_tracked->tracker;
  tracker->accumulative_bytes -= already_tracked->payload_size;
  tracker->total_bytes -= already_tracked->payload_size;
  --tracker->accumulative_packets;
  --tracker->total_packets;

  //copy the information we know now about the packet
  memcpy(already_tracked, packet, sizeof(MonitorPacket));
  recycle_add(this->recycle, packet);

  //and say that yes its discarded and tracking the stat through.
  tracker = already_tracked->tracker = &this->stat.discarded;
  tracker->accumulative_bytes += already_tracked->payload_size;
  tracker->total_bytes += already_tracked->payload_size;
  ++tracker->accumulative_packets;
  ++tracker->total_packets;

done:
  return;
}

void monitor_setup_packetbufffer(MonitorPacket* packet, GstBuffer *buffer)
{
  GstMapInfo map= GST_MAP_INFO_INIT;
  if(!gst_buffer_is_writable(buffer)){
    GST_WARNING("Buffer for serializing MonitorPacket is not writable");
    return;
  }

  gst_buffer_map(buffer, &map, GST_MAP_READWRITE);
  memcpy(map.data, packet, sizeof(MonitorPacket));
  ((MonitorPacket*)map.data)->tracker = NULL;
  gst_buffer_unmap(buffer, &map);

}

void monitor_setup_monitorstatbufffer(Monitor *this, GstBuffer *buffer)
{
  GstMapInfo map = GST_MAP_INFO_INIT;
  if(!gst_buffer_is_writable(buffer)){
    GST_WARNING("Buffer for serializing MonitorPacket is not writable");
    return;
  }

  gst_buffer_map(buffer, &map, GST_MAP_READWRITE);
  memcpy(map.data, &this->stat, sizeof(MonitorStat));
  gst_buffer_unmap(buffer, &map);

}

void monitor_set_accumulation_length(Monitor* this, GstClockTime length)
{
  slidingwindow_set_treshold(this->packets_sw, length);
}



void _monitor_packet_fire(Monitor* this, MonitorPacket *packet, MonitorPacketEvents event, gpointer arg)
{
//  g_print("Action on packet %hu-%d is %d\n", packet->tracked_seq, packet->state, event);
  switch(packet->state){
      case MONITORED_PACKET_STATE_DISCARDED:
        GST_DEBUG_OBJECT(this, "Packet with tracked seq %hu already discarded. How is that happened?", packet->tracked_seq);
        /* FALL THROUGH */
      case MONITORED_PACKET_STATE_UNKNOWN:
      switch(event){
        case ON_DISCARD:
          packet->state = MONITORED_PACKET_STATE_DISCARDED;
          packet->tracker = &this->stat.discarded;
        break;
        case ON_RECEIVED:
          packet->tracker = &this->stat.received;
          packet->state = MONITORED_PACKET_STATE_RECEIVED;
        break;
        case ON_LOST:
          packet->tracker = &this->stat.lost;
          packet->state = MONITORED_PACKET_STATE_LOST;
        break;
        default:
          GST_WARNING("Unrecognized state transition for event %d at UNKOWN state", event);
          break;
      }
      break;

      case MONITORED_PACKET_STATE_RECEIVED:
      switch(event){
        case ON_RECEIVED:
          packet->tracker = &this->stat.received;
          break;
        case ON_LOST:
          packet->tracker = &this->stat.corrupted;
          packet->state = MONITORED_PACKET_STATE_LOST;
        break;
        case ON_DISCARD:
          // SHOULD NOT HAPPEN!
          // if a packet already tracked and later on discarded
          // means that StatsMaker are chained and the packet notification
          // is slower than the tracking process.
          GST_WARNING("Tracked Seq %hu already received and now discarded. "
              "Either your chain for linking StatsMakers are too slow, or I screwed up something again.", packet->tracked_seq);
          packet->state = MONITORED_PACKET_STATE_DISCARDED;
          packet->tracker = &this->stat.discarded;
          break;
        default:
          GST_WARNING("Unrecognized state transition for event %d at RECEIVED state", event);
          break;
      }
      break;

      case MONITORED_PACKET_STATE_LOST:
      switch(event){
        case ON_DISCARD:
//          g_print("Packet %hu considred to be lost and now discarded\n", packet->tracked_seq);
          packet->tracker = &this->stat.discarded;
          packet->state = MONITORED_PACKET_STATE_DISCARDED;
          break;
        case ON_LOST:
//          g_print("Packet %hu considred to be lost and lost forever\n", packet->tracked_seq);
          packet->tracker = &this->stat.lost;
        break;
        case ON_RECEIVED:
//          g_print("Packet %hu considred to be lost, but repaired\n", packet->tracked_seq);
          packet->tracker = &this->stat.repaired;
          packet->state = MONITORED_PACKET_STATE_RECEIVED;
          break;
        default:
          GST_WARNING("Unrecognized state transition for event %d at LOST state", event);
          break;
      }
      break;
  }

  if(packet->tracker){
    slidingwindow_add_data(this->packets_sw, packet);
  }
}


void _setup_monitor_packet(Monitor* this, MonitorPacket* packet, GstRTPBuffer *rtp)
{
  packet->timestamp    = gst_rtp_buffer_get_timestamp(rtp);
  packet->payload_size = gst_rtp_buffer_get_payload_len(rtp);
  packet->payload_type = gst_rtp_buffer_get_payload_type(rtp);
  packet->header_size  = gst_rtp_buffer_get_header_len(rtp);
  packet->marker       = gst_rtp_buffer_get_marker(rtp);
}

guint16 _get_tracked_seq(Monitor *this, GstRTPBuffer* rtp)
{
  guint8 subflow_id;
  guint16 tracked_seq = 0;
  if(0 < this->mprtp_ext_header_id){
    gst_rtp_buffer_get_mprtp_extension(rtp, this->mprtp_ext_header_id, &subflow_id, &tracked_seq);
    goto done;
  }else{
    tracked_seq = gst_rtp_buffer_get_seq(rtp);
  }

done:
  return tracked_seq;
}
