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
  PROP_FEC_PAYLOAD_TYPE,
  PROP_SAMPLING_TIME,
  PROP_ACCUMULATION_LENGTH,
  PROP_MPRTP_EXT_HEADER_ID,
  PROP_CSV_LOGGING,
  PROP_TOUCHED_SYNC_LOCATION,
  PROP_PACKETSLOG_LOCATION,
  PROP_STATSLOG_LOCATION,

};

static GstStaticPadTemplate gst_rtpstatmaker_packet_src_template =
GST_STATIC_PAD_TEMPLATE ("packet_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_rtpstatmaker_packet_sink_template =
GST_STATIC_PAD_TEMPLATE ("packet_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);


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
_monitorstat_emitter (GstRTPStatMaker2 *this);

static void
gst_rtpstatmaker2_finalize (GObject * object)
{
  GstRTPStatMaker2 *this;

  this = GST_RTPSTATMAKER2 (object);


  g_free (this->last_message);
  g_cond_clear (&this->blocked_cond);
  g_cond_clear (&this->waiting_signal);

  g_queue_free_full(this->packetlogs2write,     g_free);
  g_queue_free_full(this->packetlogstr2recycle, g_free);

  g_object_unref (this->sysclock);
  g_object_unref (this->monitor);

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

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtpstatmaker_packet_src_template));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtpstatmaker_packet_sink_template));

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

  g_object_class_install_property (gobject_class, PROP_FEC_PAYLOAD_TYPE,
      g_param_spec_uint ("fec-payload-type",
          "Distinguish FEC packets",
          "Set or get the payload type of FEC packets.",
          0, 127, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SAMPLING_TIME,
      g_param_spec_uint ("sampling-time",
          "Sampling-time",
          "Set the sampling time in ms.",
          0, 100000000, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ACCUMULATION_LENGTH,
      g_param_spec_uint ("accumulation-length",
          "Accumulation-length",
          "The time window for accumulative statistics.",
          0, 1000000000, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));


  g_object_class_install_property (gobject_class, PROP_MPRTP_EXT_HEADER_ID,
      g_param_spec_uint ("mprtp-ext-header-id",
          "MPRTP Header Extesion ID",
          "The iID for the MPRTP header extension.",
          0, 15, MPRTP_DEFAULT_EXTENSION_HEADER_ID, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CSV_LOGGING,
      g_param_spec_boolean ("csv-logging",
          "CSV Logging",
          "The log format is csv",
          TRUE, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TOUCHED_SYNC_LOCATION,
      g_param_spec_string ("touch-sync-location",
            "Simple IPC syncronization",
            "Trigger the collection by a file existance.",
            "NULL", G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));


  g_object_class_install_property (gobject_class, PROP_PACKETSLOG_LOCATION,
      g_param_spec_string ("packetslog-location",
            "Packetslog file",
            "The location of the log for packet dump log.",
            "NULL", G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_STATSLOG_LOCATION,
      g_param_spec_string ("statslog-location",
            "Statlog file",
            "The location of the log for accumulative stat. file.",
            "NULL", G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));


}

static GstFlowReturn
gst_rtpstatmaker_packet_sink_chain (GstPad *pad, GstObject *parent, GstBuffer *buf)
{
  GstRTPStatMaker2 *this;
  GstFlowReturn result;

  this = GST_RTPSTATMAKER2 (parent);

  THIS_LOCK(this);

  monitor_track_packetbuffer(this->monitor, buf);

  result = GST_FLOW_OK;
  THIS_UNLOCK(this);

  return result;

}

static GstBufferPool *
_create_pool (guint size, guint min_buf, guint max_buf)
{
  GstBufferPool *pool = gst_buffer_pool_new ();
  GstStructure *conf = gst_buffer_pool_get_config (pool);
  GstCaps *caps = gst_caps_new_empty_simple ("ANY");

  gst_buffer_pool_config_set_params (conf, caps, size, min_buf, max_buf);
  gst_buffer_pool_set_config (pool, conf);
  gst_caps_unref (caps);

  return pool;
}

static void
gst_rtpstatmaker2_init (GstRTPStatMaker2 * this)
{
  this->sync = DEFAULT_SYNC;
  this->last_message = NULL;

//  init_mprtp_logger();
//  mprtp_logger_set_state(TRUE);

  g_mutex_init(&this->mutex);
  g_cond_init (&this->blocked_cond);
  g_cond_init (&this->waiting_signal);

  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM_CAST (this), TRUE);


  this->packet_sinkpad =
      gst_pad_new_from_static_template
      (&gst_rtpstatmaker_packet_sink_template, "packet_sink");

  gst_pad_set_chain_function (this->packet_sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtpstatmaker_packet_sink_chain));

  gst_element_add_pad (GST_ELEMENT (this), this->packet_sinkpad);

  this->packet_srcpad =
      gst_pad_new_from_static_template
      (&gst_rtpstatmaker_packet_src_template, "packet_src");

  gst_element_add_pad (GST_ELEMENT (this), this->packet_srcpad);


  this->monitor = make_monitor();

  g_rec_mutex_init (&this->thread_mutex);
  this->thread = gst_task_new ((GstTaskFunction) _monitorstat_emitter, this, NULL);

  this->packetlogs2write     = g_queue_new();
  this->packetlogstr2recycle = g_queue_new();

  this->sysclock = gst_system_clock_obtain ();
  this->packetbufferpool = _create_pool(1024, 5, 0);
  gst_buffer_pool_set_active (this->packetbufferpool, TRUE);
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
      gst_pad_push_event(this->packet_srcpad, event);
      break;
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
}

static void
_monitor_rtp_packet (GstRTPStatMaker2 *this, GstBuffer * buffer)
{
  guint8 first_byte;
  guint8 second_byte;
  MonitorPacket* packet = NULL;
  GstBuffer* packetbuffer = NULL;
  gboolean packetsrc_linked = FALSE;

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

  THIS_LOCK(this);
  if(this->touched_sync_active){
    if(!g_file_test(this->touched_sync_location, G_FILE_TEST_EXISTS)){
      goto done;
    }
    this->touched_sync_active = FALSE;
  }


  monitor_track_rtpbuffer(this->monitor, buffer);

again:
  packet = monitor_pop_prepared_packet(this->monitor);
  if(!packet){
    goto done;
  }

  packetsrc_linked = gst_pad_is_linked(this->packet_srcpad);
//  packetsrc_linked = FALSE;
  if(!packetsrc_linked && !this->packetlogs_linked){
    goto done;
  }

  if(packetsrc_linked){
    //TODO: 1
    gst_buffer_pool_acquire_buffer (this->packetbufferpool, &packetbuffer, NULL);
//    packetbuffer    = gst_buffer_new_wrapped(g_malloc0(1024), 1024);
    monitor_setup_packetbufffer(packet, packetbuffer);
    gst_pad_push(this->packet_srcpad, packetbuffer);
    packetbuffer = NULL;
  }

  if(this->packetlogs_linked){
    if(this->csv_logging){
      gchar* str;
      if(g_queue_is_empty(this->packetlogstr2recycle)){
        str = g_malloc(2048);
      }else{
        str = g_queue_pop_head(this->packetlogstr2recycle);
      }
      memset(str, 0, 2048);

      //mprtp_logger(this->packetslog_file,
      sprintf(str,
          "%u,%hu,%d,%lu,%d,%u,%u,%u,%d,%lu\n",
          packet->extended_seq,
          packet->tracked_seq,
          packet->state,
          packet->tracked_ntp,
          packet->marker,
          packet->header_size,
          packet->payload_size,
          packet->timestamp,
          packet->payload_type,
          packet->played_out
      );
      g_queue_push_tail(this->packetlogs2write, str);

//      gst_buffer_map(buffer, &map, GST_MAP_READWRITE);
//      memcpy(map.data, csv, 1024);
//      gst_buffer_unmap(buffer, &map);
    }else{
//      monitor_setup_packetbufffer(packet, packetbuffer);
    }
//    gst_pad_push(this->packetlogs_srcpad, packetbuffer);
  }
  goto again;
done:
  THIS_UNLOCK(this);
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

  return ret;

}

static void
gst_rtpstatmaker2_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRTPStatMaker2 *this;

  this = GST_RTPSTATMAKER2 (object);

  switch (prop_id) {
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
      this->sampling_time = (GstClockTime) g_value_get_uint (value);
      break;
    case PROP_CSV_LOGGING:
      this->csv_logging = g_value_get_boolean (value);
      break;
    case PROP_TOUCHED_SYNC_LOCATION:
      this->touched_sync_active = TRUE;
      strcpy(this->touched_sync_location, g_value_get_string(value));
      break;
    case PROP_PACKETSLOG_LOCATION:
      this->packetlogs_linked = TRUE;
      strcpy(this->packetslog_file, g_value_get_string(value));
      break;
    case PROP_STATSLOG_LOCATION:
      this->statlogs_linked = TRUE;
      strcpy(this->statslog_file, g_value_get_string(value));
//      _start_staslog_thread(this);
      break;

    case PROP_SYNC:
      this->sync = g_value_get_boolean (value);
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
    case PROP_MPRTP_EXT_HEADER_ID:
      g_value_set_uint (value, rtpstatmaker2->mprtp_ext_header_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
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

      gst_task_set_lock(this->thread, &this->thread_mutex);
      gst_task_start(this->thread);

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

      gst_task_join (this->thread);
      gst_object_unref (this->thread);

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

//
//void
//_monitorstat_emitter (GstRTPStatMaker2 *this)
//{
//  Monitor* monitor;
//  MonitorStat* stat;
////  GstBuffer* buffer = NULL;
//  gint64 end_time;
//
//  THIS_LOCK(this);
//
//  end_time = g_get_monotonic_time() + this->sampling_time * 1000; //because sampling_time is in ms.
//
//  g_cond_wait_until(&this->waiting_signal, &this->mutex, end_time);
//  if(!this->statlogs_linked){
//    goto done;
//  }
//
//  if(this->touched_sync_active){
//    if(!g_file_test(this->touched_sync_location, G_FILE_TEST_EXISTS)){
//      goto done;
//    }
//    this->touched_sync_active = FALSE;
//  }
//
////  buffer = gst_buffer_new_wrapped(g_malloc0(length), length);
//  monitor = this->monitor;
//  stat = &monitor->stat;
//
//  if(this->csv_logging){
//    mprtp_logger(this->statslog_file,
//        "%d,%d,%d,%d," //received
//        "%d,%d,%d,%d," //lost
//        "%d,%d,%d,%d," //discarded
//        "%d,%d,%d,%d," //corrupted
//        "%d,%d,%d,%d," //repaired
//        "%d,%d,%d,%d\n"  //fec
//        ,
//        stat->received.total_packets,
//        stat->received.total_bytes,
//        stat->received.accumulative_bytes,
//        stat->received.accumulative_packets,
//
//        stat->lost.total_packets,
//        stat->lost.total_bytes,
//        stat->lost.accumulative_bytes,
//        stat->lost.accumulative_packets,
//
//        stat->discarded.total_packets,
//        stat->discarded.total_bytes,
//        stat->discarded.accumulative_bytes,
//        stat->discarded.accumulative_packets,
//
//        stat->corrupted.total_packets,
//        stat->corrupted.total_bytes,
//        stat->corrupted.accumulative_bytes,
//        stat->corrupted.accumulative_packets,
//
//        stat->repaired.total_packets,
//        stat->repaired.total_bytes,
//        stat->repaired.accumulative_bytes,
//        stat->repaired.accumulative_packets,
//
//        stat->fec.total_packets,
//        stat->fec.total_bytes,
//        stat->fec.accumulative_bytes,
//        stat->fec.accumulative_packets
//      );
//
////    gst_buffer_map(buffer, &map, GST_MAP_READWRITE);
////    memcpy(map.data, csv, length);
////    gst_buffer_unmap(buffer, &map);
//
//  }else{
////    monitor_setup_monitorstatbufffer(this->monitor, buffer);
//  }
//
////  gst_pad_push(this->statlogs_srcpad, buffer);
//
//done:
//  THIS_UNLOCK(this);
//  return;
//}



void
_monitorstat_emitter (GstRTPStatMaker2 *this)
{
  Monitor* monitor;
  MonitorStat* stat;
  gint64 end_time;

  THIS_LOCK(this);

  //end_time = g_get_monotonic_time() + this->sampling_time * 1000; //because sampling_time is in ms.
  end_time = g_get_monotonic_time() + 10000; //bsampling in each 10 ms

  g_cond_wait_until(&this->waiting_signal, &this->mutex, end_time);
  if(!g_queue_is_empty(this->packetlogs2write)){
    FILE* fp = fopen(this->packetslog_file, this->last_packetlog ? "a" : "w");
    while(!g_queue_is_empty(this->packetlogs2write)){
        gchar* str = g_queue_pop_head(this->packetlogs2write);
        fprintf(fp, "%s", str);
        g_queue_push_tail(this->packetlogstr2recycle, str);
    }
    fclose(fp);
    this->last_packetlog = _now(this);
  }


  if(!this->statlogs_linked){
    goto done;
  }
  if(0 < this->last_statlog && _now(this) - this->sampling_time * GST_MSECOND < this->last_statlog ){
    goto done;
  }


  if(this->touched_sync_active){
    if(!g_file_test(this->touched_sync_location, G_FILE_TEST_EXISTS)){
      goto done;
    }
    this->touched_sync_active = FALSE;
  }

//  buffer = gst_buffer_new_wrapped(g_malloc0(length), length);
  monitor = this->monitor;
  stat = &monitor->stat;

  if(this->csv_logging){
    FILE* fp = fopen(this->statslog_file, this->last_statlog ? "a" : "w");
    //mprtp_logger(this->statslog_file,
    fprintf(fp,
        "%d,%d,%d,%d," //received
        "%d,%d,%d,%d," //lost
        "%d,%d,%d,%d," //discarded
        "%d,%d,%d,%d," //corrupted
        "%d,%d,%d,%d," //repaired
        "%d,%d,%d,%d\n"  //fec
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

    fclose(fp);

  }else{
//    monitor_setup_monitorstatbufffer(this->monitor, buffer);
  }

//  gst_pad_push(this->statlogs_srcpad, buffer);
  this->last_statlog = _now(this);

done:
  THIS_UNLOCK(this);
  return;
}

