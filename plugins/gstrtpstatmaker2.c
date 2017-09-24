#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#define _now(this) gst_clock_get_time (this->sysclock)

#define PACKET_IS_RTP_OR_RTCP(b) (b > 0x7f && b < 0xc0)
#define PACKET_IS_RTCP(b) (b > 192 && b < 223)

#define THIS_LOCK(this) (g_mutex_lock(&this->mutex))
#define THIS_UNLOCK(this) (g_mutex_unlock(&this->mutex))

#include <stdlib.h>
#include <string.h>

#include "gstrtpstatmaker2.h"



static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_rtpstatmaker2_debug);
#define GST_CAT_DEFAULT gst_rtpstatmaker2_debug

/* RTPStatMaker2 signals and args */
enum
{
  LAST_SIGNAL
};

#define DEFAULT_DUMP                    FALSE
#define DEFAULT_SYNC                    FALSE

enum
{
  PROP_0,
  PROP_SYNC,
  PROP_MPRTP_EXT_HEADER_ID,
  PROP_FEC_PAYLOAD_TYPE,
  PROP_DEFAULT_MKFIFO_LOCATION
};


typedef struct{
  guint32 ssrc;
  guint8  payload_type;
  guint32 subflow_id;
  gchar   path[256];
}Logger;



#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_rtpstatmaker2_debug, "rtpstatmaker2", 0, "rtpstatmaker2 element");
#define gst_rtpstatmaker2_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstRTPStatMaker2, gst_rtpstatmaker2, GST_TYPE_BASE_TRANSFORM,
    _do_init);

static void gst_rtpstatmaker2_finalize (GObject * object);
static void gst_rtpstatmaker2_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtpstatmaker2_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_rtpstatmaker2_sink_event (GstBaseTransform * trans,
    GstEvent * event);
static GstFlowReturn gst_rtpstatmaker2_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);
static gboolean gst_rtpstatmaker2_start (GstBaseTransform * trans);
static gboolean gst_rtpstatmaker2_stop (GstBaseTransform * trans);
static GstStateChangeReturn gst_rtpstatmaker2_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_rtpstatmaker2_accept_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps);
static gboolean gst_rtpstatmaker2_query (GstBaseTransform * base,
    GstPadDirection direction, GstQuery * query);

static void
gst_rtpstatmaker2_finalize (GObject * object)
{
  GstRTPStatMaker2 *this;

  this = GST_RTPSTATMAKER2 (object);


  g_free (this->last_message);
  g_cond_clear (&this->blocked_cond);

  g_object_unref (this->sysclock);
  g_free(this->tmp_packet);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_rtpstatmaker2_class_init (GstRTPStatMaker2Class * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *gstbasetrans_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbasetrans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_rtpstatmaker2_set_property;
  gobject_class->get_property = gst_rtpstatmaker2_get_property;

  g_object_class_install_property (gobject_class, PROP_SYNC,
      g_param_spec_boolean ("sync", "Synchronize",
          "Synchronize to pipeline clock", DEFAULT_SYNC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gobject_class->finalize = gst_rtpstatmaker2_finalize;

  gst_element_class_set_static_metadata (gstelement_class,
      "RTPStatMaker2",
      "Generic",
      "Collect information about RTP packets goes through the element", "Balazs Kreith <balazs.kreith@gmail.com>");
  gst_element_class_add_static_pad_template (gstelement_class, &srctemplate);
  gst_element_class_add_static_pad_template (gstelement_class, &sinktemplate);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtpstatmaker2_change_state);

  gstbasetrans_class->sink_event = GST_DEBUG_FUNCPTR (gst_rtpstatmaker2_sink_event);
  gstbasetrans_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_rtpstatmaker2_transform_ip);
  gstbasetrans_class->start = GST_DEBUG_FUNCPTR (gst_rtpstatmaker2_start);
  gstbasetrans_class->stop = GST_DEBUG_FUNCPTR (gst_rtpstatmaker2_stop);
  gstbasetrans_class->accept_caps =
      GST_DEBUG_FUNCPTR (gst_rtpstatmaker2_accept_caps);
  gstbasetrans_class->query = gst_rtpstatmaker2_query;


  g_object_class_install_property (gobject_class, PROP_DEFAULT_MKFIFO_LOCATION,
      g_param_spec_string ("default-logfile-location",
            "Logfile location if no logger is matched",
            "Logfile location if no logger is matched",
            "NULL", G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS)
  );


  g_object_class_install_property (gobject_class, PROP_MPRTP_EXT_HEADER_ID,
      g_param_spec_uint ("mprtp-ext-header-id",
               "Setup the mprtp extension header id",
               "Setup the mprtp extension header id",
                0, 16, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS)
  );

  g_object_class_install_property (gobject_class, PROP_FEC_PAYLOAD_TYPE,
      g_param_spec_uint ("fec-payload-type",
               "Setup the fec payload type",
               "Setup the fec payload type",
                0, 128, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS)
  );

}

static void
gst_rtpstatmaker2_init (GstRTPStatMaker2 * this)
{
  this->sync = DEFAULT_SYNC;
  this->last_message = NULL;

//  init_mprtp_logger();
//  mprtp_logger_set_state(TRUE);

  g_cond_init (&this->blocked_cond);

  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM_CAST (this), TRUE);

  this->sysclock = gst_system_clock_obtain ();
  this->tmp_packet = g_malloc0(sizeof(RTPStatPacket));
  this->fec_payload_type = FEC_PAYLOAD_DEFAULT_ID;
}



