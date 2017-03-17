/*
 * stream_splitter.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef STREAM_SPLITTERN_H_
#define STREAM_SPLITTERN_H_

#include <gst/gst.h>
#include "sndsubflows.h"
#include "sndpackets.h"

typedef struct _StreamSplitter StreamSplitter;
typedef struct _StreamSplitterClass StreamSplitterClass;
typedef struct _StreamSplitterPrivate StreamSplitterPrivate;

#define STREAM_SPLITTER_TYPE             (stream_splitter_get_type())
#define STREAM_SPLITTER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),STREAM_SPLITTER_TYPE,StreamSplitter))
#define STREAM_SPLITTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),STREAM_SPLITTER_TYPE,StreamSplitterClass))
#define STREAM_SPLITTER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),STREAM_SPLITTER_TYPE))
#define STREAM_SPLITTER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),STREAM_SPLITTER_TYPE))
#define STREAM_SPLITTER_CAST(src)        ((StreamSplitter *)(src))


struct _StreamSplitter
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;

  SndSubflows*         subflows;
  gint32               total_bitrate;
  gint32               actual_rates[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
  gint32               actual_targets[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
  gdouble              actual_weights[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
  guint8               max_state;
  gboolean             keyframe_filtering;
  gint32               total_target;

};

struct _StreamSplitterClass{
  GObjectClass parent_class;
};

StreamSplitter*
make_stream_splitter(SndSubflows* sndsubflows);

void
stream_splitter_set_keyframe_filtering(
    StreamSplitter* this,
    gboolean keyframe_filtering);

void
stream_splitter_on_target_bitrate_changed(
    StreamSplitter* this,
    SndSubflow* subflow);

void
stream_splitter_on_packet_sent(
    StreamSplitter* this,
    SndPacket* packet);


void
stream_splitter_on_packet_obsolated(
    StreamSplitter* this,
    SndPacket* packet);

void
stream_splitter_on_subflow_state_changed(
    StreamSplitter* this,
    SndSubflow* subflow);


SndSubflow*
stream_splitter_select_subflow(
    StreamSplitter * this,
    SndPacket *packet);

GType stream_splitter_get_type (void);

#endif /* STREAM_SPLITTERN_H_ */
