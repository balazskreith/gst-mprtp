/*
 * smanctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef STREAM_JOINER_H_
#define STREAM_JOINER_H_

#include <gst/gst.h>

typedef struct _StreamJoiner StreamJoiner;
typedef struct _StreamJoinerClass StreamJoinerClass;

#include "mprtprpath.h"

#define STREAM_JOINER_TYPE             (stream_joiner_get_type())
#define STREAM_JOINER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),STREAM_JOINER_TYPE,StreamJoiner))
#define STREAM_JOINER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),STREAM_JOINER_TYPE,StreamJoinerClass))
#define STREAM_JOINER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),STREAM_JOINER_TYPE))
#define STREAM_JOINER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),STREAM_JOINER_TYPE))
#define STREAM_JOINER_CAST(src)        ((StreamJoiner *)(src))

#define MPRTP_SENDER_SCHTREE_MAX_PATH_NUM 32
#define MAX_SKEWS_ARRAY_LENGTH 256

typedef struct _FrameNode FrameNode;
typedef struct _Frame Frame;


struct _StreamJoiner
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;
  GHashTable*          subflows;
  gint                 subflow_num;
  GRWLock              rwmutex;
  gboolean             playout_allowed;
  gboolean             playout_halt;
  GstClockTime         playout_halt_time;
  gdouble              playout_delay;
  GstClockTime         last_playout;
  GstClockTime         last_playout_refresh;
  NumsTracker*         skews;
  GstClockTime         max_skew;
  GstClockTime         min_skew;
  GstClockTime         init_delay;
  Frame*               head;
  Frame*               tail;
  gint32               bytes_in_queue;
  guint32              last_played_timestamp;
  gboolean             flushing;
  gint32               framecounter;

  GQueue*              urgent;
  gboolean             init_delay_applied;
  guint64              last_snd_ntp_reference;

};
struct _StreamJoinerClass{
  GObjectClass parent_class;
};

StreamJoiner*
make_stream_joiner(void);

void
stream_joiner_do_logging(StreamJoiner *this);

void
stream_joiner_add_path(
    StreamJoiner * this,
    guint8 subflow_id,
    MpRTPRPath *path);

void
stream_joiner_rem_path(
    StreamJoiner * this,
    guint8 subflow_id);

void
stream_joiner_push(
    StreamJoiner * this,
    GstMpRTPBuffer *mprtp);

GstMpRTPBuffer*
stream_joiner_pop(
    StreamJoiner *this);

void
stream_joiner_get_stats(
    StreamJoiner *this,
    gdouble *playout_delay,
    gint32 *playout_buffer_size);

void
stream_joiner_set_playout_halt_time(
    StreamJoiner *this,
    GstClockTime halt_time);

void
stream_joiner_set_playout_allowed(
    StreamJoiner *this,
    gboolean playout_permission);

void
stream_joiner_set_initial_delay(
    StreamJoiner *this,
    GstClockTime tick_interval);

GType
stream_joiner_get_type (void);

#endif /* STREAM_JOINER_H_ */
