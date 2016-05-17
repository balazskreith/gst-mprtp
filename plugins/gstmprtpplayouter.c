/* GStreamer
 * Copyright (C) 2015 FIXME <fixme@example.com>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstmprtpplayouter
 *
 * The mprtpplayouter element does FIXME stuff.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v fakesrc ! mprtpplayouter ! FIXME ! fakesink
 * ]|
 * FIXME Describe what the pipeline does.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif



#include <gst/gst.h>
#include <gst/gst.h>
#include <string.h>
#include "gstmprtpplayouter.h"
#include "gstmprtcpbuffer.h"
#include "mprtprpath.h"
#include "mprtpspath.h"
#include "streamjoiner.h"
#include "gstmprtpbuffer.h"
#include "mprtplogger.h"


#include "rcvctrler.h"

typedef struct _SubflowSpecProp{
  #if G_BYTE_ORDER == G_LITTLE_ENDIAN
    guint32  value : 24;
    guint32  id     : 8;
  #elif G_BYTE_ORDER == G_BIG_ENDIAN
    guint32  id     : 8;
    guint32  value : 24;
  #else
  #error "G_BYTE_ORDER should be big or little endian."
  #endif
  }SubflowSpecProp;

GST_DEBUG_CATEGORY_STATIC (gst_mprtpplayouter_debug_category);
#define GST_CAT_DEFAULT gst_mprtpplayouter_debug_category

#define THIS_READLOCK(this) g_rw_lock_reader_lock(&this->rwmutex)
#define THIS_READUNLOCK(this) g_rw_lock_reader_unlock(&this->rwmutex)
#define THIS_WRITELOCK(this) g_rw_lock_writer_lock(&this->rwmutex)
#define THIS_WRITEUNLOCK(this) g_rw_lock_writer_unlock(&this->rwmutex)

#define MPRTP_PLAYOUTER_DEFAULT_SSRC 0
#define MPRTP_PLAYOUTER_DEFAULT_CLOCKRATE 90000


static void gst_mprtpplayouter_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_mprtpplayouter_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_mprtpplayouter_dispose (GObject * object);
static void gst_mprtpplayouter_finalize (GObject * object);

static GstStateChangeReturn
gst_mprtpplayouter_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_mprtpplayouter_query (GstElement * element,
    GstQuery * query);
static GstFlowReturn gst_mprtpplayouter_mprtp_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static GstFlowReturn gst_mprtpplayouter_mprtcp_sr_sink_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static gboolean gst_mprtpplayouter_sink_query (GstPad * sinkpad,
    GstObject * parent, GstQuery * query);
static gboolean gst_mprtpplayouter_src_query (GstPad * sinkpad,
    GstObject * parent, GstQuery * query);
static gboolean gst_mprtpplayouter_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static void
gst_mprtpplayouter_mprtcp_sender (gpointer ptr, GstBuffer * buf);

static void _processing_mprtp_packet (GstMprtpplayouter * mprtpr,
    GstBuffer * buf);
static GstFlowReturn _processing_mprtcp_packet (GstMprtpplayouter * this,
    GstBuffer * buf);
static void _join_path (GstMprtpplayouter * this, guint8 subflow_id);
static void _detach_path (GstMprtpplayouter * this, guint8 subflow_id);
static gboolean _try_get_path (GstMprtpplayouter * this, guint16 subflow_id,
    MpRTPRPath ** result);
static void
_setup_paths_tresholds(
    GstMprtpplayouter * this,
    guint8 subflow_id,
    void (*cb)(MpRTPRPath*,GstClockTime),
    GstClockTime treshold);

static GstMpRTPBuffer *_make_mprtp_buffer(GstMprtpplayouter * this, GstBuffer *buffer);
#define _trash_mprtp_buffer(this, mprtp) mprtp_free(mprtp)

#define _now(this) gst_clock_get_time (this->sysclock)

static void
_mprtpplayouter_process_run (void *data);

enum
{
  PROP_0,
  PROP_MPRTP_EXT_HEADER_ID,
  PROP_ABS_TIME_EXT_HEADER_ID,
  PROP_FEC_PAYLOAD_TYPE,
  PROP_PIVOT_SSRC,
  PROP_JOIN_SUBFLOW,
  PROP_DETACH_SUBFLOW,
  PROP_PIVOT_CLOCK_RATE,
  PROP_SETUP_CONTROLLING_MODE,
  PROP_SETUP_RTCP_INTERVAL_TYPE,
  PROP_RTP_PASSTHROUGH,
  PROP_LOG_ENABLED,
  PROP_LOG_PATH,
  PROP_OWD_WINDOW_TRESHOLD,
  PROP_JOIN_MIN_TRESHOLD,
  PROP_JOIN_MAX_TRESHOLD,
  PROP_JOIN_WINDOW_TRESHOLD,
  PROP_JOIN_BETHA_FACTOR,
  PROP_PLAYOUT_MAX_RATE,
  PROP_PLAYOUT_MIN_RATE,
  PROP_PLAYOUT_DESIRED_FRAMENUM,
  PROP_PLAYOUT_SPREAD_FACTOR,
  PROP_SPIKE_DELAY_TRESHOLD,
  PROP_SPIKE_VAR_TRESHOLD,
  PROP_REPAIR_WINDOW_MIN,
  PROP_REPAIR_WINDOW_MAX,

};

/* pad templates */

