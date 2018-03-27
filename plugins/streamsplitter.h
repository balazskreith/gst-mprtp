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
#include "sndtracker.h"
#include "sndqueue.h"

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

  gdouble              refresh_ratio;
  GstClockTime         last_refresh;
  SndSubflows*         subflows;
  GQueue*              packets;
  SndQueue*            sndqueue;

  volatile gint32      stable_targets[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
  volatile gint32      desired_targets[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
  guint8               max_state;
  gboolean             keyframe_filtering;
  gint32               total_target;
  gint32               total_stable_target;
  gdouble              target_off;
  gint32               sending_rate_avg;

  SndSubflow*          last_selected;
  guint32              last_ts;
  gint                 mode;
  guint32              last_timestamp;
  SndSubflow*          last_subflow;

};

struct _StreamSplitterClass{
  GObjectClass parent_class;
};

StreamSplitter*
make_stream_splitter(SndSubflows* sndsubflows, SndQueue* sndqueue);

void
stream_splitter_set_keyframe_filtering(
    StreamSplitter* this,
    gboolean keyframe_filtering);


gint32
stream_splitter_get_total_media_rate(
    StreamSplitter* this);

void
stream_splitter_on_packet_queued(
    StreamSplitter* this,
    SndPacket* packet);

void
stream_splitter_on_subflow_joined(
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
stream_splitter_on_subflow_stable_target_bitrate_chaned(
    StreamSplitter* this,
    SndSubflow* subflow);

void
stream_splitter_on_subflow_desired_target_chaned(
    StreamSplitter* this,
    SndSubflow* subflow);


void
stream_splitter_on_subflow_state_changed(
    StreamSplitter* this,
    SndSubflow* subflow);

void
stream_splitter_on_subflow_state_stat_changed(
    StreamSplitter* this,
    SndSubflowsStateStat* state_stat);

SndSubflow*
stream_splitter_select_subflow(
    StreamSplitter * this,
    SndPacket *packet);

GType stream_splitter_get_type (void);

#endif /* STREAM_SPLITTERN_H_ */
