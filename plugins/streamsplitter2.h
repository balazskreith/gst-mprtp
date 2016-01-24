/*
 * stream_splitter2.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef STREAM_SPLITTER2_H_
#define STREAM_SPLITTER2_H_

#include <gst/gst.h>

#include "mprtpspath.h"
#include "numstracker.h"
#include "streamsplitter.h"

typedef struct _StreamSplitter2 StreamSplitter2;
typedef struct _StreamSplitter2Class StreamSplitter2Class;
typedef struct _StreamSplitter2Private StreamSplitter2Private;
typedef struct _SchNode SchNode;

#define STREAM_SPLITTER2_TYPE             (stream_splitter2_get_type())
#define STREAM_SPLITTER2(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),STREAM_SPLITTER2_TYPE,StreamSplitter2))
#define STREAM_SPLITTER2_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),STREAM_SPLITTER2_TYPE,StreamSplitter2Class))
#define STREAM_SPLITTER2_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),STREAM_SPLITTER2_TYPE))
#define STREAM_SPLITTER2_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),STREAM_SPLITTER2_TYPE))
#define STREAM_SPLITTER2_CAST(src)        ((StreamSplitter2 *)(src))


struct _StreamSplitter2
{
  GObject              object;

  gboolean             new_path_added;
  gboolean             path_is_removed;
  gboolean             changes_are_committed;

  SchNode*             tree;
  SchNode*             next_tree;

  guint32              sending_target;

  GHashTable*          subflows;
  GRWLock              rwmutex;

  GstClock*            sysclock;
  GstTask*             thread;
  GRecMutex            thread_mutex;

  guint8               active_subflow_num;

  gboolean             separation_is_possible;
  gboolean             last_delta_flag;
  gboolean             first_delta_flag;
  NumsTracker*         incoming_bytes;
  guint8               monitor_payload_type;

  PointerPool*         pointerpool;

};

struct _StreamSplitter2Class{
  GObjectClass parent_class;
};

//class functions
void stream_splitter2_add_path(StreamSplitter2 * this,
                              guint8 subflow_id,
                              MPRTPSPath *path,
                              gint32 sending_rate);

void stream_splitter2_rem_path(
    StreamSplitter2 * this,
    guint8 subflow_id);

MPRTPSPath*
stream_splitter2_get_next_path(
    StreamSplitter2* this,
    GstBuffer* buf);

void stream_splitter2_setup_sending_rate(
    StreamSplitter2* this,
    guint8 subflow_id,
    gint32 sending_target);

guint32 stream_splitter2_get_media_rate(
    StreamSplitter2* this);

void stream_splitter2_set_monitor_payload_type(
    StreamSplitter2 *this,
    guint8 playload_type);

gdouble stream_splitter2_get_sending_rate(
    StreamSplitter2* this,
    guint8 subflow_id);

void stream_splitter2_commit_changes (
    StreamSplitter2 * this);

GType stream_splitter2_get_type (void);

#endif /* STREAM_SPLITTER2_H_ */