static void
gst_rtpstatmaker2_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRTPStatMaker2 *this;

  this = GST_RTPSTATMAKER2 (object);

  switch (prop_id) {
    case PROP_SYNC:
      this->sync = g_value_get_boolean (value);
      break;
    case PROP_MPRTP_EXT_HEADER_ID:
      this->mprtp_ext_header_id = g_value_get_uint(value);
      break;
    case PROP_FEC_PAYLOAD_TYPE:
      this->fec_payload_type = g_value_get_uint(value);
      break;
    case PROP_DEFAULT_MKFIFO_LOCATION:
      {
        const gchar *path = g_value_get_string(value);
        const gchar *sock_path = path;
        gchar *next;
        while ((next = strpbrk(sock_path + 1, "\\/"))) sock_path = next;
        if (path != sock_path) ++sock_path;

        this->fifofd =
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (this), TRUE);

}

static void
gst_rtpstatmaker2_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstRTPStatMaker2 *rtpstatmaker2;

  rtpstatmaker2 = GST_RTPSTATMAKER2 (object);

  switch (prop_id) {
    case PROP_SYNC:
      g_value_set_boolean (value, rtpstatmaker2->sync);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_rtpstatmaker2_do_sync (GstRTPStatMaker2 * this, GstClockTime running_time)
{
  GstFlowReturn ret = GST_FLOW_OK;

  if (this->sync &&
      GST_BASE_TRANSFORM_CAST (this)->segment.format == GST_FORMAT_TIME) {
    GstClock *clock;

    GST_OBJECT_LOCK (this);

    while (this->blocked)
      g_cond_wait (&this->blocked_cond, GST_OBJECT_GET_LOCK (this));


    if ((clock = GST_ELEMENT (this)->clock)) {
      GstClockReturn cret;
      GstClockTime timestamp;

      timestamp = running_time + GST_ELEMENT (this)->base_time +
          this->upstream_latency;

      /* save id if we need to unlock */
      this->clock_id = gst_clock_new_single_shot_id (clock, timestamp);
      GST_OBJECT_UNLOCK (this);

      cret = gst_clock_id_wait (this->clock_id, NULL);

      GST_OBJECT_LOCK (this);
      if (this->clock_id) {
        gst_clock_id_unref (this->clock_id);
        this->clock_id = NULL;
      }
      if (cret == GST_CLOCK_UNSCHEDULED)
        ret = GST_FLOW_EOS;
    }
    GST_OBJECT_UNLOCK (this);
  }

  return ret;
}

static gboolean
gst_rtpstatmaker2_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstRTPStatMaker2* this = (GstRTPStatMaker2*) trans;
  switch (GST_EVENT_TYPE(event)) {
    case GST_EVENT_STREAM_START:
    case GST_EVENT_SEGMENT:
      gst_event_ref(event);
      break;
    default:
      break;
  }
  DISABLE_LINE g_print("%d", this->mprtp_ext_header_id);
  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
}


