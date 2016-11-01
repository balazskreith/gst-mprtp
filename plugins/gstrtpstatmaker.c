#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gst/gst.h>
#include "gstrtpstatmaker.h"
#include "streamsplitter.h"
#include "gstmprtcpbuffer.h"
#include "sndctrler.h"
#include "sndpackets.h"


GST_DEBUG_CATEGORY_STATIC (gst_rtpstatmaker_debug_category);
#define GST_CAT_DEFAULT gst_rtpstatmaker_debug_category

#define PACKET_IS_RTP_OR_RTCP(b) (b > 0x7f && b < 0xc0)
#define PACKET_IS_RTCP(b) (b > 192 && b < 223)
#define THIS_LOCK(this) (g_mutex_lock(&this->mutex))
#define THIS_UNLOCK(this) (g_mutex_unlock(&this->mutex))

#define _now(this) gst_clock_get_time (this->sysclock)

static void gst_rtpstatmaker_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_rtpstatmaker_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_rtpstatmaker_dispose (GObject * object);
static void gst_rtpstatmaker_finalize (GObject * object);

static GstStateChangeReturn
gst_rtpstatmaker_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_rtpstatmaker_query (GstElement * element,
    GstQuery * query);
static GstFlowReturn gst_rtpstatmaker_rtp_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static GstFlowReturn gst_rtpstatmaker_rtp_sink_chainlist (GstPad * pad,
    GstObject * parent, GstBufferList * list);

static GstFlowReturn gst_rtpstatmaker_packet_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * outbuf);

static gboolean gst_rtpstatmaker_src_query (GstPad * srckpad, GstObject * parent,
    GstQuery * query);

static gboolean gst_rtpstatmaker_mprtp_src_event (GstPad * srckpad, GstObject * parent, GstEvent * event);
static gboolean gst_rtpstatmaker_sink_event (GstPad * srckpad, GstObject * parent, GstEvent * event);

static GstPadLinkReturn _on_packet_srcpad_linked (GstPad * pad, GstObject * parent, GstPad * peer);
static void _on_packet_srcpad_unlinked (GstPad * pad, GstObject * parent);
static GstPadLinkReturn _on_packetlogs_srcpad_linked (GstPad * pad, GstObject * parent, GstPad * peer);
static void _on_packetlogs_srcpad_unlinked (GstPad * pad, GstObject * parent);
static GstPadLinkReturn _on_statlogs_srcpad_linked (GstPad * pad, GstObject * parent, GstPad * peer);
static void _on_statlogs_srcpad_unlinked (GstPad * pad, GstObject * parent);


enum
{
  PROP_0,
  PROP_FEC_PAYLOAD_TYPE,
  PROP_SAMPLING_TIME,
  PROP_ACCUMULATION_LENGTH,
  PROP_MPRTP_EXT_HEADER_ID,
  PROP_CSV_LOGGING,
};

/* signals and args */
enum
{
  SIGNAL_SYSTEM_STATE,
  LAST_SIGNAL
};



