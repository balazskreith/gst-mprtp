/* GStreamer
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
#  include "config.h"
#endif

#include <stdio.h>
#include <gst/gst.h>
#include "gstmprtpsender2.h"
#include "mprtpspath.h"
#include "gstmprtcpbuffer.h"
#include <string.h>
#include <string.h>

#define PACKET_IS_RTP_OR_RTCP(b) (b > 0x7f && b < 0xc0)
#define PACKET_IS_DTLS(b) (b > 0x13 && b < 0x40)
#define PACKET_IS_RTCP(b) (b > 192 && b < 223)

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("mprtp_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_mprtpsender_mprtcp_rr_sink_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_rr_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
//    GST_STATIC_CAPS ("application/x-rtcp")
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_mprtpsender_mprtcp_sr_sink_template =
GST_STATIC_PAD_TEMPLATE ("mprtcp_sr_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
//    GST_STATIC_CAPS ("application/x-rtcp")
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_mprtp_sender2_debug);
#define GST_CAT_DEFAULT gst_mprtp_sender2_debug

#define DEFAULT_PROP_NUM_SRC_PADS	0
#define DEFAULT_PROP_HAS_CHAIN		TRUE
#define DEFAULT_PROP_SILENT		TRUE
#define DEFAULT_PROP_LAST_MESSAGE	NULL
#define DEFAULT_PULL_MODE		GST_MPRTPSENDER2_PULL_MODE_NEVER
#define DEFAULT_PROP_ALLOW_NOT_LINKED	FALSE

enum
{
  PROP_0,
  PROP_MPRTP_EXT_HEADER_ID,
  PROP_PIVOT_OUTPAD,
};

static GstStaticPadTemplate mprtp_sender2_src_template =
GST_STATIC_PAD_TEMPLATE ("src_%u",
    GST_PAD_SRC,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_mprtp_sender2_debug, "mprtp_sender2", 0, "mprtp_sender2 element");
#define gst_mprtp_sender2_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstMPRTPSender2, gst_mprtp_sender2, GST_TYPE_ELEMENT, _do_init);

GType gst_mprtp_sender2_pad_get_type (void);

#define GST_TYPE_SUBFLOW \
  (gst_mprtp_sender2_pad_get_type())
#define GST_SUBFLOW(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SUBFLOW, Subflow))
#define GST_SUBFLOW_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_SUBFLOW, SubflowClass))
#define GST_IS_SUBFLOW(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SUBFLOW))
#define GST_IS_SUBFLOW_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_SUBFLOW))
#define GST_SUBFLOW_CAST(obj) \
  ((Subflow *)(obj))

typedef struct _Subflow Subflow;
typedef struct _SubflowClass SubflowClass;

struct _Subflow
{
  GstPad        parent;
  guint         id;
  GstFlowReturn result;
  gboolean      removed;
};

struct _SubflowClass
{
  GstPadClass parent;
};



G_DEFINE_TYPE (Subflow, gst_mprtp_sender2_pad, GST_TYPE_PAD);

static void
gst_mprtp_sender2_pad_class_init (SubflowClass * klass)
{
}

static void
gst_mprtp_sender2_pad_reset (Subflow * pad)
{
  pad->result = GST_FLOW_NOT_LINKED;
  pad->removed = FALSE;
}

static void
gst_mprtp_sender2_pad_init (Subflow * pad)
{
  gst_mprtp_sender2_pad_reset (pad);
}

static GstBuffer *_assemble_report (Subflow * this, GstBuffer * blocks);
static Subflow *_get_subflow_from_blocks (GstMPRTPSender2 * this,
    GstBuffer * blocks);
static gboolean _select_subflow (GstMPRTPSender2 * this, guint8 id,
    Subflow ** result);

static GstPad *gst_mprtp_sender2_request_new_pad (GstElement * element,
    GstPadTemplate * temp, const gchar * unused, const GstCaps * caps);
static void gst_mprtp_sender2_release_pad (GstElement * element, GstPad * pad);

static void gst_mprtp_sender2_finalize (GObject * object);
static void gst_mprtp_sender2_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mprtp_sender2_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_mprtp_sender2_dispose (GObject * object);

static GstFlowReturn gst_mprtp_sender2_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);

static GstPad*
_get_mprtp_sink (GstMPRTPSender2 *this, gpointer data, gboolean is_list);

static GstFlowReturn gst_mprtp_sender2_chain_list (GstPad * pad, GstObject * parent,
    GstBufferList * list);
static gboolean gst_mprtp_sender2_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_mprtp_sender2_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_mprtp_sender2_sink_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active);
static gboolean gst_mprtp_sender2_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_mprtp_sender2_src_activate_mode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active);
static GstFlowReturn
gst_mprtpsender_mprtcp_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);
static void
gst_mprtp_sender2_dispose (GObject * object)
{
  GList *item;

restart:
  for (item = GST_ELEMENT_PADS (object); item; item = g_list_next (item)) {
    GstPad *pad = GST_PAD (item->data);
    if (GST_PAD_IS_SRC (pad)) {
      gst_element_release_request_pad (GST_ELEMENT (object), pad);
      goto restart;
    }
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_mprtp_sender2_finalize (GObject * object)
{
  GstMPRTPSender2 *mprtp_sender2;

  mprtp_sender2 = GST_MPRTPSENDER2 (object);

  g_hash_table_unref (mprtp_sender2->pad_indexes);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_mprtp_sender2_class_init (GstMPRTPSender2Class * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = gst_mprtp_sender2_finalize;
  gobject_class->set_property = gst_mprtp_sender2_set_property;
  gobject_class->get_property = gst_mprtp_sender2_get_property;
  gobject_class->dispose = gst_mprtp_sender2_dispose;

  gst_element_class_set_static_metadata (gstelement_class,
      "MPRTPSender2",
      "MpRTP Plugin",
      "MPRTP Demuxer for sending",
      "Balázs Kreith<balazs.kreith@gmail.com>");
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&mprtp_sender2_src_template));
  gst_element_class_add_pad_template (gstelement_class,
        gst_static_pad_template_get (&gst_mprtpsender_mprtcp_sr_sink_template));
    gst_element_class_add_pad_template (gstelement_class,
        gst_static_pad_template_get (&gst_mprtpsender_mprtcp_rr_sink_template));

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_mprtp_sender2_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_mprtp_sender2_release_pad);

  g_object_class_install_property (gobject_class, PROP_MPRTP_EXT_HEADER_ID,
      g_param_spec_uint ("mprtp-ext-header-id",
          "Set or get the id for the RTP extension",
          "Sets or gets the id for the extension header the MpRTP based on. The default is 3",
          0, 15, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PIVOT_OUTPAD,
      g_param_spec_uint ("pivot-outpad",
          "The id of the subflow sets to pivot for non-mp packets.",
          "The id of the subflow sets to pivot for non-mp packets. (DTLS, RTCP, Others)",
          0, 255, 0, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_mprtp_sender2_init (GstMPRTPSender2 * mprtp_sender2)
{
  mprtp_sender2->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "mprtp_sink");
  mprtp_sender2->sink_mode = GST_PAD_MODE_NONE;

  gst_pad_set_event_function (mprtp_sender2->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtp_sender2_sink_event));
  gst_pad_set_query_function (mprtp_sender2->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtp_sender2_sink_query));
  gst_pad_set_activatemode_function (mprtp_sender2->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtp_sender2_sink_activate_mode));
  gst_pad_set_chain_function (mprtp_sender2->sinkpad, GST_DEBUG_FUNCPTR (gst_mprtp_sender2_chain));
  gst_pad_set_chain_list_function (mprtp_sender2->sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtp_sender2_chain_list));
  GST_OBJECT_FLAG_SET (mprtp_sender2->sinkpad, GST_PAD_FLAG_PROXY_CAPS);
  gst_element_add_pad (GST_ELEMENT (mprtp_sender2), mprtp_sender2->sinkpad);

  mprtp_sender2->mprtcp_sr_sinkpad =
      gst_pad_new_from_static_template
      (&gst_mprtpsender_mprtcp_sr_sink_template, "mprtcp_sr_sink");
  gst_pad_set_chain_function (mprtp_sender2->mprtcp_sr_sinkpad,
      GST_DEBUG_FUNCPTR (gst_mprtpsender_mprtcp_sink_chain));
  gst_element_add_pad (GST_ELEMENT (mprtp_sender2),
                       mprtp_sender2->mprtcp_sr_sinkpad);

  mprtp_sender2->mprtcp_rr_sinkpad =
        gst_pad_new_from_static_template
        (&gst_mprtpsender_mprtcp_rr_sink_template, "mprtcp_rr_sink");
    gst_pad_set_chain_function (mprtp_sender2->mprtcp_rr_sinkpad,
        GST_DEBUG_FUNCPTR (gst_mprtpsender_mprtcp_sink_chain));
    gst_element_add_pad (GST_ELEMENT (mprtp_sender2),
                         mprtp_sender2->mprtcp_rr_sinkpad);

  mprtp_sender2->pad_indexes = g_hash_table_new (NULL, NULL);
  mprtp_sender2->mprtp_ext_header_id = MPRTP_DEFAULT_EXTENSION_HEADER_ID;
  mprtp_sender2->pivot_outpad = NULL;

}

static gboolean
forward_sticky_events (GstPad * pad, GstEvent ** event, gpointer user_data)
{
  GstPad *srcpad = GST_PAD_CAST (user_data);
  GstFlowReturn ret;

  ret = gst_pad_store_sticky_event (srcpad, *event);
  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (srcpad, "storing sticky event %p (%s) failed: %s", *event,
        GST_EVENT_TYPE_NAME (*event), gst_flow_get_name (ret));
  }

  return TRUE;
}

static GstPad *
gst_mprtp_sender2_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name_templ, const GstCaps * caps)
{
  gchar *name;
  GstPad *srcpad;
  GstMPRTPSender2 *mprtp_sender2;
  GstPadMode mode;
  gboolean res;
  guint index = 0;

  mprtp_sender2 = GST_MPRTPSENDER2 (element);

  GST_DEBUG_OBJECT (mprtp_sender2, "requesting pad");

  GST_OBJECT_LOCK (mprtp_sender2);

  if (name_templ && sscanf (name_templ, "src_%u", &index) == 1) {
    GST_LOG_OBJECT (element, "name: %s (index %d)", name_templ, index);
    if (g_hash_table_contains (mprtp_sender2->pad_indexes, GUINT_TO_POINTER (index))) {
      GST_ERROR_OBJECT (element, "pad name %s is not unique", name_templ);
      GST_OBJECT_UNLOCK (mprtp_sender2);
      return NULL;
    }
    if(index > 255){
      GST_ERROR_OBJECT (element, "subflow id can not be greater than 255");
      GST_OBJECT_UNLOCK (mprtp_sender2);
      return NULL;
    }
    if (index >= mprtp_sender2->next_pad_index)
      mprtp_sender2->next_pad_index = index + 1;
  } else {
    index = mprtp_sender2->next_pad_index;

    while (g_hash_table_contains (mprtp_sender2->pad_indexes, GUINT_TO_POINTER (index)))
      index++;

    mprtp_sender2->next_pad_index = index + 1;
  }

  g_hash_table_insert (mprtp_sender2->pad_indexes, GUINT_TO_POINTER (index), NULL);

  name = g_strdup_printf ("src_%u", index);

  srcpad = GST_PAD_CAST (g_object_new (GST_TYPE_SUBFLOW,
          "name", name, "direction", templ->direction, "template", templ,
          NULL));
  GST_SUBFLOW_CAST (srcpad)->id = index;
  g_free (name);

  mode = mprtp_sender2->sink_mode;

  GST_OBJECT_UNLOCK (mprtp_sender2);

  switch (mode) {
    case GST_PAD_MODE_PUSH:
      res = gst_pad_activate_mode (srcpad, GST_PAD_MODE_PUSH, TRUE);
      break;
    default:
      res = TRUE;
      break;
  }

  if (!res)
    goto activate_failed;

  gst_pad_set_activatemode_function (srcpad,
      GST_DEBUG_FUNCPTR (gst_mprtp_sender2_src_activate_mode));
  gst_pad_set_query_function (srcpad, GST_DEBUG_FUNCPTR (gst_mprtp_sender2_src_query));
  /* Forward sticky events to the new srcpad */
  gst_pad_sticky_events_foreach (mprtp_sender2->sinkpad, forward_sticky_events, srcpad);
  GST_OBJECT_FLAG_SET (srcpad, GST_PAD_FLAG_PROXY_CAPS);
  gst_element_add_pad (GST_ELEMENT_CAST (mprtp_sender2), srcpad);

  return srcpad;

  /* ERRORS */
