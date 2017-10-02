#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/if.h>
#include <netinet/if_ether.h>

#include <pcap.h>

#define current_unix_time_in_us g_get_real_time ()
#define current_unix_time_in_ms (current_unix_time_in_us / 1000L)
#define current_unix_time_in_s  (current_unix_time_in_ms / 1000L)
#define epoch_now_in_ns ((guint64)((current_unix_time_in_us + 2208988800000000LL) * 1000))
#define get_ntp_from_epoch_ns(epoch_in_ns) gst_util_uint64_scale (epoch_in_ns, (1LL << 32), GST_SECOND)
#define get_epoch_time_from_ntp_in_ns(ntp_time) gst_util_uint64_scale (ntp_time, GST_SECOND, (1LL << 32))
#define NTP_NOW get_ntp_from_epoch_ns(epoch_now_in_ns)
#define _now(this) gst_clock_get_time (this->sysclock)

typedef struct {

}UnixSocketReader;

typedef struct {

}Filter;

typedef struct {

}Sampler;

typedef struct {

}Collector;

void construct(const gchar* snd_packets_src) {

  UnixSocketReader* snd_packets_receiver = make_unix_socket_receiver("SndPackets Source",
      snd_packets_src, make_rtppacket);
  Filter* rtp_packets_filter = make_filter("RTP Packet Filter", rtp_filter);
  MonitorQueue* rtp_packets_queue = make_monitorqueue("RTP Packets Queue",
      is_rtp_packet_queue_over_1s);
  SumMonitor* rtp_packets_payload_sum = make_summonitor("RTP Packet Payload Sum", rtp_packets_queue, get_rtp_packet_payload_size);
  Sampler* monitor_sampler = make_monitor_sampler("Monitor Sampler",
      &rtp_packets_payload_sum->base, get_rtp_packet_epoch_ts, 100 GST_MSECOND);

  Collector* collector = make_collector("StatsCollector");

  cmp_connect(snd_packets_receiver->output, rtp_packets_filter->input, rtp_packets_filter);
  cmp_connect(rtp_packets_filter->output_true, rtp_packets_queue->input, rtp_packets_queue);

  cmp_connect(monitor_sampler->output, collector->sending_rate, collector);


}