static void _init_packet(GstRTPStatMaker2 *this, RTPStatPacket* packet, GstBuffer* buf){
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;

  gst_rtp_buffer_map(buf, GST_MAP_READ, &rtp);

  packet->timestamp    = gst_rtp_buffer_get_timestamp(&rtp);
  packet->payload_size = gst_rtp_buffer_get_payload_len(&rtp);
  packet->payload_type = gst_rtp_buffer_get_payload_type(&rtp);
  packet->header_size  = gst_rtp_buffer_get_header_len(&rtp);
  packet->marker       = gst_rtp_buffer_get_marker(&rtp);
  packet->ssrc         = gst_rtp_buffer_get_ssrc(&rtp);
  packet->seq_num      = gst_rtp_buffer_get_seq(&rtp);
  packet->tracked_ntp  = NTP_NOW;



  if(0 < this->mprtp_ext_header_id){
    gst_rtp_buffer_get_mprtp_extension(&rtp, this->mprtp_ext_header_id, &packet->subflow_id, &packet->subflow_seq);
  }

  if(0 < this->fec_payload_type && packet->payload_type == this->fec_payload_type){
    GstRTPFECHeader fec_header;
    memcpy(&fec_header, gst_rtp_buffer_get_payload(&rtp), sizeof(GstRTPFECHeader));
    packet->protect_begin = g_ntohs(fec_header.sn_base);
    packet->protect_end = g_ntohs(fec_header.sn_base) + fec_header.N_MASK - 1;
  }else{
    packet->protect_begin = packet->protect_end = 0;
  }

  gst_rtp_buffer_unmap(&rtp);
}

static void
_monitor_rtp_packet (GstRTPStatMaker2 *this, GstBuffer * buffer)
{
  guint8 first_byte;
  guint8 second_byte;

  if (!GST_IS_BUFFER(buffer)) {
    goto exit;
  }

  if (gst_buffer_extract (buffer, 0, &first_byte, 1) != 1 ||
      gst_buffer_extract (buffer, 1, &second_byte, 1) != 1) {
    GST_WARNING_OBJECT (this, "could not extract first byte from buffer");
    goto exit;
  }

  if (!PACKET_IS_RTP_OR_RTCP (first_byte)) {
    GST_DEBUG_OBJECT (this, "Not RTP Packet arrived at sink");
    goto exit;
  }

  if(PACKET_IS_RTCP(second_byte)){
    GST_DEBUG_OBJECT (this, "RTCP Packet arrived on rtp sink");
    goto exit;
  }


  _init_packet(this, (RTPStatPacket*)this->tmp_packet, buffer);
PROFILING("_socket_writer_sendto",
  if (this->socket_writer) {
    _socket_writer_sendto(this->socket_writer, this->tmp_packet, sizeof(RTPStatPacket));
  }
);
//);
exit:
  return;
}


//static int received = 1;
static GstFlowReturn
gst_rtpstatmaker2_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstRTPStatMaker2 *this = GST_RTPSTATMAKER2 (trans);
  GstClockTime rundts = GST_CLOCK_TIME_NONE;
  GstClockTime runpts = GST_CLOCK_TIME_NONE;
  GstClockTime runtimestamp;
  gsize size;
  PROFILING("gstrtpstatmaker",
  size = gst_buffer_get_size (buf);

  //artifical lost
//    if(++received % 11 == 0){
//      gst_buffer_unref(buf);
//      return ret;
//    }

  /* update prev values */
  this->prev_timestamp = GST_BUFFER_TIMESTAMP (buf);
  this->prev_duration = GST_BUFFER_DURATION (buf);
  this->prev_offset_end = GST_BUFFER_OFFSET_END (buf);
  this->prev_offset = GST_BUFFER_OFFSET (buf);

  if (trans->segment.format == GST_FORMAT_TIME) {
    rundts = gst_segment_to_running_time (&trans->segment,
        GST_FORMAT_TIME, GST_BUFFER_DTS (buf));
    runpts = gst_segment_to_running_time (&trans->segment,
        GST_FORMAT_TIME, GST_BUFFER_PTS (buf));
  }

  if (GST_CLOCK_TIME_IS_VALID (rundts))
    runtimestamp = rundts;
  else if (GST_CLOCK_TIME_IS_VALID (runpts))
    runtimestamp = runpts;
  else
    runtimestamp = 0;
  ret = gst_rtpstatmaker2_do_sync (this, runtimestamp);

  this->offset += size;

  _monitor_rtp_packet(this, buf);
);
  return ret;

}