activate_failed:
  {
    GST_OBJECT_LOCK (mprtp_sender2);
    GST_DEBUG_OBJECT (mprtp_sender2, "warning failed to activate request pad");
    GST_OBJECT_UNLOCK (mprtp_sender2);
    gst_object_unref (srcpad);
    return NULL;
  }
}

static void
gst_mprtp_sender2_release_pad (GstElement * element, GstPad * pad)
{
  GstMPRTPSender2 *mprtp_sender2;
  guint index;

  mprtp_sender2 = GST_MPRTPSENDER2 (element);

  GST_DEBUG_OBJECT (mprtp_sender2, "releasing pad");

  GST_OBJECT_LOCK (mprtp_sender2);
  index = GST_SUBFLOW_CAST (pad)->id;
  /* mark the pad as removed so that future pad_alloc fails with NOT_LINKED. */
  GST_SUBFLOW_CAST (pad)->removed = TRUE;
  GST_OBJECT_UNLOCK (mprtp_sender2);

  gst_object_ref (pad);
  gst_element_remove_pad (GST_ELEMENT_CAST (mprtp_sender2), pad);

  gst_pad_set_active (pad, FALSE);

  gst_object_unref (pad);

  GST_OBJECT_LOCK (mprtp_sender2);
  g_hash_table_remove (mprtp_sender2->pad_indexes, GUINT_TO_POINTER (index));
  GST_OBJECT_UNLOCK (mprtp_sender2);
}

