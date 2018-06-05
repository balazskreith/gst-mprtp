/*
 * smanctrler.h
 *
 *  Created on: Jun 30, 2015
 *      Author: balazs
 */

#ifndef STREAM_JOINER_H_
#define STREAM_JOINER_H_

#include <gst/gst.h>
#include "lib_swplugins.h"
#include "rcvtracker.h"
#include "mediator.h"

typedef struct _StreamJoiner StreamJoiner;
typedef struct _StreamJoinerClass StreamJoinerClass;


#define STREAM_JOINER_TYPE             (stream_joiner_get_type())
#define STREAM_JOINER(src)             (G_TYPE_CHECK_INSTANCE_CAST((src),STREAM_JOINER_TYPE,StreamJoiner))
#define STREAM_JOINER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass),STREAM_JOINER_TYPE,StreamJoinerClass))
#define STREAM_JOINER_IS_SOURCE(src)          (G_TYPE_CHECK_INSTANCE_TYPE((src),STREAM_JOINER_TYPE))
#define STREAM_JOINER_IS_SOURCE_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass),STREAM_JOINER_TYPE))
#define STREAM_JOINER_CAST(src)        ((StreamJoiner *)(src))


struct _StreamJoiner
{
  GObject              object;
  GstClock*            sysclock;
  GstClockTime         made;
  GQueue*              outq;
  GQueue*              frames;
  Recycle*             frames_recycle;
  guint16              hsn;
  gboolean             hsn_init;
  TimestampGenerator*  rtp_ts_generator;

  guint16              last_abs_seq;
  guint32              last_snd_rtp_ts;
  guint32              last_rcv_rtp_ts;
  guint8               last_subflow_id;

  guint32              max_join_delay_in_ts;
  guint32              min_join_delay_in_ts;
  guint32              join_delay_in_ts;
  gint                 join_frame_nr;
  gdouble              max_skew_in_ts, playout_delay_in_ts;

  gint64               max_diff_delay_in_ts;
  GstClockTime         last_max_skew_updated;
  GstClockTime         last_updated;
  GQueue*              playout_items;
  GQueue*              playour_items_recycle;
  RcvSubflows*         subflows;

  guint32              frame_inter_arrival_avg_in_ts;
  guint32              last_frame_ts;
  gboolean             first_frame_popped;

  struct {
    gdouble skew;
    gint32 meas_num;
    guint16 last_seq;
  }skew_info[MPRTP_PLUGIN_MAX_SUBFLOW_NUM];
};
struct _StreamJoinerClass{
  GObjectClass parent_class;
};

StreamJoiner*
make_stream_joiner(TimestampGenerator* rtp_ts_generator, RcvSubflows* subflows);

guint32
stream_joiner_get_max_join_delay_in_ts(
    StreamJoiner *this);

void
stream_joiner_set_max_join_delay_in_ts(
    StreamJoiner *this,
    guint32 join_delay_in_ts
);


guint32
stream_joiner_get_min_join_delay_in_ts(
    StreamJoiner *this);

void
stream_joiner_set_min_join_delay_in_ts(
    StreamJoiner *this,
    guint32 join_delay_in_ts
);

guint32
stream_joiner_get_join_delay_in_ts(
    StreamJoiner *this);

void
stream_joiner_push_packet(
    StreamJoiner *this,
    RcvPacket* packet);

RcvPacket*
stream_joiner_pop_packet(
    StreamJoiner *this);

GType
stream_joiner_get_type (void);

#endif /* STREAM_JOINER_H_ */