static GstStaticPadTemplate gst_mprtpplayouter_mprtp_sink_template =
    GST_STATIC_PAD_TEMPLATE ("mprtp_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp;application/x-rtcp;application/x-srtcp")
    );


static GstStaticPadTemplate gst_mprtpplayouter_mprtcp_sr_sink_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_sr_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

static GstStaticPadTemplate gst_mprtpplayouter_mprtp_src_template =
GST_STATIC_PAD_TEMPLATE ("mprtp_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );


static GstStaticPadTemplate gst_mprtpplayouter_mprtcp_rr_src_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_rr_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtcp")
    );

/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstMprtpplayouter, gst_mprtpplayouter,
    GST_TYPE_ELEMENT,
    GST_DEBUG_CATEGORY_INIT (gst_mprtpplayouter_debug_category,
        "mprtpplayouter", 0, "debug category for mprtpplayouter element"));

static void
gst_mprtpplayouter_class_init (GstMprtpplayouterClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpplayouter_mprtp_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get
      (&gst_mprtpplayouter_mprtcp_sr_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpplayouter_mprtp_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mprtpplayouter_mprtcp_rr_src_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "MPRTP Playouter", "Generic",
      "MPRTP Playouter FIXME", "Balázs Kreith <balazs.kreith@gmail.com>");

  gobject_class->set_property = gst_mprtpplayouter_set_property;
  gobject_class->get_property = gst_mprtpplayouter_get_property;
  gobject_class->dispose = gst_mprtpplayouter_dispose;
  gobject_class->finalize = gst_mprtpplayouter_finalize;

  g_object_class_install_property (gobject_class, PROP_PIVOT_CLOCK_RATE,
      g_param_spec_uint ("pivot-clock-rate", "Clock rate of the pivot stream",
          "Sets the clock rate of the pivot stream used for calculating "
          "skew and playout delay at the receiver", 0,
          G_MAXUINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PIVOT_SSRC,
      g_param_spec_uint ("pivot-ssrc", "SSRC of the pivot stream",
          "Sets the ssrc of the pivot stream used selecting MPRTP packets "
          "for playout delay at the receiver", 0,
          G_MAXUINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MPRTP_EXT_HEADER_ID,
      g_param_spec_uint ("mprtp-ext-header-id",
          "Set or get the id for the RTP extension",
          "Sets or gets the id for the extension header the MpRTP based on. The default is 3",
          0, 15, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_REPAIR_WINDOW_MIN,
      g_param_spec_uint ("fec-repair-window-min",
          "Set the repair window minimum treshold for FEC recovery",
          "Set the repair window minimum treshold for FEC recovery",
          0, 10000, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_REPAIR_WINDOW_MAX,
      g_param_spec_uint ("fec-repair-window-max",
          "Set the repair window maximum treshold for FEC recovery",
          "Set the repair window maximum treshold for FEC recovery",
          0, 10000, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ABS_TIME_EXT_HEADER_ID,
      g_param_spec_uint ("abs-time-ext-header-id",
          "Set or get the id for the absolute time RTP extension",
          "Sets or gets the id for the extension header the absolute time based on. The default is 8",
          0, 15, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FEC_PAYLOAD_TYPE,
      g_param_spec_uint ("fec-payload-type",
          "Set or get the payload type of fec packets",
          "Set or get the payload type of fec packets. The default is 126",
          0, 127, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_JOIN_SUBFLOW,
      g_param_spec_uint ("join-subflow", "the subflow id requested to join",
          "Join a subflow with a given id.", 0,
          MPRTP_PLUGIN_MAX_SUBFLOW_NUM, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DETACH_SUBFLOW,
       g_param_spec_uint ("detach-subflow", "the subflow id requested to detach",
           "Detach a subflow with a given id.", 0,
           MPRTP_PLUGIN_MAX_SUBFLOW_NUM, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RTP_PASSTHROUGH,
      g_param_spec_boolean ("rtp-passthrough",
          "Indicate the passthrough mode on no active subflow case",
          "Indicate weather the schdeuler let the packets travel "
          "through the element if it hasn't any active subflow.",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LOG_ENABLED,
      g_param_spec_boolean ("logging",
          "Indicate weather a log for subflow is enabled or not",
          "Indicate weather a log for subflow is enabled or not",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LOG_PATH,
        g_param_spec_string ("logs-path",
            "Determines the path for logfiles",
            "Determines the path for logfiles",
            "NULL", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_OWD_WINDOW_TRESHOLD,
       g_param_spec_uint ("owd-window-treshold",
                          "set the window tresholds for owd sampling on subflows.",
                          "A 32bit unsigned integer upper 8bit in host order are used to identify the subflow. "
                          "If the value is 255 then the option will be applied on all subflow. The latter 24bits are identified as a value in ms",
                          0,
                          4294967295, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_JOIN_MIN_TRESHOLD,
         g_param_spec_uint ("join-min-treshold",
                            "set the minimum treshold for streamjoiner in ms.",
                            "set the minimum treshold for streamjoiner in ms.",
                            0,
                            4294967295, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_JOIN_MAX_TRESHOLD,
         g_param_spec_uint ("join-max-treshold",
                            "set the maximum treshold for streamjoiner in ms.",
                            "set the maxumum treshold for streamjoiner in ms.",
                            0,
                            4294967295, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_JOIN_WINDOW_TRESHOLD,
         g_param_spec_uint ("join-window-treshold",
                            "set the window treshold for streamjoiner in seconds.",
                            "set the window treshold for streamjoiner in seconds.",
                            0,
                            4294967295, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_JOIN_BETHA_FACTOR,
          g_param_spec_double ("join-betha-factor",
                               "set the betha factor for streamjoiner.",
                               "set the betha factor for streamjoiner.",
                               0.0, 10.0, 1.,
                               G_PARAM_WRITABLE  | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject_class, PROP_PLAYOUT_MAX_RATE,
           g_param_spec_uint ("playout-max-rate",
                              "set the maximum playout rate in ms.",
                              "set the maximum playout rate in ms.",
                              0,
                              4294967295, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

      g_object_class_install_property (gobject_class, PROP_PLAYOUT_MIN_RATE,
           g_param_spec_uint ("playout-min-rate",
                              "set the minimum playout rate in ms.",
                              "set the minimum playout rate in ms.",
                              0,
                              4294967295, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

      g_object_class_install_property (gobject_class, PROP_PLAYOUT_DESIRED_FRAMENUM,
           g_param_spec_uint ("playout-desired-framenum",
                              "set the desired frame num in the rcvqueue at playout.",
                              "set the desired frame num in the rcvqueue at playout.",
                              0,
                              4294967295, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

      g_object_class_install_property (gobject_class, PROP_PLAYOUT_SPREAD_FACTOR,
            g_param_spec_double ("playout-spread-factor",
                                 "set the spread factor for playout.",
                                 "set the spread factor for playout.",
                                 0.0, 10.0, 1.,
                                 G_PARAM_WRITABLE  | G_PARAM_STATIC_STRINGS));


      g_object_class_install_property (gobject_class, PROP_SPIKE_DELAY_TRESHOLD,
           g_param_spec_uint ("spike-delay-treshold",
                              "if the actual packet delay larger than the average plus this delay value in ms a path considered to be in spike mode.",
                              "A 32bit unsigned integer upper 8bit in host order are used to identify the subflow. "
                              "If the value is 255 then the option will be applied on all subflow. The latter 24bits are identified as a value in ms",
                              0,
                              4294967295, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

      g_object_class_install_property (gobject_class, PROP_SPIKE_VAR_TRESHOLD,
           g_param_spec_uint ("spike-var-treshold",
                              "if the path is in spike mode this variance value is used to determine weather we can turn it on into a normal mode again.",
                              "A 32bit unsigned integer upper 8bit in host order are used to identify the subflow. "
                              "If the value is 255 then the option will be applied on all subflow. The latter 24bits are identified as a value in ms",
                              0,
                              4294967295, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SETUP_RTCP_INTERVAL_TYPE,
        g_param_spec_uint ("setup-rtcp-interval-type",
                           "RTCP interval types: 0 - regular, 1 - early, 2 - immediate feedback",
                           "A 32bit unsigned integer for setup a target. The first 8 bit identifies the subflow, the latter the mode. "
                           "RTCP interval types: 0 - regular, 1 - early, 2 - immediate feedback",
                           0,
                           4294967295, 2, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SETUP_CONTROLLING_MODE,
      g_param_spec_uint ("setup-controlling-mode",
          "set the controlling mode to the subflow",
          "A 32bit unsigned integer for setup a target. The first 8 bit identifies the subflow, the latter the mode. "
          "0 - no sending rate controller, 1 - no controlling, but sending SRs, 2 - FBRA with MARC",
          0, 4294967295, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_change_state);
  element_class->query = GST_DEBUG_FUNCPTR (gst_mprtpplayouter_query);
}


static void
gst_mprtpplayouter_init (GstMprtpplayouter * this)
{
  init_mprtp_logger();

  this->mprtp_sinkpad =
      gst_pad_new_from_static_template (&gst_mprtpplayouter_mprtp_sink_template,
      "mprtp_sink");
  gst_element_add_pad (GST_ELEMENT (this), this->mprtp_sinkpad);

  this->mprtp_srcpad =
      gst_pad_new_from_static_template (&gst_mprtpplayouter_mprtp_src_template,
      "mprtp_src");
  gst_element_add_pad (GST_ELEMENT (this), this->mprtp_srcpad);

  this->mprtcp_sr_sinkpad =
      gst_pad_new_from_static_template
      (&gst_mprtpplayouter_mprtcp_sr_sink_template, "mprtcp_sr_sink");
  gst_element_add_pad (GST_ELEMENT (this), this->mprtcp_sr_sinkpad);

  this->mprtcp_rr_srcpad =
      gst_pad_new_from_static_template
      (&gst_mprtpplayouter_mprtcp_rr_src_template, "mprtcp_rr_src");
  gst_element_add_pad (GST_ELEMENT (this), this->mprtcp_rr_srcpad);

  gst_pad_set_query_function (this->mprtp_srcpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_src_query));
  gst_pad_set_query_function (this->mprtcp_rr_srcpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_src_query));

  gst_pad_set_chain_function (this->mprtcp_sr_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_mprtcp_sr_sink_chain));
  gst_pad_set_chain_function (this->mprtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_mprtp_sink_chain));

  gst_pad_set_query_function (this->mprtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_sink_query));
  gst_pad_set_event_function (this->mprtp_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpplayouter_sink_event));

  g_rw_lock_init (&this->rwmutex);

  this->thread                   = gst_task_new (_mprtpplayouter_process_run, this, NULL);
  this->rtp_passthrough          = TRUE;
  this->mprtp_ext_header_id      = MPRTP_DEFAULT_EXTENSION_HEADER_ID;
  this->abs_time_ext_header_id   = ABS_TIME_DEFAULT_EXTENSION_HEADER_ID;
  this->sysclock                 = gst_system_clock_obtain ();
  this->pivot_clock_rate         = MPRTP_PLAYOUTER_DEFAULT_CLOCKRATE;
  this->pivot_ssrc               = MPRTP_PLAYOUTER_DEFAULT_SSRC;
  this->paths                    = g_hash_table_new_full (NULL, NULL, NULL, mprtpr_path_destroy);
  this->rcvqueue                 = make_packetsrcvqueue();
  this->joiner                   = make_stream_joiner(this->rcvqueue);
  this->controller               = g_object_new(RCVCTRLER_TYPE, NULL);
  this->fec_payload_type         = FEC_PAYLOAD_DEFAULT_ID;
  this->pivot_address_subflow_id = 0;
  this->pivot_address            = NULL;
  this->fec_decoder              = make_fecdecoder();
  this->expected_seq             = 0;
  this->expected_seq_init        = FALSE;

  rcvctrler_setup(this->controller, this->joiner, this->fec_decoder);
  rcvctrler_setup_callbacks(this->controller, this, gst_mprtpplayouter_mprtcp_sender);

  fecdecoder_set_payload_type(this->fec_decoder, this->fec_payload_type);
  packetsrcvqueue_set_playout_allowed(this->rcvqueue, FALSE);

}


void
gst_mprtpplayouter_finalize (GObject * object)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (object);

  GST_DEBUG_OBJECT (this, "finalize");
  g_object_unref (this->joiner);
  g_object_unref (this->controller);
  g_object_unref (this->sysclock);

  /* clean up object here */
  gst_task_join (this->thread);
  gst_object_unref (this->thread);
  g_hash_table_destroy (this->paths);
  G_OBJECT_CLASS (gst_mprtpplayouter_parent_class)->finalize (object);
//  while(!g_queue_is_empty(this->mprtp_buffer_pool)){
//    mprtp_free(g_queue_pop_head(this->mprtp_buffer_pool));
//  }
//  g_object_unref(this->mprtp_buffer_pool);
}


void
gst_mprtpplayouter_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (object);
  gboolean gboolean_value;
  guint guint_value;
  gdouble gdouble_value;
  SubflowSpecProp *subflow_prop;
  GST_DEBUG_OBJECT (this, "set_property");

  subflow_prop = (SubflowSpecProp*) &guint_value;
  switch (property_id) {
    case PROP_MPRTP_EXT_HEADER_ID:
      THIS_WRITELOCK (this);
      this->mprtp_ext_header_id = (guint8) g_value_get_uint (value);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_ABS_TIME_EXT_HEADER_ID:
      THIS_WRITELOCK (this);
      this->abs_time_ext_header_id = (guint8) g_value_get_uint (value);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_FEC_PAYLOAD_TYPE:
      THIS_WRITELOCK (this);
      this->fec_payload_type = (guint8) g_value_get_uint (value);
      fecdecoder_set_payload_type(this->fec_decoder, this->fec_payload_type);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_PIVOT_SSRC:
      THIS_WRITELOCK (this);
      this->pivot_ssrc = g_value_get_uint (value);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_PIVOT_CLOCK_RATE:
      THIS_WRITELOCK (this);
      this->pivot_clock_rate = g_value_get_uint (value);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_JOIN_SUBFLOW:
      THIS_WRITELOCK (this);
      _join_path (this, g_value_get_uint (value));
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_DETACH_SUBFLOW:
      THIS_WRITELOCK (this);
      _detach_path (this, g_value_get_uint (value));
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_RTP_PASSTHROUGH:
      THIS_WRITELOCK (this);
      gboolean_value = g_value_get_boolean (value);
      this->rtp_passthrough = gboolean_value;
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_LOG_ENABLED:
      THIS_WRITELOCK (this);
      gboolean_value = g_value_get_boolean (value);
      this->logging = gboolean_value;
      if(this->logging)
        enable_mprtp_logger();
      else
        disable_mprtp_logger();
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_LOG_PATH:
      THIS_WRITELOCK (this);
      mprtp_logger_set_target_directory(g_value_get_string(value));
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_OWD_WINDOW_TRESHOLD:
      THIS_WRITELOCK (this);
      guint_value = g_value_get_uint (value);
      _setup_paths_tresholds(this,
                             subflow_prop->id,
                             mprtpr_path_set_owd_window_treshold,
                             (GstClockTime) subflow_prop->value * GST_MSECOND);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_JOIN_MIN_TRESHOLD:
      THIS_WRITELOCK (this);
      guint_value = g_value_get_uint (value);
      stream_joiner_set_min_treshold(this->joiner, (GstClockTime)guint_value * GST_MSECOND);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_JOIN_MAX_TRESHOLD:
      THIS_WRITELOCK (this);
      guint_value = g_value_get_uint (value);
      stream_joiner_set_max_treshold(this->joiner, (GstClockTime)guint_value * GST_MSECOND);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_JOIN_WINDOW_TRESHOLD:
      THIS_WRITELOCK (this);
      guint_value = g_value_get_uint (value);
      stream_joiner_set_window_treshold(this->joiner, (GstClockTime)guint_value * GST_SECOND);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_JOIN_BETHA_FACTOR:
      THIS_WRITELOCK (this);
      gdouble_value = g_value_get_double (value);
      stream_joiner_set_betha_factor(this->joiner, gdouble_value);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_PLAYOUT_MAX_RATE:
      THIS_WRITELOCK (this);
      guint_value = g_value_get_uint (value);
      packetsrcvqueue_set_max_playoutrate(this->rcvqueue, (GstClockTime)guint_value * GST_MSECOND);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_PLAYOUT_MIN_RATE:
      THIS_WRITELOCK (this);
      guint_value = g_value_get_uint (value);
      packetsrcvqueue_set_min_playoutrate(this->rcvqueue, (GstClockTime)guint_value * GST_MSECOND);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_PLAYOUT_DESIRED_FRAMENUM:
      THIS_WRITELOCK (this);
      guint_value = g_value_get_uint (value);
      packetsrcvqueue_set_desired_framenum(this->rcvqueue, guint_value);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_PLAYOUT_SPREAD_FACTOR:
      THIS_WRITELOCK (this);
      gdouble_value = g_value_get_double (value);
      packetsrcvqueue_set_spread_factor(this->rcvqueue, gdouble_value);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_SPIKE_DELAY_TRESHOLD:
      THIS_WRITELOCK (this);
      guint_value = g_value_get_uint (value);
      _setup_paths_tresholds(this,
                             subflow_prop->id,
                             mprtpr_path_set_spike_delay_treshold,
                             (GstClockTime) subflow_prop->value * GST_MSECOND);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_SPIKE_VAR_TRESHOLD:
      THIS_WRITELOCK (this);
      guint_value = g_value_get_uint (value);
      _setup_paths_tresholds(this,
                             subflow_prop->id,
                             mprtpr_path_set_spike_var_treshold,
                             (GstClockTime) subflow_prop->value * GST_MSECOND);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_SETUP_RTCP_INTERVAL_TYPE:
      THIS_WRITELOCK (this);
      guint_value = g_value_get_uint (value);
      rcvctrler_change_interval_type(this->controller, subflow_prop->id, subflow_prop->value);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_SETUP_CONTROLLING_MODE:
      THIS_WRITELOCK (this);
      guint_value = g_value_get_uint (value);
      rcvctrler_change_controlling_mode(this->controller, subflow_prop->id, subflow_prop->value);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_REPAIR_WINDOW_MIN:
      THIS_WRITELOCK (this);
      this->repair_window_min = (GstClockTime) g_value_get_uint (value) * GST_MSECOND;
      fecdecoder_set_repair_window(this->fec_decoder, this->repair_window_min, this->repair_window_max);
      THIS_WRITEUNLOCK (this);
      break;
    case PROP_REPAIR_WINDOW_MAX:
      THIS_WRITELOCK (this);
      this->repair_window_max = (GstClockTime) g_value_get_uint (value) * GST_MSECOND;
      fecdecoder_set_repair_window(this->fec_decoder, this->repair_window_min, this->repair_window_max);
      THIS_WRITEUNLOCK (this);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}


void
gst_mprtpplayouter_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (object);

  GST_DEBUG_OBJECT (this, "get_property");

  switch (property_id) {
    case PROP_MPRTP_EXT_HEADER_ID:
      THIS_READLOCK (this);
      g_value_set_uint (value, (guint) this->mprtp_ext_header_id);
      THIS_READUNLOCK (this);
      break;
    case PROP_ABS_TIME_EXT_HEADER_ID:
      THIS_READLOCK (this);
      g_value_set_uint (value, (guint) this->abs_time_ext_header_id);
      THIS_READUNLOCK (this);
      break;
    case PROP_FEC_PAYLOAD_TYPE:
      THIS_READLOCK (this);
      g_value_set_uint (value, (guint) this->fec_payload_type);
      THIS_READUNLOCK (this);
      break;
    case PROP_PIVOT_CLOCK_RATE:
      THIS_READLOCK (this);
      g_value_set_uint (value, this->pivot_clock_rate);
      THIS_READUNLOCK (this);
      break;
    case PROP_PIVOT_SSRC:
      THIS_READLOCK (this);
      g_value_set_uint (value, this->pivot_ssrc);
      THIS_READUNLOCK (this);
      break;
    case PROP_RTP_PASSTHROUGH:
      THIS_READLOCK (this);
      g_value_set_boolean (value, this->rtp_passthrough);
      THIS_READUNLOCK (this);
      break;
    case PROP_LOG_ENABLED:
      THIS_READLOCK (this);
      g_value_set_boolean (value, this->logging);
      THIS_READUNLOCK (this);
      break;
    case PROP_LOG_PATH:
      THIS_READLOCK (this);
      {
        gchar string[255];
        mprtp_logger_get_target_directory(string);
        g_value_set_string (value, string);
      }
      THIS_READUNLOCK (this);
      break;
    case PROP_REPAIR_WINDOW_MIN:
      THIS_READLOCK (this);
      g_value_set_uint (value, this->repair_window_min / GST_MSECOND);
      THIS_READUNLOCK (this);
      break;
    case PROP_REPAIR_WINDOW_MAX:
      THIS_READLOCK (this);
      g_value_set_uint (value, this->repair_window_max / GST_MSECOND);
      THIS_READUNLOCK (this);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
_setup_paths_tresholds(
    GstMprtpplayouter * this,
    guint8 subflow_id,
    void (*cb)(MpRTPRPath*,GstClockTime),
    GstClockTime treshold)
{
  GHashTableIter iter;
  gpointer key, val;
  MpRTPRPath *path;
  gboolean subflow_match = FALSE;

  g_hash_table_iter_init (&iter, this->paths);
  while (g_hash_table_iter_next (&iter, (gpointer) & key, (gpointer) & val)) {
    path = (MpRTPRPath *) val;
    subflow_match = mprtpr_path_get_id(path) != subflow_id;
    if(subflow_id != 255 && subflow_id != 0 && !subflow_match){
      continue;
    }
    cb(path, treshold);
  }
}


gboolean
gst_mprtpplayouter_src_query (GstPad * sinkpad, GstObject * parent,
    GstQuery * query)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (parent);
  gboolean result = FALSE;
  GST_DEBUG_OBJECT (this, "query");
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:
    {
      gboolean live;
      GstClockTime min, max;
      GstPad *peer;
      peer = gst_pad_get_peer (this->mprtp_sinkpad);
      if ((result = gst_pad_query (peer, query))) {
          gst_query_parse_latency (query, &live, &min, &max);
//          min= GST_MSECOND;
          min = 0;
          max = -1;
          gst_query_set_latency (query, live, min, max);
      }
      gst_object_unref (peer);
    }
    break;
    default:
      result = gst_pad_peer_query (this->mprtp_srcpad, query);
      break;
  }
  return result;
}


gboolean
gst_mprtpplayouter_sink_query (GstPad * sinkpad, GstObject * parent,
    GstQuery * query)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (parent);
  gboolean result;
  GST_DEBUG_OBJECT (this, "query");
  switch (GST_QUERY_TYPE (query)) {

    default:
      result = gst_pad_peer_query (this->mprtp_srcpad, query);
      break;
  }
  return result;
}


static gboolean
gst_mprtpplayouter_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (parent);
  gboolean result;
  GstPad *peer;
  GstCaps *caps;
  const GstStructure *s;
  gint gint_value;
  guint guint_value;

  GST_DEBUG_OBJECT (this, "sink event");
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_LATENCY:
      {
        GstClockTime latency;
        gst_event_parse_latency(event, &latency);
      }
      goto default_;
    case GST_EVENT_CAPS:
      gst_event_parse_caps (event, &caps);
      s = gst_caps_get_structure (caps, 0);
      THIS_WRITELOCK (this);
      if (gst_structure_has_field (s, "clock-rate")) {
        gst_structure_get_int (s, "clock-rate", &gint_value);
        this->pivot_clock_rate = (guint32) gint_value;
      }
      if (gst_structure_has_field (s, "clock-base")) {
        gst_structure_get_uint (s, "clock-base", &guint_value);
        this->clock_base = (guint64) gint_value;
      } else {
        this->clock_base = -1;
      }
      THIS_WRITEUNLOCK (this);
      goto default_;
    default:
    default_:
      peer = gst_pad_get_peer (this->mprtp_srcpad);
            result = gst_pad_send_event (peer, event);
            gst_object_unref (peer);
      //result = gst_pad_event_default (pad, parent, event);
      break;
  }

  return result;
}


void
gst_mprtpplayouter_dispose (GObject * object)
{
  GstMprtpplayouter *mprtpplayouter = GST_MPRTPPLAYOUTER (object);

  GST_DEBUG_OBJECT (mprtpplayouter, "dispose");

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (gst_mprtpplayouter_parent_class)->dispose (object);
}

void
_join_path (GstMprtpplayouter * this, guint8 subflow_id)
{
  MpRTPRPath *path;
  path =
      (MpRTPRPath *) g_hash_table_lookup (this->paths,
      GINT_TO_POINTER (subflow_id));
  if (path != NULL) {
    GST_WARNING_OBJECT (this, "Join operation can not be done "
        "due to duplicated subflow id (%d)", subflow_id);
    goto exit;
  }
  path = make_mprtpr_path (subflow_id);
  g_hash_table_insert (this->paths, GINT_TO_POINTER (subflow_id), path);
  stream_joiner_add_path (this->joiner, subflow_id, path);
  rcvctrler_add_path(this->controller, subflow_id, path);
  ++this->subflows_num;
exit:
  return;
}

void
_detach_path (GstMprtpplayouter * this, guint8 subflow_id)
{
  MpRTPRPath *path;

  path =
      (MpRTPRPath *) g_hash_table_lookup (this->paths,
      GINT_TO_POINTER (subflow_id));
  if (path == NULL) {
    GST_WARNING_OBJECT (this, "Detach operation can not be done "
        "due to not existed subflow id (%d)", subflow_id);
    goto exit;
  }
  stream_joiner_rem_path (this->joiner, subflow_id);
  rcvctrler_rem_path(this->controller, subflow_id);
  g_hash_table_remove (this->paths, GINT_TO_POINTER (subflow_id));
  if (this->pivot_address && subflow_id == this->pivot_address_subflow_id) {
    g_object_unref (this->pivot_address);
    this->pivot_address = NULL;
  }
  if (--this->subflows_num) {
    this->subflows_num = 0;
  }
exit:
  return;
}



static GstStateChangeReturn
gst_mprtpplayouter_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstMprtpplayouter *this;
  g_return_val_if_fail (GST_IS_MPRTPPLAYOUTER (element),
      GST_STATE_CHANGE_FAILURE);

  this = GST_MPRTPPLAYOUTER (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
        gst_task_set_lock (this->thread, &this->thread_mutex);
        gst_task_start (this->thread);
        packetsrcvqueue_set_playout_allowed(this->rcvqueue, TRUE);
        break;
      default:
        break;
    }

    ret =
        GST_ELEMENT_CLASS (gst_mprtpplayouter_parent_class)->change_state
        (element, transition);

    switch (transition) {
      case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
        packetsrcvqueue_set_playout_allowed(this->rcvqueue, FALSE);
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
gst_mprtpplayouter_query (GstElement * element, GstQuery * query)
{
  GstMprtpplayouter *this = GST_MPRTPPLAYOUTER (element);
  gboolean ret = TRUE;
  GstStructure *s;
  GST_DEBUG_OBJECT (this, "query");
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CUSTOM:
      THIS_WRITELOCK (this);
      s = gst_query_writable_structure (query);
      if (!gst_structure_has_name (s,
              GST_MPRTCP_PLAYOUTER_SENT_BYTES_STRUCTURE_NAME)) {
        ret =
            GST_ELEMENT_CLASS (gst_mprtpplayouter_parent_class)->query (element,
            query);
        break;
      }
      gst_structure_set (s,
          GST_MPRTCP_PLAYOUTER_SENT_OCTET_SUM_FIELD,
          G_TYPE_UINT, this->rtcp_sent_octet_sum, NULL);
      THIS_WRITEUNLOCK (this);
      break;
    default:
      ret =
          GST_ELEMENT_CLASS (gst_mprtpplayouter_parent_class)->query (element,
          query);
      break;
  }

  return ret;
}


static GstFlowReturn
gst_mprtpplayouter_mprtp_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  GstMprtpplayouter *this;
  GstMapInfo info;
  guint8 *data;
  GstFlowReturn result = GST_FLOW_OK;

  this = GST_MPRTPPLAYOUTER (parent);
  GST_DEBUG_OBJECT (this, "RTP/RTCP/MPRTP/MPRTCP sink");
//  g_print("START PROCESSING RTP\n");
  THIS_READLOCK (this);
  if(!GST_IS_BUFFER(buf)){
    GST_WARNING("The arrived buffer is not a buffer.");
    goto done;
  }
  if (!gst_buffer_map (buf, &info, GST_MAP_READ)) {
    GST_WARNING ("Buffer is not readable");
    result = GST_FLOW_ERROR;
    goto done;
  }
  data = info.data + 1;
  gst_buffer_unmap (buf, &info);
  //demultiplexing based on RFC5761
  if (*data == MPRTCP_PACKET_TYPE_IDENTIFIER) {
    result = _processing_mprtcp_packet (this, buf);
    goto done;
  }
  //check weather the packet is rtcp or mprtp
  if (*data > 192 && *data < 223) {
    if(GST_IS_BUFFER(buf))
      result = gst_pad_push (this->mprtp_srcpad, buf);
    goto done;
  }
  //check weather the packet is mprtp
  if(!gst_buffer_is_mprtp(buf, this->mprtp_ext_header_id)){
    if(GST_IS_BUFFER(buf))
      result = gst_pad_push (this->mprtp_srcpad, buf);
    goto done;
  }

  _processing_mprtp_packet (this, buf);
  result = GST_FLOW_OK;
done:
  THIS_READUNLOCK (this);
//  g_print("END PROCESSING RTP\n");
  return result;

}


static GstFlowReturn
gst_mprtpplayouter_mprtcp_sr_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  GstMprtpplayouter *this;
  GstMapInfo info;
  GstFlowReturn result;
  guint8 *data;

  this = GST_MPRTPPLAYOUTER (parent);
  GST_DEBUG_OBJECT (this, "RTCP/MPRTCP sink");
  THIS_READLOCK (this);
  if (!gst_buffer_map (buf, &info, GST_MAP_READ)) {
    GST_WARNING ("Buffer is not readable");
    result = GST_FLOW_ERROR;
    goto done;
  }

  data = info.data + 1;
  gst_buffer_unmap (buf, &info);
  //demultiplexing based on RFC5761
  if (*data != this->fec_payload_type) {
    result = _processing_mprtcp_packet (this, buf);
  }else{
    _processing_mprtp_packet(this, buf);
    result = GST_FLOW_OK;
  }

done:
  THIS_READUNLOCK (this);
  return result;

}

void
gst_mprtpplayouter_mprtcp_sender (gpointer ptr, GstBuffer * buf)
{
  GstMprtpplayouter *this;
  this = GST_MPRTPPLAYOUTER (ptr);
  THIS_READLOCK (this);
  gst_pad_push (this->mprtcp_rr_srcpad, buf);
  THIS_READUNLOCK (this);
}


GstFlowReturn
_processing_mprtcp_packet (GstMprtpplayouter * this, GstBuffer * buf)
{
  GstFlowReturn result;
  rcvctrler_receive_mprtcp(this->controller, buf);
  result = GST_FLOW_OK;
  return result;
}

//static GstClockTime in_prev;
void
_processing_mprtp_packet (GstMprtpplayouter * this, GstBuffer * buf)
{
  MpRTPRPath *path = NULL;
  GstNetAddressMeta *meta;
  GstMpRTPBuffer *mprtp = NULL;

  mprtp = _make_mprtp_buffer(this, buf);
  if (this->pivot_ssrc != MPRTP_PLAYOUTER_DEFAULT_SSRC &&
      mprtp->ssrc != this->pivot_ssrc) {

    _trash_mprtp_buffer(this, mprtp);
    GST_DEBUG_OBJECT (this, "RTP packet ssrc is %u, the pivot ssrc is %u",
        this->pivot_ssrc, mprtp->ssrc);
    if(GST_IS_BUFFER(buf))
      gst_pad_push (this->mprtp_srcpad, buf);
    return;
  }

  //to avoid the check_collision problem in rtpsession.
  meta = gst_buffer_get_net_address_meta (buf);
  if (meta) {
    if (!this->pivot_address) {
      this->pivot_address_subflow_id = mprtp->subflow_id;
      this->pivot_address = G_SOCKET_ADDRESS (g_object_ref (meta->addr));
    } else if (mprtp->subflow_seq != this->pivot_address_subflow_id) {
      if(gst_buffer_is_writable(buf))
        gst_buffer_add_net_address_meta (buf, this->pivot_address);
      else{
        buf = gst_buffer_make_writable(buf);
        gst_buffer_add_net_address_meta (buf, this->pivot_address);
      }
    }
  }
  if (_try_get_path (this, mprtp->subflow_id, &path) == FALSE) {
    _join_path (this, mprtp->subflow_id);
    if (_try_get_path (this, mprtp->subflow_id, &path) == FALSE) {
      GST_WARNING_OBJECT (this, "Subflow not found");
      _trash_mprtp_buffer(this, mprtp);
      return;
    }
  }

  if(mprtp->fec_packet){
    if(0 < this->repair_window_max){
      fecdecoder_add_fec_packet(this->fec_decoder, mprtp);
    }
    _trash_mprtp_buffer(this, mprtp);
  }else{
    if(0 < this->repair_window_max){
      fecdecoder_add_rtp_packet(this->fec_decoder, mprtp);
    }
    stream_joiner_push(this->joiner, mprtp);
  }
  return;
}

gboolean
_try_get_path (GstMprtpplayouter * this, guint16 subflow_id,
    MpRTPRPath ** result)
{
  MpRTPRPath *path;
  path =
      (MpRTPRPath *) g_hash_table_lookup (this->paths,
      GINT_TO_POINTER (subflow_id));
  if (path == NULL) {
    return FALSE;
  }
  if (result) {
    *result = path;
  }
  return TRUE;
}


GstMpRTPBuffer *_make_mprtp_buffer(GstMprtpplayouter * this, GstBuffer *buffer)
{
  GstMpRTPBuffer *result;
//  result = g_slice_new0(GstMpRTPBuffer);
  result = g_malloc0(sizeof(GstMpRTPBuffer));
  gst_mprtp_buffer_init(result,
                    buffer,
                    this->mprtp_ext_header_id,
                    this->abs_time_ext_header_id,
                    this->fec_payload_type);
  return result;
}


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

void
_mprtpplayouter_process_run (void *data)
{
  GstMprtpplayouter *this;
  GstClockID clock_id;
  GstClockTime next_scheduler_time;
  GstMpRTPBuffer *mprtp;
  GstBuffer *buffer = NULL;
  GstBuffer *repairedbuf = NULL;
  GstClockTime now;

  this = (GstMprtpplayouter *) data;

  THIS_READLOCK (this);
  now = _now(this);
  //Todo: we need more time than one msecond, so it will sleep with the rate.
  next_scheduler_time = _now(this) + 1 * GST_MSECOND;
  if(this->last_fec_clean < now - 200 * GST_MSECOND){
    fecdecoder_clean(this->fec_decoder);
    this->last_fec_clean = now;
  }

  stream_joiner_transfer(this->joiner);
  //flush the urgent queue
  for(mprtp = packetsrcvqueue_pop_urgent(this->rcvqueue); mprtp;
      mprtp = packetsrcvqueue_pop_urgent(this->rcvqueue)){
      buffer = mprtp->buffer;
      _trash_mprtp_buffer(this, mprtp);
      gst_pad_push (this->mprtp_srcpad, buffer);

  }

  if(now < this->playout_point){
    goto done;
  }
  packetsrcvqueue_refresh(this->rcvqueue);
  this->playout_point = packetsrcvqueue_get_playout_point(this->rcvqueue);

again:
  mprtp = packetsrcvqueue_pop_normal(this->rcvqueue);
  if(!mprtp){
    goto done;
  }
  buffer = mprtp->buffer;

  if(!this->expected_seq_init){
    this->expected_seq_init = TRUE;
    this->expected_seq = mprtp->abs_seq;
  }

  while(fecdecoder_has_repaired_rtpbuffer(this->fec_decoder, this->expected_seq, &repairedbuf)){
    gst_pad_push(this->mprtp_srcpad, repairedbuf);
  }
  if(mprtp->abs_seq != this->expected_seq){
    if(_cmp_seq(this->expected_seq, mprtp->abs_seq) < 0){
      this->expected_seq = mprtp->abs_seq + 1;
    }
  }else{
      ++this->expected_seq;
  }

  _trash_mprtp_buffer(this, mprtp);
  if(!buffer) {
    goto done;
  }
//  if(1 < buffer->mini_object.refcount){
//    gst_buffer_unref(buffer);
//  }
  gst_pad_push (this->mprtp_srcpad, buffer);
  goto again;
done:
  clock_id = gst_clock_new_single_shot_id (this->sysclock, next_scheduler_time);
  THIS_READUNLOCK (this);

  if (gst_clock_id_wait (clock_id, NULL) == GST_CLOCK_UNSCHEDULED) {
    GST_WARNING_OBJECT (this, "The scheduler clock wait is interrupted");
  }
  gst_clock_id_unref (clock_id);
}

#undef THIS_READLOCK
#undef THIS_READUNLOCK
#undef THIS_WRITELOCK
#undef THIS_WRITEUNLOCK