/* pad templates */
static GstStaticPadTemplate gst_rtpstatmaker_rtp_sink_template =
GST_STATIC_PAD_TEMPLATE ("rtp_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );


static GstStaticPadTemplate gst_rtpstatmaker_mprtp_src_template =
GST_STATIC_PAD_TEMPLATE ("rtp_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );


static GstStaticPadTemplate gst_rtpstatmaker_packet_src_template =
GST_STATIC_PAD_TEMPLATE ("packet_src",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_rtpstatmaker_packet_sink_template =
GST_STATIC_PAD_TEMPLATE ("packet_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);


static GstStaticPadTemplate gst_rtpstatmaker_statlogs_src_template =
GST_STATIC_PAD_TEMPLATE ("statlogs_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_rtpstatmaker_packetlogs_src_template =
GST_STATIC_PAD_TEMPLATE ("packetlogs_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);


/* class initialization */
G_DEFINE_TYPE_WITH_CODE (GstRTPStatMaker, gst_rtpstatmaker,
    GST_TYPE_ELEMENT,
    GST_DEBUG_CATEGORY_INIT (gst_rtpstatmaker_debug_category,
        "rtpstatmaker", 0, "debug category for rtpstatmaker element"));

#define GST_RTPSTATMAKER_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_RTPSTATMAKER, GstRTPStatMakerPrivate))

struct _GstRTPStatMakerPrivate
{

};

static void
gst_rtpstatmaker_class_init (GstRTPStatMakerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpstatmaker_rtp_sink_template));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpstatmaker_mprtp_src_template));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpstatmaker_packet_src_template));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rtpstatmaker_packet_sink_template));


  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "RTPStat Maker Element", "Generic", "Make an accumulative statistical and a sequence based log going through the element",
      "Balázs Kreith <balazs.kreith@gmail.com>");

  gobject_class->set_property = gst_rtpstatmaker_set_property;
  gobject_class->get_property = gst_rtpstatmaker_get_property;
  gobject_class->dispose = gst_rtpstatmaker_dispose;
  gobject_class->finalize = gst_rtpstatmaker_finalize;
  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtpstatmaker_change_state);
  element_class->query = GST_DEBUG_FUNCPTR (gst_rtpstatmaker_query);


  g_object_class_install_property (gobject_class, PROP_FEC_PAYLOAD_TYPE,
      g_param_spec_uint ("fec-payload-type",
          "Set or get the payload type of FEC packets",
          "Set or get the payload type of FEC packets. The default is 126",
          0, 127, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SAMPLING_TIME,
      g_param_spec_uint ("sampling-time",
          "Set the sampling time in ms. Default is 100ms",
          "The sampling time is the period an accumulative statistic is sampled",
          0, 100000000, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ACCUMULATION_LENGTH,
      g_param_spec_uint ("accumulation-length",
          "Set the length of the accumulative window in ms",
          "The accumulation window is a time based window for packet and it is the base of the accumulative statistics.",
          0, 1000000000, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));


  g_object_class_install_property (gobject_class, PROP_MPRTP_EXT_HEADER_ID,
      g_param_spec_uint ("mprtp-ext-header-id",
          "Set or get the id for the RTP extension",
          "Sets or gets the id for the extension header the MpRTP based on. The default is 3",
          0, 15, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CSV_LOGGING,
      g_param_spec_boolean ("csv-logging",
          "Indicate weatherthe log is transmitted in a csv format or a structure",
          "Indicate weatherthe log is transmitted in a csv format or a structure",
          FALSE, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

}


static void
gst_rtpstatmaker_init (GstRTPStatMaker * this)
{
//  GstRTPStatMakerPrivate *priv;
//  priv = this->priv = GST_RTPSTATMAKER_GET_PRIVATE (this);

  init_mprtp_logger();

  this->rtp_sinkpad =
      gst_pad_new_from_static_template (&gst_rtpstatmaker_rtp_sink_template,
      "rtp_sink");

  gst_pad_set_chain_function (this->rtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtpstatmaker_rtp_sink_chain));
  gst_pad_set_chain_list_function (this->rtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtpstatmaker_rtp_sink_chainlist));

  GST_PAD_SET_PROXY_CAPS (this->rtp_sinkpad);
  GST_PAD_SET_PROXY_ALLOCATION (this->rtp_sinkpad);


  gst_element_add_pad (GST_ELEMENT (this), this->rtp_sinkpad);

  this->packet_sinkpad =
      gst_pad_new_from_static_template
      (&gst_rtpstatmaker_packet_sink_template, "packet_sink");

  gst_pad_set_chain_function (this->packet_sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtpstatmaker_packet_sink_chain));

  //  gst_pad_set_event_function (this->rtp_sinkpad,
  //      GST_DEBUG_FUNCPTR (gst_rtpstatmaker_sink_eventfunc));
  gst_pad_set_event_function (this->rtp_sinkpad,
        GST_DEBUG_FUNCPTR (gst_rtpstatmaker_sink_event));

  gst_element_add_pad (GST_ELEMENT (this), this->packet_sinkpad);

  this->packet_srcpad =
      gst_pad_new_from_static_template
      (&gst_rtpstatmaker_packet_src_template, "packet_src");

  gst_pad_set_link_function (this->packet_srcpad,
      GST_DEBUG_FUNCPTR (_on_packet_srcpad_linked));
  gst_pad_set_unlink_function (this->packet_srcpad,
      GST_DEBUG_FUNCPTR (_on_packet_srcpad_unlinked));

  gst_element_add_pad (GST_ELEMENT (this), this->packet_srcpad);

  this->packetlogs_srcpad =
      gst_pad_new_from_static_template
      (&gst_rtpstatmaker_packetlogs_src_template, "packetlogs_src");

  gst_pad_set_link_function (this->packetlogs_srcpad,
      GST_DEBUG_FUNCPTR (_on_packetlogs_srcpad_linked));
  gst_pad_set_unlink_function (this->packetlogs_srcpad,
      GST_DEBUG_FUNCPTR (_on_packetlogs_srcpad_unlinked));


  gst_element_add_pad (GST_ELEMENT (this), this->packetlogs_srcpad);

  this->statlogs_srcpad =
      gst_pad_new_from_static_template
      (&gst_rtpstatmaker_statlogs_src_template, "statlogs_src");

  gst_pad_set_link_function (this->statlogs_srcpad,
      GST_DEBUG_FUNCPTR (_on_statlogs_srcpad_linked));
  gst_pad_set_unlink_function (this->statlogs_srcpad,
      GST_DEBUG_FUNCPTR (_on_statlogs_srcpad_unlinked));


  gst_element_add_pad (GST_ELEMENT (this), this->statlogs_srcpad);


  this->rtp_srcpad =
      gst_pad_new_from_static_template (&gst_rtpstatmaker_mprtp_src_template,
      "rtp_src");

  gst_pad_set_event_function (this->rtp_srcpad,
      GST_DEBUG_FUNCPTR (gst_rtpstatmaker_mprtp_src_event));
  gst_pad_use_fixed_caps (this->rtp_srcpad);
  GST_PAD_SET_PROXY_CAPS (this->rtp_srcpad);
  GST_PAD_SET_PROXY_ALLOCATION (this->rtp_srcpad);

  gst_pad_set_query_function(this->rtp_srcpad,
    GST_DEBUG_FUNCPTR(gst_rtpstatmaker_src_query));

  gst_element_add_pad (GST_ELEMENT (this), this->rtp_srcpad);



  this->sysclock = gst_system_clock_obtain ();
  g_cond_init(&this->waiting_signal);

  this->fec_payload_type = FEC_PAYLOAD_DEFAULT_ID;

  //TODO: Monitor can be ssrc based in a hash table for more sophisticated stats, but it is ... THE FUTURE - HAHAHAHA!
  this->monitor       = make_monitor();

}


void
gst_rtpstatmaker_dispose (GObject * object)
{
  GstRTPStatMaker *rtpstatmaker = GST_RTPSTATMAKER (object);

  GST_DEBUG_OBJECT (rtpstatmaker, "dispose");

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (gst_rtpstatmaker_parent_class)->dispose (object);
}

void
gst_rtpstatmaker_finalize (GObject * object)
{
  GstRTPStatMaker *this = GST_RTPSTATMAKER (object);

  GST_DEBUG_OBJECT (this, "finalize");

  /* clean up object here */

  g_object_unref (this->sysclock);
  g_object_unref (this->monitor);

  G_OBJECT_CLASS (gst_rtpstatmaker_parent_class)->finalize (object);
}



void
gst_rtpstatmaker_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRTPStatMaker *this = GST_RTPSTATMAKER (object);

  GST_DEBUG_OBJECT (this, "set_property");
  THIS_LOCK(this);
  switch (property_id) {
  case PROP_MPRTP_EXT_HEADER_ID:
    this->mprtp_ext_header_id = (guint8) g_value_get_uint (value);
    monitor_set_mprtp_ext_header_id(this->monitor, this->mprtp_ext_header_id);
    break;
  case PROP_FEC_PAYLOAD_TYPE:
    this->fec_payload_type = (guint8) g_value_get_uint (value);
    monitor_set_fec_payload_type(this->monitor, this->fec_payload_type);
    break;
  case PROP_ACCUMULATION_LENGTH:
    this->accumulation_length = (GstClockTime) g_value_get_uint (value) * GST_MSECOND;
    monitor_set_accumulation_length(this->monitor, this->accumulation_length);
    break;
  case PROP_SAMPLING_TIME:
    this->sampling_time = (GstClockTime) g_value_get_uint (value) * GST_MSECOND;
    break;
  case PROP_CSV_LOGGING:
    this->csv_logging = g_value_get_boolean (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
  THIS_UNLOCK(this);
}


void
gst_rtpstatmaker_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstRTPStatMaker *this = GST_RTPSTATMAKER (object);

  GST_DEBUG_OBJECT (this, "get_property");
  THIS_LOCK(this);
  switch (property_id) {
    case PROP_MPRTP_EXT_HEADER_ID:
      g_value_set_uint (value, (guint) this->mprtp_ext_header_id);
    break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  THIS_UNLOCK(this);

}

static GstStateChangeReturn
gst_rtpstatmaker_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRTPStatMaker * this;

  this = GST_RTPSTATMAKER (element);
  g_return_val_if_fail (GST_IS_RTPSTATMAKER (element),
      GST_STATE_CHANGE_FAILURE);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
       gst_task_set_lock (this->thread, &this->thread_mutex);
       gst_task_start (this->thread);
       break;
     default:
       break;
   }

   ret =
       GST_ELEMENT_CLASS (gst_rtpstatmaker_parent_class)->change_state
       (element, transition);

   switch (transition) {
     case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
       gst_task_stop (this->thread);
       break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}


static gboolean
gst_rtpstatmaker_query (GstElement * element, GstQuery * query)
{
  GstRTPStatMaker *this = GST_RTPSTATMAKER (element);
  gboolean ret = TRUE;

  GST_DEBUG_OBJECT (this, "query");
  switch (GST_QUERY_TYPE (query)) {
    default:
      ret =
          GST_ELEMENT_CLASS (gst_rtpstatmaker_parent_class)->query (element,
          query);
      break;
  }

  return ret;
}



static GstFlowReturn
gst_rtpstatmaker_rtp_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer)
{
  GstRTPStatMaker *this;
  guint8 first_byte;
  guint8 second_byte;
  MonitorPacket* packet = NULL;
  GstBuffer* packetbuffer = NULL;

  this = GST_RTPSTATMAKER (parent);

  if (GST_PAD_IS_FLUSHING(pad)) {
    goto exit;
  }

  if (gst_buffer_extract (buffer, 0, &first_byte, 1) != 1 ||
      gst_buffer_extract (buffer, 1, &second_byte, 1) != 1) {
    GST_WARNING_OBJECT (this, "could not extract first byte from buffer");
    goto exit;
  }

  if (!PACKET_IS_RTP_OR_RTCP (first_byte)) {
    GST_DEBUG_OBJECT (this, "Not RTP Packet arrived at rtp_sink");
    goto exit;
  }


  if(PACKET_IS_RTCP(second_byte)){
    GST_DEBUG_OBJECT (this, "RTCP Packet arrived on rtp sink");
    goto exit;
  }

  THIS_LOCK(this);

  packet = monitor_track_rtpbuffer(this->monitor, buffer);
  if(!packet){
    goto done;
  }

  if(!this->packetsrc_linked && !this->packetlogs_linked){
    goto done;
  }


  if(this->packetsrc_linked){
    packetbuffer    = gst_buffer_new_wrapped(g_malloc0(1024), 1024);
    monitor_setup_packetbufffer(packet, packetbuffer);
    gst_pad_push(this->packet_srcpad, packetbuffer);
    packetbuffer = NULL;
  }

  if(this->packetlogs_linked){
    GstMapInfo map = GST_MAP_INFO_INIT;

    packetbuffer = gst_buffer_new_wrapped(g_malloc0(1024), 1024);
    if(this->csv_logging){
      gchar csv[1024];


      sprintf(csv,
          "%hu,%d,%lu,%d,%u,%u,%u,%d",
          packet->tracked_seq,
          packet->state,
          packet->tracked_ntp,
          packet->marker,
          packet->header_size,
          packet->payload_size,
          packet->timestamp,
          packet->payload_type

      );


      gst_buffer_map(buffer, &map, GST_MAP_READWRITE);
      memcpy(map.data, csv, 1024);
      gst_buffer_unmap(buffer, &map);
    }else{
      monitor_setup_packetbufffer(packet, packetbuffer);
    }
    gst_pad_push(this->packetlogs_srcpad, packetbuffer);
  }

done:
  THIS_UNLOCK(this);

exit:
  return gst_pad_push(this->rtp_srcpad, buffer);
}


