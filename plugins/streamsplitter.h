/*
 * stream_splitter.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef STREAM_SPLITTER_H_
#define STREAM_SPLITTER_H_

#include <gst/gst.h>

#include "mprtpspath.h"

typedef struct _StreamSplitter StreamSplitter;
typedef struct _StreamSplitterClass StreamSplitterClass;
typedef struct _StreamSplitterPrivate StreamSplitterPrivate;
typedef struct _SchNode SchNode;
typedef struct _RetainedItem RetainedItem;

#define STREAM_SPLITTER_TYPE             (stream_splitter_get_type())
#define STREAM_SPLITTER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),STREAM_SPLITTER_TYPE,StreamSplitter))
#define STREAM_SPLITTER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),STREAM_SPLITTER_TYPE,StreamSplitterClass))
#define STREAM_SPLITTER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),STREAM_SPLITTER_TYPE))
#define STREAM_SPLITTER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),STREAM_SPLITTER_TYPE))
#define STREAM_SPLITTER_CAST(src)        ((StreamSplitter *)(src))


#define MPRTP_SENDER_STREAM_SPLITTER_MAX_PATH_NUM 32

struct _RetainedItem{
  GstClockTime time;
  GstBuffer   *buffer;
};

struct _StreamSplitter
{
  GObject          object;

  gboolean         new_path_added;
  gboolean         path_is_removed;
  gboolean         changes_are_committed;

  SchNode*         non_keyframes_tree;
  SchNode*         keyframes_tree;

  GHashTable*      subflows;
  guint32          charge_value;
  guint8           ext_header_id;
  GRWLock          rwmutex;

  GstClock*        sysclock;
  GstTask*         thread;
  GRecMutex        thread_mutex;

  gfloat           non_keyframe_ratio;
  gfloat           keyframe_ratio;
  guint8           active_subflow_num;

};

struct _StreamSplitterClass{
  GObjectClass parent_class;
};

//class functions
void stream_splitter_add_path(StreamSplitter * this, guint8 subflow_id, MPRTPSPath *path);
void stream_splitter_rem_path(StreamSplitter * this, guint8 subflow_id);
MPRTPSPath* stream_splitter_get_next_path(StreamSplitter* this, GstBuffer* buf);
void stream_splitter_set_rtp_ext_header_id(StreamSplitter* this, guint8 ext_header_id);
void stream_splitter_setup_sending_bid(StreamSplitter* this, guint8 subflow_id, guint32 bid);
void stream_splitter_commit_changes(StreamSplitter *this);
GType stream_splitter_get_type (void);

#endif /* STREAM_SPLITTER_H_ */
