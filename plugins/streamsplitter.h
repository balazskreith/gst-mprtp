/*
 * stream_splitter.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef STREAM_SPLITTERN_H_
#define STREAM_SPLITTERN_H_

#include <gst/gst.h>
#include "mprtpspath.h"

typedef struct _StreamSplitter StreamSplitter;
typedef struct _StreamSplitterClass StreamSplitterClass;
typedef struct _StreamSplitterPrivate StreamSplitterPrivate;
typedef struct _SchNode SchNode;

#define STREAM_SPLITTER_TYPE             (stream_splitter_get_type())
#define STREAM_SPLITTER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),STREAM_SPLITTER_TYPE,StreamSplitter))
#define STREAM_SPLITTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),STREAM_SPLITTER_TYPE,StreamSplitterClass))
#define STREAM_SPLITTER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),STREAM_SPLITTER_TYPE))
#define STREAM_SPLITTER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),STREAM_SPLITTER_TYPE))
#define STREAM_SPLITTER_CAST(src)        ((StreamSplitter *)(src))

#define SCHTREE_MAX_VALUE 128

struct _StreamSplitter
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;
  SchNode*             tree;

//  guint                active_subflow_num;
//  guint8               max_flag;
//  guint                keyframe_filtering;

  SndSubflows*         subflows;
};

struct _StreamSplitterClass{
  GObjectClass parent_class;
};

StreamSplitter*
make_stream_splitter(SndSubflows* sndsubflows);

void
stream_splitter_set_mpath_keyframe_filtering(
    StreamSplitter * this,
    guint keyframe_filtering);

gboolean
stream_splitter_approve_buffer(
    StreamSplitter * this,
	RTPPacket *packet,
    MPRTPSPath **path);

void stream_splitter_commit_changes (
    StreamSplitter * this);

GType stream_splitter_get_type (void);

#endif /* STREAM_SPLITTERN_H_ */