static void
gst_mprtp_sender2_set_property (GObject * object, guint property_id, const GValue * value,
    GParamSpec * pspec)
{
  GstMPRTPSender2 *this = GST_MPRTPSENDER2 (object);
  guint subflow_id;
  Subflow *subflow;

  GST_OBJECT_LOCK (this);
  switch (property_id) {
    case PROP_MPRTP_EXT_HEADER_ID:
      this->mprtp_ext_header_id = (guint8) g_value_get_uint (value);
      break;
    case PROP_PIVOT_OUTPAD:
      subflow_id = (guint8) g_value_get_uint (value);
      if (_select_subflow (this, subflow_id, &subflow)) {
        this->pivot_outpad = (GstPad*)subflow;
      } else {
        this->pivot_outpad = NULL;
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (this);
}

static void
gst_mprtp_sender2_get_property (GObject * object, guint property_id, GValue * value,
    GParamSpec * pspec)
{
  GstMPRTPSender2 *this = GST_MPRTPSENDER2 (object);

  GST_OBJECT_LOCK (this);
  switch (property_id) {
    case PROP_MPRTP_EXT_HEADER_ID:
      g_value_set_uint (value, (guint) this->mprtp_ext_header_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (this);
}

static gboolean
gst_mprtp_sender2_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res;

  switch (GST_EVENT_TYPE (event)) {
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }

  return res;
}

static gboolean
gst_mprtp_sender2_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res;

  switch (GST_QUERY_TYPE (query)) {
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }
  return res;
}


static GstFlowReturn
gst_mprtp_sender2_do_push (GstMPRTPSender2 * mprtp_sender2, GstPad * pad, gpointer data, gboolean is_list)
{
  GstFlowReturn res;

  /* Push */
  if (is_list) {
    res =
        gst_pad_push_list (pad,
        gst_buffer_list_ref (GST_BUFFER_LIST_CAST (data)));
  } else {
    res = gst_pad_push (pad, gst_buffer_ref (GST_BUFFER_CAST (data)));
  }
  return res;
}

static GstFlowReturn
gst_mprtp_sender2_handle_data (GstMPRTPSender2 * mprtp_sender2, gpointer data, gboolean is_list)
{
  GList *pads_list;
  GstFlowReturn ret, cret;
  GstPad *pad;

  GST_OBJECT_LOCK (mprtp_sender2);
  pads_list = GST_ELEMENT_CAST (mprtp_sender2)->srcpads;

  /* special case for zero pads */
  if (G_UNLIKELY (!pads_list))
    goto no_pads;

  /* special case for just one pad that avoids reffing the buffer */
  if (!pads_list->next) {
    GstPad *pad = GST_PAD_CAST (pads_list->data);

    /* Keep another ref around, a pad probe
     * might release and destroy the pad */
    gst_object_ref (pad);
    GST_OBJECT_UNLOCK (mprtp_sender2);

    if (is_list) {
      ret = gst_pad_push_list (pad, GST_BUFFER_LIST_CAST (data));
    } else {
      ret = gst_pad_push (pad, GST_BUFFER_CAST (data));
    }

    gst_object_unref (pad);
    return ret;
  }

  cret = GST_FLOW_NOT_LINKED;
  pad = _get_mprtp_sink(mprtp_sender2, data, is_list);

  gst_object_ref (pad);
  GST_OBJECT_UNLOCK (mprtp_sender2);

  GST_LOG_OBJECT (pad, "Starting to push %s %p",
      is_list ? "list" : "buffer", data);

  ret = gst_mprtp_sender2_do_push (mprtp_sender2, pad, data, is_list);

  GST_LOG_OBJECT (pad, "Pushing item %p yielded result %s", data,
        gst_flow_get_name (ret));

  GST_OBJECT_LOCK (mprtp_sender2);
    /* keep track of which pad we pushed and the result value */
  GST_SUBFLOW_CAST (pad)->result = ret;
  gst_object_unref (pad);
  pad = NULL;

  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto error;

  GST_OBJECT_UNLOCK (mprtp_sender2);

  gst_mini_object_unref (GST_MINI_OBJECT_CAST (data));

  return cret;

  /* ERRORS */
no_pads:
  {
    GST_DEBUG_OBJECT (mprtp_sender2, "there are no pads, return not-linked");
    ret = GST_FLOW_NOT_LINKED;
    goto end;
  }
error:
  {
    GST_DEBUG_OBJECT (mprtp_sender2, "received error %s", gst_flow_get_name (ret));
    goto end;
  }
end:
  {
    GST_OBJECT_UNLOCK (mprtp_sender2);
    gst_mini_object_unref (GST_MINI_OBJECT_CAST (data));
    return ret;
  }
}
typedef enum
{
  PACKET_IS_MPRTP,
  PACKET_IS_MPRTCP,
  PACKET_IS_NOT_MP,
} PacketTypes;


static PacketTypes
_get_packet_mptype (GstMPRTPSender2 * this,
    GstBuffer * buf, GstMapInfo * info, guint8 * subflow_id)
{
  guint8 first_byte, second_byte;
  PacketTypes result = PACKET_IS_NOT_MP;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  MPRTPSubflowHeaderExtension *subflow_infos = NULL;
  guint size;
  gpointer pointer;

  if (gst_buffer_extract (buf, 0, &first_byte, 1) != 1 ||
      gst_buffer_extract (buf, 1, &second_byte, 1) != 1) {
    GST_WARNING_OBJECT (this, "could not extract first byte from buffer");
    goto done;
  }
  if (PACKET_IS_DTLS (first_byte)) {
    goto done;
  }

  if (PACKET_IS_RTP_OR_RTCP (first_byte)) {
    if (PACKET_IS_RTCP (second_byte)) {
      if (second_byte != MPRTCP_PACKET_TYPE_IDENTIFIER) {
        goto done;
      }
      if (subflow_id) {
        *subflow_id = (guint8)
            g_ntohs (*((guint16 *) (info->data + 8 /*RTCP Header */  +
                    6 /*first block info until subflow id */ )));
      }
      result = PACKET_IS_MPRTCP;
      goto done;
    }

    if (G_UNLIKELY (!gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp))) {
      GST_WARNING_OBJECT (this, "The RTP packet is not readable");
      goto done;
    }

    if (!gst_rtp_buffer_get_extension (&rtp)) {
      gst_rtp_buffer_unmap (&rtp);
      goto done;
    }

    if (!gst_rtp_buffer_get_extension_onebyte_header (&rtp,
            this->mprtp_ext_header_id, 0, &pointer, &size)) {
      gst_rtp_buffer_unmap (&rtp);
      goto done;
    }

    if (subflow_id) {
      subflow_infos = (MPRTPSubflowHeaderExtension *) pointer;
      *subflow_id = subflow_infos->id;

      //SRTP validation - it must be fail
//      gst_rtp_buffer_add_extension_onebyte_header (&rtp, 2,
//            (gpointer) subflow_infos, sizeof (*subflow_infos));
    }
    gst_rtp_buffer_unmap (&rtp);
    result = PACKET_IS_MPRTP;
    goto done;
  }
done:
  return result;
}

GstPad*
_get_mprtp_sink (GstMPRTPSender2 *this, gpointer data, gboolean is_list)
{
  GstMapInfo map;
  PacketTypes packet_type;
  guint8 subflow_id;
  Subflow *subflow;
  GstPad *result = NULL;
  GstBuffer *buf;

  if (is_list) {
    buf = gst_buffer_list_get( GST_BUFFER_LIST_CAST (data), 0);
  } else {
    buf = GST_BUFFER_CAST (data);
  }

  if (!gst_buffer_map (buf, &map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (this, "Buffer is not readable");
    goto done;
  }
  packet_type = _get_packet_mptype (this, buf, &map, &subflow_id);
  if (packet_type != PACKET_IS_NOT_MP &&
      _select_subflow (this, subflow_id, &subflow) != FALSE) {
//      g_print("%d->%p|", subflow_id, subflow->outpad);
      result = (GstPad*)subflow;
  } else if (this->pivot_outpad != NULL &&
      gst_pad_is_active (this->pivot_outpad) &&
      gst_pad_is_linked (this->pivot_outpad)) {
    result = this->pivot_outpad;
  } else {
    result = (GstPad*) GST_ELEMENT_CAST (this)->srcpads->data;
  }
  gst_buffer_unmap (buf, &map);
done:
  return result;

}

static GstFlowReturn
gst_mprtp_sender2_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstFlowReturn res;
  GstMPRTPSender2 *mprtp_sender2;

  mprtp_sender2 = GST_MPRTPSENDER2_CAST (parent);

  GST_DEBUG_OBJECT (mprtp_sender2, "received buffer %p", buffer);

  res = gst_mprtp_sender2_handle_data (mprtp_sender2, buffer, FALSE);

  GST_DEBUG_OBJECT (mprtp_sender2, "handled buffer %s", gst_flow_get_name (res));

  return res;
}

static GstFlowReturn
gst_mprtp_sender2_chain_list (GstPad * pad, GstObject * parent, GstBufferList * list)
{
  GstFlowReturn res;
  GstMPRTPSender2 *mprtp_sender2;

  mprtp_sender2 = GST_MPRTPSENDER2_CAST (parent);

  GST_DEBUG_OBJECT (mprtp_sender2, "received list %p", list);

  res = gst_mprtp_sender2_handle_data (mprtp_sender2, list, TRUE);

  GST_DEBUG_OBJECT (mprtp_sender2, "handled list %s", gst_flow_get_name (res));

  return res;
}

static gboolean
gst_mprtp_sender2_sink_activate_mode (GstPad * pad, GstObject * parent, GstPadMode mode,
    gboolean active)
{
  gboolean res;
  GstMPRTPSender2 *mprtp_sender2;

  mprtp_sender2 = GST_MPRTPSENDER2 (parent);

  switch (mode) {
    case GST_PAD_MODE_PUSH:
    {
      GST_OBJECT_LOCK (mprtp_sender2);
      mprtp_sender2->sink_mode = active ? mode : GST_PAD_MODE_NONE;
      GST_OBJECT_UNLOCK (mprtp_sender2);
      res = TRUE;
      break;
    }
    default:
      res = FALSE;
      break;
  }
  return res;
}

static gboolean
gst_mprtp_sender2_src_activate_mode (GstPad * pad, GstObject * parent, GstPadMode mode,
    gboolean active)
{
  GstMPRTPSender2 *this;
  gboolean res;

  this = GST_MPRTPSENDER2 (parent);

  switch (mode) {
    case GST_PAD_MODE_PULL:
    {
      GST_OBJECT_LOCK (this);
      goto cannot_pull;
      GST_OBJECT_UNLOCK (this);
      break;
    }
    default:
      res = TRUE;
      break;
  }

  return res;

  /* ERRORS */
cannot_pull:
  {
    GST_OBJECT_UNLOCK (this);
    GST_INFO_OBJECT (this, "Cannot activate in pull mode");
    return FALSE;
  }
}

static gboolean
gst_mprtp_sender2_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res;

  switch (GST_QUERY_TYPE (query)) {
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}



GstFlowReturn
gst_mprtpsender_mprtcp_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf)
{
  GstMPRTPSender2 *this;
  GstFlowReturn result = GST_FLOW_OK;
  Subflow *subflow = NULL;
  this = GST_MPRTPSENDER2 (parent);
  GST_OBJECT_LOCK (this);
  subflow = _get_subflow_from_blocks (this, buf);
  if (!subflow) {
    goto done;
  }
//  g_print("############################ SENT (%lu)################################\n", GST_TIME_AS_MSECONDS(gst_clock_get_time(subflow->sysclock)));
  GST_OBJECT_UNLOCK (this);
  result = gst_pad_push ((GstPad*)subflow, _assemble_report (subflow, buf));

done:
  return result;
}