static GstFlowReturn
gst_rtpstatmaker_rtp_sink_chainlist (GstPad * pad, GstObject * parent,
    GstBufferList * list)
{
  GstBuffer *buffer;
  gint i, len;
  GstFlowReturn result;

  result = GST_FLOW_OK;

  /* chain each buffer in list individually */
  len = gst_buffer_list_length (list);

  if (len == 0)
    goto done;

  for (i = 0; i < len; i++) {
    buffer = gst_buffer_list_get (list, i);

    result = gst_rtpstatmaker_rtp_sink_chain (pad, parent, buffer);
    if (result != GST_FLOW_OK)
      break;
  }

done:
  return result;
}


static GstFlowReturn
gst_rtpstatmaker_packet_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buf)
{
  GstRTPStatMaker *this;
  GstFlowReturn result;

  this = GST_RTPSTATMAKER (parent);

  THIS_LOCK(this);

  monitor_track_packetbuffer(this->monitor, buf);

  result = GST_FLOW_OK;
  THIS_UNLOCK(this);

  return result;

}



static gboolean gst_rtpstatmaker_sink_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
    gboolean ret;

    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_CAPS:
        case GST_EVENT_FLUSH_STOP:
        case GST_EVENT_STREAM_START:
        case GST_EVENT_SEGMENT:
        default:
            ret = gst_pad_event_default(pad, parent, event);
            break;
    }

    return ret;
}

