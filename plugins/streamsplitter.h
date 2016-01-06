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
#include "percentiletracker.h"
#include "variancetracker.h"

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

#define MPRTP_SENDER_STREAM_SPLITTER_MAX_PATH_NUM 32


typedef enum{
  MPRTP_STREAM_BYTE_BASED_SPLITTING,
  MPRTP_STREAM_PACKET_BASED_SPLITTING,
  MPRTP_STREAM_FRAME_BASED_SPLITTING,
}StreamSplittingMode;

struct _StreamSplitter
{
  GObject          object;

  gboolean             new_path_added;
  gboolean             path_is_removed;
  gboolean             changes_are_committed;

  SchNode*             non_keyframes_tree;
  SchNode*             keyframes_tree;

  SchNode*             next_non_keyframes_tree;
  SchNode*             next_keyframes_tree;

  GstClockTime         switch_time;
  guint32              switch_target;

  StreamSplittingMode  splitting_mode;
  GHashTable*          subflows;
  guint32              charge_value;
  guint32              last_rtp_timestamp;
  GRWLock              rwmutex;

  GstClock*            sysclock;
  GstTask*             thread;
  GRecMutex            thread_mutex;

  gfloat               non_keyframe_ratio;
  gfloat               keyframe_ratio;
  guint8               active_subflow_num;

  gboolean             separation_is_possible;
  gboolean             last_delta_flag;
  gboolean             first_delta_flag;
  VarianceTracker*     sent_bytes;
  guint8               monitor_payload_type;

};

struct _StreamSplitterClass{
  GObjectClass parent_class;
};

//class functions
void stream_splitter_add_path(StreamSplitter * this,
                              guint8 subflow_id,
                              MPRTPSPath *path,
                              guint32 start_bid);
void stream_splitter_rem_path(StreamSplitter * this, guint8 subflow_id);
MPRTPSPath* stream_splitter_get_next_path(StreamSplitter* this, GstBuffer* buf);
gboolean stream_splitter_separation_is_possible (StreamSplitter * this);
void stream_splitter_set_splitting_mode (StreamSplitter * this, StreamSplittingMode mode);
void stream_splitter_setup_sending_bid(StreamSplitter* this, guint8 subflow_id, guint32 bid);
guint32 stream_splitter_get_media_rate(StreamSplitter* this);
void stream_splitter_set_monitor_payload_type(StreamSplitter *this, guint8 playload_type);
gdouble stream_splitter_get_sending_rate(StreamSplitter* this, guint8 subflow_id);
stream_splitter_commit_changes (StreamSplitter * this, guint32 switch_rate, GstClockTime switch_max_time);
GType stream_splitter_get_type (void);

#endif /* STREAM_SPLITTER_H_ */