static gboolean
gst_rtpstatmaker2_start (GstBaseTransform * trans)
{
  GstRTPStatMaker2 *this;

  this = GST_RTPSTATMAKER2 (trans);

  this->offset = 0;
  this->prev_timestamp = GST_CLOCK_TIME_NONE;
  this->prev_duration = GST_CLOCK_TIME_NONE;
  this->prev_offset_end = GST_BUFFER_OFFSET_NONE;
  this->prev_offset = GST_BUFFER_OFFSET_NONE;

  return TRUE;
}

static gboolean
gst_rtpstatmaker2_stop (GstBaseTransform * trans)
{
  GstRTPStatMaker2 *this;

  this = GST_RTPSTATMAKER2 (trans);

  GST_OBJECT_LOCK (this);
  g_free (this->last_message);
  this->last_message = NULL;
  GST_OBJECT_UNLOCK (this);

  return TRUE;
}

static gboolean
gst_rtpstatmaker2_accept_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps)
{
  gboolean ret;
  GstPad *pad;

  /* Proxy accept-caps */

  if (direction == GST_PAD_SRC)
    pad = GST_BASE_TRANSFORM_SINK_PAD (base);
  else
    pad = GST_BASE_TRANSFORM_SRC_PAD (base);

  ret = gst_pad_peer_query_accept_caps (pad, caps);

  return ret;
}

static gboolean
gst_rtpstatmaker2_query (GstBaseTransform * base, GstPadDirection direction,
    GstQuery * query)
{
  GstRTPStatMaker2 *rtpstatmaker2;
  gboolean ret;

  rtpstatmaker2 = GST_RTPSTATMAKER2 (base);

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->query (base, direction, query);

  if (GST_QUERY_TYPE (query) == GST_QUERY_LATENCY) {
    gboolean live = FALSE;
    GstClockTime min = 0, max = 0;

    if (ret) {
      gst_query_parse_latency (query, &live, &min, &max);

      if (rtpstatmaker2->sync && max < min) {
        GST_ELEMENT_WARNING (base, CORE, CLOCK, (NULL),
            ("Impossible to configure latency before rtpstatmaker2 sync=true:"
                " max %" GST_TIME_FORMAT " < min %"
                GST_TIME_FORMAT ". Add queues or other buffering elements.",
                GST_TIME_ARGS (max), GST_TIME_ARGS (min)));
      }
    }

    /* Ignore the upstream latency if it is not live */
    GST_OBJECT_LOCK (rtpstatmaker2);
    if (live)
      rtpstatmaker2->upstream_latency = min;
    else
      rtpstatmaker2->upstream_latency = 0;
    GST_OBJECT_UNLOCK (rtpstatmaker2);

    gst_query_set_latency (query, live || rtpstatmaker2->sync, min, max);
    ret = TRUE;
  }
  return ret;
}

static GstStateChangeReturn
gst_rtpstatmaker2_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRTPStatMaker2 *this = GST_RTPSTATMAKER2 (element);
  gboolean no_preroll = FALSE;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_OBJECT_LOCK (this);
      this->blocked = TRUE;
      GST_OBJECT_UNLOCK (this);
      if (this->sync)
        no_preroll = TRUE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      GST_OBJECT_LOCK (this);
      this->blocked = FALSE;
      g_cond_broadcast (&this->blocked_cond);

      GST_OBJECT_UNLOCK (this);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_OBJECT_LOCK (this);
      if (this->clock_id) {
        GST_DEBUG_OBJECT (this, "unlock clock wait");
        gst_clock_id_unschedule (this->clock_id);
      }
      this->blocked = FALSE;
      g_cond_broadcast (&this->blocked_cond);

      GST_OBJECT_UNLOCK (this);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      GST_OBJECT_LOCK (this);

      this->upstream_latency = 0;
      this->blocked = TRUE;
      GST_OBJECT_UNLOCK (this);
      if (this->sync)
        no_preroll = TRUE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  if (no_preroll && ret == GST_STATE_CHANGE_SUCCESS)
    ret = GST_STATE_CHANGE_NO_PREROLL;

  return ret;
}