static gboolean gst_rtpstatmaker_mprtp_src_event(GstPad *pad, GstObject *parent, GstEvent *event)
{
    gboolean ret;

    switch (GST_EVENT_TYPE(event)) {
        case GST_EVENT_FLUSH_START:
        case GST_EVENT_RECONFIGURE:
        case GST_EVENT_FLUSH_STOP:
        default:
            ret = gst_pad_event_default(pad, parent, event);
            break;
    }

    return ret;
}

static gboolean
gst_rtpstatmaker_src_query (GstPad * srckpad, GstObject * parent,
    GstQuery * query)
{
  GstRTPStatMaker *this = GST_RTPSTATMAKER (parent);
  gboolean result;

  GST_DEBUG_OBJECT (this, "query");
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      gboolean live;
      GstClockTime min, max;
      GstPad *peer;
      peer = gst_pad_get_peer (this->rtp_sinkpad);
      if ((result = gst_pad_query (peer, query))) {
          gst_query_parse_latency (query, &live, &min, &max);
          min= GST_MSECOND;
          max = -1;
          gst_query_set_latency (query, live, min, max);
      }
      gst_object_unref (peer);
    }
    break;
    default:
      result = gst_pad_query_default(srckpad, parent, query);
      break;
  }

  return result;
}



static void
_monitorstat_emitter (GstRTPStatMaker *this)
{
  Monitor* monitor;
  MonitorStat* stat;
  GstBuffer* buffer = NULL;
  gint64 end_time;
  const guint length = 1024;
  gchar csv[1024];

  THIS_LOCK(this);
  end_time = g_get_monotonic_time() + this->sampling_time * 1000; //because sampling_time is in ms.
  g_cond_wait_until(&this->waiting_signal, &this->mutex, end_time);
  if(!this->statlogs_linked){
    goto done;
  }

  buffer = gst_buffer_new_wrapped(g_malloc0(length), length);
  monitor = this->monitor;
  stat = &monitor->stat;

  if(this->csv_logging){
    GstMapInfo map = GST_MAP_INFO_INIT;
    sprintf(csv,
        "%d,%d,%d,%d," //received
        "%d,%d,%d,%d," //lost
        "%d,%d,%d,%d," //discarded
        "%d,%d,%d,%d," //corrupted
        "%d,%d,%d,%d," //repaired
        "%d,%d,%d,%d"  //fec
        ,
        stat->received.total_packets,
        stat->received.total_bytes,
        stat->received.accumulative_bytes,
        stat->received.accumulative_packets,

        stat->lost.total_packets,
        stat->lost.total_bytes,
        stat->lost.accumulative_bytes,
        stat->lost.accumulative_packets,

        stat->discarded.total_packets,
        stat->discarded.total_bytes,
        stat->discarded.accumulative_bytes,
        stat->discarded.accumulative_packets,

        stat->corrupted.total_packets,
        stat->corrupted.total_bytes,
        stat->corrupted.accumulative_bytes,
        stat->corrupted.accumulative_packets,

        stat->repaired.total_packets,
        stat->repaired.total_bytes,
        stat->repaired.accumulative_bytes,
        stat->repaired.accumulative_packets,

        stat->fec.total_packets,
        stat->fec.total_bytes,
        stat->fec.accumulative_bytes,
        stat->fec.accumulative_packets
      );

    gst_buffer_map(buffer, &map, GST_MAP_READWRITE);
    memcpy(map.data, csv, length);
    gst_buffer_unmap(buffer, &map);

  }else{
    monitor_setup_monitorstatbufffer(this->monitor, buffer);
  }

  gst_pad_push(this->statlogs_srcpad, buffer);

done:
  THIS_UNLOCK(this);
  return;
}