Subflow *
_get_subflow_from_blocks (GstMPRTPSender2 * this, GstBuffer * blocks)
{
  GstMapInfo map = GST_MAP_INFO_INIT;
  Subflow *result = NULL;
  guint16 subflow_id;
  GstMPRTCPSubflowBlock *block;
  if (!gst_buffer_map (blocks, &map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (this, "Buffer is not readable");
    goto done;
  }
  block = (GstMPRTCPSubflowBlock *) map.data;
  gst_mprtcp_block_getdown (&block->info, NULL, NULL, &subflow_id);
  if (!_select_subflow (this, subflow_id, &result)) {
    result = NULL;
  }
done:
  return result;
}

GstBuffer *
_assemble_report (Subflow * this, GstBuffer * blocks)
{
  GstBuffer *result = NULL;
  gsize report_header_size = 0;
  gsize blocks_length = 0;
  GstMPRTCPSubflowReport *report;
  GstMPRTCPSubflowBlock *block;
  guint16 length;
  guint16 offset = 0;
  guint8 block_length = 0;
  guint16 subflow_id, prev_subflow_id = 0;
  GstMapInfo map = GST_MAP_INFO_INIT;
  guint8 src = 0;

  if (!gst_buffer_map (blocks, &map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (this, "Buffer is not readable");
    goto exit;
  }
  report_header_size = sizeof(GstRTCPHeader) + sizeof(guint32);
  block = (GstMPRTCPSubflowBlock *) (map.data + offset);
  for (; offset < map.size; offset += (block_length + 1) << 2, ++src) {
//      {
//            guint8 pt;
//            gst_rtcp_header_getdown(&block->block_header, NULL, NULL, NULL, &pt, NULL, NULL);
//            if(pt == GST_RTCP_TYPE_SR){
//                guint64 ntptime;
//                GstRTCPSR *sr;
//                sr = &block->sender_riport;
//                gst_rtcp_srb_getdown(&sr->sender_block, &ntptime, NULL, NULL, NULL);
//                g_print("Created NTP time for subflow %d is %lu, but it sent at: "
//                    "%lu (%lu)\n", this->id, ntptime, NTP_NOW>>32,
//                    get_epoch_time_from_ntp_in_ns(NTP_NOW - ntptime));
//            }
//          }
    gst_mprtcp_block_getdown (&block->info, NULL, &block_length, &subflow_id);
    if (prev_subflow_id > 0 && subflow_id != prev_subflow_id) {
      GST_WARNING ("MPRTCP block comes from multiple subflow");
    }
    blocks_length += (block_length + 1) << 2;
    block = (GstMPRTCPSubflowBlock *) (map.data + blocks_length);
  }
  report = (GstMPRTCPSubflowReport*) g_malloc0(report_header_size + blocks_length);
  gst_mprtcp_report_init (report);
  memcpy((gpointer) &report->blocks, (gpointer) map.data, blocks_length);
  length = (report_header_size + blocks_length - 4)>>2;

  gst_rtcp_header_change(&report->header, NULL, NULL,
                         &src, NULL, &length, NULL);
  gst_buffer_unmap(blocks, &map);
//  gst_print_rtcp(&report->header);
  result = gst_buffer_new_wrapped ((gpointer)report, (length + 1)<<2);

exit:
  return result;
}


gboolean
_select_subflow (GstMPRTPSender2 * this, guint8 id, Subflow ** result)
{
  GList *it;
  Subflow *subflow;
  it = GST_ELEMENT_CAST (this)->srcpads;
  for ( ; it; it = it->next) {
    subflow = GST_SUBFLOW_CAST (it->data);
    if (subflow->id == id) {
      *result = subflow;
      return TRUE;
    }
  }
  *result = NULL;
  return FALSE;
}

