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
#include "numstracker.h"

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
  GRWLock              rwmutex;
  GstClock*            sysclock;
  GstClockTime         made;
  GHashTable*          subflows;
  SchNode*             tree;
  PacketsSndQueue*     sndqueue;

  guint                active_subflow_num;
};

struct _StreamSplitterClass{
  GObjectClass parent_class;
};

StreamSplitter*
make_stream_splitter(
    PacketsSndQueue *sndqueue);

//class functions
void stream_splitter_add_path(StreamSplitter * this,
                              guint8 subflow_id,
                              MPRTPSPath *path,
                              gint32 sending_rate);

void stream_splitter_rem_path(
    StreamSplitter * this,
    guint8 subflow_id);

GstBuffer *
stream_splitter_pop(
    StreamSplitter * this,
    MPRTPSPath **out_path);

void stream_splitter_setup_sending_target(
    StreamSplitter* this,
    guint8 subflow_id,
    gint32 sending_target);

gdouble stream_splitter_get_sending_target(
    StreamSplitter* this,
    guint8 subflow_id);

gdouble stream_splitter_get_sending_weight(
    StreamSplitter* this,
    guint8 subflow_id);

void stream_splitter_refresh_targets (
    StreamSplitter * this);

void stream_splitter_commit_changes (
    StreamSplitter * this);

GType stream_splitter_get_type (void);

#endif /* STREAM_SPLITTERN_H_ */