GstPadLinkReturn _on_packet_srcpad_linked (GstPad * pad, GstObject * parent, GstPad * peer)
{
  GstRTPStatMaker *this;
  GstPadLinkReturn result = GST_PAD_LINK_OK;
  this = GST_RTPSTATMAKER (parent);
  THIS_LOCK(this);
  this->packetsrc_linked = TRUE;
  THIS_UNLOCK(this);
  return result;
}

void _on_packet_srcpad_unlinked (GstPad * pad, GstObject * parent)
{
  GstRTPStatMaker *this;
  this = GST_RTPSTATMAKER (parent);

  THIS_LOCK(this);
  this->packetsrc_linked = FALSE;
  THIS_UNLOCK(this);
  return;
}


GstPadLinkReturn _on_packetlogs_srcpad_linked (GstPad * pad, GstObject * parent, GstPad * peer)
{
  GstRTPStatMaker *this;
  GstPadLinkReturn result = GST_PAD_LINK_OK;
  this = GST_RTPSTATMAKER (parent);

  THIS_LOCK(this);
  this->packetlogs_linked = TRUE;
  THIS_UNLOCK(this);

  return result;
}

void _on_packetlogs_srcpad_unlinked (GstPad * pad, GstObject * parent)
{
  GstRTPStatMaker *this;
  this = GST_RTPSTATMAKER (parent);

  THIS_LOCK(this);
  this->packetlogs_linked = FALSE;
  THIS_UNLOCK(this);

  return;
}

GstPadLinkReturn _on_statlogs_srcpad_linked (GstPad * pad, GstObject * parent, GstPad * peer)
{
  GstRTPStatMaker *this;
  GstPadLinkReturn result = GST_PAD_LINK_OK;
  this = GST_RTPSTATMAKER (parent);

  THIS_LOCK(this);
  this->statlogs_linked = TRUE;
  this->thread = gst_task_new ((GstTaskFunction) _monitorstat_emitter, this, NULL);
  g_mutex_init (&this->mutex);
  THIS_UNLOCK(this);

  return result;
}

void _on_statlogs_srcpad_unlinked (GstPad * pad, GstObject * parent)
{
  GstTask* task;
  GstRTPStatMaker *this;
  this = GST_RTPSTATMAKER (parent);

  THIS_LOCK(this);
  this->statlogs_linked = FALSE;
  task = this->thread;
  this->thread = NULL;
  THIS_UNLOCK(this);

  gst_task_join (task);
  gst_object_unref (task);

  return;
}



