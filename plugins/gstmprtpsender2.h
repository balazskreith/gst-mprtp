/*
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


#ifndef __GST_MPRTPSENDER2_H__
#define __GST_MPRTPSENDER2_H__

#include <gst/gst.h>

G_BEGIN_DECLS


#define GST_TYPE_MPRTPSENDER2 \
  (gst_mprtp_sender2_get_type())
#define GST_MPRTPSENDER2(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPRTPSENDER2,GstMPRTPSender2))
#define GST_MPRTPSENDER2_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPRTPSENDER2,GstMPRTPSender2Class))
#define GST_IS_MPRTPSENDER2(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPRTPSENDER2))
#define GST_IS_MPRTPSENDER2_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPRTPSENDER2))
#define GST_MPRTPSENDER2_CAST(obj) ((GstMPRTPSender2*) obj)

typedef struct _GstMPRTPSender2 		GstMPRTPSender2;
typedef struct _GstMPRTPSender2Class 	GstMPRTPSender2Class;

/**
 * GstMPRTPSender2PullMode:
 * @GST_MPRTPSENDER2_PULL_MODE_NEVER: Never activate in pull mode.
 * @GST_MPRTPSENDER2_PULL_MODE_SINGLE: Only one src pad can be active in pull mode.
 *
 * The different ways that mprtp_sender2 can behave in pull mode. @MPRTPSENDER2_PULL_MODE_NEVER
 * disables pull mode.
 */
typedef enum {
  GST_MPRTPSENDER2_PULL_MODE_NEVER,
  GST_MPRTPSENDER2_PULL_MODE_SINGLE,
} GstMPRTPSender2PullMode;

/**
 * GstMPRTPSender2:
 *
 * Opaque #GstMPRTPSender2 data structure.
 */
struct _GstMPRTPSender2 {
  GstElement      element;

  /*< private >*/
  GstPad         *sinkpad;
  GstPad         *mprtcp_rr_sinkpad;
  GstPad         *mprtcp_sr_sinkpad;
  GstPad         *pivot_outpad;
  guint8          mprtp_ext_header_id;
  GHashTable     *pad_indexes;
  guint           next_pad_index;

  GstPadMode      sink_mode;
};

struct _GstMPRTPSender2Class {
  GstElementClass parent_class;
};

G_GNUC_INTERNAL GType	gst_mprtp_sender2_get_type	(void);

G_END_DECLS

#endif /* __GST_MPRTPSENDER2_H__ */
